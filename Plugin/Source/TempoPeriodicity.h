#pragma once
#include <JuceHeader.h>
#include "AnalysisAudio.h"

// Internal worker-only periodicity math. Compact cached onset frames retain
// their existing representation; FFT scratch is released after each estimate.
namespace rememberTempo
{
constexpr double fps = AnalysisAudio::rate / 256;
constexpr int count = 2001;
inline double tempo (int index) { return (400 + index) / 10.0; }
inline int orderFor (int minimum)
{
    int order = 0;
    while ((1 << order) < minimum) ++order;
    return order;
}
inline double interpolate (const std::vector<double>& values, double position)
{
    const auto left = juce::jlimit (0, (int) values.size() - 1, (int) std::floor (position));
    const auto right = juce::jmin (left + 1, (int) values.size() - 1);
    const auto weight = juce::jlimit (0.0, 1.0, position - left);
    return values[(size_t) left] * (1 - weight) + values[(size_t) right] * weight;
}
inline double median (std::vector<double> values)
{
    std::sort (values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) * 0.5;
}
inline double regularInterval (const std::vector<double>& values, const AnalysisAudio::Cancellation& cancelled)
{
    const auto peak = *std::max_element (values.begin(), values.end());
    if (peak <= 1.0e-6) return 0;
    std::vector<double> positions;
    for (int i = 1; i + 1 < (int) values.size(); ++i)
    {
        if (cancelled && cancelled()) return 0;
        const auto a = values[(size_t) i - 1], b = values[(size_t) i], c = values[(size_t) i + 1];
        if (b < peak * 0.4 || b < a || b <= c) continue;
        const auto denominator = a - 2 * b + c;
        const auto position = i + (std::abs (denominator) > 1.0e-9
            ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denominator) : 0);
        if (positions.empty() || position - positions.back() >= std::floor (fps * 60 / 240) * 0.7)
            positions.push_back (position);
    }
    if (positions.size() < 5) return 0;
    std::vector<double> intervals, deviations;
    for (size_t i = 1; i < positions.size(); ++i) intervals.push_back (positions[i] - positions[i - 1]);
    const auto period = median (intervals);
    if (period <= 0) return 0;
    for (auto interval : intervals) deviations.push_back (std::abs (interval - period) / period);
    const auto bpm = fps * 60 / period;
    return median (deviations) < 0.06 && *std::max_element (deviations.begin(), deviations.end()) < 0.2
        && bpm >= 39 && bpm <= 241 ? bpm : 0;
}
struct Curves
{
    std::vector<double> correlation, fourier;
    double regular = 0;
    bool valid = false;
};
// Per-estimate plans are shared between the global curve and both halves.
// No transform is shared across threads or retained after the estimate.
struct Plans
{
    std::array<std::unique_ptr<juce::dsp::FFT>, 18> transforms;
    juce::dsp::FFT& get (int order)
    {
        jassert (order > 0 && order < (int) transforms.size());
        auto& transform = transforms[(size_t) order];
        if (! transform) transform = std::make_unique<juce::dsp::FFT> (order);
        return *transform;
    }
};
inline Curves analyse (const std::vector<double>& input, const AnalysisAudio::Cancellation& cancelled, Plans* sharedPlans = nullptr)
{
    Curves result;
    if (input.size() < 128 || input.size() > 10328 || (cancelled && cancelled())) return result;
    result.correlation.resize (count); result.fourier.resize (count);
    const auto length = (int) input.size();
    const auto maximum = *std::max_element (input.begin(), input.end());
    if (maximum <= 1.0e-12) { result.valid = true; return result; }
    double mean = 0;
    for (auto value : input) mean += value / maximum;
    mean /= length;
    std::vector<double> energy ((size_t) length + 1);
    const auto order = orderFor (2 * length), size = 1 << order;
    std::vector<float> scratch ((size_t) size * 2);
    for (int i = 0; i < length; ++i)
    {
        if (cancelled && cancelled()) return {};
        const auto value = input[(size_t) i] / maximum - mean;
        scratch[(size_t) i] = (float) value;
        energy[(size_t) i + 1] = energy[(size_t) i] + value * value;
    }
    Plans localPlans;
    auto& plans = sharedPlans ? *sharedPlans : localPlans;
    auto& forward = plans.get (order);
    forward.performRealOnlyForwardTransform (scratch.data(), true);
    if (cancelled && cancelled()) return {};
    const auto interpolatedSize = size * 4;
    std::vector<float> correlation ((size_t) interpolatedSize * 2);
    for (int bin = 0; bin <= size / 2; ++bin)
    {
        const auto re = (double) scratch[(size_t) bin * 2], im = (double) scratch[(size_t) bin * 2 + 1];
        correlation[(size_t) bin * 2] = (float) ((re * re + im * im) * (bin == size / 2 ? 0.5 : 1));
    }
    auto& inverse = plans.get (order + 2);
    inverse.performRealOnlyInverseTransform (correlation.data());
    if (cancelled && cancelled()) return {};
    std::vector<double> normalized ((size_t) ((int) std::ceil (fps * 60 / 40) * 4 + 5));
    for (int index = 0; index < (int) normalized.size(); ++index)
    {
        const auto lag = index / 4.0;
        const auto a = interpolate (energy, length - lag), b = energy.back() - interpolate (energy, lag);
        const auto denominator = std::sqrt (juce::jmax (0.0, a * b));
        normalized[(size_t) index] = denominator > 1.0e-12
            ? juce::jlimit (0.0, 1.0, 4 * correlation[(size_t) index] / denominator) : 0;
    }
    for (int index = 0; index < count; ++index)
    {
        result.correlation[(size_t) index] = interpolate (normalized, fps * 60 / tempo (index) * 4);
    }
    const auto fourierOrder = orderFor (4 * length), fourierSize = 1 << fourierOrder;
    std::vector<float> spectrum ((size_t) fourierSize * 2);
    double total = 0;
    for (int i = 0; i < length; ++i)
    {
        if (cancelled && cancelled()) return {};
        const auto value = input[(size_t) i] / maximum
            * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1)));
        spectrum[(size_t) i] = (float) value; total += value;
    }
    auto& fourier = plans.get (fourierOrder);
    fourier.performFrequencyOnlyForwardTransform (spectrum.data(), true);
    if (cancelled && cancelled()) return {};
    for (int index = 0; index < count; ++index)
    {
        const auto offset = tempo (index) / 60 * fourierSize / fps;
        const auto left = (int) std::floor (offset), right = left + 1;
        const auto amplitude = spectrum[(size_t) left] * (right - offset) + spectrum[(size_t) right] * (offset - left);
        result.fourier[(size_t) index] = juce::jlimit (0.0, 1.0, 2 * amplitude / juce::jmax (total, 1.0e-12));
    }
    result.regular = regularInterval (input, cancelled);
    if (cancelled && cancelled()) return {};
    result.valid = true;
    return result;
}
struct Selection { int index = -1; double bpm = 0, score = 0, alternative = 0; };
inline Selection select (const Curves& curves)
{
    Selection result;
    if (! curves.valid) return result;
    std::vector<double> scores ((size_t) count);
    std::vector<int> candidates;
    for (int i = 0; i < count; ++i)
        scores[(size_t) i] = curves.correlation[(size_t) i] * curves.fourier[(size_t) i];
    double maximum = 0;
    for (int i = 0; i < count; ++i)
        if (curves.correlation[(size_t) i] >= 0.3 && scores[(size_t) i] >= 0.02
            && (i == 0 || scores[(size_t) i] >= scores[(size_t) i - 1])
            && (i == count - 1 || scores[(size_t) i] >= scores[(size_t) i + 1]))
        { candidates.push_back (i); maximum = juce::jmax (maximum, scores[(size_t) i]); }
    // Fastest among nearly equal, independently supported candidate maxima.
    // This is a relative evidence threshold, without an octave/range prior.
    for (auto index : candidates)
        if (scores[(size_t) index] >= maximum * 0.85) result.index = index;
    if (result.index < 0) return result;
    result.bpm = tempo (result.index);
    if (curves.regular > 0 && interpolate (curves.correlation, (curves.regular - 40) * 10) >= 0.3)
    {
        result.bpm = juce::jlimit (40.0, 240.0, curves.regular);
        result.index = juce::jlimit (0, count - 1, (int) std::round ((result.bpm - 40) * 10));
    }
    result.score = juce::jlimit (0.0, 1.0, scores[(size_t) result.index]);
    double alternativeScore = 0;
    for (auto index : candidates)
    {
        const auto bpm = tempo (index), ratio = bpm / result.bpm;
        bool related = false;
        for (auto multiple : { 0.5, 1.0, 2.0 }) related = related || std::abs (ratio - multiple) <= multiple * 0.04;
        if (! related && scores[(size_t) index] > alternativeScore)
        { alternativeScore = scores[(size_t) index]; result.alternative = bpm; }
    }
    result.bpm = std::round (result.bpm * 10) / 10;
    return result;
}
}
