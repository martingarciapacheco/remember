#include "../../Shared/PreviewAudio.h"
#include "WaveformEnvelope.h"
#include "LoudnessMeter.h"

juce::Result WaveformEnvelope::read (const juce::File& file, const std::function<bool()>& cancelled)
{
    juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
    if (cancelled()) return juce::Result::ok();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || ! std::isfinite (reader->sampleRate) || reader->sampleRate <= 0 || reader->sampleRate > 768000
        || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return juce::Result::fail ("Cannot read waveform: " + file.getFullPathName());
    const auto length = reader->lengthInSamples;
    channelCount = (int) juce::jmin (2u, reader->numChannels);
    // Keep enough detail for enlarged/high-DPI editors without decoding on paint.
    const auto bins = (int) juce::jlimit ((juce::int64) 1, (juce::int64) 32768, 1 + (length - 1) / 64);
    for (auto& channel : channels) channel.assign ((size_t) bins, {});
    const int fftOrder = juce::jlimit (10, 15, (int) std::ceil (std::log2 (reader->sampleRate / 24.0)));
    const int fftSize = 1 << fftOrder;
    juce::AudioBuffer<float> audio (2, juce::jmax (8192, fftSize));
    LoudnessMeter meter (reader->sampleRate, (int) reader->numChannels);
    // Windowed spectra are computed only on this worker. Retain energy and its
    // frequency moment per peak so resizing combines spectral content correctly.
    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> spectrum ((size_t) fftSize * 2);
    for (juce::int64 position = 0; position < length;)
    {
        if (cancelled()) { for (auto& channel : channels) channel.clear(); return juce::Result::ok(); }
        const auto count = (int) juce::jmin ((juce::int64) audio.getNumSamples(), length - position);
        if (! reader->read (&audio, 0, count, position, true, true))
        { for (auto& channel : channels) channel.clear(); return juce::Result::fail ("Waveform read failed: " + file.getFullPathName()); }
        meter.add (audio, count);
        for (int ch = 0; ch < channelCount; ++ch)
            for (int offset = 0; offset < count; offset += fftSize)
            {
                if (cancelled()) { for (auto& channel : channels) channel.clear(); return juce::Result::ok(); }
                std::fill (spectrum.begin(), spectrum.end(), 0.0f);
                const int frames = juce::jmin (fftSize, count - offset);
                for (int i = 0; i < frames; ++i)
                {
                    const auto value = audio.getSample (ch, offset + i);
                    spectrum[(size_t) i] = std::isfinite (value) ? value : 0;
                }
                window.multiplyWithWindowingTable (spectrum.data(), fftSize);
                fft.performFrequencyOnlyForwardTransform (spectrum.data());
                double power = 0, moment = 0;
                for (int i = 1; i <= fftSize / 2; ++i)
                {
                    const auto frequency = i * reader->sampleRate / fftSize;
                    if (frequency > 20000) break;
                    const double energy = (double) spectrum[(size_t) i] * spectrum[(size_t) i];
                    power += energy; moment += energy * frequency;
                }
                const auto first = (size_t) ((long double) (position + offset) * bins / length);
                const auto last = juce::jmin ((size_t) bins, (size_t) ((long double) (position + offset + frames - 1) * bins / length) + 1);
                for (auto bin = first; bin < last; ++bin)
                {
                    channels[(size_t) ch][bin].spectralPower += (float) power;
                    channels[(size_t) ch][bin].frequencyMoment += (float) moment;
                }
            }
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
    normalizationGain = meter.gain(); loudness = meter.integrated(); decodedPeak = meter.peak;
    return juce::Result::ok();
}
