#pragma once
#include <JuceHeader.h>

// Metadata readers only. Never instantiate a plugin or guess from channel names,
// sample paths, arbitrary printable strings, or a bank's first program.
namespace UsedSounds::Presets
{
inline uint32_t le32 (const uint8_t* p) { return juce::ByteOrder::littleEndianInt (p); }
inline juce::String name (const uint8_t* p, size_t n)
{
    if (n == 0 || n > 512 || ! juce::CharPointer_UTF8::isValidString ((const char*) p, (int) n)) return {};
    if (std::memchr (p, 0, n) != nullptr) return {};
    const auto result = juce::String::fromUTF8 ((const char*) p, (int) n).trim();
    for (auto c : result) if (c < 32 || c == 127) return {};
    return result;
}
inline juce::String terminatedName (const uint8_t* p, size_t n)
{
    const auto* end = (const uint8_t*) std::memchr (p, 0, n);
    return end != nullptr ? name (p, (size_t) (end - p)) : juce::String{};
}
inline juce::String componentName (const juce::String& plugin, const uint8_t* p, size_t n)
{
    if (n >= 56 && std::memcmp (p, "CcnK", 4) == 0)
    {
        const auto size = (uint64_t) juce::ByteOrder::bigEndianInt (p + 4) + 8;
        if (size > n || size < 56) return {};
        if (std::memcmp (p + 8, "FxCk", 4) == 0)
        {
            const auto parameters = juce::ByteOrder::bigEndianInt (p + 24);
            if (56ull + 4ull * parameters != size) return {};
        }
        else if (std::memcmp (p + 8, "FPCh", 4) == 0)
        {
            if (size < 60 || 60ull + juce::ByteOrder::bigEndianInt (p + 56) != size) return {};
        }
        else return {}; // Banks do not identify the active program reliably.
        return terminatedName (p + 28, 28);
    }
    // Observed Diva state: a byte count followed by the explicit #pgm path.
    if (plugin.startsWithIgnoreCase ("Diva") && n > 10 && le32 (p) == n - 4
        && std::memcmp (p + 4, "#pgm=", 5) == 0)
    {
        const auto limit = juce::jmin<size_t> (n, 521);
        for (size_t end = 9; end < limit; ++end)
            if (p[end] == '\r' || p[end] == '\n')
            {
                auto path = name (p + 9, end - 9).replaceCharacter ('\\', '/');
                if (! path.endsWithIgnoreCase (".h2p")) return {};
                path = path.fromLastOccurrenceOf ("/", false, false);
                return path.dropLastCharacters (4);
            }
        return {};
    }
    // Only this verified Pigments archive version: the first length-prefixed
    // string is the current name (OriginalPresetName later can be different).
    constexpr char pigmentsHeader[] = "22 serialization::archive 10 0 7 0 7 ";
    constexpr size_t headerSize = sizeof (pigmentsHeader) - 1;
    if (plugin.equalsIgnoreCase ("Pigments") && n > headerSize
        && std::memcmp (p, pigmentsHeader, headerSize) == 0)
    {
        size_t end = headerSize, length = 0;
        while (end < n && end < headerSize + 3 && p[end] >= '0' && p[end] <= '9')
            length = length * 10 + (p[end++] - '0');
        if (end == headerSize || end >= n || p[end] != ' ' || length == 0 || length > 512) return {};
        ++end;
        if (length >= n - end || p[end + length] != ' ') return {};
        return name (p + end, length);
    }
    if (plugin.equalsIgnoreCase ("SynthMaster3") && n > 26 && le32 (p) == n
        && std::memcmp (p + 8, "133kt3msr3mskcle", 16) == 0)
    {
        const auto length = (size_t) p[24];
        if (length == 0 || length >= n - 25 || p[25 + length] != 0) return {};
        return name (p + 25, length);
    }
    return {};
}
inline juce::String read (const juce::String& plugin, const uint8_t* p, size_t n, bool wrapper)
{
    if (p == nullptr) return {};
    if (! wrapper)
    {
        // Native resource/patch names (e.g. FL Keys' Piano) need not be the
        // loaded preset name (e.g. Broken Piano). Leave them unavailable.
        return {};
    }
    // FL's saved VST3 state envelope, observed in FL 21-26 projects. Restrict
    // to the known header and component chunk; the controller may hold banks.
    if (n >= 92 && le32 (p) == 1 && le32 (p + 4) == 1 && le32 (p + 8) == 64
        && le32 (p + 80) == 3)
    {
        const auto length = juce::ByteOrder::littleEndianInt64 (p + 84);
        if (length > n - 92) return {};
        return componentName (plugin, p + 92, (size_t) length);
    }
    return componentName (plugin, p, n);
}
}
