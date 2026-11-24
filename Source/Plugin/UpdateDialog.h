#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Icons.h"
#include "UpdateCheck.h"
#include "Updater.h"

#include <functional>

/** "An update is available", in the middle of the window: what's new, Update now or Later (or skip
    this version). Updating happens right here: the download's progress, the system's permission
    prompt, then done - or, on Windows inside a DAW, ready to install when the DAW closes. */
class UpdateDialog : public juce::Component
{
public:
    UpdateDialog();

    /** Asks whether to update to `release`. `inApp`: PromptMIDI can install it itself (else Update
        now opens the download page). `hint`: what will happen, e.g. "Your Mac will ask for your password." */
    void offer (const updates::Release& release, bool inApp, const juce::String& hint);

    /** Follows the updater while it works (call ~30 times a second). */
    void follow (const Updater::State&);

    std::function<void()> onUpdateNow, onLater, onSkip, onCancel, onRetry, onWebsite, onClose, onHide;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::Rectangle<int> getCardBounds() const;

private:
    enum class Mode { Offer, Working, Info, Failed };
    void setMode (Mode);

    Mode mode = Mode::Offer;
    updates::Release release;
    bool installsInApp = true;
    juce::String hint;
    Updater::State state;
    int frame = 0;

    IconButton primaryButton { "update now", Icons::Kind::Download, "Update now" };
    juce::TextButton secondaryButton { "Later" };
    juce::TextButton tertiaryButton { "Skip this version" };
};
