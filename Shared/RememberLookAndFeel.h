#pragma once
#include <JuceHeader.h>

// Shared semantic tokens for the native app, plugin and their dialogs.
namespace Pal
{
    inline constexpr uint32_t BgDeep   = 0xfff5f5f7;
    inline constexpr uint32_t Surface  = 0xffededf0;
    inline constexpr uint32_t Elevated = 0xffe4e5ea;
    inline constexpr uint32_t Border   = 0xffd1d3da;
    inline constexpr uint32_t Accent   = 0xff0066cc;
    inline constexpr uint32_t Text     = 0xff1d1d1f;
    inline constexpr uint32_t Subtext  = 0xff62626b;
    inline constexpr uint32_t Star     = 0xff916000;
    inline constexpr uint32_t Success  = 0xff247343;
    inline constexpr uint32_t Error    = 0xffb42318;
    inline constexpr uint32_t Content  = 0xffffffff;
    inline constexpr uint32_t Selected = 0xffe5effb;
}

namespace RememberUI
{
inline juce::Font font (float size = 13.0f, bool bold = false)
{
    auto options = juce::FontOptions{}.withHeight (size).withStyle (bold ? "Bold" : "Regular");
   #if JUCE_WINDOWS
    options = options.withName ("Segoe UI");
   #endif
    return juce::Font (options);
}

inline void panel (juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 12.0f)
{
    g.setColour (juce::Colour (Pal::Content));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (juce::Colour (Pal::Border).withAlpha (0.65f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

// Original geometric icons, kept in code so they render crisply at host DPI.
// Button text and accessible titles remain independent of the painted symbol.
inline void icon (juce::Graphics& g, const juce::String& kind, juce::Rectangle<float> b, bool filled = false)
{
    b = b.withSizeKeepingCentre (16.0f, 16.0f);
    const auto x = b.getX(), y = b.getY();
    juce::Path p;
    if (kind == "play")
    {
        p.addTriangle (x + 4, y + 2, x + 4, y + 14, x + 14, y + 8);
        g.fillPath (p);
    }
    else if (kind == "pause")
    {
        g.fillRoundedRectangle (x + 3, y + 2, 3, 12, 1);
        g.fillRoundedRectangle (x + 10, y + 2, 3, 12, 1);
    }
    else if (kind == "star")
    {
        p.addStar (b.getCentre(), 5, 3.7f, 7.5f);
        if (filled) g.fillPath (p);
        else g.strokePath (p, juce::PathStrokeType (1.4f));
    }
    else if (kind == "settings")
    {
        g.drawEllipse (b.reduced (3), 1.5f);
        g.drawEllipse (b.reduced (6), 1.5f);
        for (int i = 0; i < 8; ++i)
        {
            const auto a = i * juce::MathConstants<float>::pi / 4;
            const auto c = b.getCentre();
            g.drawLine (c.x + std::cos (a) * 5, c.y + std::sin (a) * 5,
                        c.x + std::cos (a) * 8, c.y + std::sin (a) * 8, 1.5f);
        }
    }
    else if (kind == "folder")
    {
        p.startNewSubPath (x + 1, y + 4); p.lineTo (x + 6, y + 4);
        p.lineTo (x + 8, y + 6); p.lineTo (x + 15, y + 6);
        p.lineTo (x + 15, y + 14); p.lineTo (x + 1, y + 14); p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
    }
    else if (kind == "plus")
    {
        g.drawLine (x + 8, y + 2, x + 8, y + 14, 1.5f);
        g.drawLine (x + 2, y + 8, x + 14, y + 8, 1.5f);
    }
    else if (kind == "refresh")
    {
        p.addCentredArc (x + 8, y + 8, 6, 6, 0, 0.7f, 5.8f, true);
        g.strokePath (p, juce::PathStrokeType (1.5f));
        p.clear(); p.addTriangle (x + 2, y + 1, x + 2, y + 7, x + 7, y + 3); g.fillPath (p);
    }
    else if (kind == "search")
    {
        g.drawEllipse (x + 1, y + 1, 10, 10, 1.5f);
        g.drawLine (x + 10, y + 10, x + 15, y + 15, 1.5f);
    }
    else // Ellipsis also represents loading without decorative animation.
        for (int i = 0; i < 3; ++i) g.fillEllipse (x + 2.0f + i * 5, y + 7, 2, 2);
}

inline void iconButton (juce::TextButton& button, const juce::String& kind, const juce::String& title)
{
    button.getProperties().set ("rememberIcon", kind);
    button.setTitle (title);
    button.setDescription (title);
    button.setTooltip (title);
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::TextButton::buttonColourId, juce::Colour (Pal::Elevated));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (Pal::Accent));
        setColour (juce::TextButton::textColourOffId, juce::Colour (Pal::Text));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (Pal::Content));
        setColour (juce::ComboBox::textColourId, juce::Colour (Pal::Text));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (Pal::Border));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (Pal::Subtext));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (Pal::Content));
        setColour (juce::PopupMenu::textColourId, juce::Colour (Pal::Text));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (Pal::Selected));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (Pal::Accent));
        setColour (juce::Label::textColourId, juce::Colour (Pal::Text));
        setColour (juce::ToggleButton::textColourId, juce::Colour (Pal::Text));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (Pal::Accent));
        setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (Pal::Subtext));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (Pal::Content));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (Pal::Text));
        setColour (juce::TooltipWindow::outlineColourId, juce::Colour (Pal::Border));
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (Pal::Subtext).withAlpha (0.45f));
        setColour (juce::TextEditor::highlightColourId, juce::Colour (Pal::Selected));
        setColour (juce::TextEditor::highlightedTextColourId, juce::Colour (Pal::Text));
        setColour (juce::TextEditor::textColourId, juce::Colour (Pal::Text));
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (Pal::Content));
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (Pal::BgDeep));
        setColour (juce::AlertWindow::textColourId, juce::Colour (Pal::Text));
        setColour (juce::AlertWindow::outlineColourId, juce::Colour (Pal::Border));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override { return font (juce::jmin (13.0f, height * 0.55f), true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font(); }
    juce::Font getPopupMenuFont() override { return font(); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1);
        g.setColour (juce::Colour (down ? Pal::Selected : Pal::Content));
        g.fillRoundedRectangle (bounds, 7);
        const bool focused = box.hasKeyboardFocus (true);
        g.setColour (juce::Colour (focused ? Pal::Accent : Pal::Border));
        g.drawRoundedRectangle (bounds, 7, focused ? 2.0f : 1.0f);
        g.setColour (juce::Colour (Pal::Subtext).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        const float x = (float) w - 17, y = (float) h * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (x - 4, y - 2); arrow.lineTo (x, y + 2); arrow.lineTo (x + 4, y - 2);
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool down) override
    {
        auto track = juce::Rectangle<float> (2, (float) button.getHeight() / 2 - 9, 32, 18);
        const auto opacity = button.isEnabled() ? 1.0f : 0.4f;
        auto colour = juce::Colour (button.getToggleState() ? Pal::Accent : Pal::Subtext);
        if (over || down) colour = colour.darker (down ? 0.15f : 0.05f);
        g.setColour (colour.withMultipliedAlpha (opacity)); g.fillRoundedRectangle (track, 9);
        g.setColour (juce::Colours::white.withMultipliedAlpha (opacity));
        g.fillEllipse (button.getToggleState() ? 18.0f : 4.0f, track.getY() + 2, 14, 14);
        if (button.hasKeyboardFocus (false))
        {
            g.setColour (juce::Colour (Pal::Accent));
            g.drawRoundedRectangle (track.expanded (2), 11, 1.5f);
        }
        g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (opacity));
        g.setFont (font());
        g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (42), juce::Justification::centredLeft);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced (1.5f);
        auto colour = base;
        if (down) colour = base.isTransparent() ? juce::Colour (Pal::Border) : base.darker (0.12f);
        else if (over) colour = base.isTransparent() ? juce::Colour (Pal::Elevated) : base.darker (0.04f);
        if (! b.isEnabled()) colour = colour.withMultipliedAlpha (0.45f);
        g.setColour (colour); g.fillRoundedRectangle (bounds, 7);
        if (b.hasKeyboardFocus (false))
        {
            g.setColour (juce::Colour (Pal::Accent));
            g.drawRoundedRectangle (bounds, 7, 2);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down) override
    {
        auto kind = b.getProperties()["rememberIcon"].toString();
        if (kind.isEmpty()) { juce::LookAndFeel_V4::drawButtonText (g, b, over, down); return; }
        if (kind == "play" && b.getButtonText() == "||") kind = "pause";
        else if (kind == "play" && b.getButtonText() == "...") kind = "loading";
        auto colour = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        g.setColour (colour.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        icon (g, kind, b.getLocalBounds().toFloat(), b.getButtonText() == "*");
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1), 7);
    }
    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        const bool focused = e.hasKeyboardFocus (true) && ! e.isReadOnly();
        g.setColour (juce::Colour (focused ? Pal::Accent : Pal::Border));
        g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1), 7, focused ? 2.0f : 1.0f);
    }
};

// Each root owns a reference, including detached test components and dialogs.
// Never change JUCE's global LookAndFeel: a plugin shares its process with a host.
struct Style final
{
    explicit Style (juce::Component& c) : owner (c) { owner.setLookAndFeel (&look.get()); }
    ~Style() { owner.setLookAndFeel (nullptr); }
    juce::SharedResourcePointer<LookAndFeel> look;
    juce::Component& owner;
};
}
