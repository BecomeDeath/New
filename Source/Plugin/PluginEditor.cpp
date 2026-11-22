#include "PluginEditor.h"
#include "SpecJson.h"

namespace
{
    juce::Font font (float size, bool bold = false)
    {
        return Theme::font (size, bold ? Theme::Weight::SemiBold : Theme::Weight::Regular);
    }

    juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    void styleLabel (juce::Label& l, float size, juce::Colour colour, bool bold = false)
    {
        l.setFont (font (size, bold));
        l.setColour (juce::Label::textColourId, colour);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setMinimumHorizontalScale (0.85f);
    }

    struct ExamplePrompt { const char* label; const char* prompt; };

    const ExamplePrompt examples[] = {
        { "Fantasy orchestral", "Generate me a fantasy orchestral midi" },
        { "Boss battle",        "Epic orchestral battle music for a boss fight" },
        { "Soft piano",         "Make me a soft piano midi" },
        { "Lo-fi hip hop",      "Lo-fi hip hop beat with a mellow electric piano, rainy night" },
        { "Villain theme",      "Dark fantasy orchestral theme for a villain's castle" },
        { "Village",            "Peaceful orchestral village theme with woodwinds and harp" },
        { "Jazz combo",         "Late night jazz combo with a tenor sax lead" },
        { "Synthwave",          "80s synthwave night drive with arpeggios" },
        { "Celtic tavern",      "Lively celtic tavern jig with fiddle and whistle" },
        { "Lullaby",            "Gentle lullaby for music box and harp" },
    };

    juce::String timeText (double tick, const pm::Composition& c)
    {
        if (tick < 0.0 || c.tempo <= 0)
            tick = 0.0;

        const double seconds = tick / pm::kPpq * 60.0 / c.tempo;
        const int tpb = juce::jmax (1, c.ticksPerBar);
        const int beatTicks = c.tsDen == 8 ? pm::kPpq * 3 / 2 : pm::kPpq;
        const int bar = (int) (tick / tpb) + 1;
        const int beat = (int) (std::fmod (tick, (double) tpb) / beatTicks) + 1;
        const int s = (int) seconds;
        return juce::String (bar) + "." + juce::String (beat) + "   " + juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
void DragAllArea::paint (juce::Graphics& g)
{
    const bool enabled = processor.hasComposition();
    const bool hover = isMouseOver (true) && enabled;
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (hover ? Theme::accent.withAlpha (0.22f) : Theme::accent.withAlpha (enabled ? 0.12f : 0.04f));
    g.fillRoundedRectangle (r, 6.0f);

    juce::Path border;
    border.addRoundedRectangle (r, 6.0f);
    const float dashes[] = { 4.0f, 3.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (enabled ? Theme::accent.withAlpha (hover ? 1.0f : 0.75f) : Theme::outline);
    g.fillPath (dashed);

    auto content = r.reduced (12.0f, 0.0f);
    Icons::draw (g, Icons::Kind::Download, content.removeFromLeft (20.0f).withSizeKeepingCentre (18.0f, 18.0f), enabled ? Theme::accentHot : Theme::textDim);
    content.removeFromLeft (10.0f);
    g.setColour (enabled ? Theme::text : Theme::textDim);
    g.setFont (Theme::font (13.0f, Theme::Weight::SemiBold));
    g.drawText ("Drag MIDI into your DAW", content.removeFromTop (content.getHeight() * 0.55f), juce::Justification::bottomLeft);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (11.0f));
    g.drawText ("all tracks, or drag one track's header", content, juce::Justification::topLeft);
}

//==============================================================================
SettingsPanel::SettingsPanel (Settings& s, std::function<void()> saved) : settings (s), onSaved (std::move (saved))
{
    styleLabel (heading, 16.0f, Theme::text, true);
    heading.setText ("Claude AI settings", juce::dontSendNotification);

    for (auto* l : { &keyLabel, &modelLabel, &effortLabel })
        styleLabel (*l, 13.0f, Theme::textDim);

    keyLabel.setText ("Anthropic API key", juce::dontSendNotification);
    modelLabel.setText ("Model", juce::dontSendNotification);
    effortLabel.setText ("Effort", juce::dontSendNotification);

    keyEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    keyEditor.setFont (font (14.0f));
    keyEditor.setTextToShowWhenEmpty (settings.apiKeyComesFromEnvironment() ? "Using ANTHROPIC_API_KEY from the environment"
                                                                           : "sk-ant-...", Theme::textDim);

    if (! settings.apiKeyComesFromEnvironment())
        keyEditor.setText (settings.getApiKey(), false);

    // Esc closes the panel, Return saves - from any field.
    for (auto* e : { &keyEditor, &modelEditor })
    {
        e->setEscapeAndReturnKeysConsumed (false);
        e->onReturnKey = [this] { saveButton.triggerClick(); };
    }

    keyEditor.onTextChange = [this] { clearButton.setEnabled (keyEditor.getText().isNotEmpty()); };

    modelEditor.setFont (font (14.0f));
    modelEditor.setText (settings.getModel(), false);
    modelEditor.setTextToShowWhenEmpty (Settings::defaultModel, Theme::textDim);

    effortBox.addItem ("Low (fastest)", 1);
    effortBox.addItem ("Medium", 2);
    effortBox.addItem ("High (most considered)", 3);
    const auto effort = settings.getEffort();
    effortBox.setSelectedId (effort == "low" ? 1 : (effort == "high" ? 3 : 2), juce::dontSendNotification);

    styleLabel (note, 12.0f, Theme::textDim);
    note.setJustificationType (juce::Justification::topLeft);
    auto path = settings.getFile().getFullPathName();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();

    if (home.isNotEmpty() && path.startsWith (home))
        path = "~" + path.substring (home.length());

    note.setText ("Claude reads your prompt and writes the composition plan (key, harmony, instruments, theme); "
                  "PromptMIDI then writes the notes. The key is stored only on this computer, in\n"
                  + path
                  + "\nIt is sent only to api.anthropic.com and is never saved in your DAW projects. "
                    "Requests are billed to your Anthropic account.",
                  juce::dontSendNotification);

    saveButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    saveButton.onClick = [this]
    {
        settings.setApiKey (keyEditor.getText());
        settings.setModel (modelEditor.getText().trim().isEmpty() ? juce::String (Settings::defaultModel) : modelEditor.getText());
        settings.setEffort (effortBox.getSelectedId() == 1 ? "low" : (effortBox.getSelectedId() == 3 ? "high" : "medium"));
        settings.setArticulationOutput (articulationOutputFor (articulationBox.getSelectedId()));
        settings.setArticulationValues (valuesEditor.getText());
        settings.setArticulationController (controllerEditor.getText().getIntValue());
        settings.setPreviewSound (soundSetting());

        if (onSaved)
            onSaved();

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };

    clearButton.onClick = [this] { keyEditor.clear(); clearButton.setEnabled (false); };
    clearButton.setEnabled (keyEditor.getText().isNotEmpty());

    // MIDI export: how articulations reach the sample library.
    styleLabel (exportHeading, 16.0f, Theme::text, true);
    exportHeading.setText ("MIDI export", juce::dontSendNotification);

    for (auto* l : { &articulationLabel, &valuesLabel, &controllerLabel })
        styleLabel (*l, 13.0f, Theme::textDim);

    articulationLabel.setText ("Articulations", juce::dontSendNotification);
    valuesLabel.setText ("Sustain  Legato  Stacc.  Spicc.  Marc.  Pizz.  Trem.  Trill", juce::dontSendNotification);
    controllerLabel.setText ("CC#", juce::dontSendNotification);

    articulationBox.addItem ("General MIDI (pizz. / trem. on their own tracks)", 1);
    articulationBox.addItem ("Keyswitch notes", 2);
    articulationBox.addItem ("Controller (Spitfire UACC = CC32)", 3);

    const auto output = settings.getArticulationOutput();
    articulationBox.setSelectedId (output == "keyswitch" ? 2 : (output == "cc" ? 3 : 1), juce::dontSendNotification);

    valuesEditor.setFont (font (14.0f));
    controllerEditor.setFont (font (14.0f));

    for (auto* e : { &valuesEditor, &controllerEditor })
        e->setEscapeAndReturnKeysConsumed (false);
    controllerEditor.setInputRestrictions (3, "0123456789");
    controllerEditor.setText (juce::String (settings.getArticulationController()), false);

    auto refreshValues = [this] (bool resetToDefaults)
    {
        const auto out = articulationOutputFor (articulationBox.getSelectedId());
        const bool on = out != "none";
        valuesEditor.setEnabled (on);
        controllerEditor.setEnabled (out == "cc");

        if (! on)
            valuesEditor.setText ({}, false);
        else if (resetToDefaults || valuesEditor.isEmpty())
            valuesEditor.setText (Settings::defaultArticulationValues (out), false);
    };

    if (output != "none" && settings.getArticulationValues().isNotEmpty())
        valuesEditor.setText (settings.getArticulationValues(), false);

    refreshValues (false);
    articulationBox.onChange = [this, refreshValues] { refreshValues (true); };

    styleLabel (exportNote, 12.0f, Theme::textDim);
    exportNote.setJustificationType (juce::Justification::topLeft);
    exportNote.setText ("Keyswitch notes default to C-1 upwards (MIDI 12-19); UACC values follow Spitfire's chart. "
                        "Check your library's articulation list and edit the numbers to match (-1 = don't send). "
                        "Exported and dragged MIDI then keeps each section on one track.",
                        juce::dontSendNotification);

    // Preview sound
    styleLabel (soundHeading, 16.0f, Theme::text, true);
    soundHeading.setText ("Preview sound", juce::dontSendNotification);
    styleLabel (soundNote, 12.0f, Theme::textDim);
    soundNote.setJustificationType (juce::Justification::topLeft);
    soundNote.setText ("Heard only inside PromptMIDI. The instruments are GeneralUser GS by S. Christian Collins, a General MIDI "
                       "SoundFont; your own .sf2 works too (or put it in " + SoundBank::userSoundFontFolder().getFullPathName() + ").",
                       juce::dontSendNotification);

    {
        const auto current = settings.getPreviewSound();
        soundChoice = current == "sketch" ? 2 : (current.isNotEmpty() ? 3 : 1);
        soundPath = soundChoice == 3 ? current : juce::String();
        rebuildSoundBox();
    }

    soundBox.onChange = [this]
    {
        const int id = soundBox.getSelectedId();

        if (id != 4)
        {
            soundChoice = id;
            return;
        }

        // Choose a SoundFont file.
        soundChooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", SoundBank::userSoundFontFolder(), "*.sf2");
        juce::Component::SafePointer<SettingsPanel> safe (this);
        soundChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [safe] (const juce::FileChooser& fc)
                                   {
                                       if (safe == nullptr)
                                           return;

                                       const auto file = fc.getResult();

                                       if (file.existsAsFile())
                                       {
                                           safe->soundPath = file.getFullPathName();
                                           safe->soundChoice = 3;
                                       }

                                       safe->rebuildSoundBox();
                                   });
    };

    for (auto* c : std::initializer_list<juce::Component*> { &soundHeading, &soundNote, &soundBox })
        addAndMakeVisible (c);

    for (auto* c : std::initializer_list<juce::Component*> { &heading, &keyLabel, &modelLabel, &effortLabel, &note,
                                                             &keyEditor, &modelEditor, &effortBox, &saveButton, &clearButton,
                                                             &exportHeading, &articulationLabel, &valuesLabel, &controllerLabel,
                                                             &exportNote, &articulationBox, &valuesEditor, &controllerEditor })
        addAndMakeVisible (c);

    setSize (520, 650);
}

void SettingsPanel::rebuildSoundBox()
{
    soundBox.clear (juce::dontSendNotification);
    soundBox.addItem ("Instruments - GeneralUser GS (General MIDI)", 1);
    soundBox.addItem ("Sketch synth (lightest on the CPU)", 2);

    if (soundPath.isNotEmpty())
        soundBox.addItem ("Your SoundFont: " + juce::File (soundPath).getFileName(), 3);

    soundBox.addItem ("Choose a SoundFont (.sf2)...", 4);
    soundBox.setSelectedId (soundChoice == 3 && soundPath.isEmpty() ? 1 : soundChoice, juce::dontSendNotification);
}

juce::String SettingsPanel::soundSetting() const
{
    return soundChoice == 2 ? juce::String ("sketch") : (soundChoice == 3 ? soundPath : juce::String());
}

juce::String SettingsPanel::articulationOutputFor (int id) const
{
    return id == 2 ? juce::String ("keyswitch") : (id == 3 ? juce::String ("cc") : juce::String ("none"));
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    heading.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);

    keyLabel.setBounds (r.removeFromTop (20));
    auto keyRow = r.removeFromTop (30);
    clearButton.setBounds (keyRow.removeFromRight (100));
    keyRow.removeFromRight (8);
    keyEditor.setBounds (keyRow);
    r.removeFromTop (10);

    auto row = r.removeFromTop (20);
    modelLabel.setBounds (row.removeFromLeft (r.getWidth() / 2));
    effortLabel.setBounds (row);
    row = r.removeFromTop (30);
    modelEditor.setBounds (row.removeFromLeft (r.getWidth() / 2 - 8));
    row.removeFromLeft (16);
    effortBox.setBounds (row);
    r.removeFromTop (10);

    saveButton.setBounds (r.removeFromBottom (32).removeFromRight (110));
    r.removeFromBottom (6);

    // MIDI export, from the bottom up
    auto exp = r.removeFromBottom (210);
    exportHeading.setBounds (exp.removeFromTop (26));
    exp.removeFromTop (4);
    articulationLabel.setBounds (exp.removeFromTop (20));
    articulationBox.setBounds (exp.removeFromTop (28));
    exp.removeFromTop (8);
    auto labels = exp.removeFromTop (20);
    controllerLabel.setBounds (labels.removeFromRight (60));
    valuesLabel.setBounds (labels);
    auto values = exp.removeFromTop (28);
    controllerEditor.setBounds (values.removeFromRight (60));
    values.removeFromRight (8);
    valuesEditor.setBounds (values);
    exp.removeFromTop (6);
    exportNote.setBounds (exp);

    // Preview sound, above the export section
    auto snd = r.removeFromBottom (96);
    soundHeading.setBounds (snd.removeFromTop (26));
    soundBox.setBounds (snd.removeFromTop (28));
    snd.removeFromTop (6);
    soundNote.setBounds (snd);

    r.removeFromBottom (8);
    note.setBounds (r);
}


//==============================================================================
PromptMidiEditor::PromptMidiEditor (PromptMidiProcessor& p)
    : AudioProcessorEditor (&p), processor (p), arrangement (p), dragArea (p)
{
    setLookAndFeel (&lookAndFeel);

    // Header
    for (auto* b : { &offlineButton, &claudeButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        addAndMakeVisible (b);
    }

    offlineButton.setTooltip ("Compose on this computer, instantly, without an internet connection");
    claudeButton.setTooltip ("Let Claude plan the piece from your description (needs an Anthropic API key)");
    offlineButton.onClick = [this] { if (offlineButton.getToggleState()) { processor.engine = PromptMidiProcessor::Engine::Offline; updateEngineButtons(); } };
    claudeButton.onClick = [this]
    {
        if (! claudeButton.getToggleState())
            return;

        processor.engine = PromptMidiProcessor::Engine::Claude;
        updateEngineButtons();

        if (! processor.settings->hasApiKey())
            showSettings();
    };

    settingsButton.setTooltip ("Settings: Claude API key and model, MIDI export articulations");
    settingsButton.onClick = [this] { showSettings(); };
    addAndMakeVisible (settingsButton);

    // Prompt
    promptEditor.setMultiLine (true, true);
    promptEditor.setReturnKeyStartsNewLine (false);
    promptEditor.setFont (font (16.0f));
    promptEditor.setIndents (14, 12);
    promptEditor.setTextToShowWhenEmpty ("Describe the music...   e.g. \"Generate me a fantasy orchestral midi\" or \"Make me a soft piano midi\"",
                                         Theme::textDim);
    promptEditor.setText (processor.prompt, false);
    promptEditor.onReturnKey = [this] { startGeneration(); };
    addAndMakeVisible (promptEditor);

    generateButton.setPrimary (true);
    generateButton.onClick = [this]
    {
        if (processor.isGenerating())
            processor.cancelGeneration();
        else
            startGeneration();
    };
    generateButton.setTooltip ("Turn the description into MIDI (Enter)");
    addAndMakeVisible (generateButton);

    variationButton.onClick = [this] { processor.makeVariation(); };
    variationButton.setTooltip ("A new take with the same key, tempo, length and instruments: new tune, harmony, form and orchestration");
    addAndMakeVisible (variationButton);

    for (auto& ex : examples)
    {
        auto* chip = chips.add (new juce::TextButton (ex.label));
        chipPrompts.add (ex.prompt);
        chip->setTooltip (juce::String ("\"") + ex.prompt + "\"");
        const int index = chips.size() - 1;
        chip->onClick = [this, index]
        {
            promptEditor.setText (chipPrompts[index], false);
            startGeneration();
        };
        addAndMakeVisible (chip);
    }

    // Controls
    for (auto* l : { &keyLabel, &scaleLabel, &tempoLabel, &barsLabel, &lengthLabel, &seedLabel })
    {
        styleLabel (*l, 12.5f, Theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    keyLabel.setText ("Key", juce::dontSendNotification);
    scaleLabel.setText ("Scale", juce::dontSendNotification);
    tempoLabel.setText ("Tempo", juce::dontSendNotification);
    barsLabel.setText ("Bars", juce::dontSendNotification);
    lengthLabel.setText ("Length", juce::dontSendNotification);
    seedLabel.setText ("Seed", juce::dontSendNotification);

    keyBox.addItem ("Auto", 1);

    for (int pc = 0; pc < 12; ++pc)
        keyBox.addItem (SpecJson::keyName (pc), pc + 2);

    scaleBox.addItem ("Auto", 1);

    for (auto& m : pm::allModes())
        scaleBox.addItem (m.displayName, (int) m.mode + 2);

    barsBox.addItem ("Auto", 1);

    for (int b : { 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 })
        barsBox.addItem (juce::String (b), b + 1);

    lengthBox.addItem ("Auto", 1);
    lengthBox.addItem ("Sketch (16 bars)", 2);
    lengthBox.addItem ("Cue (~1 min)", 3);
    lengthBox.addItem ("Full (~2.5 min)", 4);
    lengthBox.addItem ("Extended (~4 min)", 5);
    lengthBox.setTooltip ("How long the piece is. Orchestral cues get a complete form (intro, theme, development, build, "
                          "climax, outro or end stinger with loop points); songs get more sections.");

    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setRange (39.0, 220.0, 1.0);
    tempoSlider.textFromValueFunction = [] (double v) { return v < 40.0 ? juce::String ("Auto") : juce::String ((int) v) + " BPM"; };
    tempoSlider.valueFromTextFunction = [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("auto") ? 39.0 : (double) t.getIntValue(); };
    tempoSlider.setTooltip ("Drag to set the tempo; drag fully left (or type \"auto\") to let the prompt decide");
    tempoSlider.setValue (39.0, juce::dontSendNotification);
    tempoSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tempoSlider.updateText();

    for (auto* box : { &keyBox, &scaleBox, &barsBox, &lengthBox })
    {
        box->setSelectedId (1, juce::dontSendNotification);
        box->onChange = [this] { pushControls(); };
        addAndMakeVisible (box);
    }

    tempoSlider.onDragEnd = [this] { pushControls(); };
    tempoSlider.onValueChange = [this] { if (! tempoSlider.isMouseButtonDown()) pushControls(); };
    addAndMakeVisible (tempoSlider);

    instrumentsButton.setTooltip ("Choose the instruments: automatic from the prompt, or your own - one for a solo piece, or any ensemble");
    instrumentsButton.onClick = [this] { showInstruments(); };
    addAndMakeVisible (instrumentsButton);

    styleLabel (seedValue, 13.0f, Theme::text);
    seedValue.setEditable (true);
    seedValue.setColour (juce::Label::backgroundColourId, Theme::raised);
    seedValue.setColour (juce::Label::outlineColourId, Theme::outline);
    seedValue.setTooltip ("The random seed of this result. Type one in to recreate a result (this locks the seed).");
    seedValue.onTextChange = [this]
    {
        const auto v = seedValue.getText().getLargeIntValue();

        if (v > 0)
        {
            processor.seed = (uint64_t) v;
            processor.seedLocked = true;
            lockSeedButton.setToggleState (true, juce::dontSendNotification);
        }
    };
    addAndMakeVisible (seedValue);

    lockSeedButton.setClickingTogglesState (true);
    lockSeedButton.setTooltip ("Lock the seed: Generate gives the same result for the same prompt");
    lockSeedButton.onClick = [this] { processor.seedLocked = lockSeedButton.getToggleState(); };
    addAndMakeVisible (lockSeedButton);

    // Result
    styleLabel (titleLabel, 19.0f, Theme::text, true);
    styleLabel (summaryLabel, 13.0f, Theme::textMid);
    styleLabel (interpretationLabel, 12.0f, Theme::textDim);
    styleLabel (statusLabel, 12.5f, Theme::textDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    styleLabel (qualityLabel, 12.5f, Theme::ok, true);
    qualityLabel.setJustificationType (juce::Justification::centredRight);

    for (auto* l : { &titleLabel, &summaryLabel, &interpretationLabel, &statusLabel, &qualityLabel })
        addAndMakeVisible (l);

    prevTakeButton.setTooltip ("Previous take");
    nextTakeButton.setTooltip ("Next take");
    takeButton.setTooltip ("Every Generate and Variation is a new take, edits included: pick an earlier one to go back to it");
    prevTakeButton.onClick = [this] { processor.selectTake (processor.getCurrentTake() - 1); };
    nextTakeButton.onClick = [this] { processor.selectTake (processor.getCurrentTake() + 1); };
    takeButton.onClick = [this] { showTakeMenu(); };

    for (auto* b : std::initializer_list<juce::Component*> { &prevTakeButton, &takeButton, &nextTakeButton })
        addChildComponent (b);

    arrangement.setComponentID ("arrangement");
    addAndMakeVisible (arrangement);

    // Transport
    playButton.setTooltip ("Preview the result here (Space). The preview is only heard inside PromptMIDI - never with your DAW's playback.");
    playButton.onClick = [this] { togglePreview(); };
    addAndMakeVisible (playButton);

    styleLabel (positionLabel, 13.0f, Theme::textMid);
    positionLabel.setFont (Theme::font (13.0f, Theme::Weight::Medium));
    positionLabel.setTooltip ("Bar.beat and time of the playhead (or of the play cursor)");
    addAndMakeVisible (positionLabel);

    loopButton.setClickingTogglesState (true);
    loopButton.setTooltip ("Loop the preview. Cues with loop points play the intro once, then repeat the loop region (green), as a game would.");
    soundButton.setClickingTogglesState (true);
    soundButton.setTooltip ("Preview sound, heard only inside PromptMIDI");
    hostMidiToggle.setTooltip ("Off (default): PromptMIDI stays silent while your DAW plays. On: it sends the piece as MIDI out in time "
                               "with the host transport, to drive instruments on other tracks (never audio).");

    for (auto* c : std::initializer_list<juce::Component*> { &loopButton, &soundButton, &hostMidiToggle })
        addAndMakeVisible (c);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setTooltip ("Preview volume");
    addAndMakeVisible (volumeSlider);

    addAndMakeVisible (dragArea);

    exportButton.onClick = [this] { exportMidi(); };
    exportButton.setTooltip ("Save the MIDI file (all tracks that aren't muted)");
    addAndMakeVisible (exportButton);

    loopAttachment     = std::make_unique<ButtonAttachment> (processor.parameters, "loop", loopButton);
    hostMidiAttachment = std::make_unique<ButtonAttachment> (processor.parameters, "hostMidiOut", hostMidiToggle);
    soundAttachment    = std::make_unique<ButtonAttachment> (processor.parameters, "previewSound", soundButton);
    volumeAttachment   = std::make_unique<SliderAttachment> (processor.parameters, "previewVolume", volumeSlider);

    processor.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (1040, 680, 2600, 1600);
    setSize (1240, 800);
    setWantsKeyboardFocus (true);

    refreshFromProcessor();
    startTimerHz (30);
}

PromptMidiEditor::~PromptMidiEditor()
{
    processor.setPreviewPlaying (false); // the preview never keeps playing behind a closed window
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void PromptMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header: mark, name, tagline.
    auto header = getLocalBounds().removeFromTop (56).reduced (20, 0);
    auto mark = header.removeFromLeft (30).toFloat().withSizeKeepingCentre (28.0f, 28.0f);
    juce::ColourGradient grad (Theme::accentHot, mark.getX(), mark.getY(), Theme::accent.darker (0.35f), mark.getRight(), mark.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (mark, 7.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (mark.getX() + 7.5f, mark.getBottom() - 12.5f, 8.5f, 6.5f);
    g.fillRect (mark.getX() + 14.6f, mark.getY() + 6.0f, 1.8f, 16.0f);
    g.fillRect (mark.getX() + 14.6f, mark.getY() + 6.0f, 6.5f, 1.8f);

    header.removeFromLeft (12);
    g.setColour (Theme::text);
    g.setFont (Theme::font (19.0f, Theme::Weight::SemiBold));
    g.drawText ("PromptMIDI", header.removeFromLeft (120), juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText ("Describe it. Get MIDI.", header.removeFromLeft (220), juce::Justification::centredLeft);

    g.setColour (Theme::outline);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());

    // Prompt card
    auto card = promptEditor.getBounds().getUnion (seedValue.getBounds()).getUnion (lockSeedButton.getBounds())
                    .getUnion (generateButton.getBounds()).expanded (14, 12).toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (Theme::outline.withAlpha (0.7f));
    g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

    // Arrangement frame
    auto frame = arrangement.getBounds().toFloat().expanded (1.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (frame, 6.0f, 1.0f);

    // Footer
    auto footer = getLocalBounds().removeFromBottom (64);
    g.setColour (Theme::panelDark);
    g.fillRect (footer);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (footer.getY(), 0.0f, (float) getWidth());
}

void PromptMidiEditor::resized()
{
    auto r = getLocalBounds();
    const int pad = 20;

    // Header
    auto header = r.removeFromTop (56).reduced (pad, 12);
    settingsButton.setBounds (header.removeFromRight (34));
    header.removeFromRight (12);
    claudeButton.setBounds (header.removeFromRight (100));
    offlineButton.setBounds (header.removeFromRight (88));

    // Footer
    auto footer = r.removeFromBottom (64).reduced (pad, 12);
    playButton.setBounds (footer.removeFromLeft (48));
    footer.removeFromLeft (12);
    positionLabel.setBounds (footer.removeFromLeft (112));
    footer.removeFromLeft (6);
    loopButton.setBounds (footer.removeFromLeft (36).reduced (0, 2));
    footer.removeFromLeft (8);
    soundButton.setBounds (footer.removeFromLeft (36).reduced (0, 2));
    footer.removeFromLeft (8);
    volumeSlider.setBounds (footer.removeFromLeft (96));
    footer.removeFromLeft (20);
    exportButton.setBounds (footer.removeFromRight (132));
    footer.removeFromRight (10);
    dragArea.setBounds (footer.removeFromRight (juce::jmin (270, juce::jmax (200, footer.getWidth() - 200))));
    footer.removeFromRight (16);
    hostMidiToggle.setBounds (footer.removeFromLeft (juce::jmin (190, footer.getWidth())));

    r.reduce (pad, 0);
    r.removeFromTop (16);

    // Prompt card contents
    auto card = r.removeFromTop (172).reduced (14, 12);
    auto promptRow = card.removeFromTop (72);
    auto buttons = promptRow.removeFromRight (164);
    promptRow.removeFromRight (12);
    promptEditor.setBounds (promptRow);
    generateButton.setBounds (buttons.removeFromTop (40));
    buttons.removeFromTop (6);
    variationButton.setBounds (buttons);

    card.removeFromTop (10);
    auto chipRow = card.removeFromTop (26);
    int x = chipRow.getX();

    for (auto* chip : chips)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (Theme::font (12.5f, Theme::Weight::Medium), chip->getButtonText()) + 26;
        const bool fits = x + w <= chipRow.getRight();
        chip->setVisible (fits);

        if (fits)
            chip->setBounds (x, chipRow.getY(), w, chipRow.getHeight());

        x += w + 8;
    }

    card.removeFromTop (12);
    auto controlsRow = card.removeFromTop (32);
    const int fixedWidth = (30 + 40 + 44 + 34 + 46 + 36) + 6 * 13 + 34;
    const int naturalBoxes = 74 + 150 + 116 + 70 + 140 + 156 + 84;
    const double scale = juce::jlimit (0.6, 1.0, (double) (controlsRow.getWidth() - fixedWidth) / naturalBoxes);

    auto place = [&] (juce::Component* label, juce::Component& comp, int labelW, int compW, int minW)
    {
        if (label != nullptr)
        {
            label->setBounds (controlsRow.removeFromLeft (labelW));
            controlsRow.removeFromLeft (6);
        }

        comp.setBounds (controlsRow.removeFromLeft (juce::jmax (minW, (int) (compW * scale))));
        controlsRow.removeFromLeft (12);
    };

    place (&keyLabel, keyBox, 30, 74, 66);
    place (&scaleLabel, scaleBox, 40, 150, 100);
    place (&tempoLabel, tempoSlider, 44, 116, 80);
    place (&barsLabel, barsBox, 34, 70, 66);
    place (&lengthLabel, lengthBox, 46, 140, 96);
    place (nullptr, instrumentsButton, 0, 156, 120);
    lockSeedButton.setBounds (controlsRow.removeFromRight (32));
    controlsRow.removeFromRight (6);
    seedValue.setBounds (controlsRow.removeFromRight ((int) (84 * scale)));
    controlsRow.removeFromRight (6);
    seedLabel.setBounds (controlsRow.removeFromRight (36));

    // Result summary
    r.removeFromTop (14);
    auto line1 = r.removeFromTop (26);
    auto takeArea = line1.removeFromRight (172);
    nextTakeButton.setBounds (takeArea.removeFromRight (26).reduced (0, 1));
    takeArea.removeFromRight (4);
    prevTakeButton.setBounds (takeArea.removeFromLeft (26).reduced (0, 1));
    takeArea.removeFromLeft (4);
    takeButton.setBounds (takeArea.reduced (0, 1));
    line1.removeFromRight (12);
    statusLabel.setBounds (line1.removeFromRight (line1.getWidth() / 2));
    titleLabel.setBounds (line1);
    auto line2 = r.removeFromTop (20);
    qualityLabel.setBounds (line2.removeFromRight (juce::jmin (520, line2.getWidth() / 2)));
    summaryLabel.setBounds (line2);
    interpretationLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (10);

    arrangement.setBounds (r.withTrimmedBottom (14));
}

//==============================================================================
bool PromptMidiEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey && processor.hasComposition())
    {
        togglePreview();
        return true;
    }

    // Undo / redo note edits from anywhere in the window (the prompt box keeps its own text undo).
    const auto code = key.getKeyCode() < 256 ? juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()) : 0;

    if (key.getModifiers().isCommandDown() && (code == 'Z' || code == 'Y'))
    {
        auto& um = processor.getUndoManager();

        if (code == 'Y' || key.getModifiers().isShiftDown())
            um.redo();
        else
            um.undo();

        return true;
    }

    return false;
}

void PromptMidiEditor::togglePreview()
{
    if (! processor.hasComposition())
        return;

    const bool start = ! processor.isPreviewPlaying();

    if (start && processor.isBypassedByHost() && processor.getMsSinceLastAudio() < 1000)
    {
        setStatusText ("PromptMIDI is bypassed in your DAW: turn it back on to hear the preview.", Theme::playhead);
        return;
    }

    processor.setPreviewPlaying (start, processor.getCursorTick());
}

void PromptMidiEditor::updateTakes()
{
    const int n = processor.getNumTakes(), current = processor.getCurrentTake();
    const bool show = n > 1;

    for (auto* b : std::initializer_list<juce::Component*> { &prevTakeButton, &takeButton, &nextTakeButton })
        b->setVisible (show);

    takeButton.setButtonText ("Take " + juce::String (current + 1) + " of " + juce::String (n));
    const bool idle = ! processor.isGenerating();
    prevTakeButton.setEnabled (idle && current > 0);
    nextTakeButton.setEnabled (idle && current < n - 1);
    takeButton.setEnabled (idle);
}

void PromptMidiEditor::showTakeMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addSectionHeader ("Takes");

    for (int i = processor.getNumTakes() - 1; i >= 0; --i)
        menu.addItem (i + 1, juce::String (i + 1) + "   " + processor.getTakeTitle (i), true, i == processor.getCurrentTake());

    juce::Component::SafePointer<PromptMidiEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&takeButton), [safe] (int result)
    {
        if (safe != nullptr && result > 0)
            safe->processor.selectTake (result - 1);
    });
}

juce::String PromptMidiEditor::instrumentsText() const
{
    const auto& choices = processor.controls.instruments;

    if (choices.empty())
        return "Instruments: Auto";

    if (choices.size() == 1)
        return fromStd (choices.front().name);

    return juce::String ((int) choices.size()) + " instruments";
}

void PromptMidiEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromProcessor();
}

void PromptMidiEditor::setStatusText (const juce::String& text, juce::Colour colour)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);
    statusLabel.setTooltip (text);
}

void PromptMidiEditor::refreshFromProcessor()
{
    const auto& comp = processor.getComposition();
    const bool has = processor.hasComposition();
    const bool busy = processor.isGenerating();

    titleLabel.setText (has ? fromStd (comp.title) + (processor.isEdited() ? juce::String ("  (edited)") : juce::String())
                            : juce::String ("No MIDI yet"), juce::dontSendNotification);
    summaryLabel.setText (has ? fromStd (comp.summary()) + (comp.form.empty() ? juce::String() : juce::String::fromUTF8 ("   \xc2\xb7   ") + fromStd (comp.form))
                              : juce::String ("Pick an example below the prompt, or write your own."),
                          juce::dontSendNotification);
    summaryLabel.setTooltip (summaryLabel.getText());

    juce::StringArray notes;

    for (auto& line : processor.getSpec().interpretation)
        notes.add (fromStd (line));

    interpretationLabel.setText (has ? notes.joinIntoString (juce::String::fromUTF8 ("    \xc2\xb7    ")) : juce::String(), juce::dontSendNotification);
    interpretationLabel.setTooltip (notes.joinIntoString ("\n"));

    const auto status = processor.getStatus();
    setStatusText (processor.getStatusMessage(), status == PromptMidiProcessor::Status::Error ? Theme::error
                                               : status == PromptMidiProcessor::Status::Working ? Theme::accentHot : Theme::textDim);

    // Every generation is checked against the request and for quality; show the verdict, with
    // each criterion, the details and the form on hover.
    if (has && comp.quality.score > 0.0)
    {
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        const auto& request = comp.request;
        juce::String q;

        if (request.total() > 0)
            q << (request.allMet() ? juce::String ("Matches your request") : juce::String ("Request"))
              << " " << request.met() << "/" << request.total() << dot;

        q << "Quality " << juce::String (comp.quality.score, 1) << " / 10";

        if (comp.quality.candidates > 1)
            q << dot << "best of " << comp.quality.candidates << " drafts";

        juce::StringArray details;

        if (request.total() > 0)
        {
            details.add ("What you asked for:");

            for (auto& c : request.criteria)
                details.add (juce::String::fromUTF8 (c.met ? "  \xe2\x9c\x93  " : "  \xe2\x9c\x97  ") + fromStd (c.label)
                             + (c.met ? juce::String() : " (" + fromStd (c.detail) + ")"));

            details.add ({});
        }

        if (! comp.form.empty())
            details.add ("Form: " + fromStd (comp.form));

        if (comp.loopStart >= 0 && comp.loopEnd > comp.loopStart)
            details.add ("Loop region: bar " + juce::String (comp.loopStart / juce::jmax (1, comp.ticksPerBar) + 1) + " to bar "
                         + juce::String (comp.loopEnd / juce::jmax (1, comp.ticksPerBar) + 1));

        for (auto& line : comp.quality.notesText)
            details.add (fromStd (line));

        qualityLabel.setText (q, juce::dontSendNotification);
        qualityLabel.setColour (juce::Label::textColourId, ! request.allMet() ? Theme::playhead
                                                         : comp.quality.score >= 8.5 ? Theme::ok
                                                         : comp.quality.score >= 7.0 ? Theme::playhead : Theme::error);
        qualityLabel.setTooltip (details.joinIntoString ("\n"));
    }
    else
    {
        qualityLabel.setText ({}, juce::dontSendNotification);
        qualityLabel.setTooltip ({});
    }

    generateButton.setLabel (busy ? "Cancel" : "Generate");
    generateButton.setIcon (busy ? Icons::Kind::Cross : Icons::Kind::Sparkle);
    variationButton.setEnabled (has && ! busy);
    playButton.setEnabled (has);
    exportButton.setEnabled (has);
    dragArea.repaint();

    seedValue.setText (juce::String ((juce::int64) processor.seed), juce::dontSendNotification);
    lockSeedButton.setToggleState (processor.seedLocked, juce::dontSendNotification);
    instrumentsButton.setLabel (instrumentsText());
    instrumentsButton.setTooltip (processor.controls.instruments.empty()
                                      ? juce::String ("Choose the instruments: automatic from the prompt, or your own - one for a solo piece, or any ensemble")
                                      : "Your instruments: " + InstrumentPicker::summary (processor.controls.instruments) + ". Click to change.");
    updateTakes();
    positionLabel.setText (has ? timeText (processor.getPlayheadTick() >= 0.0 ? processor.getPlayheadTick() : processor.getCursorTick(), comp)
                               : juce::String(), juce::dontSendNotification);

    if (promptEditor.getText().isEmpty() && processor.prompt.isNotEmpty())
        promptEditor.setText (processor.prompt, false);

    if (shownVersion != processor.getCompositionVersion())
    {
        const bool newPiece = shownPiece != processor.getPieceVersion();
        shownVersion = processor.getCompositionVersion();
        shownPiece = processor.getPieceVersion();
        arrangement.compositionChanged (newPiece);
    }
    else
    {
        arrangement.mutesChanged();
    }

    updateEngineButtons();
    pullControls();
}

void PromptMidiEditor::timerCallback()
{
    if (const auto sound = processor.getPreviewSoundDescription(); sound != shownSound)
    {
        shownSound = sound;
        soundButton.setTooltip ("Preview sound: " + sound + ". Heard only inside PromptMIDI; change it in Settings.");
    }

    const bool playing = processor.isPreviewPlaying();
    const double head = processor.getPlayheadTick();
    arrangement.setPlayhead (head);
    playButton.setIcon (playing ? Icons::Kind::Stop : Icons::Kind::Play);
    positionLabel.setText (processor.hasComposition() ? timeText (head >= 0.0 ? head : processor.getCursorTick(), processor.getComposition())
                                                      : juce::String(), juce::dontSendNotification);

    // A preview needs the audio to run: say so when it doesn't, instead of a play button that does nothing.
    if (playing)
    {
        const auto now = juce::Time::getMillisecondCounter();

        if (previewStartedAt == 0)
        {
            previewStartedAt = now;
        }
        else if (! audioHintShown && now - previewStartedAt > 900 && processor.getMsSinceLastAudio() > 700)
        {
            audioHintShown = true;
            setStatusText (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone
                               ? "No audio output: choose one in Options > Audio/MIDI Settings to hear the preview."
                               : "Your DAW isn't processing audio, so the preview can't play: check its audio engine.",
                           Theme::playhead);
        }
    }
    else
    {
        previewStartedAt = 0;
        audioHintShown = false;
    }

    if (processor.getStatus() == PromptMidiProcessor::Status::Working)
    {
        ++animationFrame;
        const int dots = (animationFrame / 10) % 4;
        statusLabel.setText (processor.getStatusMessage().trimCharactersAtEnd (".") + juce::String::repeatedString (".", dots),
                             juce::dontSendNotification);
    }
}

void PromptMidiEditor::startGeneration()
{
    if (processor.engine == PromptMidiProcessor::Engine::Claude && ! processor.settings->hasApiKey())
    {
        showSettings();
        return;
    }

    processor.generate (promptEditor.getText());
}

void PromptMidiEditor::updateEngineButtons()
{
    const bool claude = processor.engine == PromptMidiProcessor::Engine::Claude;
    offlineButton.setToggleState (! claude, juce::dontSendNotification);
    claudeButton.setToggleState (claude, juce::dontSendNotification);
}

void PromptMidiEditor::pushControls()
{
    if (updatingControls)
        return;

    auto& c = processor.controls;
    c.keyRoot = keyBox.getSelectedId() > 1 ? keyBox.getSelectedId() - 2 : -1;
    c.mode = scaleBox.getSelectedId() > 1 ? scaleBox.getSelectedId() - 2 : -1;
    c.tempo = tempoSlider.getValue() < 40.0 ? 0 : (int) tempoSlider.getValue();
    c.bars = barsBox.getSelectedId() > 1 ? barsBox.getSelectedId() - 1 : 0;

    static const char* lengths[] = { "", "sketch", "cue", "full", "extended" };
    c.length = lengths[juce::jlimit (1, 5, lengthBox.getSelectedId()) - 1];
    processor.controlsChanged();
}

void PromptMidiEditor::pullControls()
{
    const juce::ScopedValueSetter<bool> svs (updatingControls, true);
    const auto& c = processor.controls;
    keyBox.setSelectedId (c.keyRoot >= 0 ? c.keyRoot + 2 : 1, juce::dontSendNotification);
    scaleBox.setSelectedId (c.mode >= 0 ? c.mode + 2 : 1, juce::dontSendNotification);
    barsBox.setSelectedId (c.bars > 0 ? c.bars + 1 : 1, juce::dontSendNotification);

    const juce::StringArray lengths { "", "sketch", "cue", "full", "extended" };
    lengthBox.setSelectedId (juce::jmax (0, lengths.indexOf (c.length)) + 1, juce::dontSendNotification);

    if (! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (c.tempo > 0 ? (double) c.tempo : 39.0, juce::dontSendNotification);
}

void PromptMidiEditor::showSettings()
{
    juce::Component::SafePointer<PromptMidiEditor> safe (this);
    const bool hadKey = processor.settings->hasApiKey();
    auto panel = std::make_unique<SettingsPanel> (*processor.settings, [safe, hadKey]
    {
        if (safe == nullptr)
            return;

        safe->processor.previewSoundChanged();

        // A key just added means you want Claude; saving other settings leaves the engine alone.
        if (! hadKey && safe->processor.settings->hasApiKey())
        {
            safe->processor.engine = PromptMidiProcessor::Engine::Claude;
            safe->updateEngineButtons();
        }
    });

    juce::CallOutBox::launchAsynchronously (std::move (panel), settingsButton.getBounds(), this);
}

void PromptMidiEditor::showInstruments()
{
    juce::Component::SafePointer<PromptMidiEditor> safe (this);
    // What the piece on screen uses, so "Customise these" can start from it.
    InstrumentPicker::Choices ensemble;

    for (auto& t : processor.getComposition().tracks)
    {
        if (t.splitFrom >= 0)
            continue;

        pm::InstrumentChoice c;
        c.name = t.name;
        c.program = t.isDrum ? -1 : t.program;
        ensemble.push_back (c);
    }

    auto picker = std::make_unique<InstrumentPicker> (processor.controls.instruments, ensemble, [safe] (const InstrumentPicker::Choices& choices)
    {
        if (safe == nullptr)
            return;

        safe->processor.controls.instruments = choices;
        safe->instrumentsButton.setLabel (safe->instrumentsText());

        if (safe->processor.hasComposition())
            safe->processor.controlsChanged();
        else if (safe->promptEditor.getText().trim().isNotEmpty())
            safe->startGeneration();
    });

    juce::CallOutBox::launchAsynchronously (std::move (picker), instrumentsButton.getBounds(), this);
}

void PromptMidiEditor::exportMidi()
{
    if (! processor.hasComposition())
        return;

    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (processor.suggestedFileName());
    chooser = std::make_unique<juce::FileChooser> ("Save MIDI file", start, "*.mid");

    juce::Component::SafePointer<PromptMidiEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              auto file = fc.getResult();

                              if (file == juce::File())
                                  return;

                              if (! file.hasFileExtension ("mid;midi"))
                                  file = file.withFileExtension ("mid");

                              const bool ok = safe->processor.writeMidiFile (file);
                              safe->setStatusText (ok ? "Saved " + file.getFileName() : "Couldn't write " + file.getFullPathName(),
                                                   ok ? Theme::ok : Theme::error);
                          });
}
