#pragma once
#include <JuceHeader.h>
#include "RememberTypography.h"
#include "AppearanceMode.h"

// Shared semantic tokens for the native app, plugin and their dialogs.
namespace Pal
{
    enum Token : uint32_t { BgDeep, Surface, Elevated, Border, Accent, Text,
        Subtext, Star, Success, Error, Content, Selected, Action, OnAction, WaveLeft, WaveRight, Count };
}

namespace RememberUI
{
inline constexpr int recordingRowHeight = 48;
inline constexpr int recordingRowPadding = 6;
inline constexpr int recordingRowActionSize = 32;
inline constexpr int soundRowHeight = 60;
inline constexpr int midiSoundRowHeight = 116;
inline constexpr int generatorSoundRowHeight = 52;
inline constexpr float surfaceRadius = 12.0f;
inline constexpr int appOuterMargin = 6;
inline constexpr int appSectionGap = 6;
inline constexpr int appSidebarPadding = 16;
inline constexpr float controlRadius = 8.0f;
inline constexpr float smallRadius = 4.0f;
inline constexpr float recordingRowRadius = controlRadius;
inline constexpr int folderSelectionInset = 10;
inline constexpr int folderIconLeading = 16;
inline constexpr int dawLauncherSize = 40;
inline float capsuleRadius (juce::Rectangle<float> bounds) { return bounds.getHeight() * 0.5f; }

class TooltipWindow : public juce::TooltipWindow
{
public:
    explicit TooltipWindow (juce::Component* parent = nullptr) : juce::TooltipWindow (parent)
    { setOpaque (! juce::Desktop::canUseSemiTransparentWindows()); }
};
inline constexpr int zoomPercentages[] = { 50, 75, 90, 100, 125, 150, 200 };
inline int validZoom (int percent)
{
    for (auto value : zoomPercentages) if (value == percent) return value;
    return 100;
}
inline void populateZoom (juce::ComboBox& combo)
{
    combo.setTitle ("Interface zoom");
    combo.setTooltip ("Scale text and controls. 100% restores the default size.");
    for (auto value : zoomPercentages)
        combo.addItem (juce::String (value) + (value == 100 ? "% (default)" : "%"), value);
}
struct AppZoom final : juce::ChangeBroadcaster
{
    int percent = 100;
    void set (int value)
    {
        value = validZoom (value);
        if (percent == value) return;
        percent = value;
        sendSynchronousChangeMessage();
    }
};
inline AppZoom& appZoom() { static AppZoom zoom; return zoom; }

// Scale logical JUCE geometry, retaining scroll access when a large zoom does
// not fit a small native window. The platform frame is never transformed.
class ZoomView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit ZoomView (std::unique_ptr<juce::Component> component) : content (std::move (component))
    {
        setLookAndFeel (&content->getLookAndFeel());
        stage.addAndMakeVisible (*content);
        viewport.setViewedComponent (&stage, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
        addAndMakeVisible (viewport);
        appZoom().addChangeListener (this);
        setSize (960, 680);
    }
    ~ZoomView() override { appZoom().removeChangeListener (this); viewport.setViewedComponent (nullptr, false); setLookAndFeel (nullptr); }
    juce::Component* getContent() const { return content.get(); }
    void paint (juce::Graphics& g) override;
    void resized() override
    {
        const float scale = appZoom().percent / 100.0f;
        viewport.setBounds (getLocalBounds());
        const int bar = viewport.getScrollBarThickness();
        const bool vertical = getHeight() < juce::roundToInt (480 * scale);
        const bool horizontal = getWidth() - (vertical ? bar : 0) < juce::roundToInt (640 * scale);
        const bool needsVertical = getHeight() - (horizontal ? bar : 0) < juce::roundToInt (480 * scale);
        content->setTopLeftPosition (0, 0);
        content->setTransform (juce::AffineTransform::scale (scale));
        content->setSize (juce::jmax (640, (int) std::ceil ((getWidth() - (needsVertical ? bar : 0)) / scale)),
                          juce::jmax (480, (int) std::ceil ((getHeight() - (horizontal ? bar : 0)) / scale)));
        stage.setSize (juce::jmax (getWidth() - (needsVertical ? bar : 0), juce::roundToInt (640 * scale)),
                       juce::jmax (getHeight() - (horizontal ? bar : 0), juce::roundToInt (480 * scale)));
    }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { resized(); repaint(); }
    std::unique_ptr<juce::Component> content;
    juce::Component stage;
    juce::Viewport viewport;
};
// Message-thread state, shared by Remember roots in this process, never the host.
inline bool darkAppearance = false;
inline RememberAppearance appearanceMode = RememberAppearance::System;
inline void setAppearanceMode (RememberAppearance);
inline bool isDark() { return darkAppearance; }
inline juce::Colour colour (uint32_t token)
{
    static constexpr uint32_t light[] = { 0xfff5f5f7, 0xffededf0, 0xffe4e5ea,
        0xffd1d3da, 0xff0060c0, 0xff1d1d1f, 0xff62626b, 0xff916000,
        0xff247343, 0xffb42318, 0xffffffff, 0xffe5effb, 0xff0066cc, 0xffffffff, 0xc00060c0, 0x995a9fe6 };
    static constexpr uint32_t dark[] = { 0xff141416, 0xff1c1c1e, 0xff2c2c30,
        0xff414146, 0xff64a9ff, 0xfff5f5f7, 0xffb8b8c0, 0xffffd60a,
        0xff66d98a, 0xffff8f86, 0xff242426, 0xff1c3048, 0xff0066cc, 0xffffffff, 0xc064a9ff, 0x99a9d0ff };
    static_assert (std::size (light) == Pal::Count && std::size (dark) == Pal::Count);
    return juce::Colour (token < Pal::Count ? (darkAppearance ? dark[token] : light[token]) : token);
}

inline void ZoomView::paint (juce::Graphics& g) { g.fillAll (colour (Pal::BgDeep)); }

// The list and selected rows share a full-width contour. Mask only the four
// outer corners after children paint, including native viewport scrolling.
class RecordingListBox : public juce::ListBox
{
public:
    using juce::ListBox::ListBox;
    void paintOverChildren (juce::Graphics& g) override
    {
        juce::Path corners;
        corners.setUsingNonZeroWinding (false);
        corners.addRectangle (getLocalBounds().toFloat());
        corners.addRoundedRectangle (getLocalBounds().toFloat(), recordingRowRadius);
        g.setColour (colour (Pal::BgDeep));
        g.fillPath (corners);
    }
};

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

inline void panel (juce::Graphics& g, juce::Rectangle<float> bounds, float radius = surfaceRadius, bool outlined = true)
{
    g.setColour (RememberUI::colour (Pal::Content));
    g.fillRoundedRectangle (bounds, radius);
    if (outlined)
    {
        g.setColour (RememberUI::colour (Pal::Border).withAlpha (0.65f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), juce::jmax (0.0f, radius - 0.5f), 1.0f);
    }
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
    else if (kind == "previous" || kind == "next")
    {
        if (kind == "next") { p.addTriangle (x + 2, y + 2, x + 2, y + 14, x + 12, y + 8); g.fillRect (x + 12, y + 2, 2.0f, 12.0f); }
        else { p.addTriangle (x + 14, y + 2, x + 14, y + 14, x + 4, y + 8); g.fillRect (x + 2, y + 2, 2.0f, 12.0f); }
        g.fillPath (p);
    }
    else if (kind == "shuffle")
    {
        p.startNewSubPath (x + 1, y + 3); p.lineTo (x + 5, y + 3); p.lineTo (x + 11, y + 13); p.lineTo (x + 15, y + 13);
        p.startNewSubPath (x + 1, y + 13); p.lineTo (x + 5, y + 13); p.lineTo (x + 11, y + 3); p.lineTo (x + 15, y + 3);
        g.strokePath (p, juce::PathStrokeType (1.5f));
        p.clear(); p.addTriangle (x + 12, y, x + 12, y + 6, x + 16, y + 3);
        p.addTriangle (x + 12, y + 10, x + 12, y + 16, x + 16, y + 13); g.fillPath (p);
    }
    else if (kind == "repeat" || kind == "repeat-single")
    {
        g.drawRoundedRectangle (b.reduced (1, 3), 3, 1.5f);
        p.addTriangle (x + 9, y, x + 9, y + 6, x + 14, y + 3); g.fillPath (p);
        if (filled || kind == "repeat-single")
        {
            g.setFont (font (10, true));
            g.drawText (kind == "repeat-single" ? "S" : "1", b.reduced (4, 3), juce::Justification::centred);
        }
    }
    else if (kind == "metronome")
    {
        p.startNewSubPath (x + 5, y + 1); p.lineTo (x + 11, y + 1);
        p.lineTo (x + 15, y + 15); p.lineTo (x + 1, y + 15); p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
        g.drawLine (x + 8, y + 12, x + 12, y + 3, 1.5f);
        g.fillRoundedRectangle (x + 9, y + 5, 4, 3, 0.6f);
    }
    else if (kind == "volume" || kind == "mute")
    {
        p.startNewSubPath (x + 1, y + 5); p.lineTo (x + 5, y + 5); p.lineTo (x + 9, y + 2); p.lineTo (x + 9, y + 14);
        p.lineTo (x + 5, y + 11); p.lineTo (x + 1, y + 11); p.closeSubPath(); g.fillPath (p);
        if (kind == "mute") { g.drawLine (x + 12, y + 5, x + 16, y + 11, 1.5f); g.drawLine (x + 16, y + 5, x + 12, y + 11, 1.5f); }
        else { p.clear(); p.addCentredArc (x + 8, y + 8, 7, 7, 0, 0.7f, 2.4f, true); g.strokePath (p, juce::PathStrokeType (1.5f)); }
    }
    else if (kind == "normalize")
    {
        for (int i = 0; i < 3; ++i) g.drawLine (x + 3 + i * 5.0f, y + 2, x + 3 + i * 5.0f, y + 14, 1.5f);
        g.fillEllipse (x + 1, y + 4, 4, 4); g.fillEllipse (x + 6, y + 9, 4, 4); g.fillEllipse (x + 11, y + 2, 4, 4);
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
    else if (kind == "sounds")
    {
        for (int i = 0; i < 4; ++i)
        {
            const float top = i == 1 ? 2.0f : i == 2 ? 5.0f : 7.0f;
            g.drawLine (x + 2 + i * 4.0f, y + top, x + 2 + i * 4.0f, y + 16 - top, 1.6f);
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

inline juce::String shortcutTooltip (const juce::String& title, const juce::String& shortcut,
                                     const juce::String& details = {})
{
    return title + " (" + shortcut + ")" + (details.isEmpty() ? juce::String{} : "\n" + details);
}

inline void iconButton (juce::TextButton& button, const juce::String& kind, const juce::String& title)
{
    button.getProperties().set ("rememberIcon", kind);
    button.setTitle (title);
    button.setDescription (title);
    button.setTooltip (title);
}

// Normal activation retains JUCE's press/release and keyboard behaviour.
// A context click is handled separately and never activates the main action.
class ContextMenuButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onContextMenu;
    void mouseDown (const juce::MouseEvent& event) override
    {
        contextPressed = event.mods.isPopupMenu();
        if (! contextPressed) juce::TextButton::mouseDown (event);
    }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! contextPressed) juce::TextButton::mouseDrag (event);
    }
    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! contextPressed) { juce::TextButton::mouseUp (event); return; }
        contextPressed = false;
        if (isEnabled() && getLocalBounds().toFloat().contains (event.position) && onContextMenu) onContextMenu();
    }
private:
    bool contextPressed = false;
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel() { applyPalette(); }
    void drawTreeviewPlusMinusBox (juce::Graphics& g, const juce::Rectangle<float>& area,
                                  juce::Colour, bool open, bool over) override
    {
        // The disclosure begins at the same inset as All's folder symbol.
        const float x = area.getX() + (float) (folderIconLeading - 8);
        const float y = area.getCentreY();
        juce::Path triangle;
        if (open) triangle.addTriangle (x, y - 3, x + 7, y - 3, x + 3.5f, y + 3);
        else      triangle.addTriangle (x, y - 3.5f, x + 6, y, x, y + 3.5f);
        g.setColour (RememberUI::colour (over ? Pal::Text : Pal::Subtext));
        g.fillPath (triangle);
    }
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
    void preparePopupMenuWindow (juce::Component& window) override
    {
        juce::LookAndFeel_V4::preparePopupMenuWindow (window);
        window.setOpaque (! juce::Desktop::canUseSemiTransparentWindows());
    }
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 10; }
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override { return surfaceRadius; }
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        g.setColour (RememberUI::colour (Pal::Content)); g.fillPath (path);
        g.setColour (RememberUI::colour (Pal::Border)); g.strokePath (path, juce::PathStrokeType (1));
    }
    int getPopupMenuBorderSize() override { return 6; }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        if (! juce::Desktop::canUseSemiTransparentWindows()) g.fillAll (RememberUI::colour (Pal::Content));
        panel (g, juce::Rectangle<float> (0, 0, (float) w, (float) h));
    }
    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool separator, bool active,
                            bool highlighted, bool ticked, bool submenu, const juce::String& text,
                            const juce::String& shortcut, const juce::Drawable* drawable,
                            const juce::Colour* textColour) override
    {
        juce::Graphics::ScopedSaveState saved (g);
        juce::Path contour;
        contour.addRoundedRectangle (area.toFloat().reduced (2, 0), controlRadius);
        g.reduceClipRegion (contour);
        juce::LookAndFeel_V4::drawPopupMenuItem (g, area, separator, active, highlighted, ticked,
                                               submenu, text, shortcut, drawable, textColour);
    }

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
        const auto bounds = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        if (! juce::Desktop::canUseSemiTransparentWindows()) g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (bounds, controlRadius);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), controlRadius - 0.5f, 1);
        tooltipLayout (text).draw (g, juce::Rectangle<float> (8, 4, (float) w - 16, (float) h - 8));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1);
        g.setColour (RememberUI::colour (down ? Pal::Selected : Pal::Content));
        g.fillRoundedRectangle (bounds, controlRadius);
        const bool focused = box.hasKeyboardFocus (true);
        g.setColour (RememberUI::colour (focused ? Pal::Accent : Pal::Border));
        g.drawRoundedRectangle (bounds, controlRadius, focused ? 2.0f : 1.0f);
        g.setColour (RememberUI::colour (Pal::Subtext).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        const float x = (float) w - 17, y = (float) h * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (x - 4, y - 2); arrow.lineTo (x, y + 2); arrow.lineTo (x + 4, y - 2);
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool down) override
    {
        if ((bool) button.getProperties()["rememberCheckbox"])
        {
            const auto box = juce::Rectangle<float> (6, button.getHeight() * 0.5f - 9, 18, 18);
            g.setColour (RememberUI::colour (button.getToggleState() ? Pal::Action : (over || down ? Pal::Elevated : Pal::Content)));
            g.fillRoundedRectangle (box, smallRadius);
            g.setColour (RememberUI::colour (button.hasKeyboardFocus (false) ? Pal::Accent : Pal::Border));
            g.drawRoundedRectangle (box, smallRadius, button.hasKeyboardFocus (false) ? 2.0f : 1.0f);
            if (button.getToggleState())
            {
                g.setColour (RememberUI::colour (Pal::OnAction));
                juce::Path tick; tick.startNewSubPath (box.getX() + 4, box.getY() + 9);
                tick.lineTo (box.getX() + 8, box.getY() + 13); tick.lineTo (box.getX() + 14, box.getY() + 5);
                g.strokePath (tick, juce::PathStrokeType (2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            return;
        }
        auto track = juce::Rectangle<float> (2, (float) button.getHeight() / 2 - 9, 32, 18);
        const auto opacity = button.isEnabled() ? 1.0f : 0.4f;
        auto colour = RememberUI::colour (button.getToggleState() ? Pal::Action : Pal::Subtext);
        if (over || down) colour = colour.darker (down ? 0.15f : 0.05f);
        g.setColour (colour.withMultipliedAlpha (opacity)); g.fillRoundedRectangle (track, capsuleRadius (track));
        g.setColour (juce::Colours::white.withMultipliedAlpha (opacity));
        g.fillEllipse (button.getToggleState() ? 18.0f : 4.0f, track.getY() + 2, 14, 14);
        if (button.hasKeyboardFocus (false))
        {
            g.setColour (RememberUI::colour (Pal::Accent));
            g.drawRoundedRectangle (track.expanded (2), capsuleRadius (track.expanded (2)), 1.5f);
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
        g.setColour (colour);
        if ((bool) b.getProperties()["rememberTransportPrimary"]) g.fillEllipse (bounds);
        else g.fillRoundedRectangle (bounds, controlRadius);
        if (b.hasKeyboardFocus (false))
        {
            g.setColour (RememberUI::colour (Pal::Accent));
            if ((bool) b.getProperties()["rememberTransportPrimary"]) g.drawEllipse (bounds, 2);
            else g.drawRoundedRectangle (bounds, controlRadius, 2);
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
        icon (g, kind, b.getLocalBounds().toFloat(), b.getButtonText() == "*" || b.getProperties()["rememberRepeatOne"] == juce::var (true));
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1), controlRadius);
    }
    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        const bool focused = e.hasKeyboardFocus (true) && ! e.isReadOnly();
        g.setColour (RememberUI::colour (focused ? Pal::Accent : Pal::Border));
        g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1), controlRadius, focused ? 2.0f : 1.0f);
    }
};

// Standalone Windows chrome. DocumentWindow still supplies native caption hit
// testing and window actions; only its presentation is replaced.
class MainWindowLookAndFeel final : public LookAndFeel
{
    class WindowButton final : public juce::Button
    {
    public:
        explicit WindowButton (int typeIn)
            : Button (typeIn == juce::DocumentWindow::closeButton ? "Close"
                      : typeIn == juce::DocumentWindow::minimiseButton ? "Minimise" : "Maximise or restore"),
              type (typeIn)
        {
            setTitle (getName());
            setTooltip (getName());
            setWantsKeyboardFocus (true);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto area = getLocalBounds().toFloat().reduced (2);
            if (over || down)
            {
                g.setColour (colour (down ? Pal::Selected : Pal::Elevated));
                g.fillRoundedRectangle (area, controlRadius);
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (colour (Pal::Accent));
                g.drawRoundedRectangle (area.reduced (1), controlRadius, 2);
            }
            g.setColour (colour (isEnabled() ? Pal::Text : Pal::Subtext));
            const auto symbol = area.withSizeKeepingCentre (10, 10);
            if (type == juce::DocumentWindow::closeButton)
            {
                g.drawLine (symbol.getX(), symbol.getY(), symbol.getRight(), symbol.getBottom(), 1.2f);
                g.drawLine (symbol.getX(), symbol.getBottom(), symbol.getRight(), symbol.getY(), 1.2f);
            }
            else if (type == juce::DocumentWindow::minimiseButton)
                g.drawHorizontalLine (juce::roundToInt (symbol.getCentreY()), symbol.getX(), symbol.getRight());
            else if (getToggleState())
            {
                g.drawRect (symbol.translated (2, -2).withTrimmedLeft (2).withTrimmedBottom (2), 1);
                g.setColour (colour (down ? Pal::Selected : over ? Pal::Elevated : Pal::BgDeep));
                g.fillRect (symbol.withTrimmedRight (2).withTrimmedTop (2));
                g.setColour (colour (Pal::Text));
                g.drawRect (symbol.withTrimmedRight (2).withTrimmedTop (2), 1);
            }
            else g.drawRect (symbol, 1);
        }
    private:
        int type;
    };
public:
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics& g,
                                    int, int, int, int, const juce::Image*, bool) override
    { g.fillAll (colour (Pal::BgDeep)); }

    juce::Button* createDocumentWindowButton (int type) override { return new WindowButton (type); }

    void positionDocumentWindowButtons (juce::DocumentWindow&, int x, int y, int w, int h,
                                       juce::Button* minimise, juce::Button* maximise,
                                       juce::Button* close, bool) override
    {
        int right = x + w;
        for (auto* button : { close, maximise, minimise })
            if (button != nullptr) { right -= 46; button->setBounds (right, y, 46, h); }
    }
};

// One native OS listener per module, retained only while UI roots exist.
// An injectable read source lets tests deliver OS notifications without changing
// the user's desktop setting. No timer, filesystem work or host theme mutation.
class SystemAppearance final : public juce::DarkModeSettingListener
{
public:
    explicit SystemAppearance (std::function<bool()> reader = [] { return juce::Desktop::getInstance().isDarkModeActive(); });
    ~SystemAppearance() override;
    void darkModeSettingChanged() override;
private:
    std::function<bool()> read;
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
    juce::SharedResourcePointer<SystemAppearance> systemAppearance;
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
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* window = desktop.getComponent (i); &window->getLookAndFeel() == &look.get())
            roots.add (window);
    for (auto root : roots)
    {
        if (root == nullptr) continue;
        refreshColours (*root);
        root->sendLookAndFeelChange();
    }
}

inline void setAppearanceMode (RememberAppearance mode)
{
    appearanceMode = mode;
    setDark (mode == RememberAppearance::System ? juce::Desktop::getInstance().isDarkModeActive()
                                               : mode == RememberAppearance::Dark);
}
inline SystemAppearance::SystemAppearance (std::function<bool()> reader) : read (std::move (reader))
{
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    darkModeSettingChanged();
}
inline SystemAppearance::~SystemAppearance() { juce::Desktop::getInstance().removeDarkModeSettingListener (this); }
inline void SystemAppearance::darkModeSettingChanged()
{
    if (appearanceMode == RememberAppearance::System) setDark (read());
}
}
