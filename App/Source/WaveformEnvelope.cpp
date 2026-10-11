#include "WaveformEnvelope.h"

juce::Result WaveformEnvelope::read (const juce::File& file, const std::function<bool()>& cancelled)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    if (cancelled()) return juce::Result::ok();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || ! std::isfinite (reader->sampleRate) || reader->sampleRate <= 0
        || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return juce::Result::fail ("Cannot read waveform: " + file.getFullPathName());
    const auto length = reader->lengthInSamples;
    const auto bins = (int) juce::jlimit ((juce::int64) 1, (juce::int64) 2048, (length + 511) / 512);
    for (auto& channel : channels) channel.resize ((size_t) bins);
    juce::AudioBuffer<float> audio (2, 8192);
    for (juce::int64 position = 0; position < length;)
    {
        if (cancelled()) { for (auto& channel : channels) channel.clear(); return juce::Result::ok(); }
        const auto count = (int) juce::jmin ((juce::int64) 8192, length - position);
        if (! reader->read (&audio, 0, count, position, true, true))
        { for (auto& channel : channels) channel.clear(); return juce::Result::fail ("Waveform read failed: " + file.getFullPathName()); }
        for (int i = 0; i < count; ++i)
        {
            const auto bin = (size_t) ((long double) (position + i) * bins / length);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto value = audio.getSample (ch, i);
                if (! std::isfinite (value)) continue;
                auto& peak = channels[(size_t) ch][bin];
                peak.low = juce::jmin (peak.low, value); peak.high = juce::jmax (peak.high, value);
            }
        }
        position += count;
    }
    return juce::Result::ok();
}
