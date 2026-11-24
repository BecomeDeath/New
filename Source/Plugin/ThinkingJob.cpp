#include "ThinkingJob.h"

ThinkingJob::ThinkingJob (pm::ThinkingRequest r, Done onDone)
    : juce::Thread ("PromptMIDI thinking"),
      request (std::move (r)),
      done (std::move (onDone)),
      alive (std::make_shared<std::atomic<bool>> (true))
{
    progress.totalDrafts = (request.budget ? *request.budget : pm::budgetFor (request.depth)).totalDrafts();
    startThread (juce::Thread::Priority::normal);
}

ThinkingJob::~ThinkingJob()
{
    alive->store (false);
    control.store (2);
    signalThreadShouldExit();
    stopThread (10000);
}

pm::ThinkingProgress ThinkingJob::getProgress() const
{
    const juce::ScopedLock sl (lock);
    return progress;
}

void ThinkingJob::run()
{
    request.control = &control;
    request.onProgress = [this] (const pm::ThinkingProgress& p)
    {
        const juce::ScopedLock sl (lock);
        progress = p;
    };

    auto result = pm::think (request);

    if (threadShouldExit() || control.load() == 2)
        return;

    juce::MessageManager::callAsync ([flag = alive, callback = done, r = std::move (result)]() mutable
    {
        if (flag->load() && callback)
            callback (std::move (r));
    });
}
