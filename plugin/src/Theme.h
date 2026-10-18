// AI Purrformer plugin: palette and small shared widgets. AGPL-3.0.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace purr
{
namespace col
{
    const juce::Colour bg      { 0xff0E0B1F };
    const juce::Colour panel   { 0xff171233 };
    const juce::Colour panelHi { 0xff1F1847 };
    const juce::Colour raise   { 0xff271E5E };
    const juce::Colour well    { 0xff0B0820 };
    const juce::Colour line    { 0xff3A2F7A };
    const juce::Colour ink     { 0xffF3EEFF };
    const juce::Colour mute    { 0xffA99CD6 };
    const juce::Colour violet  { 0xffB64EFF };
    const juce::Colour mint    { 0xff55FFB6 };
    const juce::Colour pink    { 0xffFF3FB0 };
    const juce::Colour sun     { 0xffFFEE5E };
    const juce::Colour sky     { 0xff6EC8F5 };
    const juce::Colour teal    { 0xff00F5D4 };
    const juce::Colour orange  { 0xffFF9500 };
}

inline juce::Font ui (float h, bool bold = false)
{
    return juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain);
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setGradientFill (juce::ColourGradient (col::panelHi, r.getX(), r.getY(), col::panel, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
}

inline void drawWell (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
}

// Neon sliders like the mockup: dark well, glowing fill, white thumb with a coloured ring.
struct NeonLook : juce::LookAndFeel_V4
{
    NeonLook()
    {
        setColour (juce::TextEditor::backgroundColourId, col::well);
        setColour (juce::TextEditor::textColourId, col::ink);
        setColour (juce::TextEditor::outlineColourId, col::line);
        setColour (juce::TextEditor::focusedOutlineColourId, col::sky);
        setColour (juce::ComboBox::backgroundColourId, col::well);
        setColour (juce::ComboBox::textColourId, col::ink);
        setColour (juce::ComboBox::outlineColourId, col::line);
        setColour (juce::ComboBox::arrowColourId, col::mute);
        setColour (juce::PopupMenu::backgroundColourId, col::panel);
        setColour (juce::PopupMenu::textColourId, col::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::violet);
        setColour (juce::PopupMenu::highlightedTextColourId, col::bg);
        setColour (juce::Label::textColourId, col::ink);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        auto acc = s.findColour (juce::Slider::trackColourId);
        float cy = y + h * 0.5f;
        juce::Rectangle<float> tr ((float) x, cy - 3.0f, (float) w, 6.0f);
        g.setColour (col::well);
        g.fillRoundedRectangle (tr, 3.0f);
        float tx = juce::jlimit ((float) x + 11.0f, (float) x + w - 11.0f, pos);
        auto fill = tr.withRight (tx);
        g.setColour (acc.withAlpha (0.35f));
        g.fillRoundedRectangle (fill.expanded (0.0f, 3.0f), 5.0f);
        g.setColour (acc);
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (tx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
        g.setColour (acc);
        g.drawEllipse (tx - 10.0f, cy - 10.0f, 20.0f, 20.0f, 3.0f);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return ui (13.0f, true); }
    juce::Font getLabelFont (juce::Label&) override { return ui (13.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return ui (13.0f); }
    juce::Font getPopupMenuFont() override { return ui (13.0f); }
};

// Segmented pill, like the mockup's Off/On and mode switches.
struct Seg : juce::Component
{
    juce::StringArray items;
    int index = 0;
    juce::Colour accent = col::mint;
    std::function<void (int)> onChange;

    Seg (juce::StringArray names, juce::Colour acc) : items (std::move (names)), accent (acc) {}
    void setIndex (int i, bool notify = false)
    {
        if (i == index) return;
        index = i; repaint();
        if (notify && onChange) onChange (index);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::well); g.fillRoundedRectangle (r, r.getHeight() / 2);
        float w = r.getWidth() / (float) items.size();
        for (int i = 0; i < items.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + w * i, r.getY(), w, r.getHeight());
            if (i == index) { g.setColour (accent); g.fillRoundedRectangle (cell.reduced (1.0f), (r.getHeight() - 2.0f) / 2); }
            g.setColour (i == index ? col::bg : col::mute);
            g.setFont (ui (12.0f, true));
            g.drawText (items[i], cell.toNearestInt(), juce::Justification::centred);
        }
        g.setColour (col::line); g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() / 2, 1.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        int i = juce::jlimit (0, items.size() - 1, (int) (e.position.x / (getWidth() / (float) items.size())));
        setIndex (i, true);
    }
};

inline void label (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c, float h = 11.0f,
                   bool bold = true, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c); g.setFont (ui (h, bold)); g.drawText (t, r, j);
}
} // namespace purr
