#include "Timeline.h"
#include "PluginProcessor.h"

#include <cmath>
#include <set>

namespace
{
    juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    juce::String noteName (int pitch)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 1);
    }

    bool isBlackKey (int pitch)
    {
        const int pc = pitch % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    int beatTicksOf (const pm::Composition& c) { return c.tsDen == 8 ? pm::kPpq * 3 / 2 : pm::kPpq; }
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
    draggingOut = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                               [safe]
                                                               {
                                                                   draggingOut = false;

                                                                   if (safe != nullptr)
                                                                       safe->dragging = false;
                                                               });
}

//==============================================================================
class Arrangement::Corner : public juce::Component
{
public:
    explicit Corner (Arrangement& a) : arr (a)
    {
        zoomOut.setTooltip ("Zoom out (Ctrl / Cmd + mouse wheel)");
        zoomIn.setTooltip ("Zoom in (Ctrl / Cmd + mouse wheel)");
        fitButton.setTooltip ("Show the whole piece");
        followButton.setTooltip ("Follow the playhead while previewing");
        followButton.setClickingTogglesState (true);
        followButton.setToggleState (arr.follow, juce::dontSendNotification);

        for (auto* b : { &zoomOut, &zoomIn, &fitButton, &followButton })
        {
            b->setFlat (true);
            addAndMakeVisible (b);
        }

        zoomOut.onClick = [this] { arr.zoomBy (1.0 / 1.6, arr.lanesArea().getWidth() * 0.5f); };
        zoomIn.onClick = [this] { arr.zoomBy (1.6, arr.lanesArea().getWidth() * 0.5f); };
        fitButton.onClick = [this] { arr.fit(); };
        followButton.onClick = [this] { arr.follow = followButton.getToggleState(); };
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panelDark);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (11.0f, Theme::Weight::SemiBold));
        const int n = (int) arr.comp().tracks.size();
        g.drawText (n > 0 ? juce::String::fromUTF8 ("TRACKS  \xc2\xb7  ") + juce::String (n) : juce::String ("TRACKS"), getLocalBounds().removeFromTop (24).reduced (14, 0),
                    juce::Justification::centredLeft);
        g.setColour (Theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0).withTrimmedTop (24).withTrimmedBottom (4);
        for (auto* b : { &zoomOut, &zoomIn, &fitButton, &followButton })
        {
            b->setBounds (r.removeFromLeft (28));
            r.removeFromLeft (4);
        }
    }

private:
    Arrangement& arr;
    IconButton zoomOut { "zoomOut", Icons::Kind::ZoomOut }, zoomIn { "zoomIn", Icons::Kind::ZoomIn },
               fitButton { "fit", Icons::Kind::Fit }, followButton { "follow", Icons::Kind::Follow };
};

//==============================================================================
/** Bars, sections and the loop region, and chords; click or drag to move the play cursor. */
class Arrangement::Ruler : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Ruler (Arrangement& a) : arr (a)
    {
        setTooltip ("Click to set where Play starts (while previewing, playback jumps there)");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = arr.comp();
        const int w = getWidth();
        g.fillAll (Theme::panelDark);

        if (c.lengthTicks <= 0)
            return;

        const auto& ax = arr.axis;
        const int tpb = juce::jmax (1, c.ticksPerBar);
        const double pxPerBar = tpb * ax.pixelsPerTick;
        int step = 1;

        while (pxPerBar * step < 46.0 && step < 64)
            step *= 2;

        // Bars
        auto bars = getLocalBounds().removeFromTop (20);
        g.setFont (Theme::font (11.0f, Theme::Weight::Medium));

        const int firstBar = juce::jmax (0, (int) (ax.startTick / tpb) - 1);
        const int lastBar = (int) (ax.toTick (w) / tpb) + 1;

        for (int bar = firstBar; bar <= lastBar && bar * tpb <= c.lengthTicks; ++bar)
        {
            const float x = ax.toX ((double) bar * tpb);
            const bool labelled = bar % step == 0;
            g.setColour (labelled ? Theme::gridStrong.brighter (0.4f) : Theme::gridStrong);
            g.drawVerticalLine ((int) x, labelled ? 9.0f : 14.0f, 20.0f);

            if (labelled)
            {
                g.setColour (Theme::textMid);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, 2.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
            }
        }

        // Sections
        auto sections = getLocalBounds().withTrimmedTop (20).removeFromTop (18);
        std::vector<const pm::Marker*> marks;

        for (auto& m : c.markers)
            if (m.text.rfind ("Loop", 0) != 0)
                marks.push_back (&m);

        g.setFont (Theme::font (11.0f, Theme::Weight::SemiBold));

        for (size_t i = 0; i < marks.size(); ++i)
        {
            const float x0 = ax.toX (marks[i]->tick);
            const float x1 = i + 1 < marks.size() ? ax.toX (marks[i + 1]->tick) : ax.toX (c.lengthTicks);

            if (x1 < 0.0f || x0 > (float) w)
                continue;

            auto band = juce::Rectangle<float> (x0 + 1.0f, (float) sections.getY() + 1.0f, juce::jmax (1.0f, x1 - x0 - 2.0f), (float) sections.getHeight() - 2.0f);
            g.setColour (Theme::accent.withAlpha (i % 2 == 0 ? 0.22f : 0.14f));
            g.fillRoundedRectangle (band, 3.0f);

            // The name stays visible when the section starts off-screen, but never runs into the next one.
            auto textArea = band.reduced (6.0f, 0.0f);
            textArea.setLeft (juce::jmax (textArea.getX(), 4.0f));

            if (textArea.getWidth() > 24.0f)
            {
                g.setColour (Theme::text.withAlpha (0.9f));
                g.drawText (fromStd (marks[i]->text), textArea, juce::Justification::centredLeft, true);
            }
        }

        if (c.loopStart >= 0 && c.loopEnd > c.loopStart)
        {
            const float x0 = ax.toX (c.loopStart), x1 = ax.toX (c.loopEnd);
            g.setColour (Theme::ok);
            g.fillRect (juce::Rectangle<float> (x0, (float) sections.getBottom() - 2.0f, x1 - x0, 2.0f));
            g.setFont (Theme::font (10.0f, Theme::Weight::SemiBold));
            g.drawText ("LOOP", juce::Rectangle<float> (x1 - 44.0f, (float) sections.getY() + 1.0f, 40.0f, 15.0f), juce::Justification::centredRight);
        }

        // Chords
        auto chords = getLocalBounds().withTrimmedTop (38);
        g.setFont (Theme::font (11.0f, Theme::Weight::Medium));

        for (auto& ch : c.chords)
        {
            const float x0 = ax.toX (ch.start), x1 = ax.toX (ch.end);

            if (x1 < 0.0f || x0 > (float) w)
                continue;

            g.setColour (Theme::grid);
            g.drawVerticalLine ((int) x0, (float) chords.getY() + 3.0f, (float) chords.getBottom() - 3.0f);

            if (x1 - x0 > 26.0f)
            {
                g.setColour (Theme::textMid);
                g.drawText (fromStd (ch.name), juce::Rectangle<float> (x0 + 4.0f, (float) chords.getY(), x1 - x0 - 6.0f, (float) chords.getHeight()),
                            juce::Justification::centredLeft, true);
            }
        }

        // Play cursor and playhead
        const float cx = ax.toX (arr.processor.getCursorTick());
        juce::Path cursorMark;
        cursorMark.addTriangle (cx - 5.0f, 0.0f, cx + 5.0f, 0.0f, cx, 7.0f);
        g.setColour (Theme::accentHot);
        g.fillPath (cursorMark);

        if (arr.playhead >= 0.0)
        {
            const float x = ax.toX (arr.playhead);
            g.setColour (Theme::playhead);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
        }

        g.setColour (Theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) w);
        g.drawHorizontalLine (20, 0.0f, (float) w);
    }

    void mouseDown (const juce::MouseEvent& e) override { seek (e); }
    void mouseDrag (const juce::MouseEvent& e) override { seek (e); }

private:
    void seek (const juce::MouseEvent& e)
    {
        const auto& c = arr.comp();

        if (c.lengthTicks <= 0)
            return;

        const int beat = beatTicksOf (c);
        double tick = arr.axis.toTick (e.position.x);
        tick = std::round (tick / beat) * beat;
        arr.processor.setCursorTick (juce::jlimit (0.0, (double) c.lengthTicks, tick));
        repaint();
    }

    Arrangement& arr;
};

//==============================================================================
/** One overview lane per track. */
class Arrangement::Lanes : public juce::Component
{
public:
    explicit Lanes (Arrangement& a) : arr (a) {}

    void paint (juce::Graphics& g) override
    {
        const auto& c = arr.comp();
        g.fillAll (Theme::panel);

        if (c.lengthTicks <= 0 || c.tracks.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (15.0f));
            g.drawText ("Describe some music above and press Generate", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto& ax = arr.axis;
        const int w = getWidth();
        const int tpb = juce::jmax (1, c.ticksPerBar);
        const int beat = beatTicksOf (c);
        const double tickL = ax.toTick (0.0), tickR = ax.toTick (w);

        // Rows
        for (int i = 0; i < (int) c.tracks.size(); ++i)
        {
            const int y = i * laneHeight - arr.rowOffset;

            if (y + laneHeight < 0 || y > getHeight())
                continue;

            g.setColour (i == arr.selected ? Theme::raised : (i % 2 == 0 ? Theme::panel : Theme::panel.brighter (0.025f)));
            g.fillRect (0, y, w, laneHeight);
        }

        // Loop region and grid
        if (c.loopStart >= 0 && c.loopEnd > c.loopStart)
        {
            g.setColour (Theme::ok.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (ax.toX (c.loopStart), 0.0f, ax.toX (c.loopEnd) - ax.toX (c.loopStart), (float) getHeight()));
        }

        const bool beats = beat * ax.pixelsPerTick >= 12.0;

        for (int t = ((int) (tickL / beat)) * beat; t <= tickR && t <= c.lengthTicks; t += beat)
        {
            if (t < 0)
                continue;

            const bool bar = t % tpb == 0;

            if (! bar && ! beats)
                continue;

            g.setColour (bar ? Theme::gridStrong : Theme::grid);
            g.drawVerticalLine ((int) ax.toX (t), 0.0f, (float) getHeight());
        }

        for (auto& m : c.markers)
        {
            if (m.text.rfind ("Loop", 0) == 0)
                continue;

            g.setColour (Theme::accent.withAlpha (0.35f));
            g.drawVerticalLine ((int) ax.toX (m.tick), 0.0f, (float) getHeight());
        }

        // Notes
        for (int i = 0; i < (int) c.tracks.size(); ++i)
        {
            const int y = i * laneHeight - arr.rowOffset;

            if (y + laneHeight < 0 || y > getHeight())
                continue;

            const auto& t = c.tracks[(size_t) i];
            const bool muted = arr.processor.isTrackMuted (i);
            const float alpha = muted ? 0.22f : (arr.selected < 0 || arr.selected == i ? 0.95f : 0.55f);
            const auto colour = Theme::trackColour (i);
            int lo = 127, hi = 0;

            for (auto& n : t.notes)
            {
                lo = juce::jmin (lo, n.pitch);
                hi = juce::jmax (hi, n.pitch);
            }

            if (lo > hi)
                continue;

            const float top = (float) y + 6.0f, bottom = (float) (y + laneHeight) - 6.0f;
            const float span = (float) juce::jmax (12, hi - lo);
            const float noteH = juce::jlimit (1.5f, 3.5f, (bottom - top) / (span + 1.0f) * 1.6f);

            for (auto& n : t.notes)
            {
                if (n.start + n.length < tickL || n.start > tickR)
                    continue;

                const float x0 = ax.toX (n.start);
                const float py = bottom - (float) (n.pitch - lo) / span * (bottom - top - noteH) - noteH;
                g.setColour (colour.withAlpha (alpha * (0.55f + 0.45f * (float) n.velocity / 127.0f)));

                if (t.isDrum)
                    g.fillRect (x0, py, 2.0f, noteH + 1.0f);
                else
                    g.fillRect (x0, py, juce::jmax (1.5f, ax.toX (n.start + n.length) - x0 - 0.5f), noteH);
            }

            g.setColour (Theme::grid);
            g.drawHorizontalLine (y + laneHeight - 1, 0.0f, (float) w);
        }

        if (arr.playhead >= 0.0)
        {
            g.setColour (Theme::playhead);
            g.fillRect (ax.toX (arr.playhead) - 1.0f, 0.0f, 2.0f, (float) getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int row = (int) ((e.position.y + arr.rowOffset) / laneHeight);

        if (row >= 0 && row < (int) arr.comp().tracks.size())
            arr.selectTrack (row == arr.selected && e.getNumberOfClicks() == 1 ? arr.selected : row);
    }

private:
    Arrangement& arr;
};

//==============================================================================
/** Track name, instrument and mute; click to edit, drag out for just this track. */
class Arrangement::TrackHeader : public MidiDragSource
{
public:
    TrackHeader (Arrangement& a, int index) : MidiDragSource (a.processor, index), arr (a)
    {
        mute.setClickingTogglesState (true);
        mute.setTooltip ("Mute (left out of the preview and the full export)");
        mute.onClick = [this] { processor.setTrackMuted (track, mute.getToggleState()); arr.lanes->repaint(); };
        addAndMakeVisible (mute);
        setTooltip ("Click to edit this track's notes. Drag into your DAW to get just this track.");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        refresh();
    }

    void refresh()
    {
        mute.setToggleState (processor.isTrackMuted (track), juce::dontSendNotification);
        subtitle = {};
        const auto& c = arr.comp();

        if (track >= 0 && track < (int) c.tracks.size())
        {
            // The sound it asks for and the range it plays (roles change from section to section in a cue).
            const auto& t = c.tracks[(size_t) track];
            subtitle = t.isDrum ? juce::String ("Percussion") : juce::String (pm::gmProgramName (t.program));
            int lo = 128, hi = -1;

            for (auto& n : t.notes)
            {
                lo = juce::jmin (lo, n.pitch);
                hi = juce::jmax (hi, n.pitch);
            }

            if (! t.isDrum && hi >= lo)
                subtitle << juce::String::fromUTF8 ("  \xc2\xb7  ") << pitchName (lo) << juce::String::fromUTF8 ("\xe2\x80\x93") << pitchName (hi);

            setTooltip (fromStd (t.name) + " - " + juce::String ((int) t.notes.size()) + " notes. "
                        "Click to edit its notes; drag into your DAW to get just this track.");
        }

        repaint();
    }

    static juce::String pitchName (int p)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[p % 12]) + juce::String (p / 12 - 1);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = arr.comp();

        if (track < 0 || track >= (int) c.tracks.size())
            return;

        const auto& t = c.tracks[(size_t) track];
        const bool selected = arr.selected == track;
        const bool muted = processor.isTrackMuted (track);
        auto r = getLocalBounds();

        g.setColour (selected ? Theme::raised : Theme::panelDark);
        g.fillRect (r);
        g.setColour (Theme::grid);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        const auto colour = Theme::trackColour (track);
        g.setColour (muted ? colour.withAlpha (0.3f) : colour);
        g.fillRect (r.removeFromLeft (3).reduced (0, 5));

        r.removeFromLeft (11);
        r.removeFromRight (34);
        r = r.reduced (0, 5);

        g.setColour (muted ? Theme::textDim : Theme::text);
        g.setFont (Theme::font (13.0f, Theme::Weight::Medium));
        g.drawText (fromStd (t.name), r.removeFromTop (r.getHeight() / 2 + 1), juce::Justification::bottomLeft, true);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (11.0f));
        g.drawText (subtitle, r, juce::Justification::topLeft, true);
    }

    void resized() override { mute.setBounds (getWidth() - 32, (getHeight() - 20) / 2, 24, 20); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() < 6)
            arr.selectTrack (arr.selected == track ? -1 : track);

        MidiDragSource::mouseUp (e);
    }

private:
    class MuteButton : public juce::Button
    {
    public:
        MuteButton() : Button ("M") {}

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            g.setColour (on ? Theme::error.withAlpha (0.85f) : (over ? Theme::hover : Theme::raised));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? juce::Colours::white : Theme::textMid);
            g.setFont (Theme::font (11.0f, Theme::Weight::SemiBold));
            g.drawText ("M", getLocalBounds(), juce::Justification::centred);
        }
    };

    Arrangement& arr;
    MuteButton mute;
    juce::String subtitle;
};

//==============================================================================
/** Piano-roll editor for one track: select, move, resize, add and delete notes, with undo. */
class Arrangement::NoteEditor : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit NoteEditor (Arrangement& a) : arr (a)
    {
        setWantsKeyboardFocus (true);

        snapBox.addItem ("1/4", 1);
        snapBox.addItem ("1/8", 2);
        snapBox.addItem ("1/16", 3);
        snapBox.addItem ("1/32", 4);
        snapBox.addItem ("Off", 5);
        snapBox.setSelectedId (3, juce::dontSendNotification);
        snapBox.setTooltip ("Grid for moving, resizing and adding notes");
        addAndMakeVisible (snapBox);

        undoButton.setTooltip ("Undo (Ctrl / Cmd + Z)");
        redoButton.setTooltip ("Redo (Ctrl / Cmd + Shift + Z)");
        closeButton.setTooltip ("Close the note editor");

        for (auto* b : { &undoButton, &redoButton, &closeButton })
        {
            b->setFlat (true);
            addAndMakeVisible (b);
        }

        undoButton.onClick = [this] { arr.processor.getUndoManager().undo(); };
        redoButton.onClick = [this] { arr.processor.getUndoManager().redo(); };
        closeButton.onClick = [this] { arr.selectTrack (-1); };

        vScroll.addListener (this);
        vScroll.setAutoHide (false);
        addAndMakeVisible (vScroll);
    }

    ~NoteEditor() override { vScroll.removeListener (this); }

    void setTrack (int t)
    {
        if (t != track)
        {
            track = t;
            selection.clear();
            centreOnNotes();
        }

        repaint();
    }

    void refresh()
    {
        // Indices may have moved after an edit: keep only valid ones.
        const auto& notes = trackNotes();
        std::set<int> valid;

        for (int i : selection)
            if (i < (int) notes.size())
                valid.insert (i);

        selection = valid;
        undoButton.setEnabled (arr.processor.getUndoManager().canUndo());
        redoButton.setEnabled (arr.processor.getUndoManager().canRedo());
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.position.y < barHeight ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (8, 4);
        closeButton.setBounds (bar.removeFromRight (28));
        bar.removeFromRight (6);
        redoButton.setBounds (bar.removeFromRight (28));
        undoButton.setBounds (bar.removeFromRight (28));
        bar.removeFromRight (8);
        snapBox.setBounds (bar.removeFromRight (74));
        vScroll.setBounds (getWidth() - 10, barHeight, 10, getHeight() - barHeight);
        updateScroll();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = arr.comp();
        g.fillAll (Theme::panel);

        // Title bar
        auto bar = getLocalBounds().removeFromTop (barHeight);
        g.setColour (Theme::panelDark);
        g.fillRect (bar);
        g.setColour (Theme::outline);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        g.drawHorizontalLine (barHeight - 1, 0.0f, (float) getWidth());

        if (track < 0 || track >= (int) c.tracks.size())
            return;

        const auto& t = c.tracks[(size_t) track];
        auto title = bar.reduced (14, 0);
        g.setColour (Theme::trackColour (track));
        g.fillEllipse (title.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
        title.removeFromLeft (8);
        g.setColour (Theme::text);
        g.setFont (Theme::font (13.0f, Theme::Weight::SemiBold));
        const auto name = fromStd (t.name);
        g.drawText (name, title, juce::Justification::centredLeft);
        title.removeFromLeft ((int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 14);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.0f));
        title.setRight (snapBox.getX() - 12);
        g.drawText (juce::String::fromUTF8 ("Click to select  \xc2\xb7  drag to move  \xc2\xb7  drag the end to resize  \xc2\xb7  double-click to add or delete  \xc2\xb7  arrows transpose"),
                    title, juce::Justification::centredLeft, true);

        // Keyboard
        auto body = getLocalBounds().withTrimmedTop (barHeight);
        auto keys = body.removeFromLeft (headerWidth);
        body.removeFromRight (10);
        const auto& ax = arr.axis;
        g.setColour (Theme::panelDark);
        g.fillRect (keys.withTrimmedRight (58));

        for (int p = 127; p >= 0; --p)
        {
            const float y = pitchToY (p);

            if (y > getHeight() || y + rowH < barHeight)
                continue;

            const bool black = isBlackKey (p);
            const auto key = juce::Rectangle<float> ((float) keys.getRight() - 58.0f, y, 58.0f, rowH);
            g.setColour (black ? juce::Colour (0xff0b0d10) : juce::Colour (0xffc9ced8));
            g.fillRect (black ? key.withTrimmedRight (18.0f) : key);

            if (! black)
            {
                g.setColour (juce::Colour (0xff8d939e));
                g.drawHorizontalLine ((int) (y + rowH) - 1, key.getX(), key.getRight());
            }

            if (black)
            {
                g.setColour (juce::Colour (0xffc9ced8));
                g.fillRect (key.withTrimmedLeft (40.0f));
            }

            if (p % 12 == 0)
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (10.0f, Theme::Weight::Medium));
                g.drawText (noteName (p), juce::Rectangle<float> ((float) keys.getRight() - 96.0f, y - 2.0f, 34.0f, rowH + 2.0f),
                            juce::Justification::centredRight);
            }

            // Grid row
            g.setColour (black ? Theme::panelDark.brighter (0.02f) : Theme::panel);
            g.fillRect (juce::Rectangle<float> ((float) body.getX(), y, (float) body.getWidth(), rowH));

            if (p % 12 == 0)
            {
                g.setColour (Theme::gridStrong);
                g.drawHorizontalLine ((int) (y + rowH), (float) body.getX(), (float) body.getRight());
            }
        }


        // Vertical grid
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (body);
        const int tpb = juce::jmax (1, c.ticksPerBar);
        const int beat = beatTicksOf (c);
        const int grid = snapTicks();
        const double tickL = ax.toTick (0.0), tickR = ax.toTick (body.getWidth());
        const int stepTicks = grid * ax.pixelsPerTick >= 8.0 ? grid : (beat * ax.pixelsPerTick >= 8.0 ? beat : tpb);

        for (int tk = ((int) (tickL / stepTicks)) * stepTicks; tk <= tickR; tk += stepTicks)
        {
            if (tk < 0)
                continue;

            const float x = (float) body.getX() + ax.toX (tk);
            g.setColour (tk % tpb == 0 ? Theme::gridStrong.brighter (0.1f) : (tk % beat == 0 ? Theme::gridStrong : Theme::grid));
            g.drawVerticalLine ((int) x, (float) body.getY(), (float) body.getBottom());
        }

        // Notes
        const auto& notes = dragging ? preview : trackNotes();
        const auto colour = Theme::trackColour (track);

        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];

            if (n.start + n.length < tickL || n.start > tickR)
                continue;

            auto r = noteRect (n).translated ((float) body.getX(), 0.0f);
            const bool sel = selection.count (i) > 0;
            g.setColour (colour.withAlpha (0.45f + 0.55f * (float) n.velocity / 127.0f));
            g.fillRoundedRectangle (r, 2.0f);

            if (sel)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r.expanded (0.5f), 2.0f, 1.5f);
            }
        }

        if (lassoing)
        {
            g.setColour (Theme::accent.withAlpha (0.15f));
            g.fillRect (lasso.toFloat());
            g.setColour (Theme::accent);
            g.drawRect (lasso.toFloat(), 1.0f);
        }

        if (arr.playhead >= 0.0)
        {
            g.setColour (Theme::playhead);
            g.fillRect ((float) body.getX() + ax.toX (arr.playhead) - 1.0f, (float) body.getY(), 2.0f, (float) body.getHeight());
        }
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        if (e.position.y < barHeight)
        {
            // Drag the title bar to give the editor more or less room.
            resizingFrom = arr.editorFraction;
            mode = Mode::Divider;
            return;
        }

        if (! inGrid (e.position))
            return;

        const auto& notes = trackNotes();
        const int hit = noteAt (notes, e.position);
        dragStart = e.position;
        original = notes;
        preview = notes;
        changed = false;

        if (hit >= 0)
        {
            if (e.mods.isShiftDown())
            {
                if (selection.count (hit) > 0) selection.erase (hit);
                else                           selection.insert (hit);
            }
            else if (selection.count (hit) == 0)
            {
                selection = { hit };
            }

            const auto r = noteRect (notes[(size_t) hit]);
            mode = (e.position.x - gridX() > r.getRight() - 6.0f) ? Mode::Resize : Mode::Move;
            dragging = true;
            arr.processor.auditionNote (track, notes[(size_t) hit].pitch, notes[(size_t) hit].velocity);
        }
        else
        {
            if (! e.mods.isShiftDown())
                selection.clear();

            mode = Mode::Lasso;
            lassoing = true;
            lasso = juce::Rectangle<int> (e.getPosition(), e.getPosition());
        }

        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == Mode::Divider)
        {
            const float total = (float) juce::jmax (1, arr.getHeight());
            arr.editorFraction = juce::jlimit (0.2f, 0.8f, resizingFrom - (float) e.getDistanceFromDragStartY() / total);
            arr.resized();
            return;
        }

        if (mode == Mode::Lasso)
        {
            lasso = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition());
            const auto& notes = trackNotes();
            selection.clear();

            for (int i = 0; i < (int) notes.size(); ++i)
                if (noteRect (notes[(size_t) i]).translated (gridX(), 0.0f).intersects (lasso.toFloat()))
                    selection.insert (i);

            repaint();
            return;
        }

        if (! dragging || selection.empty())
            return;

        const int grid = snapTicks();
        const double dt = (e.position.x - dragStart.x) / arr.axis.pixelsPerTick;
        const int dTicks = (int) std::round (dt / grid) * grid;
        const int dPitch = (int) std::round ((dragStart.y - e.position.y) / rowH);
        preview = original;

        for (int i : selection)
        {
            auto& n = preview[(size_t) i];
            const auto& o = original[(size_t) i];

            if (mode == Mode::Move)
            {
                n.start = juce::jmax (0, o.start + dTicks);
                n.pitch = juce::jlimit (0, 127, o.pitch + dPitch);
            }
            else
            {
                const int end = juce::jmax (o.start + grid, (int) std::round ((o.start + o.length + dt) / grid) * grid);
                n.length = juce::jmax (grid, end - o.start);
            }
        }

        changed = dTicks != 0 || dPitch != 0 || mode == Mode::Resize;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && changed)
            commit (preview, mode == Mode::Move ? "Move notes" : "Resize notes");

        dragging = false;
        lassoing = false;
        mode = Mode::None;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! inGrid (e.position) || track < 0)
            return;

        auto notes = trackNotes();
        const int hit = noteAt (notes, e.position);

        if (hit >= 0)
        {
            notes.erase (notes.begin() + hit);
            selection.clear();
            commit (notes, "Delete note");
            return;
        }

        const int grid = snapTicks();
        pm::Note n;
        n.start = juce::jmax (0, (int) std::floor (arr.axis.toTick (e.position.x - gridX()) / grid) * grid);
        n.length = juce::jmax (grid, 240);
        n.pitch = yToPitch (e.position.y);
        n.velocity = 90;
        notes.push_back (n);
        commit (notes, "Add note");
        arr.processor.auditionNote (track, n.pitch, n.velocity);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            Component::mouseWheelMove (e, w); // zoom / sideways scroll: the arrangement handles it
            return;
        }

        topPitch = juce::jlimit (visibleRows() - 1.0, 127.0, topPitch + w.deltaY * 30.0);
        updateScroll();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode() < 256 ? juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()) : 0;

        // Ctrl / Cmd + Z, Shift + Z and Y are handled by the window, so they work wherever the focus is.

        if (key == juce::KeyPress::escapeKey)
        {
            if (! selection.empty())
            {
                selection.clear();
                repaint();
            }
            else
            {
                arr.selectTrack (-1);
            }

            return true;
        }

        if (code == 'A' && mods.isCommandDown())
        {
            selection.clear();

            for (int i = 0; i < (int) trackNotes().size(); ++i)
                selection.insert (i);

            repaint();
            return true;
        }

        if (selection.empty())
            return false;

        auto notes = trackNotes();

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            std::vector<pm::Note> kept;

            for (int i = 0; i < (int) notes.size(); ++i)
                if (selection.count (i) == 0)
                    kept.push_back (notes[(size_t) i]);

            selection.clear();
            commit (kept, "Delete notes");
            return true;
        }

        int dPitch = 0, dTicks = 0;

        if (key.getKeyCode() == juce::KeyPress::upKey)    dPitch = mods.isShiftDown() ? 12 : 1;
        if (key.getKeyCode() == juce::KeyPress::downKey)  dPitch = mods.isShiftDown() ? -12 : -1;
        if (key.getKeyCode() == juce::KeyPress::rightKey) dTicks = snapTicks();
        if (key.getKeyCode() == juce::KeyPress::leftKey)  dTicks = -snapTicks();

        if (dPitch == 0 && dTicks == 0)
            return false;

        for (int i : selection)
        {
            notes[(size_t) i].pitch = juce::jlimit (0, 127, notes[(size_t) i].pitch + dPitch);
            notes[(size_t) i].start = juce::jmax (0, notes[(size_t) i].start + dTicks);
        }

        commit (notes, dPitch != 0 ? "Transpose notes" : "Move notes");

        if (dPitch != 0 && selection.size() == 1)
            arr.processor.auditionNote (track, notes[(size_t) *selection.begin()].pitch, 90);

        return true;
    }

private:
    enum class Mode { None, Move, Resize, Lasso, Divider };
    static constexpr int barHeight = 34;
    static constexpr float rowH = 10.0f;

    const std::vector<pm::Note>& trackNotes() const
    {
        static const std::vector<pm::Note> none;
        const auto& c = arr.comp();
        return track >= 0 && track < (int) c.tracks.size() ? c.tracks[(size_t) track].notes : none;
    }

    int snapTicks() const
    {
        switch (snapBox.getSelectedId())
        {
            case 1:  return 480;
            case 2:  return 240;
            case 4:  return 60;
            case 5:  return 10;
            default: return 120;
        }
    }

    float gridX() const                              { return (float) headerWidth; }
    bool inGrid (juce::Point<float> p) const         { return p.x >= gridX() && p.y >= barHeight && p.x < getWidth() - 10; }
    double visibleRows() const                       { return (getHeight() - barHeight) / rowH; }
    float pitchToY (int pitch) const                 { return (float) barHeight + (float) (topPitch - pitch) * rowH; }
    int yToPitch (float y) const                     { return juce::jlimit (0, 127, (int) std::floor (topPitch - (y - barHeight) / rowH) + 1); }

    juce::Rectangle<float> noteRect (const pm::Note& n) const
    {
        const float x0 = arr.axis.toX (n.start);
        const float x1 = arr.axis.toX (n.start + n.length);
        return { x0, pitchToY (n.pitch) + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), rowH - 2.0f };
    }

    int noteAt (const std::vector<pm::Note>& notes, juce::Point<float> p) const
    {
        for (int i = (int) notes.size() - 1; i >= 0; --i)
            if (noteRect (notes[(size_t) i]).translated (gridX(), 0.0f).expanded (0.0f, 1.0f).contains (p))
                return i;

        return -1;
    }

    void commit (std::vector<pm::Note> notes, const juce::String& name)
    {
        // Remember the selection by identity; the processor re-sorts the notes.
        std::vector<pm::Note> chosen;

        for (int i : selection)
            if (i < (int) notes.size())
                chosen.push_back (notes[(size_t) i]);

        arr.processor.editTrackNotes (track, notes, name);
        selection.clear();
        const auto& now = trackNotes();

        for (auto& s : chosen)
            for (int i = 0; i < (int) now.size(); ++i)
                if (now[(size_t) i].start == s.start && now[(size_t) i].pitch == s.pitch && selection.count (i) == 0)
                {
                    selection.insert (i);
                    break;
                }

        refresh();
    }

    void centreOnNotes()
    {
        const auto& notes = trackNotes();
        int lo = 127, hi = 0;

        for (auto& n : notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }

        const double mid = lo <= hi ? (lo + hi) * 0.5 : 60.0;
        topPitch = juce::jlimit (juce::jmax (visibleRows() - 1.0, 12.0), 127.0, mid + visibleRows() * 0.5);
        updateScroll();
    }

    void updateScroll()
    {
        vScroll.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
        vScroll.setCurrentRange (127.0 - topPitch, visibleRows(), juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar*, double start) override
    {
        topPitch = 127.0 - start;
        repaint();
    }

    Arrangement& arr;
    int track = -1;
    double topPitch = 84.0;
    std::set<int> selection;
    std::vector<pm::Note> original, preview;
    juce::Point<float> dragStart;
    Mode mode = Mode::None;
    bool dragging = false, changed = false, lassoing = false;
    float resizingFrom = 0.46f;
    juce::Rectangle<int> lasso;
    juce::ComboBox snapBox;
    IconButton undoButton { "undo", Icons::Kind::Undo }, redoButton { "redo", Icons::Kind::Redo }, closeButton { "close", Icons::Kind::Cross };
    juce::ScrollBar vScroll { true };
};

//==============================================================================
Arrangement::Arrangement (PromptMidiProcessor& p) : processor (p)
{
    corner = std::make_unique<Corner> (*this);
    ruler = std::make_unique<Ruler> (*this);
    lanes = std::make_unique<Lanes> (*this);
    editor = std::make_unique<NoteEditor> (*this);

    addAndMakeVisible (*corner);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (*lanes);
    addAndMakeVisible (headerHolder);
    addChildComponent (*editor);

    for (auto* s : { &hScroll, &vScroll })
    {
        s->addListener (this);
        s->setAutoHide (false);
        addAndMakeVisible (s);
    }

    compositionChanged (true);
}

Arrangement::~Arrangement()
{
    hScroll.removeListener (this);
    vScroll.removeListener (this);
}

const pm::Composition& Arrangement::comp() const { return processor.getComposition(); }

juce::Rectangle<int> Arrangement::lanesArea() const { return lanes->getBounds(); }

void Arrangement::compositionChanged (bool newPiece)
{
    headers.clear();

    for (int i = 0; i < (int) comp().tracks.size(); ++i)
        headerHolder.addAndMakeVisible (headers.add (new TrackHeader (*this, i)));

    if (selected >= (int) comp().tracks.size())
        selected = -1;

    if (newPiece)
    {
        selected = -1;
        rowOffset = 0;
    }

    resized();

    if (newPiece)
        fit();

    editor->setTrack (selected);
    editor->refresh();
    corner->repaint();
    ruler->repaint();
    lanes->repaint();
}

void Arrangement::mutesChanged()
{
    for (auto* h : headers)
        h->refresh();

    lanes->repaint();
}

void Arrangement::selectTrack (int index)
{
    selected = (index >= 0 && index < (int) comp().tracks.size()) ? index : -1;
    resized();

    if (selected >= 0)
    {
        revealTrack (selected);

        // Notes a pixel wide can't be edited: zoom in to about twelve bars, from the track's first note in view.
        const int tpb = juce::jmax (1, comp().ticksPerBar);

        if (axis.pixelsPerTick * tpb < 40.0)
        {
            const auto& notes = comp().tracks[(size_t) selected].notes;
            int first = -1;

            for (auto& n : notes)
                if (n.start + n.length > axis.startTick && (first < 0 || n.start < first))
                    first = n.start;

            const int w = juce::jmax (1, lanes->getWidth());
            axis.pixelsPerTick = juce::jlimit (w / (double) juce::jmax (comp().lengthTicks, tpb), 0.6, w / (12.0 * tpb));
            axis.startTick = first >= 0 ? (double) ((juce::jmax (0, first) / tpb) * tpb) : axis.startTick;
            axisChanged();
        }
    }

    editor->setTrack (selected);
    editor->refresh();

    for (auto* h : headers)
        h->repaint();

    lanes->repaint();

    if (selected >= 0)
        editor->grabKeyboardFocus();
}

void Arrangement::revealTrack (int index)
{
    const int top = index * laneHeight, bottom = top + laneHeight, h = lanes->getHeight();

    if (top < rowOffset)
        rowOffset = top;
    else if (bottom > rowOffset + h)
        rowOffset = bottom - h;

    updateScrollBars();
    layoutRows();
    lanes->repaint();
}

void Arrangement::setPlayhead (double tick)
{
    if (std::abs (tick - playhead) < 0.5)
        return;

    const double old = playhead;
    playhead = tick;
    const int w = lanes->getWidth();

    if (tick >= 0.0 && follow)
    {
        const float x = axis.toX (tick);

        if (x > w * 0.85f || x < 0.0f)
        {
            axis.startTick = tick - (w / axis.pixelsPerTick) * 0.1;
            axisChanged();
            return;
        }
    }

    auto repaintAt = [&] (double t)
    {
        if (t < 0.0)
            return;

        const int x = (int) axis.toX (t);
        ruler->repaint (x - 3, 0, 6, ruler->getHeight());
        lanes->repaint (x - 3, 0, 6, lanes->getHeight());

        if (editor->isVisible())
            editor->repaint (headerWidth + x - 3, 0, 6, editor->getHeight());
    };

    repaintAt (old);
    repaintAt (tick);
}

void Arrangement::zoomBy (double factor, float anchorX)
{
    const int w = juce::jmax (1, lanes->getWidth());
    const double length = juce::jmax (comp().lengthTicks, comp().ticksPerBar);
    const double minPpt = w / length;
    const double maxPpt = 0.6;
    const double anchorTick = axis.toTick (anchorX);
    axis.pixelsPerTick = juce::jlimit (minPpt, maxPpt, axis.pixelsPerTick * factor);
    axis.startTick = anchorTick - anchorX / axis.pixelsPerTick;
    axisChanged();
}

void Arrangement::fit()
{
    const int w = juce::jmax (1, lanes->getWidth());
    axis.pixelsPerTick = w / (double) juce::jmax (comp().lengthTicks, comp().ticksPerBar, 1);
    axis.startTick = 0.0;
    axisChanged();
}

void Arrangement::axisChanged()
{
    const double visible = lanes->getWidth() / axis.pixelsPerTick;
    axis.startTick = juce::jlimit (0.0, juce::jmax (0.0, comp().lengthTicks - visible), axis.startTick);
    updateScrollBars();
    ruler->repaint();
    lanes->repaint();
    editor->repaint();
}

void Arrangement::updateScrollBars()
{
    const double visible = lanes->getWidth() / juce::jmax (1.0e-6, axis.pixelsPerTick);
    hScroll.setRangeLimits (0.0, juce::jmax ((double) comp().lengthTicks, visible), juce::dontSendNotification);
    hScroll.setCurrentRange (axis.startTick, visible, juce::dontSendNotification);

    const int total = (int) comp().tracks.size() * laneHeight;
    rowOffset = juce::jlimit (0, juce::jmax (0, total - lanes->getHeight()), rowOffset);
    vScroll.setRangeLimits (0.0, juce::jmax ((double) total, (double) lanes->getHeight()), juce::dontSendNotification);
    vScroll.setCurrentRange (rowOffset, lanes->getHeight(), juce::dontSendNotification);
}

void Arrangement::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll)
    {
        axis.startTick = start;
        ruler->repaint();
        lanes->repaint();
        editor->repaint();
    }
    else
    {
        rowOffset = (int) start;
        layoutRows();
        lanes->repaint();
    }
}

void Arrangement::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float x = e.getEventRelativeTo (lanes.get()).position.x;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomBy (std::exp ((double) w.deltaY * 1.6), juce::jlimit (0.0f, (float) lanes->getWidth(), x));
        return;
    }

    const bool sideways = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
    const int total = (int) comp().tracks.size() * laneHeight;
    const bool overLanes = e.eventComponent == lanes.get() || headerHolder.isParentOf (e.eventComponent);

    if (! sideways && overLanes && total > lanes->getHeight())
    {
        rowOffset = juce::jlimit (0, total - lanes->getHeight(), rowOffset - (int) (w.deltaY * 240.0f));
        layoutRows();
        updateScrollBars();
        lanes->repaint();
        return;
    }

    const float delta = sideways ? (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) : w.deltaY;
    axis.startTick -= delta * 360.0 / axis.pixelsPerTick;
    axisChanged();
}

void Arrangement::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panelDark);
}

void Arrangement::layoutRows()
{
    const int width = headerHolder.getWidth();

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, i * laneHeight - rowOffset, width, laneHeight);
}

void Arrangement::resized()
{
    auto r = getLocalBounds();
    const bool editing = selected >= 0;
    const int editorH = editing ? juce::jmax (180, (int) (r.getHeight() * editorFraction)) : 0;
    auto bottom = r.removeFromBottom (editorH);
    editor->setVisible (editing);
    editor->setBounds (bottom);

    auto rulerRow = r.removeFromTop (rulerHeight);
    corner->setBounds (rulerRow.removeFromLeft (headerWidth));
    ruler->setBounds (rulerRow.withTrimmedRight (10));

    auto hBar = r.removeFromBottom (10);
    hScroll.setBounds (hBar.withTrimmedLeft (headerWidth).withTrimmedRight (10));
    vScroll.setBounds (r.removeFromRight (10));
    headerHolder.setBounds (r.removeFromLeft (headerWidth));
    lanes->setBounds (r);

    layoutRows();
    axisChanged();
}
