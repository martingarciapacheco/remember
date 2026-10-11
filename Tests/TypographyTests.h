#pragma once
#include "../Shared/RememberLookAndFeel.h"
#include <stdexcept>

inline void testFrontendTypography()
{
    for (const float size : { 12.0f, 13.0f, 14.0f, 20.0f, 24.0f })
        for (const bool emphasis : { false, true })
        {
            const auto font = RememberUI::font (size, emphasis);
            const auto face = font.getTypefacePtr();
            if (face == nullptr) throw std::runtime_error ("Missing frontend typeface");
           #if JUCE_MAC || JUCE_IOS
            if (font.getTypefaceName() != juce::Font::getSystemUIFontName())
                throw std::runtime_error ("Apple frontend bypassed the native system font");
           #else
            if (! face->getName().startsWith ("Inter") || face->getStyle().containsIgnoreCase ("semi") != emphasis
                || face->getName().contains ("Display") != (size >= 20))
                throw std::runtime_error ("Frontend fell back to a system font or lost its Inter weight/optical size");
            if (face != RememberUI::font (size, emphasis).getTypefacePtr())
                throw std::runtime_error ("Frontend decodes font data repeatedly");
           #endif
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, juce::String::fromUTF8 ("Remember · Café · canción · 124 BPM"), 0, 0);
            if (glyphs.getBoundingBox (0, -1, true).isEmpty()) throw std::runtime_error ("Frontend font cannot render interface text");
        }
    RememberUI::LookAndFeel look;
    juce::Label defaultLabel;
   #if ! JUCE_MAC && ! JUCE_IOS
    if (! look.getLabelFont (defaultLabel).getTypefacePtr()->getName().startsWith ("Inter"))
        throw std::runtime_error ("Default JUCE controls do not use the embedded typeface");
   #endif
    const auto bounds = look.getTooltipBounds ("A long preview path with an accented canción", { 50, 50 }, { 0, 0, 640, 480 });
    if (bounds.isEmpty() || ! juce::Rectangle<int> (640, 480).contains (bounds))
        throw std::runtime_error ("Typeface change clipped the tooltip");
}
