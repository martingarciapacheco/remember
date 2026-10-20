#pragma once
#include <JuceHeader.h>
#include <functional>

// Immutable, self-contained preview packages. The index describes OGG source
// ranges and gain envelopes; compressed payloads are copied, never transcoded.
// Readers and writers belong on workers, not a device or DAW audio callback.
namespace PreviewAudio
{
constexpr int maximumSpans = 256, maximumEnvelopes = 16;
constexpr juce::int64 maximumPackageBytes = 32 * 1024 * 1024;
bool isPackage (const juce::File&);
void registerFormats (juce::AudioFormatManager&);
std::unique_ptr<juce::AudioFormatReader> open (const juce::File&);
juce::Result verify (const juce::File&, const std::function<bool()>& cancelled = {});
juce::Result write (const juce::File& destination, const juce::File& captured,
                    const juce::File& previous, double rate, int channels,
                    int prefixFrames, int totalFrames, int quality, float newGain,
                    const std::function<bool()>& cancelled = {}, float outputGain = 1);
}
