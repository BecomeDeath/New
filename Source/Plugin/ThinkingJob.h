#pragma once

#include <juce_events/juce_events.h>

#include "Thinking.h"

#include <atomic>
#include <functional>
#include <memory>

/** Runs pm::think() on a worker thread, so the window stays responsive while PromptMIDI thinks.
    The progress can be read at any time; the result is delivered on the message thread.
    Destroying the job cancels it and drops the result. */
class ThinkingJob : private juce::Thread
{
public:
    using Done = std::function<void (pm::ThinkingResult)>;

    ThinkingJob (pm::ThinkingRequest, Done onDone);
    ~ThinkingJob() override;

    /** Stop and use the best draft so far. */
    void finishNow()                       { control.store (1); }
    void cancel()                          { control.store (2); }

    pm::ThinkingProgress getProgress() const;

private:
    void run() override;

    pm::ThinkingRequest request;
    Done done;
    std::atomic<int> control { 0 };
    std::shared_ptr<std::atomic<bool>> alive;
    mutable juce::CriticalSection lock;
    pm::ThinkingProgress progress;

    JUCE_DECLARE_NON_COPYABLE (ThinkingJob)
};
