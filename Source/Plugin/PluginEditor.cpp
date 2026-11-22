#include "PluginEditor.h"
#include "SpecJson.h"

namespace
{
    juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    void styleLabel (juce::Label& l, float size, juce::Colour colour, bool bold = false)
    {
        l.setFont (font (size, bold));
        l.setColour (juce::Label::textColourId, colour);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setMinimumHorizontalScale (0.8f);
    }

    struct ExamplePrompt { const char* label; const char* prompt; };

    const ExamplePrompt examples[] = {
        { "Fantasy orchestral", "Generate me a fantasy orchestral midi" },
        { "Soft piano",         "Make me a soft piano midi" },
        { "Lo-fi hip hop",      "Lo-fi hip hop beat with a mellow electric piano, rainy night" },
        { "Epic trailer",       "Epic cinematic trailer music with choir, brass and big drums" },
        { "Jazz combo",         "Late night jazz combo with a tenor sax lead" },
        { "Synthwave",          "80s synthwave night drive with arpeggios" },
        { "Celtic tavern",      "Lively celtic tavern jig with fiddle and whistle" },
        { "Dark horror",        "Creepy horror music box with tense strings" },
        { "Deep house",         "Deep house groove at 124 bpm" },
        { "Lullaby",            "Gentle lullaby for music box and harp" },
    };
}

//==============================================================================
void MidiDragSource::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! processor.hasComposition() || e.getDistanceFromDragStart() < 6)
        return;

    dragging = true;
    processor.setPreviewPlaying (false); // the MIDI goes to the DAW; the preview stays here
    const auto file = processor.writeMidiToTempFile (track);

    juce::Component::SafePointer<MidiDragSource> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                               [safe] { if (safe != nullptr) safe->dragging = false; });
}

void DragAllArea::paint (juce::Graphics& g)
{
    const bool enabled = processor.hasComposition();
    const bool hover = isMouseOver (true) && enabled;
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (hover ? Theme::accent.withAlpha (0.18f) : Theme::panelDark);
    g.fillRoundedRectangle (r, 8.0f);

    juce::Path border;
    border.addRoundedRectangle (r, 8.0f);
    const float dashes[] = { 5.0f, 4.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (enabled ? Theme::accent.withAlpha (hover ? 1.0f : 0.7f) : Theme::outline);
    g.fillPath (dashed);

    // down-arrow icon
    auto icon = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.28f);
    juce::Path arrow;
    arrow.startNewSubPath (icon.getCentreX(), icon.getY());
    arrow.lineTo (icon.getCentreX(), icon.getBottom() - 4.0f);
    arrow.startNewSubPath (icon.getCentreX() - 6.0f, icon.getBottom() - 10.0f);
    arrow.lineTo (icon.getCentreX(), icon.getBottom() - 4.0f);
    arrow.lineTo (icon.getCentreX() + 6.0f, icon.getBottom() - 10.0f);
    arrow.startNewSubPath (icon.getX(), icon.getBottom());
    arrow.lineTo (icon.getRight(), icon.getBottom());
    g.setColour (enabled ? Theme::text : Theme::textDim);
    g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto textArea = r.reduced (4.0f, 6.0f);
    g.setFont (font (14.0f, true));
    g.drawText ("Drag MIDI into your DAW", textArea.removeFromTop (textArea.getHeight() * 0.55f), juce::Justification::bottomLeft);
    g.setColour (Theme::textDim);
    g.setFont (font (11.5f));
    g.drawText ("all tracks - or drag one track from the list", textArea, juce::Justification::topLeft);
}

//==============================================================================
TrackRow::TrackRow (PromptMidiProcessor& p, int index) : MidiDragSource (p, index)
{
    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, Theme::error.withAlpha (0.8f));
    muteButton.setTooltip ("Mute this track (muted tracks are left out of playback and of the full export)");
    muteButton.onClick = [this] { processor.setTrackMuted (track, muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    setTooltip ("Click to highlight. Drag into your DAW to get just this track.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    refresh();
}

void TrackRow::setSelected (bool s)
{
    if (s != selected)
    {
        selected = s;
        repaint();
    }
}

void TrackRow::refresh()
{
    muteButton.setToggleState (processor.isTrackMuted (track), juce::dontSendNotification);
    repaint();
}

void TrackRow::resized()
{
    muteButton.setBounds (getWidth() - 34, (getHeight() - 22) / 2, 26, 22);
}

void TrackRow::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect (track);
}

void TrackRow::paint (juce::Graphics& g)
{
    const auto& comp = processor.getComposition();

    if (track < 0 || track >= (int) comp.tracks.size())
        return;

    const auto& t = comp.tracks[(size_t) track];
    const bool muted = processor.isTrackMuted (track);
    auto r = getLocalBounds().reduced (4, 2);

    g.setColour (selected ? Theme::raised : Theme::panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);

    const auto colour = PianoRoll::trackColour (track);
    g.setColour (muted ? colour.withAlpha (0.3f) : colour);
    g.fillRoundedRectangle (r.removeFromLeft (5).toFloat(), 2.5f);

    r.removeFromLeft (8);
    r.removeFromRight (36);

    g.setColour (muted ? Theme::textDim : Theme::text);
    g.setFont (font (13.5f, true));
    g.drawText (fromStd (t.name), r.removeFromTop (r.getHeight() / 2 + 1), juce::Justification::bottomLeft, true);

    const juce::String instrument = t.isDrum ? juce::String ("Drum kit") : juce::String (pm::gmProgramName (t.program));
    g.setColour (Theme::textDim);
    g.setFont (font (11.5f));
    g.drawText (instrument + "  \xc2\xb7  " + pm::roleDisplayName (t.role) + "  \xc2\xb7  ch " + juce::String (t.channel + 1),
                r, juce::Justification::topLeft, true);
}

//==============================================================================
TrackList::TrackList (PromptMidiProcessor& p) : processor (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void TrackList::rebuild()
{
    rows.clear();
    const auto& comp = processor.getComposition();

    if (selected >= (int) comp.tracks.size())
        selected = -1;

    for (int i = 0; i < (int) comp.tracks.size(); ++i)
    {
        auto* row = rows.add (new TrackRow (processor, i));
        row->setSelected (i == selected);
        row->onSelect = [this] (int index)
        {
            selected = (selected == index) ? -1 : index;

            for (auto* r : rows)
                r->setSelected (r->getTrackIndex() == selected);

            if (onSelectionChanged)
                onSelectionChanged (selected);
        };
        content.addAndMakeVisible (row);
    }

    resized();
}

void TrackList::refreshMutes()
{
    for (auto* r : rows)
        r->refresh();
}

void TrackList::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (Theme::textDim);
    g.setFont (font (11.0f, true));
    g.drawText ("TRACKS", getLocalBounds().removeFromTop (24).reduced (10, 0), juce::Justification::centredLeft);

    if (rows.isEmpty())
    {
        g.setFont (font (12.5f));
        g.drawFittedText ("Your tracks will appear here.", getLocalBounds().reduced (12, 30), juce::Justification::centredTop, 3);
    }
}

void TrackList::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (24);
    viewport.setBounds (r.reduced (2, 2));

    const int rowH = 44;
    const int width = viewport.getMaximumVisibleWidth();
    content.setSize (width, rowH * rows.size());

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, width, rowH);
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
    note.setText ("Claude reads your prompt and writes the composition plan (key, harmony, instruments, theme); "
                  "PromptMIDI then writes the notes. The key is stored only on this computer, in\n"
                  + settings.getFile().getFullPathName()
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

        if (onSaved)
            onSaved();

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };

    clearButton.onClick = [this] { keyEditor.clear(); };

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

    for (auto* c : std::initializer_list<juce::Component*> { &heading, &keyLabel, &modelLabel, &effortLabel, &note,
                                                             &keyEditor, &modelEditor, &effortBox, &saveButton, &clearButton,
                                                             &exportHeading, &articulationLabel, &valuesLabel, &controllerLabel,
                                                             &exportNote, &articulationBox, &valuesEditor, &controllerEditor })
        addAndMakeVisible (c);

    setSize (500, 560);
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

    r.removeFromBottom (8);
    note.setBounds (r);
}

//==============================================================================
PromptMidiEditor::PromptMidiEditor (PromptMidiProcessor& p)
    : AudioProcessorEditor (&p), processor (p), trackList (p), dragArea (p)
{
    setLookAndFeel (&lookAndFeel);

    // Header
    for (auto* b : { &offlineButton, &claudeButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        addAndMakeVisible (b);
    }

    offlineButton.setTooltip ("Compose on this computer, instantly, without an internet connection.");
    claudeButton.setTooltip ("Let Claude plan the piece from your description (needs an Anthropic API key).");
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

    settingsButton.onClick = [this] { showSettings(); };
    settingsButton.setTooltip ("API key and model for Claude AI mode");
    addAndMakeVisible (settingsButton);

    // Prompt
    promptEditor.setMultiLine (true, true);
    promptEditor.setReturnKeyStartsNewLine (false);
    promptEditor.setFont (font (17.0f));
    promptEditor.setIndents (12, 10);
    promptEditor.setTextToShowWhenEmpty ("Describe the music... e.g. \"Generate me a fantasy orchestral midi\" or \"Make me a soft piano midi\"",
                                         Theme::textDim);
    promptEditor.setText (processor.prompt, false);
    promptEditor.onReturnKey = [this] { startGeneration(); };
    addAndMakeVisible (promptEditor);

    generateButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
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
    variationButton.setTooltip ("Same style, key and instruments - new melody and rhythms");
    addAndMakeVisible (variationButton);

    for (auto& ex : examples)
    {
        auto* chip = chips.add (new juce::TextButton (ex.label));
        chipPrompts.add (ex.prompt);
        chip->setColour (juce::TextButton::buttonColourId, Theme::panel);
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
    lengthBox.setTooltip ("Length of orchestral cues: a sketch, or a complete cue with intro, theme, development, build, "
                          "climax and outro (or end stinger and loop points when the prompt asks for a loop)");

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

    lockSeedButton.setTooltip ("Keep the seed, so Generate gives the same result for the same prompt");
    lockSeedButton.onClick = [this] { processor.seedLocked = lockSeedButton.getToggleState(); };
    addAndMakeVisible (lockSeedButton);

    // Info
    styleLabel (titleLabel, 19.0f, Theme::text, true);
    styleLabel (summaryLabel, 13.5f, Theme::text);
    styleLabel (interpretationLabel, 12.0f, Theme::textDim);
    styleLabel (statusLabel, 12.5f, Theme::textDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    styleLabel (qualityLabel, 12.5f, Theme::ok);
    qualityLabel.setJustificationType (juce::Justification::centredRight);

    for (auto* l : { &titleLabel, &summaryLabel, &interpretationLabel, &statusLabel, &qualityLabel })
        addAndMakeVisible (l);

    trackList.onSelectionChanged = [this] (int index) { pianoRoll.setSelectedTrack (index); };
    addAndMakeVisible (trackList);
    addAndMakeVisible (pianoRoll);

    // Footer
    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { processor.setPreviewPlaying (! processor.isPreviewPlaying()); };
    playButton.setTooltip ("Preview the result. While the host is playing (and Follow host is on) playback follows the host instead.");
    addAndMakeVisible (playButton);

    loopToggle.setTooltip ("Loop during playback. Cues with loop points play the intro once, then repeat the loop region "
                           "(green in the piano roll), the way a game engine plays them");
    syncToggle.setTooltip ("Off (default): PromptMIDI stays silent while your DAW plays. On: it sends the piece as MIDI out "
                           "in time with the host transport, to drive instruments on other tracks (never audio)");
    soundToggle.setTooltip ("Hear the result through PromptMIDI's built-in sketch synth");

    for (auto* t : { &loopToggle, &syncToggle, &soundToggle })
        addAndMakeVisible (t);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setTooltip ("Preview volume");
    addAndMakeVisible (volumeSlider);

    addAndMakeVisible (dragArea);

    exportButton.onClick = [this] { exportMidi(); };
    exportButton.setTooltip ("Save the MIDI file (all tracks that aren't muted)");
    addAndMakeVisible (exportButton);

    loopAttachment    = std::make_unique<ButtonAttachment> (processor.parameters, "loop", loopToggle);
    syncAttachment    = std::make_unique<ButtonAttachment> (processor.parameters, "hostMidiOut", syncToggle);
    soundAttachment   = std::make_unique<ButtonAttachment> (processor.parameters, "previewSound", soundToggle);
    volumeAttachment  = std::make_unique<SliderAttachment> (processor.parameters, "previewVolume", volumeSlider);

    processor.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (1000, 620, 2400, 1500);
    setSize (1100, 740);

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

    auto header = getLocalBounds().removeFromTop (56).reduced (18, 0);

    // Logo: a small note-head mark next to the name.
    auto mark = header.removeFromLeft (28).toFloat().withSizeKeepingCentre (24.0f, 24.0f);
    juce::ColourGradient grad (Theme::accentHot, mark.getX(), mark.getY(), Theme::accent.darker (0.3f), mark.getRight(), mark.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (mark, 6.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (mark.getX() + 6.0f, mark.getBottom() - 11.0f, 8.0f, 6.0f);
    g.fillRect (mark.getX() + 12.5f, mark.getY() + 5.0f, 1.8f, 14.0f);

    header.removeFromLeft (10);
    g.setColour (Theme::text);
    g.setFont (font (20.0f, true));
    g.drawText ("PromptMIDI", header.removeFromLeft (130), juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (font (13.0f));
    g.drawText ("Describe it. Get MIDI.", header.removeFromLeft (200), juce::Justification::centredLeft);

    g.setColour (Theme::outline);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - 72, 0.0f, (float) getWidth());
}

void PromptMidiEditor::resized()
{
    auto r = getLocalBounds();
    const int pad = 18;

    // Header
    auto header = r.removeFromTop (56).reduced (pad, 12);
    settingsButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (12);
    claudeButton.setBounds (header.removeFromRight (100));
    offlineButton.setBounds (header.removeFromRight (90));

    r.reduce (pad, 0);
    r.removeFromTop (14);

    // Prompt + buttons
    auto promptRow = r.removeFromTop (86);
    auto buttons = promptRow.removeFromRight (150);
    promptRow.removeFromRight (12);
    promptEditor.setBounds (promptRow);
    generateButton.setBounds (buttons.removeFromTop (46));
    buttons.removeFromTop (6);
    variationButton.setBounds (buttons);

    // Example chips
    r.removeFromTop (10);
    auto chipRow = r.removeFromTop (28);
    int x = chipRow.getX();

    for (auto* chip : chips)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (font (13.0f), chip->getButtonText()) + 26;
        const bool fits = x + w <= chipRow.getRight();
        chip->setVisible (fits);

        if (fits)
            chip->setBounds (x, chipRow.getY(), w, chipRow.getHeight());

        x += w + 8;
    }

    // Overrides
    r.removeFromTop (12);
    auto controlsRow = r.removeFromTop (30);

    // The boxes share whatever width there is beyond the labels and gaps (up to their natural size).
    const int fixedWidth = (30 + 40 + 46 + 34 + 46 + 36) + 6 * 18 + 64;
    const int naturalBoxes = 72 + 150 + 124 + 70 + 142 + 96;
    const double boxScale = juce::jlimit (0.6, 1.0, (double) (controlsRow.getWidth() - fixedWidth) / naturalBoxes);

    auto place = [&] (juce::Component& label, juce::Component& comp, int labelW, int compW)
    {
        label.setBounds (controlsRow.removeFromLeft (labelW));
        controlsRow.removeFromLeft (6);
        comp.setBounds (controlsRow.removeFromLeft ((int) (compW * boxScale)));
        controlsRow.removeFromLeft (12);
    };

    place (keyLabel, keyBox, 30, 72);
    place (scaleLabel, scaleBox, 40, 150);
    place (tempoLabel, tempoSlider, 46, 124);
    place (barsLabel, barsBox, 34, 70);
    place (lengthLabel, lengthBox, 46, 142);
    place (seedLabel, seedValue, 36, 96);
    lockSeedButton.setBounds (controlsRow.removeFromLeft (64));

    // Info
    r.removeFromTop (12);
    auto line1 = r.removeFromTop (26);
    statusLabel.setBounds (line1.removeFromRight (line1.getWidth() / 2));
    titleLabel.setBounds (line1);
    auto line2 = r.removeFromTop (20);
    qualityLabel.setBounds (line2.removeFromRight (juce::jmin (420, line2.getWidth() / 2)));
    summaryLabel.setBounds (line2);
    interpretationLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (8);

    // Footer
    auto footer = getLocalBounds().removeFromBottom (72).reduced (pad, 12);
    r.removeFromBottom (72 + 12);

    playButton.setBounds (footer.removeFromLeft (84));
    footer.removeFromLeft (12);
    loopToggle.setBounds (footer.removeFromLeft (64));
    syncToggle.setBounds (footer.removeFromLeft (160));
    soundToggle.setBounds (footer.removeFromLeft (118));
    volumeSlider.setBounds (footer.removeFromLeft (90).reduced (0, 10));
    footer.removeFromLeft (10);

    exportButton.setBounds (footer.removeFromRight (112));
    footer.removeFromRight (12);
    dragArea.setBounds (footer.removeFromRight (juce::jmin (300, juce::jmax (200, footer.getWidth()))));

    // Main area
    trackList.setBounds (r.removeFromLeft (280));
    r.removeFromLeft (12);
    pianoRoll.setBounds (r);
}

//==============================================================================
void PromptMidiEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromProcessor();
}

void PromptMidiEditor::refreshFromProcessor()
{
    const auto& comp = processor.getComposition();
    const bool has = processor.hasComposition();
    const bool busy = processor.isGenerating();

    titleLabel.setText (has ? fromStd (comp.title) : juce::String ("No MIDI yet"), juce::dontSendNotification);
    summaryLabel.setText (has ? fromStd (comp.summary()) : juce::String ("Pick an example below the prompt, or write your own."),
                          juce::dontSendNotification);

    juce::StringArray notes;

    for (auto& line : processor.getSpec().interpretation)
        notes.add (fromStd (line));

    interpretationLabel.setText (has ? notes.joinIntoString ("    \xc2\xb7    ") : juce::String(), juce::dontSendNotification);
    interpretationLabel.setTooltip (notes.joinIntoString ("\n"));

    // Every generation is checked; show the verdict, with the details and the form on hover.
    if (has && comp.quality.score > 0.0)
    {
        juce::String q;
        q << "Quality " << juce::String (comp.quality.score, 1) << " / 10";

        if (comp.quality.candidates > 1)
            q << juce::String::fromUTF8 ("  \xc2\xb7  ") << "best of " << comp.quality.candidates << " takes";

        juce::StringArray details;

        if (! comp.form.empty())
            details.add ("Form: " + fromStd (comp.form));

        if (comp.loopStart >= 0 && comp.loopEnd > comp.loopStart)
            details.add ("Loop region: bar " + juce::String (comp.loopStart / juce::jmax (1, comp.ticksPerBar) + 1) + " to bar "
                         + juce::String (comp.loopEnd / juce::jmax (1, comp.ticksPerBar) + 1));

        for (auto& line : comp.quality.notesText)
            details.add (fromStd (line));

        qualityLabel.setText (q, juce::dontSendNotification);
        qualityLabel.setColour (juce::Label::textColourId, comp.quality.score >= 8.5 ? Theme::ok
                                                         : comp.quality.score >= 7.0 ? Theme::playhead : Theme::error);
        qualityLabel.setTooltip (details.joinIntoString ("\n"));
    }
    else
    {
        qualityLabel.setText ({}, juce::dontSendNotification);
        qualityLabel.setTooltip ({});
    }

    const auto status = processor.getStatus();
    statusLabel.setText (processor.getStatusMessage(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, status == PromptMidiProcessor::Status::Error ? Theme::error
                                                    : status == PromptMidiProcessor::Status::Working ? Theme::accentHot : Theme::textDim);
    statusLabel.setTooltip (processor.getStatusMessage());

    generateButton.setButtonText (busy ? "Cancel" : "Generate");
    variationButton.setEnabled (has && ! busy);
    playButton.setEnabled (has);
    exportButton.setEnabled (has);
    dragArea.repaint();

    seedValue.setText (juce::String ((juce::int64) processor.seed), juce::dontSendNotification);
    lockSeedButton.setToggleState (processor.seedLocked, juce::dontSendNotification);

    if (promptEditor.getText().isEmpty() && processor.prompt.isNotEmpty())
        promptEditor.setText (processor.prompt, false);

    if (shownVersion != processor.getCompositionVersion())
    {
        shownVersion = processor.getCompositionVersion();
        trackList.rebuild();
        pianoRoll.setComposition (has ? &comp : nullptr);
    }
    else
    {
        trackList.refreshMutes();
    }

    pianoRoll.setMutedMask (processor.getMutedMask());
    pianoRoll.setSelectedTrack (trackList.getSelected());

    updateEngineButtons();
    pullControls();
}

void PromptMidiEditor::timerCallback()
{
    pianoRoll.setPlayhead (processor.getPlayheadTick());
    playButton.setButtonText (processor.isPreviewPlaying() ? "Stop" : "Play");

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
    auto panel = std::make_unique<SettingsPanel> (*processor.settings, [safe]
    {
        if (safe != nullptr)
        {
            safe->processor.engine = PromptMidiProcessor::Engine::Claude;
            safe->updateEngineButtons();
        }
    });

    juce::CallOutBox::launchAsynchronously (std::move (panel), settingsButton.getBounds(), this);
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
                              safe->statusLabel.setText (ok ? "Saved " + file.getFileName() : "Couldn't write " + file.getFullPathName(),
                                                         juce::dontSendNotification);
                              safe->statusLabel.setColour (juce::Label::textColourId, ok ? Theme::ok : Theme::error);
                          });
}
