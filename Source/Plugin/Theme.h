#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

/** Colours, type and the look-and-feel shared by the editor's components. */
namespace Theme
{
    // Surfaces, from the window background up.
    const juce::Colour background { 0xff0d0f13 };
    const juce::Colour panelDark  { 0xff111419 };
    const juce::Colour panel      { 0xff161a20 };
    const juce::Colour raised     { 0xff1d222a };
    const juce::Colour hover      { 0xff252b35 };
    const juce::Colour outline    { 0xff272d37 };
    const juce::Colour grid       { 0xff1c2027 };
    const juce::Colour gridStrong { 0xff2a303a };

    // Type.
    const juce::Colour text       { 0xffe7e9ee };
    const juce::Colour textMid    { 0xffadb4c0 };
    const juce::Colour textDim    { 0xff737c8a };

    // Signal colours.
    const juce::Colour accent     { 0xff7d6ff7 };
    const juce::Colour accentHot  { 0xff9b90ff };
    const juce::Colour playhead   { 0xfff4b54b };
    const juce::Colour error      { 0xffef6371 };
    const juce::Colour ok         { 0xff40c98b };

    enum class Weight { Regular, Medium, SemiBold };

    inline juce::Typeface::Ptr typeface (Weight w)
    {
        static juce::Typeface::Ptr regular  = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
        static juce::Typeface::Ptr medium   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
        static juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        return w == Weight::SemiBold ? semibold : (w == Weight::Medium ? medium : regular);
    }

    inline juce::Font font (float size, Weight w = Weight::Regular)
    {
        return juce::Font (juce::FontOptions (typeface (w)).withHeight (size));
    }

    /** A distinct, calm colour per track. */
    inline juce::Colour trackColour (int index)
    {
        static const juce::uint32 colours[] = {
            0xff8b7ff7, 0xff4fb8f0, 0xff4fd1a5, 0xfff2c35b, 0xfff07a8c, 0xffb58cf5,
            0xff52cfd9, 0xffa3d16b, 0xfff5a265, 0xffe58ec0, 0xff7fa8f5, 0xffd6dc7a,
            0xffc792e0, 0xff6fd0b8, 0xfff0cf73, 0xffb9a99e
        };

        return juce::Colour (colours[(size_t) (index < 0 ? 0 : index % 16)]);
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColourScheme ({ background, panel, panel, outline, text, accent, text, raised, text });
            setDefaultSansSerifTypeface (typeface (Weight::Regular));

            setColour (juce::TextEditor::backgroundColourId, panelDark);
            setColour (juce::TextEditor::outlineColourId, outline);
            setColour (juce::TextEditor::focusedOutlineColourId, accent.withAlpha (0.8f));
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
            setColour (juce::CaretComponent::caretColourId, accentHot);
            setColour (juce::ComboBox::backgroundColourId, raised);
            setColour (juce::ComboBox::outlineColourId, outline);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::arrowColourId, textDim);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.28f));
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedTextColourId, text);
            setColour (juce::PopupMenu::headerTextColourId, textDim);
            setColour (juce::TextButton::buttonColourId, raised);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            setColour (juce::ToggleButton::textColourId, textMid);
            setColour (juce::ToggleButton::tickColourId, accent);
            setColour (juce::Label::textColourId, text);
            setColour (juce::Slider::trackColourId, accent);
            setColour (juce::Slider::backgroundColourId, raised);
            setColour (juce::Slider::thumbColourId, text);
            setColour (juce::Slider::textBoxTextColourId, text);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::ScrollBar::thumbColourId, gridStrong);
            setColour (juce::TooltipWindow::backgroundColourId, raised);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, outline);
            setColour (juce::ResizableWindow::backgroundColourId, background);
        }

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
        {
            if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || f.getTypefaceName() == "<Sans-Serif>")
                return typeface (f.isBold() ? Weight::SemiBold : Weight::Regular);

            return LookAndFeel_V4::getTypefaceForFont (f);
        }

        //======================================================================
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool on = b.getToggleState();
            const bool primary = base != findColour (juce::TextButton::buttonColourId);
            juce::Colour fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : base;

            if (! b.isEnabled())       fill = fill.withMultipliedAlpha (0.45f);
            else if (down)             fill = fill.brighter (primary || on ? 0.05f : 0.12f);
            else if (over)             fill = primary || on ? fill.brighter (0.12f) : hover;

            g.setColour (fill);
            g.fillRoundedRectangle (r, 6.0f);

            if (! primary && ! on)
            {
                g.setColour (outline.withAlpha (b.isEnabled() ? 1.0f : 0.5f));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
        }

        juce::Font getTextButtonFont (juce::TextButton&, int height) override
        {
            return font (juce::jlimit (12.5f, 14.0f, height * 0.42f), Weight::Medium);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            const bool on = b.getToggleState();
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.setColour (b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                          .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1);
        }

        /** Toggles are drawn as small switches. */
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            const float h = 16.0f, w = 28.0f;
            auto track = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - h / 2, w, h);
            const bool on = b.getToggleState();

            g.setColour (on ? accent : (over ? hover : raised));
            g.fillRoundedRectangle (track, h / 2);
            g.setColour (on ? accent.brighter (0.2f) : outline);
            g.drawRoundedRectangle (track, h / 2, 1.0f);

            const float knob = h - 4.0f;
            const float x = on ? track.getRight() - knob - 2.0f : track.getX() + 2.0f;
            g.setColour (on ? juce::Colours::white : textMid);
            g.fillEllipse (x, track.getY() + 2.0f, knob, knob);

            g.setColour (b.isEnabled() ? (on ? text : textMid) : textDim);
            g.setFont (font (13.0f));
            g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (w + 9.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        }

        //======================================================================
        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
            g.setColour (box.isMouseOver (true) ? hover : raised);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (box.hasKeyboardFocus (true) ? accent.withAlpha (0.8f) : outline);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);

            juce::Path chevron;
            const float cx = (float) width - 14.0f, cy = height * 0.5f;
            chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
            chevron.lineTo (cx, cy + 2.0f);
            chevron.lineTo (cx + 3.5f, cy - 1.5f);
            g.setColour (textDim);
            g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.5f); }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
        }

        //======================================================================
        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            g.fillAll (panel);
            g.setColour (outline);
            g.drawRect (0, 0, width, height);
        }

        juce::Font getPopupMenuFont() override { return font (14.0f); }

        void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) override
        {
            g.setFont (font (11.0f, Weight::SemiBold));
            g.setColour (textDim);
            g.drawFittedText (name.toUpperCase(), area.reduced (12, 0), juce::Justification::bottomLeft, 1);
        }

        //======================================================================
        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                               juce::Slider::SliderStyle style, juce::Slider& s) override
        {
            if (style == juce::Slider::LinearBar)
            {
                auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
                g.setColour (s.isMouseOverOrDragging() ? hover : raised);
                g.fillRoundedRectangle (r, 6.0f);

                if (s.getValue() > s.getMinimum())
                {
                    g.setColour (accent.withAlpha (0.25f));
                    g.fillRoundedRectangle (r.withRight (juce::jmax (r.getX() + 6.0f, sliderPos)), 6.0f);
                }

                g.setColour (outline);
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
                return;
            }

            const float cy = y + height * 0.5f;
            auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
            g.setColour (raised.brighter (0.15f));
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);
            g.setColour (text);
            g.fillEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        }

        juce::Font getLabelFont (juce::Label& l) override
        {
            if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
                return font (13.5f);

            return l.getFont();
        }

        juce::Label* createSliderTextBox (juce::Slider& s) override
        {
            auto* l = LookAndFeel_V4::createSliderTextBox (s);
            l->setFont (font (13.5f));
            l->setJustificationType (juce::Justification::centred);
            return l;
        }

        //======================================================================
        void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
        {
            g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), 8.0f);
        }

        void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
        {
            const bool focus = e.hasKeyboardFocus (true) && ! e.isReadOnly();
            g.setColour (focus ? e.findColour (juce::TextEditor::focusedOutlineColourId) : e.findColour (juce::TextEditor::outlineColourId));
            g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f), 8.0f, focus ? 1.5f : 1.0f);
        }

        //======================================================================
        void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                            int thumbStart, int thumbSize, bool over, bool down) override
        {
            auto thumb = vertical ? juce::Rectangle<float> ((float) x + 2.0f, (float) thumbStart, (float) width - 4.0f, (float) thumbSize)
                                  : juce::Rectangle<float> ((float) thumbStart, (float) y + 2.0f, (float) thumbSize, (float) height - 4.0f);
            g.setColour ((over || down) ? textDim : gridStrong.brighter (0.15f));
            g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
        }

        int getDefaultScrollbarWidth() override { return 10; }

        //======================================================================
        void drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image&) override
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (path, juce::AffineTransform::translation (0.0f, 3.0f));
            g.setColour (panel);
            g.fillPath (path);
            g.setColour (outline);
            g.strokePath (path, juce::PathStrokeType (1.0f));
            juce::ignoreUnused (box);
        }

        int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 16; }
        float getCallOutBoxCornerSize (const juce::CallOutBox&) override { return 10.0f; }

        juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
        {
            juce::AttributedString s;
            s.append (tip, font (13.0f), text);
            juce::TextLayout tl;
            tl.createLayoutWithBalancedLineLengths (s, 360.0f);
            const int w = (int) tl.getWidth() + 20, h = (int) tl.getHeight() + 14;
            return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                         pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
        }

        void drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height) override
        {
            auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
            g.setColour (raised);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

            juce::AttributedString s;
            s.append (tip, font (13.0f), text);
            juce::TextLayout tl;
            tl.createLayoutWithBalancedLineLengths (s, 360.0f);
            tl.draw (g, r.reduced (10.0f, 7.0f));
        }
    };
}
