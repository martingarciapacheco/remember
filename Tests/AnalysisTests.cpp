#include "AnalysisTests.h"
#include "../Plugin/Source/KeyDetector.h"
#include "../Plugin/Source/BpmDetector.h"
#include "../Plugin/Source/TempoPeriodicity.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check (bool ok, const juce::String& reason)
{
    if (! ok) throw std::runtime_error (reason.toStdString());
}
juce::AudioBuffer<float> tonal (double rate, int root, bool minor, bool antiphase)
{
    // Independent labelled spectral fixtures: every pitch has two octaves;
    // tonic, third, fifth and the mode-specific scale carry more energy.
    constexpr float major[] { 1.0f, 0.06f, 0.30f, 0.06f, 0.65f, 0.35f, 0.06f, 0.8f, 0.06f, 0.3f, 0.06f, 0.25f };
    constexpr float minorScale[] { 1.0f, 0.06f, 0.3f, 0.65f, 0.06f, 0.35f, 0.06f, 0.8f, 0.35f, 0.06f, 0.3f, 0.25f };
    const auto* weights = minor ? minorScale : major;
    juce::AudioBuffer<float> audio (2, (int) (rate * 3)); audio.clear();
    for (int pitch = 0; pitch < 12; ++pitch)
        for (int octave = 0; octave < 2; ++octave)
        {
            const auto frequency = 440.0 * std::pow (2.0, (60 + pitch + octave * 12 - 69) / 12.0);
            const auto gain = 0.04 * weights[(pitch - root + 12) % 12] * (octave == 0 ? 1 : 0.5);
            for (int i = 0; i < audio.getNumSamples(); ++i)
                audio.addSample (0, i, (float) (gain * std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate)));
        }
    audio.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
    if (antiphase) audio.applyGain (1, 0, audio.getNumSamples(), -1);
    return audio;
}
juce::AudioBuffer<float> clicks (double rate, double bpm, bool antiphase)
{
    juce::AudioBuffer<float> audio (2, (int) (rate * 12)); audio.clear();
    for (double beat = 0.2; beat < 12; beat += 60 / bpm)
        for (int i = 0, length = (int) (rate * 0.02); i < length; ++i)
        {
            const auto position = (int) std::round (beat * rate) + i;
            if (position >= audio.getNumSamples()) break;
            const auto value = (float) (0.8 * std::exp (-i / (rate * 0.004))
                * std::cos (juce::MathConstants<double>::twoPi * 1500 * i / rate));
            audio.setSample (0, position, value);
            audio.setSample (1, position, antiphase ? -value : value);
        }
    return audio;
}
}
void testAnalysis()
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    {
        // The same order is used for forward and inverse transforms in
        // different segments. Reusing a plan must preserve every coefficient.
        rememberTempo::Plans plans;
        std::vector<double> onsets (10328);
        for (size_t i = 0; i < onsets.size(); ++i)
            onsets[i] = std::pow (0.5 + 0.5 * std::sin (i * 0.147), 6) + 0.1 * std::pow (0.5 + 0.5 * std::cos (i * 0.113), 4);
        const std::vector<double> first (onsets.begin(), onsets.begin() + 5164), last (onsets.begin() + 5164, onsets.end());
        const std::array<const std::vector<double>*, 3> segments { &onsets, &first, &last };
        for (const auto* values : segments)
        {
            const auto full = rememberTempo::analyse (*values, {});
            const auto reused = rememberTempo::analyse (*values, {}, &plans);
            check (full.valid && reused.valid && full.correlation == reused.correlation && full.fourier == reused.fourier
                && full.regular == reused.regular, "Shared FFT plans changed a periodicity coefficient");
        }
        check (std::count_if (plans.transforms.begin(), plans.transforms.end(), [] (const auto& value) { return value != nullptr; }) == 4,
               "The global and local curves rebuilt redundant FFT plans");
    }
    const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int root = 0; root < 12; ++root)
        for (bool minor : { false, true })
        {
            const auto expected = names[root] + (minor ? " minor" : " major");
            const auto result = KeyDetector::analyse (tonal (22050, root, minor, true), 22050);
            check (result.key == expected, "Key corpus: expected " + expected + ", got " + result.key
                + " correlation=" + juce::String (result.correlation) + " margin=" + juce::String (result.margin));
            check (result.confidence > 0 && result.confidence <= 1, "Key confidence is invalid");
        }
    for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        check (KeyDetector::analyse (tonal (rate, 0, false, true), rate).key == "C major", "Key changed at " + juce::String (rate));
        for (auto bpm : { 60.0, 120.0, 200.0 })
        {
            const auto result = BpmDetector::analyse (clicks (rate, bpm, true), rate);
            check (std::abs (result.bpm - bpm) <= 1, "Tempo corpus: expected " + juce::String (bpm)
                + " at " + juce::String (rate) + ", got " + juce::String (result.bpm)
                + " confidence=" + juce::String (result.confidence));
            check (result.confidence > 0 && result.confidence <= 1, "Tempo confidence is invalid");
        }
    }
    juce::AudioBuffer<float> silence (2, 22050 * 8); silence.clear();
    for (auto bpm : { 40.0, 70.0, 180.0, 240.0 })
    {
        const auto result = BpmDetector::analyse (clicks (22050, bpm, false), 22050);
        check (std::abs (result.bpm - bpm) <= 1, "Tempo boundary " + juce::String (bpm) + ": got " + juce::String (result.bpm)
            + ", confidence=" + juce::String (result.confidence) + ", first=" + juce::String (result.startBpm) + ", last=" + juce::String (result.endBpm));
    }
    auto varying = clicks (22050, 120, true);
    auto faster = clicks (22050, 150, true);
    const auto half = varying.getNumSamples() / 2;
    for (int ch = 0; ch < 2; ++ch) varying.copyFrom (ch, half, faster, ch, half, half);
    const auto variation = BpmDetector::analyse (varying, 22050);
    check (variation.possibleVariation && variation.bpm == 0
        && std::abs (variation.startBpm - variation.endBpm) > 20, "A tempo change was labelled as one confident constant BPM");
    auto octaveChange = clicks (22050, 120, true);
    const auto doubled = clicks (22050, 240, true);
    for (int ch = 0; ch < 2; ++ch) octaveChange.copyFrom (ch, half, doubled, ch, half, half);
    const auto octave = BpmDetector::analyse (octaveChange, 22050);
    check (octave.possibleVariation && octave.bpm == 0
        && std::abs (octave.startBpm - 120) <= 1 && std::abs (octave.endBpm - 240) <= 1,
        "A real 120-to-240 BPM change was mistaken for stable octave ambiguity");
    check (KeyDetector::detectKey (silence, 22050) == "Unknown", "Silence has an invented key");
    check (BpmDetector::detectBpm (silence, 22050) == 0, "Silence has an invented tempo");
    auto steady = silence;
    for (int i = 0; i < steady.getNumSamples(); ++i)
        steady.setSample (0, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * i / 22050.0f));
    steady.copyFrom (1, 0, steady, 0, 0, steady.getNumSamples());
    check (BpmDetector::detectBpm (steady, 22050) == 0, "A steady tone has an invented rhythm");
    check (KeyDetector::detectKey (steady, 22050) == "Unknown", "One pitch determines an invented musical key");
    check (BpmDetector::detectBpm (steady, std::numeric_limits<double>::quiet_NaN()) == 0,
           "Invalid sample rate was analysed");
    juce::Random random (123456);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < silence.getNumSamples(); ++i) silence.setSample (ch, i, 0.2f * (random.nextFloat() - 0.5f));
    check (KeyDetector::detectKey (silence, 22050) == "Unknown", "Noise has an invented key");
    check (BpmDetector::detectBpm (silence, 22050) == 0, "Noise has an invented tempo");
    int visits = 0;
    check (KeyDetector::analyse (silence, 22050, [&] { return ++visits > 2; }).key == "Unknown" && visits <= 4,
           "Key analysis ignored cancellation");
    visits = 0;
    check (BpmDetector::analyse (silence, 22050, [&] { return ++visits > 2; }).bpm == 0 && visits <= 4,
           "Tempo analysis ignored cancellation");
    std::cout << "PASS: 24 labelled keys, 15 labelled tempos at 44.1/48/88.2/96/192 kHz, boundaries 40/70/180/240, tempo changes 120-to-150/240, antiphase, silence, steady tone, noise, cancellation; "
              << juce::Time::getMillisecondCounterHiRes() - start << " ms\n";
}
