#include "ThinkingPanel.h"

#include "Theme.h"

#include <cmath>

namespace
{
juce::String timerText (double s)
{
    if (s < 60.0)
        return juce::String (s, 1) + " s";

    const int whole = (int) s;
    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
}

const char* const stageTitles[] = { "Reading your prompt", "Sketching plans", "Writing drafts", "Developing the best ideas", "Final check" };
} // namespace

ThinkingPanel::ThinkingPanel()
{
    setInterceptsMouseClicks (true, true);   // the arrangement underneath waits

    finishButton.setPrimary (true);
    finishButton.setTooltip ("Stop thinking and use the best draft so far");
    finishButton.onClick = [this] { if (onFinishNow) onFinishNow(); };
    addAndMakeVisible (finishButton);

    cancelButton.setTooltip ("Stop thinking and keep the take you had");
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancelButton);
}

void ThinkingPanel::reset()
{
    progress = {};
    details = {};
    seconds = 0.0;
    frame = 0;
}

void ThinkingPanel::update (const pm::ThinkingProgress& p, double s, const juce::String& thinkingFor, pm::ThinkingDepth d)
{
    for (int i = 0; i < numStages; ++i)
        if (! p.stageDetail[i].empty())
            details[(size_t) i] = juce::String::fromUTF8 (p.stageDetail[i].c_str());

    progress = p;
    seconds = s;
    forText = thinkingFor;
    depth = d;
    ++frame;
    finishButton.setEnabled (p.drafts > 0);
    repaint();
}

juce::Rectangle<int> ThinkingPanel::getCardBounds() const
{
    const int w = juce::jmin (620, getWidth() - 24);
    const int h = juce::jmin (330, getHeight() - 16);
    return getLocalBounds().withSizeKeepingCentre (w, h);
}

void ThinkingPanel::resized()
{
    auto card = getCardBounds().reduced (22, 18);
    auto buttons = card.removeFromBottom (34);
    cancelButton.setBounds (buttons.removeFromRight (104));
    buttons.removeFromRight (8);
    finishButton.setBounds (buttons.removeFromRight (160));
}

void ThinkingPanel::paint (juce::Graphics& g)
{
    // The arrangement behind stays visible, dimmed.
    g.setColour (Theme::background.withAlpha (0.72f));
    g.fillRect (getLocalBounds());

    const auto cardArea = getCardBounds().toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (cardArea, 12.0f);
    g.setColour (Theme::accent.withAlpha (0.45f));
    g.drawRoundedRectangle (cardArea.reduced (0.5f), 12.0f, 1.0f);

    // A slow glow drifting across the top edge: it's working.
    {
        const float t = (float) std::fmod (seconds * 0.35, 1.0);
        const float x = cardArea.getX() + cardArea.getWidth() * t;
        juce::ColourGradient glow (Theme::accent.withAlpha (0.0f), x - 120.0f, cardArea.getY(),
                                   Theme::accent.withAlpha (0.0f), x + 120.0f, cardArea.getY(), false);
        glow.addColour (0.5, Theme::accentHot.withAlpha (0.9f));
        g.setGradientFill (glow);
        g.fillRect (cardArea.withHeight (2.0f).reduced (12.0f, 0.0f));
    }

    auto r = getCardBounds().reduced (22, 18);

    // Header: what it's thinking about, the depth, and the timer.
    auto header = r.removeFromTop (30);
    g.setColour (Theme::text);
    g.setFont (Theme::font (26.0f, Theme::Weight::SemiBold));
    const auto timer = timerText (seconds);
    const int timerW = 104;
    g.drawText (timer, header.removeFromRight (timerW), juce::Justification::centredRight, false);

    Icons::draw (g, Icons::Kind::Sparkle, header.removeFromLeft (22).toFloat().withSizeKeepingCentre (18.0f, 18.0f), Theme::accentHot);
    header.removeFromLeft (8);
    g.setFont (Theme::font (17.0f, Theme::Weight::SemiBold));
    g.drawText ("Thinking", header.removeFromLeft (82), juce::Justification::centredLeft, false);

    // The depth, as a small chip.
    {
        const juce::String chip (pm::depthName (depth));
        const auto f = Theme::font (11.5f, Theme::Weight::SemiBold);
        const int cw = (int) juce::GlyphArrangement::getStringWidth (f, chip) + 16;
        auto c = header.removeFromLeft (cw).withSizeKeepingCentre (cw, 20).toFloat();
        g.setColour (Theme::accent.withAlpha (0.18f));
        g.fillRoundedRectangle (c, 10.0f);
        g.setColour (Theme::accentHot);
        g.setFont (f);
        g.drawText (chip, c, juce::Justification::centred, false);
    }

    auto sub = r.removeFromTop (22);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (forText.isEmpty() ? juce::String() : juce::String::fromUTF8 ("about \xe2\x80\x9c") + forText + juce::String::fromUTF8 ("\xe2\x80\x9d"),
                sub, juce::Justification::centredLeft, true);
    r.removeFromTop (10);

    // The stages.
    const int current = juce::jlimit (0, numStages, (int) progress.stage);

    for (int i = 0; i < numStages; ++i)
    {
        auto row = r.removeFromTop (26);
        auto icon = row.removeFromLeft (22).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
        const bool done = i < current;
        const bool now = i == current;

        if (done)
        {
            g.setColour (Theme::ok);
            juce::Path tick;
            tick.startNewSubPath (icon.getX() + 2.0f, icon.getCentreY());
            tick.lineTo (icon.getX() + 6.0f, icon.getBottom() - 2.5f);
            tick.lineTo (icon.getRight() - 1.0f, icon.getY() + 2.5f);
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (now)
        {
            // A turning arc.
            juce::Path arc;
            const float a0 = (float) (frame * 0.22);
            arc.addCentredArc (icon.getCentreX(), icon.getCentreY(), 6.0f, 6.0f, 0.0f, a0, a0 + 4.2f, true);
            g.setColour (Theme::accentHot);
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (Theme::outline);
            g.drawEllipse (icon.reduced (3.0f), 1.2f);
        }

        row.removeFromLeft (6);
        g.setColour (done ? Theme::textMid : now ? Theme::text : Theme::textDim);
        g.setFont (Theme::font (14.0f, now ? Theme::Weight::Medium : Theme::Weight::Regular));
        g.drawText (stageTitles[i], row.removeFromLeft (190), juce::Justification::centredLeft, false);

        g.setColour (now ? Theme::textMid : Theme::textDim);
        g.setFont (Theme::font (12.5f));
        g.drawText (details[(size_t) i], row, juce::Justification::centredLeft, true);
    }

    r.removeFromTop (10);

    // The best draft so far, and how far along it is.
    auto best = r.removeFromTop (20);
    g.setFont (Theme::font (13.0f));

    if (progress.drafts > 0)
    {
        g.setColour (Theme::textMid);
        juce::String text;
        text << "Best so far: " << juce::roundToInt (progress.best) << "/100";

        if (progress.criteria > 0)
            text << juce::String::fromUTF8 ("  \xc2\xb7  meets ") << progress.bestMet << " of " << progress.criteria << " of your criteria";

        g.drawText (text, best, juce::Justification::centredLeft, true);
    }

    g.setColour (Theme::textDim);
    g.drawText (juce::String (progress.drafts) + " / " + juce::String (juce::jmax (progress.drafts, progress.totalDrafts)) + " drafts",
                best, juce::Justification::centredRight, false);

    r.removeFromTop (6);
    auto bar = r.removeFromTop (6).toFloat();
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (bar, 3.0f);

    const float fraction = progress.totalDrafts > 0 ? juce::jlimit (0.0f, 1.0f, (float) progress.drafts / (float) progress.totalDrafts) : 0.0f;

    if (fraction > 0.0f)
    {
        auto fill = bar.withWidth (juce::jmax (6.0f, bar.getWidth() * fraction));
        g.setGradientFill (juce::ColourGradient (Theme::accent, fill.getX(), 0.0f, Theme::accentHot, fill.getRight(), 0.0f, false));
        g.fillRoundedRectangle (fill, 3.0f);
    }
}
