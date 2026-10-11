#pragma once
#include <JuceHeader.h>
#include "RememberTypography.h"

// Shared semantic tokens for the native app, plugin and their dialogs.
namespace Pal
{
    enum Token : uint32_t { BgDeep, Surface, Elevated, Border, Accent, Text,
        Subtext, Star, Success, Error, Content, Selected, Action, OnAction, Count };
}

namespace RememberUI
{
// Message-thread state, shared by Remember roots in this process, never the host.
inline bool darkAppearance = false;
inline bool isDark() { return darkAppearance; }
inline juce::Colour colour (uint32_t token)
{
    static constexpr uint32_t light[] = { 0xfff5f5f7, 0xffededf0, 0xffe4e5ea,
        0xffd1d3da, 0xff0060c0, 0xff1d1d1f, 0xff62626b, 0xff916000,
        0xff247343, 0xffb42318, 0xffffffff, 0xffe5effb, 0xff0066cc, 0xffffffff };
    static constexpr uint32_t dark[] = { 0xff1c1c1e, 0xff242426, 0xff343438,
        0xff4b4b50, 0xff64a9ff, 0xfff5f5f7, 0xffb8b8c0, 0xffffd60a,
        0xff66d98a, 0xffff8f86, 0xff2c2c2e, 0xff243a52, 0xff0066cc, 0xffffffff };
    static_assert (std::size (light) == Pal::Count && std::size (dark) == Pal::Count);
    return juce::Colour (token < Pal::Count ? (darkAppearance ? dark[token] : light[token]) : token);
}

// Remember semantic overrides so existing/recycled controls can change live.
inline void bindColour (juce::Component& component, int id, uint32_t token, float alpha = 1.0f)
{
    auto key = "rememberColour" + juce::String (id);
    component.getProperties().set (key, juce::Array<juce::var> { (int) token, alpha });
    component.setColour (id, colour (token).withAlpha (alpha));
    if (id == juce::TextEditor::textColourId)
        if (auto* editor = dynamic_cast<juce::TextEditor*> (&component))
            editor->applyColourToAllText (colour (token).withAlpha (alpha));
}
inline void placeholder (juce::TextEditor& editor, const juce::String& text, uint32_t token)
{
    editor.getProperties().set ("rememberPlaceholder", (int) token);
    editor.setTextToShowWhenEmpty (text, colour (token));
}
inline void refreshColours (juce::Component& component)
{
    const auto properties = component.getProperties();
    for (int i = 0; i < properties.size(); ++i)
        if (const auto key = properties.getName (i).toString(); key.startsWith ("rememberColour"))
        {
            const auto value = properties.getValueAt (i);
            bindColour (component, key.substring (14).getIntValue(), (uint32_t) (int) value[0], (float) value[1]);
        }
    if (auto* editor = dynamic_cast<juce::TextEditor*> (&component))
        if (properties.contains ("rememberPlaceholder"))
            editor->setTextToShowWhenEmpty (editor->getTextToShowWhenEmpty(), colour ((uint32_t) (int) properties["rememberPlaceholder"]));
    for (auto* child : component.getChildren()) refreshColours (*child);
    component.repaint();
}

inline void panel (juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 12.0f)
{
    g.setColour (RememberUI::colour (Pal::Content));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (RememberUI::colour (Pal::Border).withAlpha (0.65f));
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
    LookAndFeel() { applyPalette(); }
    void applyPalette()
    {
        setColourScheme (isDark() ? getDarkColourScheme() : getLightColourScheme());
        setColour (juce::TextButton::buttonColourId, RememberUI::colour (Pal::Elevated));
        setColour (juce::TextButton::buttonOnColourId, RememberUI::colour (Pal::Action));
        setColour (juce::TextButton::textColourOffId, RememberUI::colour (Pal::Text));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, RememberUI::colour (Pal::Content));
        setColour (juce::ComboBox::textColourId, RememberUI::colour (Pal::Text));
        setColour (juce::ComboBox::outlineColourId, RememberUI::colour (Pal::Border));
        setColour (juce::ComboBox::arrowColourId, RememberUI::colour (Pal::Subtext));
        setColour (juce::PopupMenu::backgroundColourId, RememberUI::colour (Pal::Content));
        setColour (juce::PopupMenu::textColourId, RememberUI::colour (Pal::Text));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, RememberUI::colour (Pal::Selected));
        setColour (juce::PopupMenu::highlightedTextColourId, RememberUI::colour (Pal::Accent));
        setColour (juce::Label::textColourId, RememberUI::colour (Pal::Text));
        setColour (juce::ToggleButton::textColourId, RememberUI::colour (Pal::Text));
        setColour (juce::ToggleButton::tickColourId, RememberUI::colour (Pal::Accent));
        setColour (juce::ToggleButton::tickDisabledColourId, RememberUI::colour (Pal::Subtext));
        setColour (juce::TooltipWindow::backgroundColourId, RememberUI::colour (Pal::Content));
        setColour (juce::TooltipWindow::textColourId, RememberUI::colour (Pal::Text));
        setColour (juce::TooltipWindow::outlineColourId, RememberUI::colour (Pal::Border));
        setColour (juce::ScrollBar::thumbColourId, RememberUI::colour (Pal::Subtext).withAlpha (0.45f));
        setColour (juce::TextEditor::highlightColourId, RememberUI::colour (Pal::Selected));
        setColour (juce::TextEditor::highlightedTextColourId, RememberUI::colour (Pal::Text));
        setColour (juce::TextEditor::textColourId, RememberUI::colour (Pal::Text));
        setColour (juce::TextEditor::backgroundColourId, RememberUI::colour (Pal::Content));
        setColour (juce::AlertWindow::backgroundColourId, RememberUI::colour (Pal::BgDeep));
        setColour (juce::AlertWindow::textColourId, RememberUI::colour (Pal::Text));
        setColour (juce::AlertWindow::outlineColourId, RememberUI::colour (Pal::Border));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override { return font (juce::jmin (13.0f, height * 0.55f), true); }
    juce::Font getLabelFont (juce::Label& label) override
    {
        const auto requested = label.getFont();
        return requested.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
            ? font (requested.getHeight(), requested.isBold()) : requested;
    }
    juce::Font getAlertWindowTitleFont() override { return font (20, true); }
    juce::Font getAlertWindowMessageFont() override { return font (13); }
    juce::Font getAlertWindowFont() override { return font (13); }
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& requested) override
    {
        if (requested.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return font (requested.getHeight(), requested.isBold()).getTypefacePtr();
        return juce::LookAndFeel_V4::getTypefaceForFont (requested);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font(); }
    juce::Font getPopupMenuFont() override { return font(); }

    juce::TextLayout tooltipLayout (const juce::String& text)
    {
        juce::AttributedString attributed (text);
        attributed.setFont (font (13));
        attributed.setColour (findColour (juce::TooltipWindow::textColourId));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (attributed, 400);
        return layout;
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> position,
                                           juce::Rectangle<int> parent) override
    {
        const auto layout = tooltipLayout (text);
        const int w = (int) std::ceil (layout.getWidth()) + 16;
        const int h = (int) std::ceil (layout.getHeight()) + 8;
        return juce::Rectangle<int> (position.x > parent.getCentreX() ? position.x - w - 12 : position.x + 24,
            position.y > parent.getCentreY() ? position.y - h - 6 : position.y + 6, w, h).constrainedWithin (parent);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRect (0, 0, w, h);
        tooltipLayout (text).draw (g, juce::Rectangle<float> (8, 4, (float) w - 16, (float) h - 8));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1);
        g.setColour (RememberUI::colour (down ? Pal::Selected : Pal::Content));
        g.fillRoundedRectangle (bounds, 7);
        const bool focused = box.hasKeyboardFocus (true);
        g.setColour (RememberUI::colour (focused ? Pal::Accent : Pal::Border));
        g.drawRoundedRectangle (bounds, 7, focused ? 2.0f : 1.0f);
        g.setColour (RememberUI::colour (Pal::Subtext).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        const float x = (float) w - 17, y = (float) h * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (x - 4, y - 2); arrow.lineTo (x, y + 2); arrow.lineTo (x + 4, y - 2);
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool down) override
    {
        auto track = juce::Rectangle<float> (2, (float) button.getHeight() / 2 - 9, 32, 18);
        const auto opacity = button.isEnabled() ? 1.0f : 0.4f;
        auto colour = RememberUI::colour (button.getToggleState() ? Pal::Action : Pal::Subtext);
        if (over || down) colour = colour.darker (down ? 0.15f : 0.05f);
        g.setColour (colour.withMultipliedAlpha (opacity)); g.fillRoundedRectangle (track, 9);
        g.setColour (juce::Colours::white.withMultipliedAlpha (opacity));
        g.fillEllipse (button.getToggleState() ? 18.0f : 4.0f, track.getY() + 2, 14, 14);
        if (button.hasKeyboardFocus (false))
        {
            g.setColour (RememberUI::colour (Pal::Accent));
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
        if (down) colour = base.isTransparent() ? RememberUI::colour (Pal::Border) : base.darker (0.12f);
        else if (over) colour = base.isTransparent() ? RememberUI::colour (Pal::Elevated) : base.darker (0.04f);
        if (! b.isEnabled()) colour = colour.withMultipliedAlpha (0.45f);
        g.setColour (colour); g.fillRoundedRectangle (bounds, 7);
        if (b.hasKeyboardFocus (false))
        {
            g.setColour (RememberUI::colour (Pal::Accent));
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
        g.setColour (RememberUI::colour (focused ? Pal::Accent : Pal::Border));
        g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1), 7, focused ? 2.0f : 1.0f);
    }
};

// Each root owns a reference, including detached test components and dialogs.
// Never change JUCE's global LookAndFeel: a plugin shares its process with a host.
struct Style final
{
    explicit Style (juce::Component& c) : owner (c)
    { roots().add (&owner); owner.setLookAndFeel (&look.get()); }
    ~Style() { roots().removeFirstMatchingValue (&owner); owner.setLookAndFeel (nullptr); }
    static juce::Array<juce::Component*>& roots()
    { static juce::Array<juce::Component*> values; return values; }
    juce::SharedResourcePointer<LookAndFeel> look;
    juce::Component& owner;
};
inline void setDark (bool dark)
{
    if (darkAppearance == dark) return;
    darkAppearance = dark;
    juce::SharedResourcePointer<LookAndFeel> look;
    look->applyPalette();
    juce::Array<juce::Component::SafePointer<juce::Component>> roots;
    for (auto* root : Style::roots()) roots.add (root);
    for (auto root : roots)
    {
        if (root == nullptr) continue;
        refreshColours (*root);
        root->sendLookAndFeelChange();
    }
}
}
