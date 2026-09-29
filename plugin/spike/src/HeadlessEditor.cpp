// Native stub editor for the FLOWSTATE_SPIKE_HEADLESS_CHECK build (JUCE_WEB_BROWSER=0).
// It exists so the processor compiles and links on Linux CI boxes without webkit2gtk, and it
// doubles as the "native drag handle" fallback from docs/spikes/webview-host.md.

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace flowstate::spike
{
namespace
{

class DragHandle final : public juce::Component
{
public:
    explicit DragHandle (FlowstateSpikeProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTitle ("Drag MIDI");
        setDescription ("Drag the loaded clip to a DAW track as a .mid file");
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff2b6cb0));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f);
        g.setColour (juce::Colours::white);
        g.drawText ("Drag MIDI", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 4)
            return;

        const auto file = proc.writeClipToTempMidi();

        if (file.existsAsFile())
        {
            dragging = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                         [this] { dragging = false; });
        }
    }

private:
    FlowstateSpikeProcessor& proc;
    bool dragging = false;
};

class HeadlessEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer,
                             private juce::ChangeListener
{
public:
    explicit HeadlessEditor (FlowstateSpikeProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p), dragHandle (p)
    {
        addAndMakeVisible (status);
        addAndMakeVisible (clipInfo);
        addAndMakeVisible (loadButton);
        addAndMakeVisible (previewToggle);
        addAndMakeVisible (dragHandle);

        previewToggle.setToggleState (proc.isPreviewEnabled(), juce::dontSendNotification);
        previewToggle.setVisible (FlowstateSpikeProcessor::isInstrumentVariant);
        previewToggle.onClick = [this] { proc.setPreviewEnabled (previewToggle.getToggleState()); };
        loadButton.onClick = [this] { chooseClip(); };

        proc.addChangeListener (this);
        refreshClipInfo();

        setResizable (true, true);
        setResizeLimits (720, 480, 4096, 4096);
        const auto size = proc.getEditorSize();
        setSize (size.x, size.y);
        startTimerHz (30);
    }

    ~HeadlessEditor() override
    {
        proc.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff15171c));
    }

    void resized() override
    {
        proc.setEditorSize (getWidth(), getHeight());
        auto r = getLocalBounds().reduced (16);
        status.setBounds (r.removeFromTop (28));
        clipInfo.setBounds (r.removeFromTop (28));
        r.removeFromTop (12);
        auto row = r.removeFromTop (36);
        loadButton.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (12);
        previewToggle.setBounds (row.removeFromLeft (160));
        row.removeFromLeft (12);
        dragHandle.setBounds (row.removeFromLeft (140));
    }

private:
    void timerCallback() override
    {
        const auto s = proc.getStatus();
        status.setText (juce::String::formatted ("%s  %s  %.1f bpm  ppq %.3f  %d/%d  active %d",
                                                 s.hasHostTransport ? "host" : "no host transport",
                                                 s.playing ? "playing" : "stopped",
                                                 s.bpm, s.ppq, s.sigNum, s.sigDen, s.activeNotes),
                        juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshClipInfo(); }

    void refreshClipInfo()
    {
        const auto& c = proc.getClip();
        auto text = c.displayName + ": " + juce::String (c.bars) + " bars, " + juce::String (c.totalNotes()) + " notes";

        if (proc.getLastError().isNotEmpty())
            text << "  (" << proc.getLastError() << ")";

        clipInfo.setText (text, juce::dontSendNotification);
    }

    void chooseClip()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load clip", juce::File(), "*.notes.json;*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (const auto f = fc.getResult(); f != juce::File())
                                      proc.loadClipFromFile (f);

                                  refreshClipInfo();
                              });
    }

    FlowstateSpikeProcessor& proc;
    juce::Label status, clipInfo;
    juce::TextButton loadButton { "Load clip" };
    juce::ToggleButton previewToggle { "Preview sound" };
    DragHandle dragHandle;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeadlessEditor)
};

} // namespace

juce::AudioProcessorEditor* createFlowstateSpikeEditor (FlowstateSpikeProcessor& p)
{
    return new HeadlessEditor (p);
}

} // namespace flowstate::spike
