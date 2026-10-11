#include "TransportTests.h"
#include "../Plugin/Source/PluginProcessor.h"
#include "../Plugin/Source/PluginEditor.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
}
template <class Predicate> void waitFor (Predicate predicate, const juce::String& message)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + 20000;
    while (! predicate()) { check (juce::Time::getMillisecondCounterHiRes() < end, message); juce::Thread::sleep (2); }
}
struct Head : juce::AudioPlayHead
{
    bool playing = true;
    juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info; info.setIsPlaying (playing); info.setTimeInSamples (sample); info.setBpm (120.0); return info;
    }
};
struct HeldCommit
{
    explicit HeldCommit (juce::File metadata) : worker ([this, metadata]
    {
        result = LibraryStore::updateJson (metadata, [this] (juce::DynamicObject&)
        { entered.signal(); release.wait (300000); return juce::Result::ok(); });
    }) {}
    ~HeldCommit() { release.signal(); if (worker.joinable()) worker.join(); }
    juce::WaitableEvent entered, release { true };
    juce::Result result { juce::Result::ok() };
    std::thread worker;
};
}

void testTransportLoops (const juce::File& root)
{
    const auto fixture = root.getChildFile ("transport-loops");
    std::vector<double> callbacks; callbacks.reserve (100000);
    int cases = 0;
    for (int rate : { 44100, 192000 })
        for (int blockSize : { 32, 2048 })
        {
            const auto label = juce::String (rate) + "-" + juce::String (blockSize);
            const auto library = fixture.getChildFile (label);
            check (LibraryStore::ensureDirectory (library).wasOk(), "Cannot create transport library " + label);
            const auto project = fixture.getChildFile (label + ".rpp");
            check (project.replaceWithText ("transport fixture " + label), "Cannot create transport project");
            SettingsManager::Snapshot settings; settings.previews = library; settings.seconds = 15;
            settings.mono = (++cases % 2) == 1; settings.fingerprint = "transport-" + label;
            juce::var config (new juce::DynamicObject());
            config.getDynamicObject()->setProperty ("previewsFolder", library.getFullPathName());
            config.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { fixture.getFullPathName() });
            check (LibraryStore::writeJsonAtomically (LibraryStore::configFile(), config).wasOk(), "Cannot configure transport editor");
            RememberProcessor processor ([project]
            { return HostProject { true, project.getFileNameWithoutExtension(), project.getFullPathName(), "REAPER" }; },
                [settings] { return settings; });
            auto& engine = processor.getEngine();
            Head head; processor.setPlayHead (&head); processor.prepareToPlay (rate, blockSize);
            waitFor ([&] { return engine.isProjectConfirmed() && ! engine.isPreparingSettings(); }, "Transport association timed out " + label);
            const auto folder = library.getChildFile (engine.getAssociationInfo().id);
            const auto metadata = folder.getChildFile ("metadata.json");
            auto revision = [&] { return LibraryStore::readJson (metadata).getProperty ("audioRevision", "").toString(); };
            juce::AudioBuffer<float> storage (2, blockSize); juce::MidiBuffer midi;
            auto deliver = [&] (int count, float value)
            {
                for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (storage.getWritePointer (ch), value, count);
                juce::AudioBuffer<float> block (storage.getArrayOfWritePointers(), 2, count);
                const auto start = juce::Time::getMillisecondCounterHiRes(); processor.processBlock (block, midi);
                callbacks.push_back (juce::Time::getMillisecondCounterHiRes() - start);
                check (block.getSample (0, 0) == value && block.getSample (1, count - 1) == value,
                       "Transport changed passthrough audio " + label);
                check (engine.getState() != RecordingEngine::State::Error, "Transport capture failed: " + engine.getLastError());
            };
            auto render = [&] (int frames, float value, bool stop)
            {
                head.playing = true;
                for (int position = 0; position < frames;)
                {
                    const auto count = juce::jmin (blockSize, frames - position);
                    head.sample = position; deliver (count, value); position += count;
                }
                if (stop) { head.playing = false; head.sample = frames; deliver (blockSize, -0.95f); }
            };
            auto saved = [&]
            {
                waitFor ([&]
                {
                    check (engine.getState() != RecordingEngine::State::Error, "Transport save failed: " + engine.getLastError());
                    return engine.getState() == RecordingEngine::State::Done;
                }, "Transport save timed out " + label);
            };
            auto audioIs = [&] (float prefix)
            {
                juce::AudioFormatManager formats; formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
                juce::AudioBuffer<float> probe (2, 1);
                check (reader && reader->sampleRate == rate && reader->lengthInSamples == rate * 3 / 2
                       && reader->numChannels == (settings.mono ? 1u : 2u), "Transport changed duration/rate/channels " + label);
                for (auto sample : { 0, rate * 11 / 8, rate * 3 / 2 - 1 })
                {
                    check (reader->read (&probe, 0, 1, sample, true, true), "Cannot read transport prefix/tail");
                    check (std::abs (probe.getSample (0, 0) - (sample == 0 ? prefix : 0.2f)) < 0.00001f,
                           "A short/skipped loop changed confirmed prefix/tail " + label);
                }
                check (LibraryStore::recoverEntry (folder).result.wasOk(), "Transport receipt verification failed");
            };
            render (rate * 3 / 2, 0.2f, true); saved();
            const auto seeded = revision();
            render (rate - 1, 0.9f, true);
            check (revision() == seeded && engine.getDroppedCaptures() == 0,
                   "A sub-second capture changed confirmed audio or counted as queue overflow");
            check (engine.getTransportWarning().contains ("1 second"), "Sub-second capture was silently discarded " + label);
            check (engine.getShortCaptures() == 1, "The sub-second boundary was not counted exactly once");
            if (cases == 1)
            {
                std::unique_ptr<RememberEditor> editor (static_cast<RememberEditor*> (processor.createEditor()));
                waitFor ([&]
                {
                    editor->timerCallback();
                    for (auto* child : editor->getChildren())
                        if (auto* text = dynamic_cast<juce::Label*> (child))
                            if (text->getText().contains ("1 second") && text->getTooltip().contains ("short captures: 1")) return true;
                    return false;
                }, "The actual editor hid its short-capture warning/counter");
            }
            processor.prepareToPlay (rate, blockSize);
            waitFor ([&] { return ! engine.isPreparingSettings(); }, "Exact-second preparation timed out");
            render (rate, 0.3f, true); saved(); audioIs (0.3f);
            check (engine.getTransportWarning().isEmpty(), "A valid capture did not clear its transport warning");
            check (engine.getShortCaptures() == 1, "Exactly one second was wrongly counted as too short");
            const auto beforeShortLoops = revision();
            const auto shortBefore = engine.getShortCaptures(), skippedBefore = engine.getDroppedCaptures();
            for (int loop = 0; loop < 8; ++loop) render (rate / 4, 0.9f, loop == 7);
            check (revision() == beforeShortLoops && engine.getTransportWarning().isNotEmpty(),
                   "Short continuous loops changed confirmed audio or hid their limitation");
            check (engine.getShortCaptures() - shortBefore + engine.getDroppedCaptures() - skippedBefore == 8
                   && engine.getShortCaptures() > shortBefore,
                   "Short loop attempts were lost or confused with valid captures");
            processor.prepareToPlay (rate, blockSize);
            waitFor ([&] { return ! engine.isPreparingSettings(); }, "Queued-loop preparation timed out");
            const auto droppedBefore = engine.getDroppedCaptures();
            const auto shortAfterLoops = engine.getShortCaptures();
            {
                HeldCommit held (metadata);
                check (held.entered.wait (1000), "Cannot hold the transport commit");
                render (rate * 5 / 4, 0.4f, false);
                render (rate * 5 / 4, 0.6f, false);
                render (rate * 5 / 4, 0.9f, false);
                check (engine.getDroppedCaptures() == droppedBefore + 1 && engine.getShortCaptures() == shortAfterLoops
                       && engine.getTransportWarning().contains ("buffers are busy"),
                       "A third loop did not report its saturated queue");
                held.release.signal(); saved();
            }
            audioIs (0.6f);
            const auto beforeMiddle = revision();
            head.sample = rate * 2; deliver (blockSize, 0.9f);
            check (engine.getState() == RecordingEngine::State::Done && revision() == beforeMiddle,
                   "A released queue restarted a skipped loop away from zero");
            render (rate * 5 / 4, 0.8f, true); saved(); audioIs (0.8f);
            check (engine.getDroppedCaptures() == droppedBefore + 1 && engine.getShortCaptures() == shortAfterLoops
                   && engine.getUnknownPositions() == 0 && engine.getTransportWarning().isEmpty(),
                   "Returning to zero did not rearm after queue recovery");
            std::cout << "TRANSPORT rate=" << rate << " block=" << blockSize << " mono=" << settings.mono
                      << " dropped=" << engine.getDroppedCaptures() << " short=" << engine.getShortCaptures()
                      << " reservedBytes=" << engine.getReservedCaptureBytes() << std::endl;
        }
    waitFor ([] { return BackgroundRetirement::pending() == 0; }, "Transport resources did not retire");
    std::sort (callbacks.begin(), callbacks.end());
    std::cout << "PASS: four actual processor transport cases, exact one-second boundary, eight short continuous loops, held commits, saturated loops, zero rearm and preserved tail/receipts; callbacks="
              << callbacks.size() << " p99Ms=" << callbacks[(size_t) (callbacks.size() * 0.99)]
              << " p999Ms=" << callbacks[(size_t) (callbacks.size() * 0.999)] << " maxMs=" << callbacks.back() << std::endl;
}
