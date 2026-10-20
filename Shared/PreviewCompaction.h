#pragma once
#include "PreviewAudio.h"

namespace PreviewCompaction
{
struct Encoding
{
    float gain = 1, peak = 0;
    int passes = 0;
};
// Worker-only. Reads the existing mix and writes one ordinary OGG, without WAV.
juce::Result encode (const juce::File& source, const juce::File& destination, int quality,
                     Encoding&, const std::function<bool()>& cancelled = {});
}
