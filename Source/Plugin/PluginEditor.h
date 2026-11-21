#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PianoRoll.h"
#include "PluginProcessor.h"
#include "Theme.h"

//==============================================================================
/** Drag source that hands a .mid file to the DAW (or the desktop). */
class MidiDragSource : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDragSource (PromptMidiProcessor& p, int trackIndex) : processor (p), track (trackIndex) {}

    void setTrackIndex (int t) { track = t; }
    int getTrackIndex() const  { return track; }

    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

protected:
    PromptMidiProcessor& processor;
    int track = -1;
    bool dragging = false;
};

/** The big "Drag MIDI into your DAW" target in the footer. */
class DragAllArea : public MidiDragSource
{
public:
    explicit DragAllArea (PromptMidiProcessor& p) : MidiDragSource (p, -1) {}
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
};

/** One row in the track list: colour, name, instrument, mute, and drag-out of that track alone. */
class TrackRow : public MidiDragSource
{
public:
    TrackRow (PromptMidiProcessor&, int index);

    std::function<void (int)> onSelect;
    void setSelected (bool);
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::TextButton muteButton { "M" };
    bool selected = false;
};

class TrackList : public juce::Component
{
public:
    explicit TrackList (PromptMidiProcessor&);

    std::function<void (int)> onSelectionChanged;
    void rebuild();
    void refreshMutes();
    int getSelected() const { return selected; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PromptMidiProcessor& processor;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<TrackRow> rows;
    int selected = -1;
};

/** Claude settings shown in a call-out: API key, model, effort. */
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

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshFromProcessor();
    void startGeneration();
    void showSettings();
    void exportMidi();
    void updateEngineButtons();
    void pushControls();
    void pullControls();

    PromptMidiProcessor& processor;
    Theme::LookAndFeel lookAndFeel;

    juce::TextButton offlineButton { "Offline" }, claudeButton { "Claude AI" }, settingsButton { "Settings" };
    juce::TextEditor promptEditor;
    juce::TextButton generateButton { "Generate" }, variationButton { "Variation" };
    juce::OwnedArray<juce::TextButton> chips;
    juce::StringArray chipPrompts;

    juce::Label keyLabel, scaleLabel, tempoLabel, barsLabel, seedLabel;
    juce::ComboBox keyBox, scaleBox, barsBox;
    juce::Slider tempoSlider;
    juce::Label seedValue;
    juce::ToggleButton lockSeedButton { "Lock" };

    juce::Label titleLabel, summaryLabel, interpretationLabel, statusLabel;
    TrackList trackList;
    PianoRoll pianoRoll;

    juce::TextButton playButton { "Play" };
    juce::ToggleButton loopToggle { "Loop" }, syncToggle { "Follow host" }, soundToggle { "Preview sound" }, midiOutToggle { "MIDI out" };
    juce::Slider volumeSlider;
    DragAllArea dragArea;
    juce::TextButton exportButton { "Export .mid" };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 500 };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ButtonAttachment> loopAttachment, syncAttachment, soundAttachment, midiOutAttachment;
    std::unique_ptr<SliderAttachment> volumeAttachment;

    int animationFrame = 0;
    int shownVersion = -1;
    bool updatingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptMidiEditor)
};
