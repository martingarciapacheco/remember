#include "MusicFeatures.h"
#include <algorithm>
#include <stdexcept>

namespace
{
void check (bool value, const char* message)
{
    if (! value) throw std::runtime_error (message);
}
constexpr int size = 8192, hop = 2048, peakLimit = 100;
struct Peak { float frequency, magnitude, background; };
}

juce::File exportMusicFeatures (const juce::AudioBuffer<float>& audio,
                                const KeyDetector::Features& key,
                                const BpmDetector::Features& tempo,
                                const juce::File& report)
{
    check (key.valid && tempo.valid && key.samples == audio.getNumSamples()
           && key.channels == audio.getNumChannels(), "Calibration input/features disagree");
    check (report.getParentDirectory().createDirectory().wasOk(), "Cannot create calibration output directory");
    const auto file = report.getSiblingFile (".features-" + juce::Uuid().toString().removeCharacters ("-") + ".bin");
    check (! file.exists(), "Calibration output already exists");
    struct RemoveOnFailure
    {
        juce::File file; bool completed = false;
        ~RemoveOnFailure() { if (! completed) file.deleteFile(); }
    } cleanup { file };
    juce::FileOutputStream output (file, 65536);
    check (output.openedOk(), "Cannot create calibration output");
    check (output.write ("REMFEAT1", 8)
           && output.writeInt (22050) && output.writeInt (key.samples) && output.writeInt (key.channels)
           && output.writeInt ((int) key.frames.size()) && output.writeInt ((int) tempo.frames.size()),
           "Cannot write calibration header");
    juce::dsp::FFT fft (13);
    std::vector<float> window ((size_t) size), spectrum ((size_t) (2 * size));
    std::vector<double> prefix ((size_t) size / 2 + 2);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f * (1 - std::cos (juce::MathConstants<float>::twoPi * i / (size - 1)));
    std::vector<Peak> peaks, framePeaks;
    for (size_t frame = 0; frame < key.frames.size(); ++frame)
    {
        for (auto value : key.frames[frame]) check (output.writeFloat (value), "Cannot write calibration chroma");
        double energy = 0;
        framePeaks.clear();
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer (channel, (int) frame * hop);
            for (int i = 0; i < size; ++i)
            {
                const auto value = std::isfinite (samples[i]) ? samples[i] : 0;
                energy += (double) value * value;
                spectrum[(size_t) i] = value * window[(size_t) i];
            }
            std::fill (spectrum.begin() + size, spectrum.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (spectrum.data());
            prefix[0] = 0;
            for (int bin = 0; bin <= size / 2; ++bin)
                prefix[(size_t) bin + 1] = prefix[(size_t) bin] + spectrum[(size_t) bin];
            peaks.clear();
            for (int bin = 21; bin <= 1857; ++bin) // 56.5–4998.4 Hz at the fixed analysis rate.
            {
                const auto left = spectrum[(size_t) bin - 1], centre = spectrum[(size_t) bin], right = spectrum[(size_t) bin + 1];
                if (centre <= 1.0e-8f || centre <= left || centre < right) continue;
                const auto a = std::log (juce::jmax (left, 1.0e-12f));
                const auto b = std::log (centre), c = std::log (juce::jmax (right, 1.0e-12f));
                const auto denominator = a - 2 * b + c;
                const auto delta = std::abs (denominator) > 1.0e-9f
                    ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denominator) : 0.0f;
                const auto first = juce::jmax (1, (int) std::floor (bin / std::exp2 (1.0 / 3)));
                const auto last = juce::jmin (size / 2, (int) std::ceil (bin * std::exp2 (1.0 / 3)));
                peaks.push_back ({ (float) ((bin + delta) * AnalysisAudio::rate / size),
                    std::exp (b - 0.25f * (a - c) * delta),
                    (float) ((prefix[(size_t) last + 1] - prefix[(size_t) first]) / (last - first + 1)) });
            }
            std::stable_sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
            if (peaks.size() > peakLimit) peaks.resize (peakLimit);
            framePeaks.insert (framePeaks.end(), peaks.begin(), peaks.end());
        }
        check (output.writeDouble (energy) && output.writeInt ((int) framePeaks.size()), "Cannot write calibration frame");
        for (const auto& peak : framePeaks)
            check (output.writeFloat (peak.frequency) && output.writeFloat (peak.magnitude) && output.writeFloat (peak.background),
                   "Cannot write calibration spectral peak");
    }
    for (const auto& frame : tempo.frames)
        check (output.writeDouble (frame.energy) && output.writeDouble (frame.magnitude) && output.writeDouble (frame.flux),
               "Cannot write calibration tempo feature");
    output.flush();
    check (output.getStatus().wasOk() && output.getPosition() == file.getSize(), "Incomplete calibration output");
    cleanup.completed = true;
    return file;
}

void testMusicFeatures (const juce::File& root)
{
    juce::AudioBuffer<float> audio (2, 22050 * 6); audio.clear();
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto value = (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * 440 * i / 22050));
        audio.setSample (0, i, value); audio.setSample (1, i, -value);
    }
    const auto key = KeyDetector::extract (audio, audio.getNumSamples());
    const auto tempo = BpmDetector::extract (audio, audio.getNumSamples());
    const auto report = root.getChildFile ("new-export/trace.json");
    check (! report.getParentDirectory().exists(), "Calibration directory fixture was already present");
    const auto file = exportMusicFeatures (audio, key, tempo, report);
    juce::FileInputStream input (file);
    char magic[8] {};
    check (input.openedOk() && input.read (magic, 8) == 8 && std::memcmp (magic, "REMFEAT1", 8) == 0,
           "Calibration output has an invalid identity");
    check (input.readInt() == 22050 && input.readInt() == audio.getNumSamples() && input.readInt() == 2
           && input.readInt() == (int) key.frames.size() && input.readInt() == (int) tempo.frames.size(),
           "Calibration header differs from actual features");
    for (const auto& frame : key.frames)
    {
        for (auto value : frame) check (input.readFloat() == value, "Export changed a baseline chroma value");
        check (input.readDouble() > 0, "Antiphase cancelled calibration energy");
        const auto count = input.readInt();
        check (count > 0 && count <= 2 * peakLimit, "Calibration peak count is invalid");
        bool foundPitch = false;
        for (int i = 0; i < count; ++i)
        {
            const auto frequency = input.readFloat(), magnitude = input.readFloat(), background = input.readFloat();
            check (std::isfinite (frequency) && std::isfinite (magnitude) && std::isfinite (background)
                   && magnitude > 0 && background > 0, "Calibration peak is invalid");
            foundPitch = foundPitch || std::abs (frequency - 440) < 1;
        }
        check (foundPitch, "Interpolated calibration spectrum lost the antiphase 440 Hz tone");
    }
    for (const auto& frame : tempo.frames)
        check (input.readDouble() == frame.energy && input.readDouble() == frame.magnitude && input.readDouble() == frame.flux,
               "Export changed a baseline tempo value");
    check (input.getStatus().wasOk() && input.getPosition() == input.getTotalLength(), "Calibration output has trailing/missing bytes");
}
