#include "BpmDetector.h"
#include "TempoPeriodicity.h"

namespace
{
constexpr int fftOrder = 11, fftSize = 1 << fftOrder, hop = 256;
const std::array<float, fftSize>& window()
{
    static const auto value = []
    {
        std::array<float, fftSize> result {};
        for (int i = 0; i < fftSize; ++i)
            result[(size_t) i] = 0.5f * (1 - std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1)));
        return result;
    }();
    return value;
}
}

double BpmDetector::detectBpm (const juce::AudioBuffer<float>& audio, double rate)
{
    return analyse (audio, rate).bpm;
}

BpmDetector::Estimate BpmDetector::analyse (const juce::AudioBuffer<float>& audio, double rate,
                                           const AnalysisAudio::Cancellation& cancelled)
{
    Estimate result;
    if (! std::isfinite (rate) || rate <= 0 || audio.getNumChannels() == 0 || audio.getNumSamples() / rate < 5
        || (cancelled && cancelled())) return result;
    juce::AudioBuffer<float> normalised;
    const auto* input = &audio;
    if (rate != AnalysisAudio::rate)
    {
        normalised = AnalysisAudio::normalise (audio, rate, cancelled);
        input = &normalised;
    }
    return estimate (extract (*input, input->getNumSamples(), nullptr, 0, cancelled), cancelled);
}

int BpmDetector::requiredSamples (int totalSamples, int unchangedFrom, bool reuse)
{
    const auto limit = juce::jlimit (0, (int) (120 * AnalysisAudio::rate), totalSamples);
    const auto frames = limit < 5 * AnalysisAudio::rate ? 0 : 1 + (limit - fftSize) / hop;
    const auto boundary = juce::jlimit (0, limit, unchangedFrom);
    const auto computed = reuse ? juce::jlimit (0, frames, (boundary + hop - 1) / hop + 1) : frames;
    return computed == 0 ? 0 : (computed - 1) * hop + fftSize;
}

BpmDetector::Features BpmDetector::extract (const juce::AudioBuffer<float>& audio, int totalSamples,
                                            const Features* previous, int unchangedFrom,
                                            const AnalysisAudio::Cancellation& cancelled)
{
    Features result;
    result.samples = juce::jlimit (0, (int) (120 * AnalysisAudio::rate), totalSamples);
    result.channels = juce::jmin (2, audio.getNumChannels());
    const auto frames = result.samples < 5 * AnalysisAudio::rate ? 0 : 1 + (result.samples - fftSize) / hop;
    const auto reuse = previous && previous->valid && previous->samples == result.samples
        && previous->channels == result.channels && (int) previous->frames.size() == frames;
    if (result.channels == 0 || audio.getNumSamples() < requiredSamples (result.samples, unchangedFrom, reuse)
        || (cancelled && cancelled())) return result;
    result.frames.resize ((size_t) frames);
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> scratch (2 * fftSize), previousSpectrum ((size_t) (result.channels * fftSize / 2));
    for (int index = 0; index < frames; ++index)
    {
        if (cancelled && cancelled()) return {};
        const auto start = index * hop;
        auto& frame = result.frames[(size_t) index];
        if (reuse && start - hop >= unchangedFrom)
        {
            frame = previous->frames[(size_t) index]; ++result.reused; continue;
        }
        ++result.computed;
        for (int ch = 0; ch < result.channels; ++ch)
        {
            bool nonzero = false;
            const auto* samples = audio.getReadPointer (ch, start);
            for (int i = 0; i < fftSize; ++i)
            {
                const auto value = std::isfinite (samples[i]) ? samples[i] : 0;
                nonzero = nonzero || value != 0;
                frame.energy += value * value;
                scratch[(size_t) i] = value * window()[(size_t) i];
            }
            std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);
            auto* last = previousSpectrum.data() + ch * fftSize / 2;
            if (! nonzero)
            {
                // A zero window has no positive flux, even after a nonzero
                // spectrum. Reset its history without visiting every bin.
                std::fill (last, last + fftSize / 2, 0.0f);
                continue;
            }
            fft.performFrequencyOnlyForwardTransform (scratch.data());
            for (int bin = 3; bin < fftSize / 2; ++bin)
            {
                frame.flux += juce::jmax (0.0f, scratch[(size_t) bin] - last[bin]);
                frame.magnitude += scratch[(size_t) bin];
                last[bin] = scratch[(size_t) bin];
            }
        }
        if (index == 0) frame.flux = 0;
    }
    result.valid = true;
    return result;
}

BpmDetector::Estimate BpmDetector::estimate (const Features& features, const AnalysisAudio::Cancellation& cancelled)
{
    Estimate result;
    const auto frames = (int) features.frames.size();
    if (! features.valid || features.samples < 5 * AnalysisAudio::rate || features.samples > 120 * AnalysisAudio::rate
        || features.channels < 1 || features.channels > 2 || frames != 1 + (features.samples - fftSize) / hop
        || (cancelled && cancelled())) return result;
    std::vector<double> onset ((size_t) frames), prefix ((size_t) frames + 1);
    double energy = 0, magnitude = 0, flux = 0;
    for (int i = 0; i < frames; ++i)
    {
        if (cancelled && cancelled()) return {};
        const auto& frame = features.frames[(size_t) i];
        if (! std::isfinite (frame.energy) || ! std::isfinite (frame.magnitude) || ! std::isfinite (frame.flux)
            || frame.energy < 0 || frame.magnitude < 0 || frame.flux < 0) return result;
        energy += frame.energy; magnitude += frame.magnitude; flux += frame.flux;
        // Version 5 parameters frozen on development: sqrt flux, normalized
        // lag/Fourier evidence, 40-240 BPM, local variation and regular pulses.
        onset[(size_t) i] = std::sqrt (frame.flux);
        prefix[(size_t) i + 1] = prefix[(size_t) i] + onset[(size_t) i];
    }
    if (energy / (frames * fftSize * features.channels) < 1.0e-9 || magnitude <= 0 || flux / magnitude < 0.002) return result;
    const auto radius = (int) (rememberTempo::fps * 0.25);
    for (int i = 0; i < frames; ++i)
    {
        const auto first = juce::jmax (0, i - radius), last = juce::jmin (frames, i + radius + 1);
        onset[(size_t) i] = juce::jmax (0.0, onset[(size_t) i] - (prefix[(size_t) last] - prefix[(size_t) first]) / (last - first));
    }
    rememberTempo::Plans plans;
    const auto global = rememberTempo::analyse (onset, cancelled, &plans);
    if (! global.valid) return {};
    const auto chosen = rememberTempo::select (global);
    if (chosen.index < 0) return result;
    const std::vector<double> first (onset.begin(), onset.begin() + frames / 2), last (onset.begin() + frames / 2, onset.end());
    const auto start = rememberTempo::analyse (first, cancelled, &plans);
    const auto end = rememberTempo::analyse (last, cancelled, &plans);
    if (! start.valid || ! end.valid || (cancelled && cancelled())) return {};
    result.startBpm = rememberTempo::select (start).bpm;
    result.endBpm = rememberTempo::select (end).bpm;
    result.possibleVariation = start.regular > 0 && end.regular > 0
        && std::abs (start.regular - end.regular) / juce::jmax (start.regular, end.regular) > 0.05;
    for (const auto* local : { &start, &end })
    {
        const auto maximum = *std::max_element (local->correlation.begin(), local->correlation.end());
        if (maximum >= 0.65 && local->correlation[(size_t) chosen.index] < juce::jmax (0.25, maximum * 0.45))
            result.possibleVariation = true;
    }
    if (result.possibleVariation) return result;
    result.bpm = chosen.bpm;
    result.confidence = chosen.score; // Evidence score, not a calibrated probability.
    result.alternative = chosen.alternative;
    return result;
}
