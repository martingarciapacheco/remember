#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

// Stereo/mono BS.1770 K-weighting and absolute/relative integrated gates.
// Worker-only. Playback gain is capped by decoded sample peak with 1 dB headroom.
class LoudnessMeter
{
public:
    LoudnessMeter (double rate, int channels) : channelCount (juce::jlimit (1, 2, channels)),
        window ((size_t) juce::jmax (1, (int) std::round (rate * 0.4)), 0),
        hop (juce::jmax (1, (int) std::round (rate * 0.1)))
    {
        const auto k = std::tan (juce::MathConstants<double>::pi * 1681.974450955533 / rate);
        const auto vh = std::pow (10.0, 3.999843853973347 / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const auto q = 0.7071752369554196, a0 = 1 + k / q + k * k;
        const auto r = std::tan (juce::MathConstants<double>::pi * 38.13547087602444 / rate);
        const auto rq = 0.5003270373238773, ra0 = 1 + r / rq + r * r;
        for (int ch = 0; ch < channelCount; ++ch)
        {
            shelf[(size_t) ch] = { (vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0,
                (vh - vb * k / q + k * k) / a0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0 };
            highpass[(size_t) ch] = { 1, -2, 1, 2 * (r * r - 1) / ra0, (1 - r / rq + r * r) / ra0 };
        }
    }
    void add (const juce::AudioBuffer<float>& audio, int samples)
    {
        for (int i = 0; i < samples; ++i)
        {
            double energy = 0;
            for (int ch = 0; ch < channelCount; ++ch)
            {
                const auto raw = (double) audio.getSample (ch, i);
                if (! std::isfinite (raw)) continue;
                peak = juce::jmax (peak, std::abs (raw));
                const auto weighted = highpass[(size_t) ch].process (shelf[(size_t) ch].process (raw));
                energy += weighted * weighted;
            }
            sum += energy - window[cursor]; window[cursor] = energy;
            cursor = (cursor + 1) % window.size(); ++count;
            if (count >= window.size() && (count - window.size()) % (size_t) hop == 0)
                blocks.push_back (juce::jmax (0.0, sum / (double) window.size()));
        }
    }
    double integrated() const
    {
        const auto absolute = std::pow (10.0, (-70.0 + 0.691) / 10);
        double first = 0; int n = 0;
        for (const auto e : blocks) if (e >= absolute) { first += e; ++n; }
        if (n == 0) return -100;
        const auto threshold = juce::jmax (absolute, first / n * 0.1);
        double gated = 0; n = 0;
        for (const auto e : blocks) if (e >= threshold) { gated += e; ++n; }
        return n > 0 && gated > 0 ? -0.691 + 10 * std::log10 (gated / n) : -100;
    }
    float gain (double target = -14) const
    {
        const auto lufs = integrated();
        if (lufs <= -70 || peak <= 0) return 1;
        return (float) juce::jmin (std::pow (10.0, (target - lufs) / 20.0), std::pow (10.0, -1.0 / 20.0) / peak);
    }
    double peak = 0;
private:
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x) { const auto y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    std::array<Biquad, 2> shelf, highpass;
    int channelCount, hop;
    std::vector<double> window, blocks;
    size_t cursor = 0, count = 0;
    double sum = 0;
};
