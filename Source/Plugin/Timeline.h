#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Composition.h"
#include "Icons.h"

class PromptMidiProcessor;

/** The tick <-> pixel mapping shared by the ruler, the lanes and the note editor. */
struct TimeAxis
{
    double pixelsPerTick = 0.05;
    double startTick = 0.0;

    float toX (double tick) const      { return (float) ((tick - startTick) * pixelsPerTick); }
    double toTick (double x) const     { return startTick + x / pixelsPerTick; }
};

/** Drag source that hands a .mid file to the DAW (or the desktop). */
class MidiDragSource : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDragSource (PromptMidiProcessor& p, int trackIndex) : processor (p), track (trackIndex) {}

    void setTrackIndex (int t) { track = t; }
    int getTrackIndex() const  { return track; }

    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    /** True while PromptMIDI itself is dragging a file out (so it doesn't import its own drop). */
    static bool isDraggingOut()                    { return draggingOut; }

protected:
    PromptMidiProcessor& processor;
    int track = -1;
    bool dragging = false;
    static inline bool draggingOut = false;
};

/** The arrangement: track headers beside one lane per track, under a ruler with bars, sections,
    the loop region and chords; scrolls and zooms like a DAW, and opens a note editor for the
    selected track.

    Mouse: click the ruler to move the play cursor; wheel scrolls (Shift = sideways),
    Ctrl / Cmd + wheel zooms; click a lane or header to edit that track (the view zooms in to
    its notes if they're too small to edit); drag a header out to the DAW to get that track alone.
    Keys in the note editor: arrows move / transpose, Delete removes, Ctrl / Cmd + A selects all,
    Esc clears the selection, then closes the editor.
*/
class Arrangement : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit Arrangement (PromptMidiProcessor&);
    ~Arrangement() override;

    /** A new piece (refits the view) or an edit of the current one (keeps the view). */
    void compositionChanged (bool newPiece);
    void mutesChanged();
    void setPlayhead (double tick);

    void zoomBy (double factor, float anchorX);
    void fit();
    void selectTrack (int index);
    int getSelectedTrack() const { return selected; }
    const TimeAxis& getAxis() const { return axis; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int headerWidth = 216;
    static constexpr int rulerHeight = 56;
    static constexpr int laneHeight = 40;

private:
    class Ruler;
    class Lanes;
    class TrackHeader;
    class NoteEditor;
    class Corner;

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void updateScrollBars();
    void axisChanged();
    void layoutRows();
    void revealTrack (int index);
    juce::Rectangle<int> lanesArea() const;
    const pm::Composition& comp() const;

    PromptMidiProcessor& processor;
    TimeAxis axis;
    int selected = -1;
    int rowOffset = 0;   // vertical scroll of the lanes, in pixels
    double playhead = -1.0;
    bool follow = true;
    float editorFraction = 0.46f;

    std::unique_ptr<Corner> corner;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Lanes> lanes;
    juce::Component headerHolder;
    juce::OwnedArray<TrackHeader> headers;
    std::unique_ptr<NoteEditor> editor;
    juce::ScrollBar hScroll { false }, vScroll { true };

    friend class Ruler;
    friend class Lanes;
    friend class TrackHeader;
    friend class NoteEditor;
    friend class Corner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Arrangement)
};
