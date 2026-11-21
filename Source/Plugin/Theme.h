#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Colours and the custom look-and-feel shared by the editor's components. */
namespace Theme
{
    const juce::Colour background { 0xff121419 };
    const juce::Colour panel      { 0xff1a1d24 };
    const juce::Colour panelDark  { 0xff15171d };
    const juce::Colour raised     { 0xff242832 };
    const juce::Colour outline    { 0xff2c313c };
    const juce::Colour grid       { 0xff23272f };
    const juce::Colour text       { 0xffe9eaee };
    const juce::Colour textDim    { 0xff9aa1ae };
    const juce::Colour accent     { 0xff8b7cff };
    const juce::Colour accentHot  { 0xffa699ff };
    const juce::Colour playhead   { 0xffffd166 };
    const juce::Colour error      { 0xffff6b7d };
    const juce::Colour ok         { 0xff66d9a3 };

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColourScheme ({ background, panel, panel, outline, text, accent, text, raised, text });

            setColour (juce::TextEditor::backgroundColourId, panelDark);
            setColour (juce::TextEditor::outlineColourId, outline);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
            setColour (juce::CaretComponent::caretColourId, accent);
            setColour (juce::ComboBox::backgroundColourId, raised);
            setColour (juce::ComboBox::outlineColourId, outline);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::arrowColourId, textDim);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.5f));
            setColour (juce::TextButton::buttonColourId, raised);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            setColour (juce::ToggleButton::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, accent);
            setColour (juce::ToggleButton::tickDisabledColourId, textDim);
            setColour (juce::Slider::thumbColourId, accent);
            setColour (juce::Slider::trackColourId, accent.withAlpha (0.6f));
            setColour (juce::Slider::backgroundColourId, raised);
            setColour (juce::Slider::textBoxTextColourId, text);
            setColour (juce::Slider::textBoxOutlineColourId, outline);
            setColour (juce::Slider::textBoxBackgroundColourId, raised);
            setColour (juce::Label::textColourId, text);
            setColour (juce::TooltipWindow::backgroundColourId, raised);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, outline);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;

            if (! b.isEnabled())   c = c.withMultipliedAlpha (0.45f);
            else if (down)         c = c.brighter (0.15f);
            else if (highlighted)  c = c.brighter (0.08f);

            g.setColour (c);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (b.getToggleState() ? c.brighter (0.2f) : outline);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.5f)));
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
            g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (box.hasKeyboardFocus (true) ? accent : outline);
            g.drawRoundedRectangle (r, 5.0f, 1.0f);

            juce::Path arrow;
            const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
            arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
            g.setColour (textDim);
            g.fillPath (arrow);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                               float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearBar)
            {
                juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
                return;
            }

            auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
            g.setColour (raised);
            g.fillRoundedRectangle (r, 5.0f);

            if (slider.getValue() > slider.getMinimum())
            {
                g.setColour (accent.withAlpha (0.35f));
                g.fillRoundedRectangle (r.withRight (juce::jmax (r.getX() + 6.0f, sliderPos)), 5.0f);
            }

            g.setColour (slider.isMouseOverOrDragging() ? accent : outline);
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
            label.setFont (juce::Font (juce::FontOptions (13.5f)));
        }
    };
}
