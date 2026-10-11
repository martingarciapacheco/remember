#include "PlaybackTests.h"
#include "../App/Source/ReadAheadSource.h"
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
        begun = juce::Time::getMillisecondCounterHiRes();
        source.setNextReadPosition (300019);
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
