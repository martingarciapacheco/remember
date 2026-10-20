#include "KeyDetector.h"

namespace
{
// Version 5: profiles fitted to the 301 frozen development references only.
// Parameters and internal validation: candidate-v5-native-plan.json.
constexpr std::array<float, 12> majorProfile {
    0.455908367f, -0.345346810f, 0.152558367f, -0.280896637f,
    0.178232991f, -0.000087256f, -0.231534834f, 0.579290211f,
    -0.317191331f, 0.010380040f, -0.220598334f, 0.019285227f
};
constexpr std::array<float, 12> minorProfile {
    0.587392468f, -0.272400146f, 0.097743321f, 0.130584844f,
    -0.189454637f, 0.006783512f, -0.305307193f, 0.506913859f,
    -0.191953537f, -0.243709386f, 0.115302918f, -0.241896021f
};
const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr int fftOrder = 13, fftSize = 1 << fftOrder, hop = fftSize / 4;
struct Peak { float frequency = 0, magnitude = 0, background = 0; };
struct Plan
{
    std::array<float, fftSize> window {};
    std::array<std::pair<int, int>, 1858> background {};
    Plan()
    {
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f * (1 - std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1)));
        for (int bin = 21; bin <= 1857; ++bin)
            background[(size_t) bin] = {
                juce::jmax (1, (int) std::floor (bin / std::exp2 (1.0 / 3))),
                juce::jmin (fftSize / 2, (int) std::ceil (bin * std::exp2 (1.0 / 3))) };
    }
};
const Plan& plan() { static const Plan value; return value; }
}

juce::String KeyDetector::detectKey (const juce::AudioBuffer<float>& audio, double rate)
{
    return analyse (audio, rate).key;
}

KeyDetector::Estimate KeyDetector::analyse (const juce::AudioBuffer<float>& audio, double rate,
                                           const AnalysisAudio::Cancellation& cancelled)
{
    if (! std::isfinite (rate) || rate <= 0 || audio.getNumChannels() == 0 || (cancelled && cancelled())) return {};
    // Keep an already-normalised buffer; avoid another 20 MiB copy.
    juce::AudioBuffer<float> normalised;
    const auto* input = &audio;
    if (rate != AnalysisAudio::rate)
    {
        normalised = AnalysisAudio::normalise (audio, rate, cancelled);
        input = &normalised;
    }
    if (input->getNumSamples() < fftSize || input->getNumChannels() == 0) return {};
    const auto features = extract (*input, input->getNumSamples(), nullptr, 0, cancelled);
    if (cancelled && cancelled()) return {};
    return estimate (features);
}

int KeyDetector::requiredSamples (int totalSamples, int unchangedFrom, bool reuse)
{
    const auto limit = juce::jlimit (0, (int) (120 * AnalysisAudio::rate), totalSamples);
    const auto frames = limit < fftSize ? 0 : 1 + (limit - fftSize) / hop;
    const auto boundary = juce::jlimit (0, limit, unchangedFrom);
    const auto computed = reuse ? juce::jlimit (0, frames, (boundary + hop - 1) / hop) : frames;
    return computed == 0 ? 0 : (computed - 1) * hop + fftSize;
}

KeyDetector::Features KeyDetector::extract (const juce::AudioBuffer<float>& audio, int totalSamples,
                                            const Features* previous, int unchangedFrom,
                                            const AnalysisAudio::Cancellation& cancelled)
{
    Features result;
    result.samples = juce::jlimit (0, (int) (120 * AnalysisAudio::rate), totalSamples);
    result.channels = juce::jmin (2, audio.getNumChannels());
    const auto frames = result.samples < fftSize ? 0 : 1 + (result.samples - fftSize) / hop;
    const auto reuse = previous && previous->valid && previous->samples == result.samples
        && previous->channels == result.channels && (int) previous->frames.size() == frames;
    if (result.channels == 0 || audio.getNumSamples() < requiredSamples (result.samples, unchangedFrom, reuse)
        || (cancelled && cancelled())) return result;
    result.frames.resize ((size_t) frames);
    const auto& prepared = plan();
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> buffer (2 * fftSize, 0);
    std::vector<double> prefix ((size_t) fftSize / 2 + 2);
    std::vector<Peak> peaks; peaks.reserve (1024);
    for (int index = 0; index < frames; ++index)
    {
        if (cancelled && cancelled()) return {};
        const auto start = index * hop;
        auto& frame = result.frames[(size_t) index];
        if (reuse && start >= unchangedFrom)
        {
            frame = previous->frames[(size_t) index]; ++result.reused; continue;
        }
        ++result.computed;
        double energy = 0;
        std::array<double, 12> chroma {};
        for (int ch = 0; ch < result.channels; ++ch)
        {
            bool nonzero = false;
            const auto* samples = audio.getReadPointer (ch, start);
            for (int i = 0; i < fftSize; ++i)
            {
                const auto value = std::isfinite (samples[i]) ? samples[i] : 0;
                nonzero = nonzero || value != 0;
                energy += (double) value * value;
                buffer[(size_t) i] = value * prepared.window[(size_t) i];
            }
            std::fill (buffer.begin() + fftSize, buffer.end(), 0.0f);
            if (! nonzero) continue;
            fft.performFrequencyOnlyForwardTransform (buffer.data());
            prefix[0] = 0;
            for (int bin = 0; bin <= fftSize / 2; ++bin)
                prefix[(size_t) bin + 1] = prefix[(size_t) bin] + buffer[(size_t) bin];
            peaks.clear();
            for (int bin = 21; bin <= 1857; ++bin)
            {
                const auto left = buffer[(size_t) bin - 1], centre = buffer[(size_t) bin], right = buffer[(size_t) bin + 1];
                if (centre <= 1.0e-8f || centre <= left || centre < right) continue;
                const auto a = std::log (juce::jmax (left, 1.0e-12f));
                const auto b = std::log (centre), c = std::log (juce::jmax (right, 1.0e-12f));
                const auto denominator = a - 2 * b + c;
                const auto delta = std::abs (denominator) > 1.0e-9f
                    ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denominator) : 0.0f;
                const auto [first, last] = prepared.background[(size_t) bin];
                peaks.push_back ({ (float) ((bin + delta) * AnalysisAudio::rate / fftSize),
                    std::exp (b - 0.25f * (a - c) * delta),
                    (float) ((prefix[(size_t) last + 1] - prefix[(size_t) first]) / (last - first + 1)) });
            }
            std::stable_sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
            if (peaks.size() > 100) peaks.resize (100);
            for (const auto& peak : peaks)
            {
                if (peak.frequency > 2093 || peak.background <= 0) continue;
                const auto pitch = 69 + 12 * std::log2 ((double) peak.frequency / 440);
                const auto nearest = (int) std::floor (pitch + 0.5);
                const auto support = std::cos (juce::MathConstants<double>::pi * (pitch - nearest));
                chroma[(size_t) ((nearest % 12 + 12) % 12)] += peak.magnitude / std::sqrt ((double) peak.background) * support * support;
            }
        }
        double sum = 0;
        for (auto value : chroma) sum += value;
        if (energy / fftSize < 1.0e-9 || sum <= 0) frame.fill (0);
        else for (int pitch = 0; pitch < 12; ++pitch) frame[(size_t) pitch] = (float) (chroma[(size_t) pitch] / sum);
    }
    result.valid = true;
    return result;
}

KeyDetector::Estimate KeyDetector::estimate (const Features& features)
{
    if (! features.valid) return {};
    std::array<float, 12> chroma {};
    for (const auto& frame : features.frames)
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] += frame[(size_t) i];
    const auto maximum = *std::max_element (chroma.begin(), chroma.end());
    if (maximum > 0) for (auto& value : chroma) value /= maximum;
    return findBestKey (chroma);
}

float KeyDetector::pearson (const std::array<float, 12>& a, const std::array<float, 12>& b)
{
    float meanA = 0, meanB = 0;
    for (int i = 0; i < 12; ++i) { meanA += a[(size_t) i]; meanB += b[(size_t) i]; }
    meanA /= 12; meanB /= 12;
    float numerator = 0, denominatorA = 0, denominatorB = 0;
    for (int i = 0; i < 12; ++i)
    {
        const auto da = a[(size_t) i] - meanA, db = b[(size_t) i] - meanB;
        numerator += da * db;
        denominatorA += da * da; denominatorB += db * db;
    }
    const auto denominator = std::sqrt (denominatorA * denominatorB);
    return denominator > 0 ? numerator / denominator : 0;
}

KeyDetector::Estimate KeyDetector::findBestKey (const std::array<float, 12>& chroma)
{
    Estimate result;
    float best = -2, second = -2;
    int bestRoot = 0;
    bool bestMajor = true;
    for (int root = 0; root < 12; ++root)
        for (int mode = 0; mode < 2; ++mode)
        {
            std::array<float, 12> profile {};
            for (int i = 0; i < 12; ++i)
                profile[(size_t) i] = (mode == 0 ? majorProfile : minorProfile)[(size_t) ((i - root + 12) % 12)];
            const auto score = pearson (chroma, profile);
            if (score > best)
            { second = best; best = score; bestRoot = root; bestMajor = mode == 0; }
            else second = juce::jmax (second, score);
        }
    result.correlation = best;
    result.margin = best - second;
    const auto maximum = *std::max_element (chroma.begin(), chroma.end());
    const auto minimum = *std::min_element (chroma.begin(), chroma.end());
    float sum = 0;
    for (auto value : chroma) sum += value;
    if (maximum == 0 || maximum / sum > 0.6f || maximum - minimum < 0.12f
        || best < 0.6f || result.margin < 0.04) return result;
    result.confidence = juce::jlimit (0.0, 1.0, (double) best * juce::jmin (1.0, result.margin / 0.2));
    result.key = noteNames[bestRoot] + (bestMajor ? " major" : " minor");
    return result;
}
