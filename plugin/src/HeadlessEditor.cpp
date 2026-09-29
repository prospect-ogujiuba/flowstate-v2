// Native stand-in editor for FLOWSTATE_PLUGIN_HEADLESS builds (JUCE_WEB_BROWSER=0: Linux CI and
// pluginval runs without a WebView). Shows the session at a glance and offers a drag handle for
// the current idea. Holds no domain state.

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace flowstate::plugin {
namespace {

class HeadlessEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer,
                             private juce::ChangeListener,
                             private EditorActions {
public:
    explicit HeadlessEditor(FlowstateProcessor& p) : juce::AudioProcessorEditor(&p), proc(p) {
        addAndMakeVisible(status);
        status.setJustificationType(juce::Justification::topLeft);
        status.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(dragAll);
        dragAll.setTitle("Drag all parts");
        dragAll.onClick = [this] {
            proc.handleBridgeCommand(R"({"type":"startDrag","nodeId":null,"partIds":null,"splitDrums":false})");
        };
        proc.addChangeListener(this);
        proc.setEditorActions(this);
        // Read the saved size first: setResizeLimits resizes (and so calls resized()) straight away.
        const auto size = proc.getEditorSize();
        setResizable(true, true);
        setResizeLimits(720, 480, 4096, 4096);
        setSize(size.x, size.y);
        constructed = true;
        refresh();
        startTimerHz(4);
    }

    ~HeadlessEditor() override {
        stopTimer();
        proc.setEditorActions(nullptr);
        proc.removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff14161b)); }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        dragAll.setBounds(area.removeFromBottom(32).removeFromLeft(160));
        status.setBounds(area);
        if (constructed) proc.setEditorSize(getWidth(), getHeight());
    }

private:
    void refresh() {
        const auto view = proc.getSession().view(proc.hostSnapshot(), 0);
        const auto host = proc.hostSnapshot().toTransport();
        juce::String text;
        text << "Flowstate (headless editor)\n\n"
             << "Ideas: " << static_cast<int>(view.nodes.size())
             << (view.currentNodeId ? "   current: " + juce::String(*view.currentNodeId) : juce::String()) << "\n"
             << "Parts: " << (view.clip ? static_cast<int>(view.clip->parts.size()) : 0) << "\n"
             << "Context: " << fb::toString(view.context.tonic) << " " << fb::toString(view.context.mode) << ", "
             << view.context.tempo << " bpm (" << fb::toString(view.context.timeFrom) << "), "
             << view.context.meterNumerator << "/" << view.context.meterDenominator << "\n"
             << "Transport: " << (host.playing ? "playing" : "stopped") << ", bar " << host.bar << "\n";
        status.setText(text, juce::dontSendNotification);
    }

    void timerCallback() override { refresh(); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    std::optional<fb::ErrorInfo> startDrag(const juce::File& file) override {
        if (juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this, [] {}))
            return std::nullopt;
        return fb::ErrorInfo{fb::ErrorCode::Internal, "The host didn't accept a drag from here."};
    }

    void releaseFocus() override { unfocusAllComponents(); }

    FlowstateProcessor& proc;
    bool constructed = false;
    juce::Label status;
    juce::TextButton dragAll{"Drag all parts"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeadlessEditor)
};

}  // namespace

juce::AudioProcessorEditor* createFlowstateEditor(FlowstateProcessor& p) { return new HeadlessEditor(p); }

}  // namespace flowstate::plugin
