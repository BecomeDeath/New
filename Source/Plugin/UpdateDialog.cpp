#include "UpdateDialog.h"

#include "Theme.h"

namespace
{
juce::String megabytes (juce::int64 bytes)
{
    return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
}
} // namespace

UpdateDialog::UpdateDialog()
{
    setWantsKeyboardFocus (true);

    primaryButton.setPrimary (true);
    primaryButton.onClick = [this]
    {
        switch (mode)
        {
            case Mode::Offer:   if (onUpdateNow) onUpdateNow(); break;
            case Mode::Failed:  if (onRetry) onRetry(); break;
            case Mode::Info:    if (onClose) onClose(); break;
            case Mode::Working: break;
        }
    };

    secondaryButton.onClick = [this]
    {
        switch (mode)
        {
            case Mode::Offer:   if (onLater) onLater(); break;
            case Mode::Working: if (onCancel) onCancel(); break;
            case Mode::Failed:  if (onWebsite) onWebsite(); break;
            case Mode::Info:    break;
        }
    };

    tertiaryButton.onClick = [this]
    {
        if (mode == Mode::Offer && onSkip)        onSkip();
        else if (mode == Mode::Failed && onClose) onClose();
    };

    tertiaryButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    tertiaryButton.setColour (juce::TextButton::textColourOffId, Theme::textDim);

    for (auto* b : std::initializer_list<juce::Component*> { &primaryButton, &secondaryButton, &tertiaryButton })
        addAndMakeVisible (b);

    setMode (Mode::Offer);
}

void UpdateDialog::offer (const updates::Release& r, bool inApp, const juce::String& h)
{
    release = r;
    installsInApp = inApp;
    hint = h;
    state = {};
    setMode (Mode::Offer);
}

void UpdateDialog::follow (const Updater::State& s)
{
    state = s;
    ++frame;

    switch (s.stage)
    {
        case Updater::Stage::Idle:           break;
        case Updater::Stage::Downloading:
        case Updater::Stage::Verifying:
        case Updater::Stage::Installing:     setMode (Mode::Working); break;
        case Updater::Stage::WaitingForHost:
        case Updater::Stage::Done:           setMode (Mode::Info); break;
        case Updater::Stage::Failed:         setMode (Mode::Failed); break;
    }

    repaint();
}

void UpdateDialog::setMode (Mode m)
{
    mode = m;

    switch (m)
    {
        case Mode::Offer:
            primaryButton.setLabel (installsInApp ? "Update now" : "Download");
            primaryButton.setIcon (Icons::Kind::Download);
            secondaryButton.setButtonText ("Later");
            tertiaryButton.setButtonText ("Skip this version");
            break;
        case Mode::Working:
            secondaryButton.setButtonText ("Cancel");
            break;
        case Mode::Info:
            primaryButton.setLabel ("OK");
            primaryButton.setIcon (Icons::Kind::Sparkle);
            break;
        case Mode::Failed:
            primaryButton.setLabel ("Try again");
            primaryButton.setIcon (Icons::Kind::Download);
            secondaryButton.setButtonText ("Get it from the website");
            tertiaryButton.setButtonText ("Close");
            break;
    }

    const bool downloading = m == Mode::Working && state.stage == Updater::Stage::Downloading;
    primaryButton.setVisible (m != Mode::Working && ! (m == Mode::Info && state.restartApp));
    secondaryButton.setVisible (m == Mode::Offer || m == Mode::Failed || downloading);
    tertiaryButton.setVisible (m == Mode::Offer || m == Mode::Failed);
    resized();
}

juce::Rectangle<int> UpdateDialog::getCardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (540, getWidth() - 24), juce::jmin (300, getHeight() - 24));
}

void UpdateDialog::resized()
{
    auto card = getCardBounds().reduced (24, 20);
    auto buttons = card.removeFromBottom (36);

    if (primaryButton.isVisible())
    {
        primaryButton.setBounds (buttons.removeFromRight (mode == Mode::Info ? 96 : 150));
        buttons.removeFromRight (10);
    }

    if (secondaryButton.isVisible())
        secondaryButton.setBounds (buttons.removeFromRight (mode == Mode::Failed ? 190 : 104));

    if (tertiaryButton.isVisible())
        tertiaryButton.setBounds (buttons.removeFromLeft (mode == Mode::Failed ? 80 : 140));
}

bool UpdateDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (mode == Mode::Offer && onLater)  onLater();
        else if (mode == Mode::Working && state.stage == Updater::Stage::Downloading && onCancel) onCancel();
        else if (mode != Mode::Working && onClose) onClose();
        return true;
    }

    if (key == juce::KeyPress::returnKey && primaryButton.isVisible())
    {
        primaryButton.triggerClick();
        return true;
    }

    return false;
}

void UpdateDialog::paint (juce::Graphics& g)
{
    g.setColour (Theme::background.withAlpha (0.78f));
    g.fillRect (getLocalBounds());

    const auto cardArea = getCardBounds().toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (cardArea, 14.0f);
    g.setColour (Theme::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (cardArea.reduced (0.5f), 14.0f, 1.0f);

    auto r = getCardBounds().reduced (24, 20);
    r.removeFromBottom (48);

    // Title row: the mark and what this is about.
    auto title = r.removeFromTop (34);
    auto mark = title.removeFromLeft (34).toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (Theme::accentHot, mark.getX(), mark.getY(), Theme::accent.darker (0.3f), mark.getRight(), mark.getBottom(), false));
    g.fillRoundedRectangle (mark, 8.0f);
    Icons::draw (g, mode == Mode::Failed ? Icons::Kind::Cross : Icons::Kind::Download, mark.reduced (8.0f), juce::Colours::white);
    title.removeFromLeft (12);

    juce::String heading, sub;

    switch (mode)
    {
        case Mode::Offer:
            heading = "PromptMIDI " + release.version + " is available";
            sub = "You have " + updates::currentVersion() + ".";
            break;
        case Mode::Working:
            heading = "Updating to " + state.version;
            sub = state.stage == Updater::Stage::Downloading ? "Downloading" : state.stage == Updater::Stage::Verifying ? "Checking the download" : "Installing";
            break;
        case Mode::Info:
            heading = state.restartApp ? "Restarting" : state.stage == Updater::Stage::WaitingForHost ? "Ready to install" : "Updated to " + state.version;
            break;
        case Mode::Failed:
            heading = "The update didn't finish";
            sub = "Nothing was changed.";
            break;
    }

    g.setColour (Theme::text);
    g.setFont (Theme::font (19.0f, Theme::Weight::SemiBold));
    g.drawText (heading, title.removeFromTop (22), juce::Justification::centredLeft, true);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (sub, title, juce::Justification::centredLeft, true);
    r.removeFromTop (16);

    auto bodyText = [&] (const juce::String& text, juce::Colour colour, float size, int lines)
    {
        g.setColour (colour);
        g.setFont (Theme::font (size));
        g.drawFittedText (text, r.removeFromTop ((int) (size * 1.45f) * lines), juce::Justification::topLeft, lines, 1.0f);
    };

    if (mode == Mode::Offer)
    {
        g.setColour (Theme::textMid);
        g.setFont (Theme::font (12.0f, Theme::Weight::SemiBold));
        g.drawText ("WHAT'S NEW", r.removeFromTop (18), juce::Justification::centredLeft, false);
        bodyText (release.notes.isNotEmpty() ? release.notes : juce::String ("Improvements and fixes."), Theme::text, 14.0f, 4);
        r.removeFromTop (6);
        bodyText (hint, Theme::textDim, 12.5f, 2);
        return;
    }

    if (mode == Mode::Working)
    {
        bodyText (state.stage == Updater::Stage::Installing ? state.message
                                                            : juce::String ("Keep working: PromptMIDI downloads in the background."),
                  Theme::textMid, 14.0f, 2);
        r.removeFromTop (10);

        auto line = r.removeFromTop (18);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.5f));

        if (state.stage == Updater::Stage::Downloading)
            g.drawText (megabytes (state.downloaded) + (state.total > 0 ? " of " + megabytes (state.total) : juce::String()),
                        line, juce::Justification::centredLeft, false);

        r.removeFromTop (6);
        auto bar = r.removeFromTop (6).toFloat();
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (bar, 3.0f);

        if (state.stage == Updater::Stage::Downloading && state.total > 0)
        {
            const float fraction = juce::jlimit (0.0f, 1.0f, (float) ((double) state.downloaded / (double) state.total));
            auto fill = bar.withWidth (juce::jmax (6.0f, bar.getWidth() * fraction));
            g.setGradientFill (juce::ColourGradient (Theme::accent, fill.getX(), 0.0f, Theme::accentHot, fill.getRight(), 0.0f, false));
            g.fillRoundedRectangle (fill, 3.0f);
        }
        else
        {
            // Working without a measure: a segment sweeping along.
            const float w = bar.getWidth() * 0.25f;
            const float x = bar.getX() + (bar.getWidth() + w) * (float) ((frame % 60) / 60.0) - w;
            g.saveState();
            g.reduceClipRegion (bar.toNearestInt());
            g.setColour (Theme::accentHot);
            g.fillRoundedRectangle (juce::Rectangle<float> (x, bar.getY(), w, bar.getHeight()), 3.0f);
            g.restoreState();
        }

        return;
    }

    bodyText (state.message, mode == Mode::Failed ? Theme::textMid : Theme::text, 14.0f, 4);
}
