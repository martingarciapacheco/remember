#include "PlaybackTests.h"
#include "../App/Source/ReadAheadSource.h"
#include "../App/Source/MetronomeSource.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check (bool value, const char* error) { if (! value) throw std::runtime_error (error); }
template <class Condition> void await (Condition condition)
{
    const auto end = juce::Time::getMillisecondCounter() + 5000;
    while (! condition()) { check (juce::Time::getMillisecondCounter() < end, "Read-ahead fixture timed out"); juce::Thread::sleep (2); }
}
struct Control
{
    juce::WaitableEvent blocked, release { true };
    juce::int64 blockAt = std::numeric_limits<juce::int64>::max(), failAt = std::numeric_limits<juce::int64>::max();
    std::atomic<bool> destroyed { false }, readOnCaller { false };
    juce::Thread::ThreadID caller = juce::Thread::getCurrentThreadId();
};
float sample (juce::int64 position) { return 0.2f + (float) (position % 97) * 0.0001f; }
class Reader final : public juce::AudioFormatReader
{
public:
    Reader (std::shared_ptr<Control> control, double rate = 48000, bool mono = false)
        : juce::AudioFormatReader (nullptr, "Read-ahead fixture"), state (std::move (control))
    { sampleRate = rate; lengthInSamples = 1000000; numChannels = mono ? 1 : 2; usesFloatingPointData = true; bitsPerSample = 32; }
    ~Reader() override { state->destroyed.store (true); }
    bool readSamples (int* const* output, int channels, int offset, juce::int64 position, int count) override
    {
        if (juce::Thread::getCurrentThreadId() == state->caller) state->readOnCaller.store (true);
        if (position >= state->blockAt) { state->blocked.signal(); state->release.wait (10000); }
        if (position >= state->failAt) return false;
        for (int ch = 0; ch < channels; ++ch)
            if (output[ch])
                for (int i = 0; i < count; ++i)
                    reinterpret_cast<float*> (output[ch])[offset + i] = (ch == 0 ? 1 : -1) * sample (position + i);
        return true;
    }
    std::shared_ptr<Control> state;
};
struct Unblock { std::shared_ptr<Control> state; ~Unblock() { state->release.signal(); } };
}

void testPlayback (const juce::File&)
{
    {
        juce::AudioTransportSource transport;
        MetronomeSource metronome (transport);
        check (! metronome.isEnabled(), "Metronome must start disabled");
        juce::AudioBuffer<float> disabled (2, 256); disabled.clear(); disabled.applyGain (1);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) disabled.setSample (ch, i, 0.125f);
        metronome.setTempo (120); metronome.mixClick ({ &disabled, 0, 256 }, 0, 1.0 / 48000);
        check (disabled.getSample (0, 100) == 0.125f, "Disabled metronome changes sample volume");
        metronome.setEnabled (true); metronome.setVolume (1);
        juce::AudioBuffer<float> full (2, 48000); full.clear();
        metronome.mixClick ({ &full, 0, full.getNumSamples() }, 0, 1.0 / 48000);
        check (full.getMagnitude (0, 0, 2048) > 0.01f, "Embedded metronome WAV is silent");
        check (full.getMagnitude (0, 12000, 2048) == 0, "120 BPM metronome ticks between beats");
        for (int i = 0; i < 2048; ++i) check (std::abs (full.getSample (0, i) - full.getSample (0, 24000 + i)) < 0.00001f, "Metronome drifts across beats");
        juce::AudioBuffer<float> sought (2, 4096); sought.clear();
        metronome.mixClick ({ &sought, 0, 4096 }, 23000.0 / 48000, 1.0 / 48000);
        for (int i = 0; i < 4096; ++i) check (std::abs (sought.getSample (0, i) - full.getSample (0, 23000 + i)) < 0.00001f, "Seek changes metronome phase");
        metronome.setVolume (0.25f); sought.clear(); metronome.mixClick ({ &sought, 0, 4096 }, 0, 1.0 / 48000);
        for (int i = 0; i < 4096; ++i) check (std::abs (sought.getSample (0, i) - full.getSample (0, i) * 0.25f) < 0.00001f, "Metronome volume is not independent");
        metronome.setMasterGain (0); sought.clear(); metronome.mixClick ({ &sought, 0, 4096 }, 0, 1.0 / 48000);
        check (sought.getMagnitude (0, 0, 4096) == 0, "Master mute leaves metronome audible");
        metronome.setMasterGain (1); metronome.setTempo (0); sought.clear(); metronome.mixClick ({ &sought, 0, 4096 }, 0, 1.0 / 48000);
        check (sought.getMagnitude (0, 0, 4096) == 0, "Unknown BPM starts a free-running metronome");
        std::cout << "PASS: supplied metronome WAV, disabled passthrough, beat phase/seek, independent volume, master mute and unknown BPM\n";
    }
    // A large audio block must still yield a distinct, correctly timed cursor
    // on every 60/120 Hz frame, across sample rates and callback boundaries.
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto fps : { 60, 120 })
        {
            PlaybackCursorClock clock;
            check (! clock.position (100, 0, rate), "Unstarted audio supplied a presentation position");
            const double blockSeconds = 8192.0 / rate;
            int previousBlock = -1;
            double previousPosition = -1;
            for (int frame = 0; frame < fps * 2; ++frame)
            {
                const double elapsed = (frame + 0.5) / fps;
                const int block = (int) (elapsed / blockSeconds);
                if (block != previousBlock)
                {
                    clock.publish (block * 8192.0, (block + 1) * 8192.0, 2, 100 + block * blockSeconds);
                    previousBlock = block;
                }
                const auto visual = clock.position (100 + elapsed, 2, rate);
                check (visual && std::abs (*visual - elapsed) < 0.000001 && *visual > previousPosition,
                       "Large audio blocks caused a stalled, inaccurate or backwards presentation frame");
                previousPosition = *visual;
            }
            const auto stopped = clock.position (200, 2, rate);
            check (stopped && *stopped == (previousBlock + 1) * 8192.0 / rate,
                   "Cursor advanced beyond the supplied audio during a stall");
            check (! clock.position (200, 4, rate), "Seek reused an obsolete presentation block");
            clock.publish (0, 100, 4, 201);
            check (clock.position (202, 4, rate) == std::optional<double> (100 / rate),
                   "Short final/partial audio block let the cursor overshoot");
        }
    for (const auto hz : { 30, 60, 120, 144, 240 })
    {
        PlaybackFramePacer pacer;
        int frames = 0;
        for (int frame = 0; frame < hz * 2; ++frame)
            if (pacer.advance (100 + (double) frame / hz)) ++frames;
        const int expected = juce::jmin (hz, 120) * 2;
        check (frames == expected, "Cursor frame pacing missed display frames or exceeded its 120 Hz limit");
        check (! pacer.advance (100 + (double) (hz * 2 - 1) / hz), "A duplicate display event repainted the same frame");
        pacer.reset();
        check (pacer.advance (0), "Changing display clocks retained obsolete frame pacing");
    }
    {
        PlaybackFramePacer pacer;
        for (int frame = 0; frame < 240; ++frame)
            check (pacer.advance (100 + frame / 120.0 + (frame % 2 == 0 ? 0.0001 : -0.0001)),
                   "Slight display timestamp jitter halved 120 Hz cursor updates");
        check (pacer.advance (200) && ! pacer.advance (200.001), "A delayed display frame caused catch-up repaints");
    }
    std::cout << "PASS: 60/120 Hz sub-block cursor interpolation, stalled/partial audio, seeks, and 30/60/120/144/240 Hz display pacing\n";
    const auto baseline = ReadAheadSource::processBufferBytes();
    double maximumCallbackMs = 0;
    std::vector<double> callbackTimes;
    callbackTimes.reserve (12016);
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto state = std::make_shared<Control>();
        const bool mono = rate == 96000;
        ReadAheadSource source (std::make_unique<Reader> (state, rate, mono));
        check (source.valid() && source.bufferBytes() < 4 * 1024 * 1024, "Read-ahead did not bound its cache");
        source.prepareToPlay (8192, rate);
        for (const auto block : { 32, 128, 2048, 8192 })
        {
            await ([&] { return source.ready (block); });
            const auto start = source.getNextReadPosition();
            juce::AudioBuffer<float> audio (3, block + 8);
            for (int ch = 0; ch < 3; ++ch) juce::FloatVectorOperations::fill (audio.getWritePointer (ch), -5, block + 8);
            const auto begun = juce::Time::getMillisecondCounterHiRes();
            source.getNextAudioBlock ({ &audio, 4, block });
            const auto callbackMs = juce::Time::getMillisecondCounterHiRes() - begun;
            maximumCallbackMs = juce::jmax (maximumCallbackMs, callbackMs);
            callbackTimes.push_back (callbackMs);
            for (int i = 0; i < block; ++i)
            {
                check (audio.getSample (0, i + 4) == sample (start + i), "Read-ahead skipped/reordered PCM frames");
                check (audio.getSample (1, i + 4) == (mono ? 1 : -1) * sample (start + i), "Read-ahead changed channels");
                check (audio.getSample (2, i + 4) == 0, "Read-ahead left extra outputs uncleared");
            }
            check (audio.getSample (0, 0) == -5 && audio.getSample (0, block + 7) == -5, "Read-ahead wrote outside the active buffer");
            check (source.getNextReadPosition() == start + block, "Read-ahead position changed");
        }
        source.setActive (true);
        juce::AudioBuffer<float> streaming (2, 128);
        for (int i = 0; i < 3000; ++i)
        {
            await ([&] { return source.ready (128); });
            const auto start = source.getNextReadPosition();
            const auto begun = juce::Time::getMillisecondCounterHiRes();
            source.getNextAudioBlock ({ &streaming, 0, 128 });
            const auto callbackMs = juce::Time::getMillisecondCounterHiRes() - begun;
            maximumCallbackMs = juce::jmax (maximumCallbackMs, callbackMs);
            callbackTimes.push_back (callbackMs);
            const auto delivered = (int) (source.getNextReadPosition() - start);
            check (delivered >= 0 && delivered <= 128, "Streaming cache advanced outside the requested block");
            for (int j = 0; j < delivered; ++j)
                check (streaming.getSample (0, j) == sample (start + j), "Streaming cache recycled a page before consumption");
            for (int j = delivered; j < 128; ++j)
                check (streaming.getSample (0, j) == 0, "Streaming cache underrun retained stale samples");
        }
        source.setActive (false);
        for (int i = 0; i < 100; ++i) source.setNextReadPosition (234567 + i);
        await ([&] { return source.ready (512); });
        juce::AudioBuffer<float> audio (2, 512);
        source.getNextAudioBlock ({ &audio, 0, 512 });
        check (audio.getSample (0, 0) == sample (234666), "Rapid seeks played stale data");
        source.setNextReadPosition (source.getTotalLength() - 25);
        await ([&] { return source.ready (512); });
        source.getNextAudioBlock ({ &audio, 0, 512 });
        check (audio.getSample (0, 24) == sample (source.getTotalLength() - 1) && audio.getSample (0, 25) == 0,
               "EOF lost valid frames or retained old samples");
        check (! state->readOnCaller.load(), "A callback/control operation read the decoder");
    }
    {
        auto state = std::make_shared<Control>(); state->blockAt = 16384;
        ReadAheadSource source (std::make_unique<Reader> (state));
        Unblock unblock { state };
        check (state->blocked.wait (1000), "Slow-read fixture did not block");
        await ([&] { return source.ready (16384); });
        juce::AudioBuffer<float> audio (2, 2048);
        for (int i = 0; i < 8; ++i) source.getNextAudioBlock ({ &audio, 0, 2048 });
        auto begun = juce::Time::getMillisecondCounterHiRes();
        source.getNextAudioBlock ({ &audio, 0, 2048 });
        const auto missMs = juce::Time::getMillisecondCounterHiRes() - begun;
        check (missMs < 50 && audio.getMagnitude (0, 2048) == 0 && source.underruns() == 1
               && source.getNextReadPosition() == 16384, "A disk stall blocked audio, hid an underrun or advanced past missing samples");
        check (source.presentationPosition (juce::Time::getMillisecondCounterHiRes() * 0.001)
                   == std::optional<double> (16384.0 / 48000), "A storage underrun advanced the visual cursor");
        begun = juce::Time::getMillisecondCounterHiRes();
        source.setNextReadPosition (300019);
        check (! source.presentationPosition (juce::Time::getMillisecondCounterHiRes() * 0.001),
               "A seek retained the old block's interpolated cursor");
        check (juce::Time::getMillisecondCounterHiRes() - begun < 50, "Seeking waited for disk I/O");
        source.getNextAudioBlock ({ &audio, 0, 2048 });
        check (audio.getMagnitude (0, 2048) == 0 && source.getNextReadPosition() == 300019, "Seek replayed a stale cache page");
        state->release.signal();
        await ([&] { return source.ready (2048); });
        source.getNextAudioBlock ({ &audio, 0, 2048 });
        check (audio.getSample (0, 0) == sample (300019), "Resume did not start at the requested frame");
        std::cout << "PASS: blocked storage, observable underruns, seek without I/O wait; cache miss=" << missMs << " ms\n";
    }
    await ([&] { return ReadAheadSource::processBufferBytes() == baseline && BackgroundRetirement::pending() == 0; });
    {
        auto state = std::make_shared<Control>(); state->blockAt = 0;
        auto source = std::make_unique<ReadAheadSource> (std::make_unique<Reader> (state));
        Unblock unblock { state };
        check (state->blocked.wait (1000), "Close fixture did not block its reader");
        const auto begun = juce::Time::getMillisecondCounterHiRes(); source.reset();
        const auto closeMs = juce::Time::getMillisecondCounterHiRes() - begun;
        check (closeMs < 200 && ! state->destroyed.load() && BackgroundRetirement::pending() > 0,
               "Closing waited for storage or destroyed an active reader");
        state->release.signal();
        await ([&] { return state->destroyed.load() && ReadAheadSource::processBufferBytes() == baseline
                           && BackgroundRetirement::pending() == 0; });
        std::cout << "PASS: read-ahead closes during blocked I/O in " << closeMs << " ms and then releases its reader/cache\n";
    }
    {
        auto state = std::make_shared<Control>(); state->failAt = 8192;
        ReadAheadSource source (std::make_unique<Reader> (state));
        await ([&] { return source.readFailed(); });
        check (! source.ready(), "Decoder failure was reported as ready");
    }
    await ([&] { return ReadAheadSource::processBufferBytes() == baseline && BackgroundRetirement::pending() == 0; });
    {
        // Retiring a blocked decoder must retain its reservation. Many rapid
        // selections cannot grow the process cache beyond its shared budget.
        auto state = std::make_shared<Control>(); state->blockAt = 0;
        Unblock unblock { state };
        std::vector<std::unique_ptr<ReadAheadSource>> sources;
        bool rejected = false;
        for (int i = 0; i < 30; ++i)
        {
            auto source = std::make_unique<ReadAheadSource> (std::make_unique<Reader> (state, 192000));
            if (! source->valid())
            {
                check (source->error().contains ("64 MiB"), "Cache budget failure has no actionable error");
                check (source->bufferBytes() == 0, "Rejected cache retained a reservation");
                rejected = true; break;
            }
            sources.push_back (std::move (source));
            check (ReadAheadSource::processBufferBytes() <= 64 * 1024 * 1024, "Playback caches exceeded the process budget");
        }
        check (rejected && ! sources.empty(), "Concurrent playback caches did not enforce the process budget");
        check (state->blocked.wait (1000), "Budget fixture did not block its readers");
        const auto peakBytes = ReadAheadSource::processBufferBytes();
        sources.clear();
        check (ReadAheadSource::processBufferBytes() > baseline, "Retirement prematurely released a blocked cache");
        state->release.signal();
        await ([&] { return ReadAheadSource::processBufferBytes() == baseline && BackgroundRetirement::pending() == 0; });
        std::cout << "PASS: concurrent/retiring playback caches respect 64 MiB process budget; peak=" << peakBytes << " bytes\n";
    }
    std::cout << "PASS: read-ahead at 44.1/48/96/192 kHz, 32/128/2048/8192 frames, mono/stereo, 100 seeks and EOF; max callback="
              << maximumCallbackMs << " ms\n";
    std::sort (callbackTimes.begin(), callbackTimes.end());
    const auto percentile = [&] (double fraction) { return callbackTimes[(size_t) (fraction * (callbackTimes.size() - 1))]; };
    std::cout << "Playback cache callbacks=" << callbackTimes.size() << " p99=" << percentile (0.99)
              << " ms p99.9=" << percentile (0.999) << " ms max=" << callbackTimes.back() << " ms\n";
}
