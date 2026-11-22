#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Icons.h"
#include "InstrumentPicker.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Timeline.h"

//==============================================================================
/** The "Drag MIDI into your DAW" target in the footer (all tracks that aren't muted). */
class DragAllArea : public MidiDragSource
{
public:
    explicit DragAllArea (PromptMidiProcessor& p) : MidiDragSource (p, -1) {}
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
};

/** Settings shown in a call-out: Claude (API key, model, effort) and MIDI export articulations. */
class SettingsPanel : public juce::Component
{
public:
    SettingsPanel (Settings&, std::function<void()> onSaved);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Settings& settings;
    std::function<void()> onSaved;
    juce::Label heading, keyLabel, modelLabel, effortLabel, note;
    juce::TextEditor keyEditor, modelEditor;
    juce::ComboBox effortBox;
    juce::Label exportHeading, articulationLabel, valuesLabel, controllerLabel, exportNote;
    juce::ComboBox articulationBox;
    juce::TextEditor valuesEditor, controllerEditor;
    juce::String articulationOutputFor (int id) const;
    juce::TextButton saveButton { "Save" }, clearButton { "Remove key" };
};

//==============================================================================
class PromptMidiEditor : public juce::AudioProcessorEditor,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit PromptMidiEditor (PromptMidiProcessor&);
    ~PromptMidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshFromProcessor();
    void startGeneration();
    void showSettings();
    void showInstruments();
    void exportMidi();
    void updateEngineButtons();
    void pushControls();
    void pullControls();
    void setStatusText (const juce::String&, juce::Colour);

    PromptMidiProcessor& processor;
    Theme::LookAndFeel lookAndFeel;

    // Header
    juce::TextButton offlineButton { "Offline" }, claudeButton { "Claude AI" };
    IconButton settingsButton { "settings", Icons::Kind::Gear };

    // Prompt
    juce::TextEditor promptEditor;
    IconButton generateButton { "generate", Icons::Kind::Sparkle, "Generate" };
    IconButton variationButton { "variation", Icons::Kind::Shuffle, "Variation" };
    juce::OwnedArray<juce::TextButton> chips;
    juce::StringArray chipPrompts;

    // Controls
    juce::Label keyLabel, scaleLabel, tempoLabel, barsLabel, lengthLabel, instrumentsLabel, seedLabel;
    juce::ComboBox keyBox, scaleBox, barsBox, lengthBox;
    juce::Slider tempoSlider;
    IconButton instrumentsButton { "instruments", Icons::Kind::Instruments, "Auto" };
    juce::Label seedValue;
    IconButton lockSeedButton { "lock", Icons::Kind::Lock };

    // Result
    juce::Label titleLabel, summaryLabel, interpretationLabel, statusLabel, qualityLabel;
    Arrangement arrangement;

    // Transport
    IconButton playButton { "play", Icons::Kind::Play };
    juce::Label positionLabel;
    IconButton loopButton { "loop", Icons::Kind::Loop };
    IconButton soundButton { "sound", Icons::Kind::Speaker };
    juce::Slider volumeSlider;
    juce::ToggleButton hostMidiToggle { "Send MIDI with host" };
    DragAllArea dragArea;
    IconButton exportButton { "export", Icons::Kind::Export, "Export .mid" };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 500 };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ButtonAttachment> loopAttachment, hostMidiAttachment, soundAttachment;
    std::unique_ptr<SliderAttachment> volumeAttachment;

    int animationFrame = 0;
    int shownVersion = -1, shownPiece = -1;
    bool updatingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptMidiEditor)
};
