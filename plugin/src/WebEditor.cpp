// WebView editor: serves the bundled ui/ through the WebBrowserComponent resource provider and
// carries the bridge (docs/bridge-spec.md):
//   JS -> C++  native function `bridge(commandJson)` completes with the Reply JSON;
//   C++ -> JS  event `bridge` carries a PluginEvent JSON (`session` on change, `transport` at 30 Hz,
//              generation progress and notices as they happen).
// The editor holds no domain state; closing it changes nothing in the processor. Message thread only.

#include "FlowstateUi.h"
#include "FocusRelease.h"
#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#if !JUCE_WEB_BROWSER
#error "WebEditor.cpp needs JUCE_WEB_BROWSER=1; headless builds use HeadlessEditor.cpp"
#endif

#if !JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
#error "The resource provider needs WKWebView (macOS) or WebView2 (JUCE_USE_WIN_WEBVIEW2) on Windows"
#endif

namespace flowstate::plugin {
namespace {

using Browser = juce::WebBrowserComponent;

const char* mimeFor(const juce::String& file) {
    const auto ext = file.fromLastOccurrenceOf(".", false, false).toLowerCase();
    if (ext == "html") return "text/html";
    if (ext == "js" || ext == "mjs") return "text/javascript";
    if (ext == "css") return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "woff2") return "font/woff2";
    return "application/octet-stream";
}

// Serves the files embedded by juce_add_binary_data, looked up by original file name. A pure
// function over static data, so it is safe on whichever thread the backend calls it.
std::optional<Browser::Resource> getResource(const juce::String& url) {
    auto path = url.upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);
    path = path == "/" ? juce::String("index.html") : path.fromFirstOccurrenceOf("/", false, false);

    for (int i = 0; i < FlowstateUi::namedResourceListSize; ++i) {
        const auto* name = FlowstateUi::namedResourceList[i];
        if (path != FlowstateUi::getNamedResourceOriginalFilename(name)) continue;
        int size = 0;
        const auto* data = FlowstateUi::getNamedResource(name, size);
        if (data == nullptr) return std::nullopt;
        Browser::Resource r;
        r.data.resize(static_cast<size_t>(size));
        std::memcpy(r.data.data(), data, static_cast<size_t>(size));
        r.mimeType = mimeFor(path);
        return r;
    }
    return std::nullopt;
}

// The page the editor opens: index.html, or another bundled page named by FLOWSTATE_UI_PAGE (e.g.
// gallery.html, the P1-8 component gallery). Anything that isn't a bundled .html file is ignored.
juce::String startPage() {
    const auto page = juce::SystemStats::getEnvironmentVariable("FLOWSTATE_UI_PAGE", {});
    if (page.endsWithIgnoreCase(".html") && !page.containsChar('/') && getResource("/" + page).has_value()) return page;
    return {};
}

// Only the bundled page may load; links never navigate the plugin UI away.
class SinglePageBrowser final : public Browser {
public:
    using Browser::Browser;
    bool pageAboutToLoad(const juce::String& url) override { return url.startsWith(getResourceProviderRoot()); }
    void newWindowAttemptingToLoad(const juce::String&) override {}
};

class WebEditor final : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::ChangeListener,
                        private EditorActions {
public:
    explicit WebEditor(FlowstateProcessor& p) : juce::AudioProcessorEditor(&p), proc(p), web(makeOptions()) {
        addAndMakeVisible(web);
        web.goToURL(Browser::getResourceProviderRoot() + startPage());
        proc.addChangeListener(this);
        proc.setEditorActions(this);
        proc.setEventListener([this](const std::string& event) { emit(event); });

        // Read the saved size first: setResizeLimits resizes (and so calls resized()) straight away.
        const auto size = proc.getEditorSize();
        setResizable(true, true);
        setResizeLimits(720, 480, 4096, 4096);
        setSize(size.x, size.y);
        constructed = true;
        startTimerHz(30);
    }

    ~WebEditor() override {
        stopTimer();
        proc.setEventListener(nullptr);
        proc.setEditorActions(nullptr);
        proc.removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff070708)); }

    void resized() override {
        web.setBounds(getLocalBounds());
        if (constructed) proc.setEditorSize(getWidth(), getHeight());
    }

private:
    Browser::Options makeOptions() {
        const auto userDataFolder =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Flowstate-WebView2");

        return Browser::Options{}
            .withBackend(Browser::Options::Backend::webview2)
            .withWinWebView2Options(Browser::Options::WinWebView2{}
                                        .withUserDataFolder(userDataFolder)
                                        .withStatusBarDisabled()
                                        .withBuiltInErrorPageDisabled()
                                        .withBackgroundColour(juce::Colour(0xff070708)))
            .withNativeIntegrationEnabled()
            .withResourceProvider([](const juce::String& url) { return getResource(url); })
            .withNativeFunction("bridge", [this](const juce::Array<juce::var>& args, Browser::NativeFunctionCompletion done) {
                // The page's listeners are live once it sends its first command (hello); emitting
                // earlier makes JUCE assert, so events are gated on it.
                pageReady = true;
                const auto command = args.isEmpty() ? std::string() : args[0].toString().toStdString();
                done(juce::String::fromUTF8(proc.handleBridgeCommand(command).c_str()));
            });
    }

    void emit(const std::string& eventJson) {
        if (pageReady) web.emitEventIfBrowserIsVisible("bridge", juce::String::fromUTF8(eventJson.c_str()));
    }

    void timerCallback() override { emit(proc.transportEventJson()); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { emit(proc.sessionEventJson()); }

    // EditorActions
    std::optional<fb::ErrorInfo> startDrag(const juce::File& file) override {
        // Runs while the mouse button is still down (the page sends startDrag on pointerdown).
        // sourceComponent = the editor, so macOS uses our peer's NSView and its current event.
        if (juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this, [] {}))
            return std::nullopt;
        return fb::ErrorInfo{fb::ErrorCode::Internal, "The host didn't accept a drag from here."};
    }

    void releaseFocus() override {
        unfocusAllComponents();
        if (auto* peer = getPeer()) releaseKeyboardFocusToHost(peer->getNativeHandle());
    }

    FlowstateProcessor& proc;
    bool constructed = false;
    bool pageReady = false;
    SinglePageBrowser web;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebEditor)
};

}  // namespace

juce::AudioProcessorEditor* createFlowstateEditor(FlowstateProcessor& p) { return new WebEditor(p); }

}  // namespace flowstate::plugin
