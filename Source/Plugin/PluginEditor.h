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

/** Settings shown in a call-out: Claude (API key, model, effort), the preview's sound and MIDI
    export articulations. */
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

    juce::Label soundHeading, soundNote;
    juce::ComboBox soundBox;
    juce::String soundPath;      // your SoundFont, if chosen
    int soundChoice = 1;
    std::unique_ptr<juce::FileChooser> soundChooser;
    void rebuildSoundBox();
    juce::String soundSetting() const;
};

/** Names a preset before it's saved (shown in a call-out under the Presets button). */
class SavePresetPanel : public juce::Component
{
public:
    SavePresetPanel (const juce::String& suggestedName, bool hasPiece, std::function<void (const juce::String&)> onSave);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label heading, note;
    juce::TextEditor nameEditor;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };
    std::function<void (const juce::String&)> onSave;
    void save();
};

//==============================================================================
class PromptMidiEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit PromptMidiEditor (PromptMidiProcessor&);
    ~PromptMidiEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Drop a .mid or .promptmidi file anywhere on the window to import it.
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override  { fileDragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { fileDragHover = false; repaint(); }

    /** Imports a preset or a MIDI file (by extension); reports in the status line. */
    void importFile (const juce::File&);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshFromProcessor();
    void startGeneration();
    void showSettings();
    void showInstruments();
    void exportMidi();
    void togglePreview();
    void showPresetsMenu();
    void savePreset();
    void exportPreset();
    void chooseFileToImport();
    void showTakeMenu();
    void updateTakes();
    juce::String instrumentsText() const;
    void updateEngineButtons();
    void pushControls();
    void pullControls();
    void setStatusText (const juce::String&, juce::Colour);

    PromptMidiProcessor& processor;
    Theme::LookAndFeel lookAndFeel;

    // Header
    IconButton presetsButton { "presets", Icons::Kind::Folder, "Presets" };
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
    IconButton prevTakeButton { "previous take", Icons::Kind::ChevronLeft }, nextTakeButton { "next take", Icons::Kind::ChevronRight };
    juce::TextButton takeButton;
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
    bool fileDragHover = false;
    juce::TooltipWindow tooltips { this, 500 };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ButtonAttachment> loopAttachment, hostMidiAttachment, soundAttachment;
    std::unique_ptr<SliderAttachment> volumeAttachment;

    int animationFrame = 0;
    juce::uint32 previewStartedAt = 0;
    juce::String shownSound;
    bool audioHintShown = false;
    int shownVersion = -1, shownPiece = -1;
    bool updatingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptMidiEditor)
};
