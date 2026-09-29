// WebView editor: one bundled page served by the WebBrowserComponent resource provider.
// JUCE 9 API: WebBrowserComponent::Options + withNativeIntegrationEnabled / withResourceProvider /
// withNativeFunction; C++ -> JS via emitEventIfBrowserIsVisible. Message thread only.

#include "FocusRelease.h"
#include "FlowstateSpikeUi.h"
#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#if ! JUCE_WEB_BROWSER
 #error "WebEditor.cpp needs JUCE_WEB_BROWSER=1; use FLOWSTATE_SPIKE_HEADLESS_CHECK for the stub editor"
#endif

#if ! JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
 #error "The resource provider needs WKWebView (macOS) or WebView2 (JUCE_USE_WIN_WEBVIEW2) on Windows"
#endif

namespace flowstate::spike
{
namespace
{

using Browser = juce::WebBrowserComponent;

const char* mimeFor (const juce::String& file)
{
    const auto ext = file.fromLastOccurrenceOf (".", false, false).toLowerCase();

    if (ext == "html") return "text/html";
    if (ext == "js")   return "text/javascript";
    if (ext == "css")  return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "png")  return "image/png";
    return "application/octet-stream";
}

/** Serves the files embedded by juce_add_binary_data (looked up by original file name).
    Pure function over static data, so it is safe whichever thread the backend calls it on. */
std::optional<Browser::Resource> getResource (const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    path = path == "/" ? juce::String ("index.html") : path.fromFirstOccurrenceOf ("/", false, false);

    for (int i = 0; i < FlowstateSpikeUi::namedResourceListSize; ++i)
    {
        const auto* name = FlowstateSpikeUi::namedResourceList[i];

        if (path != FlowstateSpikeUi::getNamedResourceOriginalFilename (name))
            continue;

        int size = 0;
        const auto* data = FlowstateSpikeUi::getNamedResource (name, size);

        if (data == nullptr)
            return std::nullopt;

        Browser::Resource r;
        r.data.resize ((size_t) size);
        std::memcpy (r.data.data(), data, (size_t) size);
        r.mimeType = mimeFor (path);
        return r;
    }

    return std::nullopt;
}

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();

    for (const auto& [k, v] : props)
        o->setProperty (k, v);

    return juce::var (o);
}

/** Only our bundled page may load; links never navigate the plugin UI away. */
class SinglePageBrowser final : public Browser
{
public:
    using Browser::Browser;

    bool pageAboutToLoad (const juce::String& url) override
    {
        return url.startsWith (getResourceProviderRoot());
    }

    void newWindowAttemptingToLoad (const juce::String&) override {}
};

//==================================================================================================
class WebEditor final : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit WebEditor (FlowstateSpikeProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p), web (makeOptions())
    {
        addAndMakeVisible (web);
        web.goToURL (Browser::getResourceProviderRoot());

        proc.addChangeListener (this);

        setResizable (true, true);
        setResizeLimits (720, 480, 4096, 4096);
        const auto size = proc.getEditorSize();
        setSize (size.x, size.y);

        startTimerHz (30); // status throttle: the audio thread only writes atomics
    }

    ~WebEditor() override
    {
        stopTimer();
        chooser.reset();
        proc.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff14161b));
    }

    void resized() override
    {
        web.setBounds (getLocalBounds());
        proc.setEditorSize (getWidth(), getHeight());
    }

private:
    Browser::Options makeOptions()
    {
        const auto userDataFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                        .getChildFile ("FlowstateSpike-WebView2");

        return Browser::Options {}
            .withBackend (Browser::Options::Backend::webview2)
            .withWinWebView2Options (Browser::Options::WinWebView2 {}
                                         .withUserDataFolder (userDataFolder)
                                         .withStatusBarDisabled()
                                         .withBuiltInErrorPageDisabled()
                                         .withBackgroundColour (juce::Colour (0xff14161b)))
            .withNativeIntegrationEnabled()
            .withResourceProvider ([] (const juce::String& url) { return getResource (url); })
            .withNativeFunction ("getInitialState", [this] (const juce::Array<juce::var>&, Browser::NativeFunctionCompletion done)
            {
                // The page's module (and JUCE's interop listeners) are live once it calls this;
                // emitting earlier makes JUCE assert, so events are gated on it.
                pageReady = true;
                done (object ({ { "variant", FlowstateSpikeProcessor::isInstrumentVariant ? "instrument" : "midifx" },
                                { "preview", proc.isPreviewEnabled() },
                                { "clip", toVar (proc.getClip()) },
                                { "error", proc.getLastError() },
                                { "juceVersion", juce::SystemStats::getJUCEVersion() },
                                { "backend", backendName() } }));
            })
            .withNativeFunction ("loadClip", [this] (const juce::Array<juce::var>&, Browser::NativeFunctionCompletion done)
            {
                chooseClip (std::move (done));
            })
            .withNativeFunction ("loadSample", [this] (const juce::Array<juce::var>&, Browser::NativeFunctionCompletion done)
            {
                const auto r = proc.loadBundledSample();
                done (object ({ { "ok", r.wasOk() }, { "error", r.getErrorMessage() } }));
            })
            .withNativeFunction ("startDrag", [this] (const juce::Array<juce::var>& args, Browser::NativeFunctionCompletion done)
            {
                startDrag (args.isEmpty() ? -1 : (int) args[0], std::move (done));
            })
            .withNativeFunction ("ping", [] (const juce::Array<juce::var>& args, Browser::NativeFunctionCompletion done)
            {
                done (args.isEmpty() ? juce::var() : args[0]); // echo straight back
            })
            .withNativeFunction ("setPreview", [this] (const juce::Array<juce::var>& args, Browser::NativeFunctionCompletion done)
            {
                proc.setPreviewEnabled (! args.isEmpty() && (bool) args[0]);
                done (object ({ { "preview", proc.isPreviewEnabled() } }));
            })
            .withNativeFunction ("setInternalPlay", [this] (const juce::Array<juce::var>& args, Browser::NativeFunctionCompletion done)
            {
                proc.setInternalTransportPlaying (! args.isEmpty() && (bool) args[0]);
                done (true);
            })
            .withNativeFunction ("submitText", [] (const juce::Array<juce::var>& args, Browser::NativeFunctionCompletion done)
            {
                const auto text = args.isEmpty() ? juce::String() : args[0].toString();
                done (object ({ { "text", text }, { "length", text.length() } }));
            })
            .withNativeFunction ("releaseFocus", [this] (const juce::Array<juce::var>&, Browser::NativeFunctionCompletion done)
            {
                releaseFocus();
                done (true);
            });
    }

    static juce::String backendName()
    {
       #if JUCE_WINDOWS
        return "WebView2";
       #elif JUCE_MAC
        return "WKWebView";
       #else
        return "WebKitGTK";
       #endif
    }

    void chooseClip (Browser::NativeFunctionCompletion done)
    {
        const auto current = juce::File (proc.getClipPath());
        const auto startDir = current.existsAsFile() ? current.getParentDirectory()
                                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Load a Flowstate clip (*.notes.json)", startDir, "*.notes.json;*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, done = std::move (done)] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();

                                  if (file == juce::File())
                                  {
                                      done (object ({ { "ok", false }, { "cancelled", true } }));
                                      return;
                                  }

                                  const auto r = proc.loadClipFromFile (file);
                                  done (object ({ { "ok", r.wasOk() },
                                                  { "error", r.getErrorMessage() },
                                                  { "clip", r.wasOk() ? toVar (proc.getClip()) : juce::var() } }));
                              });
    }

    // partIndex < 0 drags every part (one MIDI track each); otherwise just that part.
    void startDrag (int partIndex, Browser::NativeFunctionCompletion done)
    {
        const auto file = proc.writeClipToTempMidi (partIndex);

        if (! file.existsAsFile())
        {
            done (object ({ { "ok", false }, { "error", "could not write the temp .mid" } }));
            return;
        }

        // Must run while the mouse button is still down (the page calls this on pointerdown).
        // sourceComponent = the editor, so macOS uses our peer's NSView and its current event.
        const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
            { file.getFullPathName() }, false, this, [] {});

        done (object ({ { "ok", started },
                        { "path", file.getFullPathName() },
                        { "error", started ? juce::String() : juce::String ("performExternalDragDropOfFiles returned false") } }));
    }

    void releaseFocus()
    {
        unfocusAllComponents();

        if (auto* peer = getPeer())
            releaseKeyboardFocusToHost (peer->getNativeHandle());
    }

    void timerCallback() override
    {
        if (! pageReady)
            return;

        const auto s = proc.getStatus();
        web.emitEventIfBrowserIsVisible ("status", object ({
            { "host", s.hasHostTransport },
            { "internal", s.internalTransport },
            { "playing", s.playing },
            { "ppq", s.ppq },
            { "bpm", s.bpm },
            { "sig", juce::Array<juce::var> { s.sigNum, s.sigDen } },
            { "looping", s.looping },
            { "loopStart", s.loopStart },
            { "loopEnd", s.loopEnd },
            { "active", s.activeNotes },
            { "jumps", (int) s.discontinuities } }));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (! pageReady)
            return; // getInitialState will deliver the current clip

        web.emitEventIfBrowserIsVisible ("clip", toVar (proc.getClip()));

        if (proc.getLastError().isNotEmpty())
            web.emitEventIfBrowserIsVisible ("error", object ({ { "message", proc.getLastError() } }));
    }

    FlowstateSpikeProcessor& proc;
    bool pageReady = false;
    SinglePageBrowser web;                       // destroyed after the chooser
    std::unique_ptr<juce::FileChooser> chooser;  // destroyed first: its callback captures `this`

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebEditor)
};

} // namespace

juce::AudioProcessorEditor* createFlowstateSpikeEditor (FlowstateSpikeProcessor& p)
{
    return new WebEditor (p);
}

} // namespace flowstate::spike
