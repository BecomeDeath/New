#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "StyleSpec.h"

#include <functional>
#include <memory>

/** Talks to the Claude Messages API.

    Claude doesn't write the notes itself: it reads the prompt and returns a composition
    plan (key, mode, harmony, instruments, patterns, an optional theme) as structured JSON.
    The same engine that runs offline then turns the plan into MIDI. That keeps requests
    small and fast, and guarantees the result is always valid, playable MIDI.
*/
namespace Claude
{
    struct Options
    {
        juce::String apiKey;
        juce::String model = "claude-opus-5-5";
        juce::String effort = "medium";   // low | medium | high
        int timeoutMs = 180000;
    };

    struct Result
    {
        bool ok = false;
        bool retryable = false;
        pm::StyleSpec spec;
        juce::String error;
        juce::String model; // which model produced the answer
    };

    juce::String systemPrompt();
    juce::String buildUserMessage (const juce::String& prompt, const juce::String& constraints);
    juce::String buildRequestBody (const juce::String& prompt, const juce::String& constraints, const Options&);
    juce::String buildHeaders (const Options&);
    bool modelSupportsFallbacks (const juce::String& model);

    /** Turns an HTTP status and body into a Result (no network involved; unit-tested). */
    Result parseResponse (int statusCode, const juce::String& body);

    /** Runs one request on a background thread and calls back on the message thread.
        Destroying the job cancels the request; the callback is then never called.
    */
    class Job : private juce::Thread
    {
    public:
        using Callback = std::function<void (const Result&)>;

        Job (juce::String prompt, juce::String constraints, Options, Callback onFinished);
        ~Job() override;

        void cancel();

    private:
        void run() override;
        Result attempt();

        const juce::String prompt, constraints;
        const Options options;
        Callback callback;

        juce::CriticalSection streamLock;
        std::unique_ptr<juce::WebInputStream> stream;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    };
}
