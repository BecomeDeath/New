#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Icons.h"
#include "Thinking.h"

#include <array>
#include <functional>

/** Shown over the arrangement while PromptMIDI thinks: a live timer, the stages of the work
    (reading the prompt, sketching plans, writing drafts, developing the best, the final check)
    with what each found, the best draft so far, and buttons to take the best so far or cancel. */
class ThinkingPanel : public juce::Component
{
public:
    ThinkingPanel();

    /** Call ~30 times a second while thinking. */
    void update (const pm::ThinkingProgress&, double seconds, const juce::String& thinkingFor, pm::ThinkingDepth);
    void reset();

    std::function<void()> onFinishNow, onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The card's area (the rest is a dimmed backdrop over the arrangement). */
    juce::Rectangle<int> getCardBounds() const;

private:
    static constexpr int numStages = 5;
    pm::ThinkingProgress progress;
    std::array<juce::String, numStages> details;
    double seconds = 0.0;
    juce::String forText;
    pm::ThinkingDepth depth = pm::ThinkingDepth::Thorough;
    int frame = 0;

    IconButton finishButton { "use best so far", Icons::Kind::Sparkle, "Use best so far" };
    IconButton cancelButton { "cancel", Icons::Kind::Cross, "Cancel" };
};
