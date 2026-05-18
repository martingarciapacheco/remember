#include "BpmDetector.h"

double BpmDetector::detectBpm (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples < (int) (sampleRate * 5.0) || sampleRate <= 0.0)
        return 0.0;

    // Mix down to mono
    juce::AudioBuffer<float> mono (1, numSamples);
    mono.clear();
    const float invCh = 1.0f / (float) buffer.getNumChannels();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buffer, ch, 0, numSamples, invCh);

    constexpr int fftOrder = 11;            // 2048
    constexpr int fftSize  = 1 << fftOrder;
    constexpr int hopSize  = 512;

    juce::dsp::FFT fft (fftOrder);

    // Hann window
    std::array<float, fftSize> window;
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                        * (float) i / (float) (fftSize - 1)));

    const int numFrames = (numSamples - fftSize) / hopSize;
    if (numFrames < 16) return 0.0;

    // Spectral-flux onset envelope
    std::vector<float> onset ((size_t) numFrames, 0.0f);
    std::vector<float> prevMag ((size_t) fftSize / 2, 0.0f);
    std::vector<float> buf (2 * fftSize, 0.0f);

    const float* samples = mono.getReadPointer (0);

    for (int f = 0; f < numFrames; ++f)
    {
        const int start = f * hopSize;
        for (int i = 0; i < fftSize; ++i)
            buf[(size_t) i] = samples[start + i] * window[(size_t) i];
        std::fill (buf.begin() + fftSize, buf.end(), 0.0f);

        fft.performFrequencyOnlyForwardTransform (buf.data());

        float flux = 0.0f;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const float diff = buf[(size_t) bin] - prevMag[(size_t) bin];
            if (diff > 0.0f) flux += diff;
            prevMag[(size_t) bin] = buf[(size_t) bin];
        }
        onset[(size_t) f] = flux;
    }

    // Subtract local mean, rectify, normalize
    float mean = 0.0f;
    for (auto v : onset) mean += v;
    mean /= (float) onset.size();

    float maxV = 0.0f;
    for (auto& v : onset)
    {
        v -= mean;
        if (v < 0.0f) v = 0.0f;
        if (v > maxV) maxV = v;
    }
    if (maxV <= 0.0f) return 0.0;
    for (auto& v : onset) v /= maxV;

    // Autocorrelation in 60–200 BPM range
    const double framesPerSec = sampleRate / (double) hopSize;
    int minLag = (int) std::round (framesPerSec * 60.0 / 200.0);    // 200 BPM → shortest lag
    int maxLag = (int) std::round (framesPerSec * 60.0 / 60.0);     // 60 BPM → longest lag

    if (maxLag >= numFrames) maxLag = numFrames - 1;
    if (minLag < 1)          minLag = 1;
    if (maxLag <= minLag)    return 0.0;

    double bestScore = -1.0;
    int    bestLag   = -1;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum   = 0.0;
        int    count = 0;
        for (int f = 0; f + lag < numFrames; ++f)
        {
            sum += (double) onset[(size_t) f] * (double) onset[(size_t) (f + lag)];
            ++count;
        }
        if (count > 0)
        {
            const double score = sum / (double) count;
            if (score > bestScore)
            {
                bestScore = score;
                bestLag   = lag;
            }
        }
    }

    if (bestLag <= 0) return 0.0;

    double bpm = framesPerSec * 60.0 / (double) bestLag;

    // If detected BPM is suspiciously low/high, try double/half
    while (bpm < 70.0)  bpm *= 2.0;
    while (bpm > 180.0) bpm *= 0.5;

    // Round to one decimal place
    return std::round (bpm * 10.0) / 10.0;
}
