#include "ResamplingTests.h"
#include "../Plugin/Source/AnalysisAudio.h"
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double rates[] { 44100, 48000, 88200, 96000, 192000 };
void check (bool ok, const juce::String& reason)
{
    if (! ok) throw std::runtime_error (reason.toStdString());
}
void check (bool ok, const char* reason)
{
    if (! ok) throw std::runtime_error (reason);
}
juce::AudioBuffer<float> tone (double rate, double frequency)
{
    juce::AudioBuffer<float> audio (2, (int) std::round (rate));
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto value = (float) (0.8 * std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate));
        audio.setSample (0, i, value); audio.setSample (1, i, -value);
    }
    return audio;
}
double amplitude (const juce::AudioBuffer<float>& audio, double rate, double frequency)
{
    double real = 0, imaginary = 0;
    const int first = (int) (rate * 0.1), last = (int) (rate * 0.9);
    for (int i = first; i < last; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * frequency * i / rate;
        real += audio.getSample (0, i) * std::cos (phase); imaginary += audio.getSample (0, i) * std::sin (phase);
    }
    return 2 * std::hypot (real, imaginary) / (last - first);
}
void write (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
        .withNumChannels (audio.getNumChannels()).withBitsPerSample (32)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    check (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "Cannot write resampling fixture");
}
juce::AudioBuffer<float> read (const juce::File& file, double rate, bool varied)
{
    AnalysisAudio::FileStream stream (file, rate);
    check (stream.valid(), "Cannot open resampling fixture");
    juce::AudioBuffer<float> result (2, stream.length()), block (2, 4096);
    int sequence = 0;
    const int sizes[] { 1, 7, 32, 127, 512, 2048, 4096 };
    for (int offset = 0; offset < result.getNumSamples();)
    {
        const auto count = juce::jmin (varied ? sizes[sequence++ % 7] : 4096, result.getNumSamples() - offset);
        check (stream.read (block, count), "Resampling read failed");
        for (int ch = 0; ch < 2; ++ch) result.copyFrom (ch, offset, block, ch, 0, count);
        offset += count;
    }
    check (stream.read (block, 4096) && block.getMagnitude (0, 4096) == 0, "Resampler did not zero-pad after EOF");
    return result;
}
void spectral()
{
    double worstAlias = -200, worstPassband = 0;
    const auto began = juce::Time::getMillisecondCounterHiRes();
    for (auto sourceRate : rates)
        for (auto frequency : { 1000.0, 8820.0, 13230.0, 19000.0, 30000.0 })
        {
            if (frequency >= sourceRate / 2) continue;
            const auto input = tone (sourceRate, frequency);
            const auto output = AnalysisAudio::normalise (input, sourceRate);
            check (output.getNumSamples() == 22050, "Analysis resampling changed duration");
            const auto alias = std::abs (std::remainder (frequency, AnalysisAudio::rate));
            const auto db = 20 * std::log10 (juce::jmax (1.0e-12, amplitude (output, AnalysisAudio::rate, alias) / 0.8));
            if (frequency <= AnalysisAudio::rate * 0.4)
            {
                worstPassband = juce::jmax (worstPassband, std::abs (db));
                check (std::abs (db) < 0.02, "Resampling passband error: " + juce::String (db) + " dB");
                for (int i = 2205; i < 19845; ++i)
                {
                    const auto expected = 0.8 * std::sin (juce::MathConstants<double>::twoPi * frequency * i / AnalysisAudio::rate);
                    check (std::abs (output.getSample (0, i) - expected) < 1.0e-4, "Resampling shifted the source timeline");
                }
            }
            else
            {
                worstAlias = juce::jmax (worstAlias, db);
                check (db < -75, "Resampling alias rejection: " + juce::String (db) + " dB");
            }
            for (int i = 0; i < output.getNumSamples(); ++i)
                check (output.getSample (0, i) == -output.getSample (1, i), "Resampling cancelled or mixed antiphase channels");
        }
    // Exercise interpolated phase tables for both a fractional audio rate
    // and an integer rate whose rational denominator exceeds the table budget.
    for (auto sourceRate : { 48000.25, 47999.0 })
    {
        const auto output = AnalysisAudio::normalise (tone (sourceRate, 8820), sourceRate);
        check (output.getNumSamples() >= 22049, "Fallback resampler lost duration");
        for (int i = 2205; i < 19845; ++i)
            check (std::abs (output.getSample (0, i) - 0.8 * std::sin (juce::MathConstants<double>::twoPi * 8820 * i / 22050)) < 1.0e-4,
                   "Interpolated phases introduced an audible time/level error");
    }
    std::cout << "SRC worstPassbandDb=" << worstPassband << " worstAliasDb=" << worstAlias
              << " matrixMs=" << juce::Time::getMillisecondCounterHiRes() - began << std::endl;
}
}
void testResampling (const juce::File& root)
{
    // Mono files now filter one channel and duplicate the result. Compare
    // against the independent two-channel memory path, including bypass and
    // interpolated phases, with DC, headroom and endpoint transients present.
    for (auto sourceRate : { 22050.0, 48000.0, 47999.0, 192000.0 })
    {
        juce::AudioBuffer<float> mono (1, (int) (sourceRate / 8)), stereo (2, mono.getNumSamples());
        for (int i = 0; i < mono.getNumSamples(); ++i)
        {
            const auto value = (float) (0.7 + 0.8 * std::sin (juce::MathConstants<double>::twoPi * 8820 * i / sourceRate));
            mono.setSample (0, i, value);
        }
        mono.addSample (0, 0, 1.5f); mono.addSample (0, mono.getNumSamples() - 1, -1.5f);
        for (int ch = 0; ch < 2; ++ch) stereo.copyFrom (ch, 0, mono, 0, 0, mono.getNumSamples());
        const auto file = root.getChildFile ("mono-parity-" + juce::String ((int) sourceRate) + ".wav");
        write (file, mono, sourceRate);
        const auto output = read (file, AnalysisAudio::rate, true);
        const auto reference = AnalysisAudio::normalise (stereo, sourceRate);
        check (output.getNumSamples() == reference.getNumSamples(), "Mono optimisation changed duration");
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < output.getNumSamples(); ++i)
                check (output.getSample (ch, i) == reference.getSample (ch, i), "Mono optimisation changed a PCM sample");
    }
    spectral();
    double worstError = 0;
    const auto began = juce::Time::getMillisecondCounterHiRes();
    for (auto sourceRate : rates)
    {
        auto audio = tone (sourceRate, 1000);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            audio.addSample (0, i, 0.7f); audio.addSample (1, i, -0.7f);
        }
        const auto file = root.getChildFile ("src-" + juce::String ((int) sourceRate) + ".wav"); write (file, audio, sourceRate);
        const auto memory = AnalysisAudio::normalise (audio, sourceRate);
        const auto analysis = AnalysisAudio::fromFile (file, 2);
        check (analysis.getNumSamples() == memory.getNumSamples(), "File/memory conversion duration differs");
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < memory.getNumSamples(); ++i)
                check (analysis.getSample (ch, i) == memory.getSample (ch, i), "File and memory resampling disagree");
        for (auto targetRate : rates)
        {
            const auto output = read (file, targetRate, false), varied = read (file, targetRate, true);
            check (output.getNumSamples() == (int) targetRate, "Rate-pair duration changed");
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < output.getNumSamples(); ++i)
                {
                    check (output.getSample (ch, i) == varied.getSample (ch, i), "Block sizes changed the converter's timeline");
                    if (sourceRate == targetRate)
                        check (output.getSample (ch, i) == audio.getSample (ch, i), "Same-rate float PCM was filtered or clipped");
                    if (i >= targetRate * 0.01 && i < targetRate * 0.99)
                    {
                        const auto expected = (0.7 + 0.8 * std::sin (juce::MathConstants<double>::twoPi * 1000 * i / targetRate)) * (ch == 0 ? 1 : -1);
                        const auto error = std::abs (output.getSample (ch, i) - expected);
                        worstError = juce::jmax (worstError, error);
                        if (! (error < 1.0e-4))
                            check (false, "Rate-pair amplitude/alignment error: " + juce::String (error));
                    }
                }
        }
        juce::AudioBuffer<float> impulse (1, (int) sourceRate); impulse.clear(); impulse.setSample (0, (int) sourceRate / 2, 1.5f);
        const auto impulseFile = root.getChildFile ("impulse-" + juce::String ((int) sourceRate) + ".wav"); write (impulseFile, impulse, sourceRate);
        for (auto targetRate : rates)
        {
            const auto output = read (impulseFile, targetRate, true);
            int peakPosition = 0; float peak = 0;
            for (int i = 0; i < output.getNumSamples(); ++i)
            {
                check (output.getSample (0, i) == output.getSample (1, i), "Mono file was not duplicated into the reader's stereo block");
                if (std::abs (output.getSample (0, i)) > peak) { peak = std::abs (output.getSample (0, i)); peakPosition = i; }
            }
            check (peakPosition == (int) targetRate / 2, "Impulse moved by the converter's filter delay");
        }
    }
    auto input = tone (48000, 1000);
    {
        // 60,000 frames at 48 kHz last 1.25 s: at 22.05 kHz this is
        // exactly 27,562.5 frames, rounded to 27,563 by the duration contract.
        auto fractional = tone (48000, 1000); fractional.setSize (2, 60000, true, true);
        const auto file = root.getChildFile ("half-output-frame.wav"); write (file, fractional, 48000);
        const auto fromFile = read (file, 22050, true);
        const auto fromMemory = AnalysisAudio::normalise (fractional, 48000);
        check (fromFile.getNumSamples() == 27563 && fromMemory.getNumSamples() == 27563,
               "Half-frame duration was rounded differently in the reader and analysis cache");
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < fromMemory.getNumSamples(); ++i)
                check (fromFile.getSample (ch, i) == fromMemory.getSample (ch, i), "Half-frame file/memory timelines differ");
    }
    check (AnalysisAudio::normalise (input, std::numeric_limits<double>::quiet_NaN()).getNumSamples() == 0
           && AnalysisAudio::normalise (input, 1.0e12).getNumSamples() == 0, "Invalid rates allocated an unbounded converter");
    int visits = 0;
    check (AnalysisAudio::normalise (input, 48000, [&] { return ++visits > 3; }).getNumSamples() == 0 && visits == 4,
           "Coefficient preparation ignored cancellation");
    bool stop = false;
    AnalysisAudio::FileStream stream (root.getChildFile ("src-192000.wav"), 22050, [&] { return stop; });
    juce::AudioBuffer<float> block (2, 4096);
    check (stream.valid() && stream.read (block, 4096), "Cancellation reader could not start");
    stop = true; check (! stream.read (block, 4096), "Streaming conversion ignored cancellation");
    std::cout << "PASS: 25 file rate pairs, 25 centred mono impulses, exact block invariance, float headroom, file/memory equivalence, rational/interpolated phases, EOF and cancellation; worstError="
              << worstError << " matrixMs=" << juce::Time::getMillisecondCounterHiRes() - began << '\n';
}
