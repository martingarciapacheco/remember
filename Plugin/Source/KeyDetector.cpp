#include "KeyDetector.h"

// Krumhansl-Schmuckler key profiles (tonic = index 0)
static constexpr std::array<float, 12> kMajor = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
    2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};
static constexpr std::array<float, 12> kMinor = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
    2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

static const juce::StringArray kNoteNames {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

juce::String KeyDetector::detectKey (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0)
        return "Unknown";

    // Mix down to mono
    juce::AudioBuffer<float> mono (1, numSamples);
    mono.clear();
    float invCh = 1.0f / (float) buffer.getNumChannels();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buffer, ch, 0, numSamples, invCh);

    auto chroma = computeChroma (mono.getReadPointer (0), numSamples, sampleRate);

    // Reject signals that are essentially silent or whose chroma is too flat
    // to support any meaningful key estimate — otherwise the algorithm would
    // tie at zero correlation and pick whichever root came first.
    float chromaSum = 0.0f, chromaMax = 0.0f;
    for (auto v : chroma) { chromaSum += v; if (v > chromaMax) chromaMax = v; }
    float chromaMean = chromaSum / 12.0f;
    if (chromaSum < 0.01f || (chromaMax - chromaMean) < 0.05f)
        return "Unknown";

    return findBestKey (chroma);
}

std::array<float, 12> KeyDetector::computeChroma (const float* samples, int numSamples, double sampleRate)
{
    // FFT order 13 → 8192 points (~186 ms at 44.1 kHz). Larger window gives
    // finer frequency resolution (~5.4 Hz/bin) — important to disambiguate
    // adjacent pitch classes at low frequencies.
    constexpr int fftOrder = 13;
    constexpr int fftSize  = 1 << fftOrder;
    constexpr int hopSize  = fftSize / 4;

    if (numSamples < fftSize) return {};

    juce::dsp::FFT fft (fftOrder);

    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                        * (float) i / (float) (fftSize - 1)));

    std::array<float, 12> chroma {};
    std::vector<float>     buf (2 * fftSize, 0.0f);

    int activeFrames = 0;

    for (int start = 0; start + fftSize <= numSamples; start += hopSize)
    {
        for (int i = 0; i < fftSize; ++i)
            buf[(size_t) i] = samples[start + i] * window[(size_t) i];
        std::fill (buf.begin() + fftSize, buf.end(), 0.0f);

        fft.performFrequencyOnlyForwardTransform (buf.data());

        // Frame energy — skip near-silent frames so they don't bias the result
        float frameEnergy = 0.0f;
        for (int bin = 1; bin < fftSize / 2; ++bin) frameEnergy += buf[(size_t) bin];
        if (frameEnergy < 1.0e-3f) continue;

        // Per-frame chroma with linear interpolation between adjacent pitch classes
        std::array<float, 12> frameChroma {};
        for (int bin = 1; bin <= fftSize / 2; ++bin)
        {
            float freq = (float) bin * (float) sampleRate / (float) fftSize;
            // Focus on the fundamental-pitch range (A1=55 Hz to C7=2093 Hz).
            // Outside this band, harmonics dominate over true pitch content.
            if (freq < 55.0f || freq > 2093.0f) continue;

            float midi  = 12.0f * std::log2 (freq / 440.0f) + 69.0f;
            float pcFrac = std::fmod (midi + 1200.0f, 12.0f);   // [0, 12)
            int   pcLo   = ((int) pcFrac) % 12;
            float w      = pcFrac - std::floor (pcFrac);
            float mag    = buf[(size_t) bin];

            frameChroma[(size_t) pcLo]              += mag * (1.0f - w);
            frameChroma[(size_t) ((pcLo + 1) % 12)] += mag * w;
        }

        // Normalize this frame's contribution so loud frames don't dominate
        float frameSum = 0.0f;
        for (auto v : frameChroma) frameSum += v;
        if (frameSum > 0.0f)
        {
            for (int i = 0; i < 12; ++i) chroma[(size_t) i] += frameChroma[(size_t) i] / frameSum;
            ++activeFrames;
        }
    }

    if (activeFrames == 0) return chroma;   // all zeros → caller returns "Unknown"

    float maxVal = *std::max_element (chroma.begin(), chroma.end());
    if (maxVal > 0.0f)
        for (auto& v : chroma) v /= maxVal;

    return chroma;
}

float KeyDetector::pearson (const std::array<float, 12>& a, const std::array<float, 12>& b)
{
    float meanA = 0.0f, meanB = 0.0f;
    for (int i = 0; i < 12; ++i) { meanA += a[(size_t) i]; meanB += b[(size_t) i]; }
    meanA /= 12.0f; meanB /= 12.0f;

    float num = 0.0f, denA = 0.0f, denB = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        float da = a[(size_t) i] - meanA;
        float db = b[(size_t) i] - meanB;
        num  += da * db;
        denA += da * da;
        denB += db * db;
    }

    float den = std::sqrt (denA * denB);
    return den > 0.0f ? num / den : 0.0f;
}

juce::String KeyDetector::findBestKey (const std::array<float, 12>& chroma)
{
    float bestCorr  = -2.0f;
    int   bestRoot  = 0;
    bool  bestMajor = true;

    for (int root = 0; root < 12; ++root)
    {
        // Rotate profiles so that index 'root' is the tonic
        std::array<float, 12> major {}, minor {};
        for (int i = 0; i < 12; ++i)
        {
            major[(size_t) i] = kMajor[(size_t) ((i - root + 12) % 12)];
            minor[(size_t) i] = kMinor[(size_t) ((i - root + 12) % 12)];
        }

        float cMajor = pearson (chroma, major);
        float cMinor = pearson (chroma, minor);

        if (cMajor > bestCorr) { bestCorr = cMajor; bestRoot = root; bestMajor = true;  }
        if (cMinor > bestCorr) { bestCorr = cMinor; bestRoot = root; bestMajor = false; }
    }

    return kNoteNames[bestRoot] + (bestMajor ? " major" : " minor");
}
