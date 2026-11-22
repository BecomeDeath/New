#pragma once

#include "Theme.h"

/** Small vector icons drawn in a 24 x 24 box, stroked or filled by IconButton. */
namespace Icons
{
    enum class Kind { Play, Stop, Loop, Speaker, Gear, Download, Plus, Cross, Lock, Sparkle, Shuffle, ZoomIn, ZoomOut,
                      Fit, Follow, MidiOut, Instruments, Undo, Redo, Edit, Export, ChevronLeft, ChevronRight, Folder };

    inline void addLine (juce::Path& p, float x0, float y0, float x1, float y1)
    {
        p.startNewSubPath (x0, y0);
        p.lineTo (x1, y1);
    }

    /** True for icons that are filled rather than stroked. */
    inline bool filled (Kind k) { return k == Kind::Play || k == Kind::Stop || k == Kind::Sparkle; }

    inline juce::Path path (Kind k)
    {
        juce::Path p;

        switch (k)
        {
            case Kind::Play:     p.addTriangle (8.0f, 5.0f, 8.0f, 19.0f, 19.0f, 12.0f); break;
            case Kind::Stop:     p.addRoundedRectangle (6.5f, 6.5f, 11.0f, 11.0f, 1.5f); break;
            case Kind::Loop:
                p.addCentredArc (12.0f, 12.0f, 7.0f, 7.0f, 0.0f, 0.6f, 5.2f, true);
                addLine (p, 15.5f, 3.5f, 18.6f, 5.6f);
                addLine (p, 18.6f, 5.6f, 16.0f, 8.6f);
                break;
            case Kind::Speaker:
                p.startNewSubPath (4.0f, 9.5f); p.lineTo (8.0f, 9.5f); p.lineTo (12.5f, 5.5f); p.lineTo (12.5f, 18.5f);
                p.lineTo (8.0f, 14.5f); p.lineTo (4.0f, 14.5f); p.closeSubPath();
                p.addCentredArc (13.0f, 12.0f, 4.5f, 4.5f, 0.0f, 0.6f, 2.55f, true);
                p.addCentredArc (13.0f, 12.0f, 8.0f, 8.0f, 0.0f, 0.75f, 2.4f, true);
                break;
            case Kind::Gear:
                p.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
                for (int i = 0; i < 8; ++i)
                {
                    const float a = i * juce::MathConstants<float>::twoPi / 8.0f;
                    addLine (p, 12.0f + 6.0f * std::sin (a), 12.0f - 6.0f * std::cos (a), 12.0f + 8.5f * std::sin (a), 12.0f - 8.5f * std::cos (a));
                }
                p.addEllipse (5.5f, 5.5f, 13.0f, 13.0f);
                break;
            case Kind::Download:
                addLine (p, 12.0f, 4.0f, 12.0f, 15.0f);
                p.startNewSubPath (7.5f, 10.5f); p.lineTo (12.0f, 15.0f); p.lineTo (16.5f, 10.5f);
                p.startNewSubPath (5.0f, 15.5f); p.lineTo (5.0f, 19.5f); p.lineTo (19.0f, 19.5f); p.lineTo (19.0f, 15.5f);
                break;
            case Kind::Export:
                addLine (p, 12.0f, 15.0f, 12.0f, 4.0f);
                p.startNewSubPath (7.5f, 8.5f); p.lineTo (12.0f, 4.0f); p.lineTo (16.5f, 8.5f);
                p.startNewSubPath (5.0f, 13.5f); p.lineTo (5.0f, 19.5f); p.lineTo (19.0f, 19.5f); p.lineTo (19.0f, 13.5f);
                break;
            case Kind::Plus:     addLine (p, 12.0f, 5.5f, 12.0f, 18.5f); addLine (p, 5.5f, 12.0f, 18.5f, 12.0f); break;
            case Kind::Cross:    addLine (p, 7.0f, 7.0f, 17.0f, 17.0f); addLine (p, 17.0f, 7.0f, 7.0f, 17.0f); break;
            case Kind::Lock:
                p.addRoundedRectangle (6.0f, 11.0f, 12.0f, 9.0f, 2.0f);
                p.startNewSubPath (8.5f, 11.0f); p.lineTo (8.5f, 8.5f);
                p.addCentredArc (12.0f, 8.5f, 3.5f, 3.5f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
                p.lineTo (15.5f, 11.0f);
                break;
            case Kind::Sparkle:
                p.startNewSubPath (12.0f, 3.0f);
                p.quadraticTo (13.0f, 11.0f, 21.0f, 12.0f);
                p.quadraticTo (13.0f, 13.0f, 12.0f, 21.0f);
                p.quadraticTo (11.0f, 13.0f, 3.0f, 12.0f);
                p.quadraticTo (11.0f, 11.0f, 12.0f, 3.0f);
                p.closeSubPath();
                break;
            case Kind::Shuffle:
                p.startNewSubPath (4.0f, 7.0f); p.lineTo (8.0f, 7.0f); p.cubicTo (13.0f, 7.0f, 11.0f, 17.0f, 16.0f, 17.0f); p.lineTo (19.5f, 17.0f);
                p.startNewSubPath (4.0f, 17.0f); p.lineTo (8.0f, 17.0f); p.cubicTo (13.0f, 17.0f, 11.0f, 7.0f, 16.0f, 7.0f); p.lineTo (19.5f, 7.0f);
                p.startNewSubPath (17.0f, 4.5f); p.lineTo (19.5f, 7.0f); p.lineTo (17.0f, 9.5f);
                p.startNewSubPath (17.0f, 14.5f); p.lineTo (19.5f, 17.0f); p.lineTo (17.0f, 19.5f);
                break;
            case Kind::ZoomIn:
            case Kind::ZoomOut:
                p.addEllipse (4.5f, 4.5f, 11.0f, 11.0f);
                addLine (p, 14.0f, 14.0f, 19.5f, 19.5f);
                addLine (p, 7.5f, 10.0f, 12.5f, 10.0f);
                if (k == Kind::ZoomIn)
                    addLine (p, 10.0f, 7.5f, 10.0f, 12.5f);
                break;
            case Kind::Fit:
                p.startNewSubPath (4.0f, 9.0f); p.lineTo (4.0f, 4.0f); p.lineTo (9.0f, 4.0f);
                p.startNewSubPath (15.0f, 4.0f); p.lineTo (20.0f, 4.0f); p.lineTo (20.0f, 9.0f);
                p.startNewSubPath (20.0f, 15.0f); p.lineTo (20.0f, 20.0f); p.lineTo (15.0f, 20.0f);
                p.startNewSubPath (9.0f, 20.0f); p.lineTo (4.0f, 20.0f); p.lineTo (4.0f, 15.0f);
                break;
            case Kind::Follow:
                addLine (p, 18.0f, 4.0f, 18.0f, 20.0f);
                addLine (p, 4.0f, 12.0f, 14.0f, 12.0f);
                p.startNewSubPath (10.0f, 8.0f); p.lineTo (14.0f, 12.0f); p.lineTo (10.0f, 16.0f);
                break;
            case Kind::MidiOut:
                p.addEllipse (4.0f, 4.0f, 16.0f, 16.0f);
                for (auto pt : { juce::Point<float> (8.0f, 11.0f), { 16.0f, 11.0f }, { 12.0f, 8.0f }, { 9.5f, 15.0f }, { 14.5f, 15.0f } })
                    p.addEllipse (pt.x - 0.9f, pt.y - 0.9f, 1.8f, 1.8f);
                break;
            case Kind::Instruments:
                p.addRoundedRectangle (4.0f, 6.0f, 16.0f, 12.0f, 1.5f);
                addLine (p, 8.0f, 6.0f, 8.0f, 18.0f); addLine (p, 12.0f, 6.0f, 12.0f, 18.0f); addLine (p, 16.0f, 6.0f, 16.0f, 18.0f);
                break;
            case Kind::Undo:
            case Kind::Redo:
            {
                p.addCentredArc (12.0f, 13.0f, 6.0f, 6.0f, 0.0f, -2.2f, 2.2f, true);
                p.startNewSubPath (5.0f, 6.5f); p.lineTo (7.2f, 9.6f); p.lineTo (10.6f, 8.4f);
                if (k == Kind::Redo)
                    p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 12.0f, 12.0f));
                break;
            }
            case Kind::Folder:
                p.startNewSubPath (3.5f, 18.5f); p.lineTo (3.5f, 6.0f); p.lineTo (9.5f, 6.0f); p.lineTo (11.5f, 8.5f);
                p.lineTo (20.5f, 8.5f); p.lineTo (20.5f, 18.5f); p.closeSubPath();
                addLine (p, 3.5f, 11.5f, 20.5f, 11.5f);
                break;
            case Kind::ChevronLeft:  p.startNewSubPath (14.5f, 6.5f); p.lineTo (9.0f, 12.0f); p.lineTo (14.5f, 17.5f); break;
            case Kind::ChevronRight: p.startNewSubPath (9.5f, 6.5f); p.lineTo (15.0f, 12.0f); p.lineTo (9.5f, 17.5f); break;
            case Kind::Edit:
                p.startNewSubPath (5.0f, 19.0f); p.lineTo (6.0f, 15.0f); p.lineTo (16.0f, 5.0f); p.lineTo (19.0f, 8.0f);
                p.lineTo (9.0f, 18.0f); p.closeSubPath();
                break;
        }

        return p;
    }

    inline void draw (juce::Graphics& g, Kind k, juce::Rectangle<float> area, juce::Colour colour, float stroke = 1.6f)
    {
        const float size = juce::jmin (area.getWidth(), area.getHeight());
        auto box = area.withSizeKeepingCentre (size, size);
        auto p = path (k);
        p.applyTransform (juce::AffineTransform::scale (size / 24.0f).translated (box.getX(), box.getY()));
        g.setColour (colour);

        if (filled (k))
            g.fillPath (p);
        else
            g.strokePath (p, juce::PathStrokeType (stroke * size / 24.0f * 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

/** A button with an icon and optional text; toggles show the accent colour when on. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, Icons::Kind k, const juce::String& labelText = {}) : Button (name), kind (k), label (labelText) {}

    void setIcon (Icons::Kind k)          { kind = k; repaint(); }
    void setLabel (const juce::String& t) { label = t; repaint(); }
    void setPrimary (bool p)              { primary = p; repaint(); }
    void setFlat (bool f)                 { flat = f; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        juce::Colour fill = primary ? Theme::accent : (on ? Theme::accent.withAlpha (0.18f) : Theme::raised);

        if (! isEnabled())  fill = fill.withMultipliedAlpha (0.4f);
        else if (down)      fill = fill.brighter (0.08f);
        else if (over)      fill = primary ? Theme::accentHot : (on ? Theme::accent.withAlpha (0.26f) : Theme::hover);

        if (! flat || over || on)
        {
            g.setColour (fill);
            g.fillRoundedRectangle (r, 6.0f);
        }

        if (! primary && ! flat)
        {
            g.setColour (on ? Theme::accent.withAlpha (0.7f) : Theme::outline);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }

        const juce::Colour ink = ! isEnabled() ? Theme::textDim
                               : primary ? juce::Colours::white
                               : on ? Theme::accentHot : (over ? Theme::text : Theme::textMid);

        auto content = r.reduced (label.isEmpty() ? 0.0f : 10.0f, 0.0f);
        const float iconSize = juce::jmin (18.0f, r.getHeight() - 10.0f);

        if (label.isEmpty())
        {
            Icons::draw (g, kind, content.withSizeKeepingCentre (iconSize, iconSize), ink);
            return;
        }

        const auto f = Theme::font (juce::jlimit (12.5f, 14.0f, r.getHeight() * 0.4f), Theme::Weight::Medium);
        const float textW = juce::GlyphArrangement::getStringWidth (f, label);
        const float total = iconSize + 7.0f + textW;
        auto row = content.withSizeKeepingCentre (juce::jmin (total, content.getWidth()), r.getHeight());
        Icons::draw (g, kind, row.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize), ink);
        row.removeFromLeft (7.0f);
        g.setColour (ink);
        g.setFont (f);
        g.drawText (label, row, juce::Justification::centredLeft, false);
    }

private:
    Icons::Kind kind;
    juce::String label;
    bool primary = false, flat = false;
};
