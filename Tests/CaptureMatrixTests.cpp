#include "../Shared/PreviewAudio.h"
#include "CaptureMatrixTests.h"
#include "TransportTests.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Shared/LibraryStore.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check (bool ok, const juce::String& reason)
{
    if (! ok) throw std::runtime_error (reason.toStdString());
}
template <typename Predicate> void waitFor (Predicate predicate, const char* reason)
{
    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (! predicate())
    {
        check (juce::Time::getMillisecondCounter() < end, reason);
        juce::Thread::sleep (2);
    }
}
struct Head : juce::AudioPlayHead
{
    bool playing = true;
    juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing); info.setTimeInSamples (sample); info.setBpm (137.5);
        return info;
    }
};
juce::File savedEntry (const juce::File& library)
{
    for (const auto& folder : library.findChildFiles (juce::File::findDirectories, false))
        if (! folder.getFileName().startsWithChar ('.')) return folder;
    return {};
}
}
void testCaptureMatrix (const juce::File& root)
{
    const auto initialMemory = RecordingEngine::getProcessCaptureBytes();
    std::vector<double> callbackTimes;
    auto matrix = root.getChildFile ("capture-matrix"); check (matrix.createDirectory().wasOk(), "Matrix folder failed");
    int cases = 0;
    for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (auto blockSize : { 32, 64, 128, 512, 2048 })
        {
            const auto label = juce::String ((int) rate) + "-" + juce::String (blockSize);
            auto project = matrix.getChildFile (label + ".rpp");
            check (project.replaceWithText ("fixture " + label), "Matrix project creation failed");
            auto library = matrix.getChildFile (label); check (library.createDirectory().wasOk(), "Matrix library failed");
            RecordingEngine engine ([project] { return HostProject { true, project.getFileNameWithoutExtension(), project.getFullPathName(), "REAPER" }; });
            SettingsManager::Snapshot settings;
            settings.seconds = 120; settings.mono = (++cases % 2) == 0;
            settings.previews = library; settings.fingerprint = "matrix-" + label;
            engine.prepareAsync (rate, blockSize, [settings] { return settings; });
            waitFor ([&] { return engine.isProjectConfirmed(); }, "Matrix project discovery timed out");
            check (engine.getReservedCaptureBytes() <= 16 * 1024 * 1024, "Capture exceeded 16 MiB per prepared bank");
            Head head;
            const auto length = (int) (rate * 1.25);
            juce::AudioBuffer<float> storage (2, blockSize);
            for (int position = 0; position < length;)
            {
                const auto count = juce::jmin (blockSize, length - position);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < count; ++i)
                    storage.setSample (ch, i, (ch == 0 ? 1.25f : 0.25f) * (float) std::cos (juce::MathConstants<double>::twoPi * 400 * (position + i) / rate));
                auto* const* channels = storage.getArrayOfWritePointers();
                juce::AudioBuffer<float> block (channels, 2, count); // Non-owning; no data allocation.
                head.sample = position;
                const auto begin = juce::Time::getMillisecondCounterHiRes();
                engine.processBlock (block, &head, (cases % 3) == 0);
                if ((cases % 3) != 0) callbackTimes.push_back (juce::Time::getMillisecondCounterHiRes() - begin);
                check (block.getSample (0, 0) == 1.25f * (float) std::cos (juce::MathConstants<double>::twoPi * 400 * position / rate)
                    && block.getSample (1, count - 1) == 0.25f * (float) std::cos (juce::MathConstants<double>::twoPi * 400 * (position + count - 1) / rate), "Audio passthrough changed");
                position += count;
            }
            head.playing = false;
            engine.processBlock (storage, &head);
            waitFor ([&] { return engine.getState() == RecordingEngine::State::Done || engine.getState() == RecordingEngine::State::Error; }, "Matrix capture timed out");
            check (engine.getState() == RecordingEngine::State::Done, "Matrix " + label + ": " + engine.getLastError());
            check (engine.getDroppedCaptures() == 0, "Matrix dropped a capture");
            const auto folder = savedEntry (library);
            const auto metadata = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
            juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
            std::unique_ptr<juce::AudioFormatReader> pcm (formats.createReaderFor (LibraryStore::audioFile (folder, metadata, true)));
            check (pcm && pcm->sampleRate == rate && pcm->lengthInSamples == length, "Matrix PCM rate/length changed");
            check (pcm->numChannels == (settings.mono ? 1u : 2u), "Mono capture still stores two channels");
            check (pcm->usesFloatingPointData && (int) metadata["audioStorageVersion"] == 3, "Segmented preview format missing");
            juce::AudioBuffer<float> sample (2, 1);
            const auto probe = (int) (rate / 2);
            check (pcm->read (&sample, 0, 1, probe, true, true), "Matrix compressed preview read failed");
            const auto expected = (settings.mono ? 0.75f : 1.25f) * (float) (double) metadata["previewGain"];
            check (std::abs (sample.getSample (0, 0) - expected) < 0.035f, "Matrix channel/gain data changed");
            if (! settings.mono) check (std::abs (sample.getSample (1, 0) * 5 - sample.getSample (0, 0)) < 0.04f, "Matrix lost stereo channel ratio");
            check ((double) metadata.getProperty ("bpm", 0) == 137.5 && metadata.getProperty ("bpmSource", "") == juce::var ("host"), "Host tempo lost priority");
            check ((double) metadata.getProperty ("bpmConfidence", 0) == 1, "Host tempo confidence missing");
            check ((double) metadata.getProperty ("previewGain", 0) > 0 && (double) metadata.getProperty ("previewGain", 0) <= 1, "Preview gain policy missing");
        }
    // More audio than the bounded ring holds: a non-real-time bounce must pace
    // against storage instead of losing the remainder of a fast export.
    {
        auto project = matrix.getChildFile ("accelerated.rpp"); project.replaceWithText ("accelerated export fixture");
        auto library = matrix.getChildFile ("accelerated"); library.createDirectory();
        RecordingEngine engine ([project] { return HostProject { true, "accelerated", project.getFullPathName(), "REAPER" }; });
        SettingsManager::Snapshot settings; settings.seconds = 15; settings.previews = library; settings.fingerprint = "accelerated";
        engine.prepareAsync (192000, 512, [settings] { return settings; });
        waitFor ([&] { return engine.isProjectConfirmed(); }, "Export discovery timed out");
        const auto started = juce::Time::getMillisecondCounterHiRes();
        juce::AudioBuffer<float> audio (2, 512);
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 0.25f, 512);
        Head head; head.playing = false;
        for (int position = 0; position < 192000 * 15; position += 512)
        {
            head.sample = position; engine.processBlock (audio, &head, true);
        }
        waitFor ([&] { return engine.getState() == RecordingEngine::State::Done || engine.getState() == RecordingEngine::State::Error; }, "Accelerated export save timed out");
        check (engine.getState() == RecordingEngine::State::Done && engine.getDroppedCaptures() == 0,
               "Accelerated export was dropped: " + engine.getLastError());
        const auto folder = savedEntry (library);
        juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
        check (reader && reader->lengthInSamples == 192000 * 15, "Accelerated export truncated at the queue capacity");
        std::cout << "PASS: 15-second accelerated 192 kHz export through an 8 MiB ring, elapsed="
                  << juce::Time::getMillisecondCounterHiRes() - started << " ms\n";
    }
    testTransportLoops (root);
    waitFor ([&] { return RecordingEngine::getProcessCaptureBytes() == initialMemory; }, "Capture reservation leaked after asynchronous cleanup");
    // Simultaneous preparations must cap the process budget, including failures.
    {
        std::vector<std::unique_ptr<RecordingEngine>> engines;
        SettingsManager::Snapshot settings; settings.seconds = 120; settings.previews = matrix; settings.fingerprint = "budget";
        for (int i = 0; i < 18; ++i)
        {
            auto engine = std::make_unique<RecordingEngine> ([] { return HostProject{}; });
            engine->prepareAsync (192000, 128, [settings] { return settings; });
            check (RecordingEngine::getProcessCaptureBytes() <= 256 * 1024 * 1024, "Multiple plugins exceeded process memory budget");
            engines.push_back (std::move (engine));
        }
        check (engines.back()->getState() == RecordingEngine::State::Error && ! engines.back()->getTransportWarning().isEmpty(), "Budget exhaustion was silent");
    }
    waitFor ([&] { return RecordingEngine::getProcessCaptureBytes() == initialMemory; }, "Global capture budget leaked");
    std::sort (callbackTimes.begin(), callbackTimes.end());
    std::cout << "PASS: " << cases << " capture combinations with asynchronous preparation (44.1/48/88.2/96/192 kHz x 32/64/128/512/2048), mono/stereo, offline, float, host tempo, 18-instance budget; real-time callbacks="
              << callbackTimes.size() << " p99=" << callbackTimes[(size_t) (callbackTimes.size() * 0.99)]
              << " p99.9=" << callbackTimes[(size_t) (callbackTimes.size() * 0.999)] << " max=" << callbackTimes.back() << " ms\n";
}
