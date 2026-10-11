#pragma once
#include <JuceHeader.h>
#if ! JUCE_MAC && ! JUCE_IOS
 #include <RememberFontData.h>
#endif

namespace RememberUI
{
#if ! JUCE_MAC && ! JUCE_IOS
inline juce::Typeface::Ptr interTypeface (float size, bool emphasis)
{
    struct Faces
    {
        juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
            RememberFontData::InterRegular_ttf, RememberFontData::InterRegular_ttfSize);
        juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (
            RememberFontData::InterSemiBold_ttf, RememberFontData::InterSemiBold_ttfSize);
        juce::Typeface::Ptr display = juce::Typeface::createSystemTypefaceFor (
            RememberFontData::InterDisplayRegular_ttf, RememberFontData::InterDisplayRegular_ttfSize);
        juce::Typeface::Ptr displaySemibold = juce::Typeface::createSystemTypefaceFor (
            RememberFontData::InterDisplaySemiBold_ttf, RememberFontData::InterDisplaySemiBold_ttfSize);
    };
    // Decode once on the UI thread; never scan installed fonts or touch audio.
    static const Faces faces;
    return size >= 20.0f ? (emphasis ? faces.displaySemibold : faces.display)
                         : (emphasis ? faces.semibold : faces.regular);
}
#endif

inline juce::Font font (float size = 13.0f, bool emphasis = false)
{
   #if JUCE_MAC || JUCE_IOS
    // JUCE resolves this through Core Text's system UI font (San Francisco).
    // Do not bundle Apple's downloadable mock-up font files.
    return juce::Font (juce::FontOptions{}.withName (juce::Font::getSystemUIFontName())
        .withStyle (emphasis ? "Semibold" : "Regular").withHeight (size));
   #else
    if (auto face = interTypeface (size, emphasis))
        return juce::Font (juce::FontOptions (face).withHeight (size));
    jassertfalse; // A corrupt asset must be caught by frontend validation.
    return juce::Font (juce::FontOptions{}.withHeight (size).withStyle (emphasis ? "Bold" : "Regular"));
   #endif
}
}
