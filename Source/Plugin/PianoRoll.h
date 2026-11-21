#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Composition.h"

/** Read-only overview of the generated piece: chord lane, notes coloured per track,
    a drum lane and the playhead. The notes are cached in an image, so repainting the
    playhead at 30 fps is cheap.
*/
class PianoRoll : public juce::Component
{
public:
    PianoRoll();

    void setComposition (const pm::Composition*);
    void setSelectedTrack (int index);
    void setMutedMask (uint32_t mask);
    void setPlayhead (double tick);

    static juce::Colour trackColour (int index);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuildCache();
    juce::Rectangle<int> chordLane() const;
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> drumLane() const;
    bool hasDrums() const;

    const pm::Composition* comp = nullptr;
    int selectedTrack = -1;
    uint32_t mutedMask = 0;
    double playhead = -1.0;
    juce::Image cache;
    bool cacheDirty = true;
    float cacheScale = 1.0f;
};
