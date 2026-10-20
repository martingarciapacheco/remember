#include "ReadAheadSource.h"
#include "../../Shared/BackgroundRetirement.h"
#include <cmath>

namespace
{
std::atomic<size_t> reservedReadAheadBytes { 0 };
constexpr size_t readAheadProcessBudget = 64 * 1024 * 1024;
static_assert (std::atomic<uint64_t>::is_always_lock_free && std::atomic<juce::int64>::is_always_lock_free,
               "Playback requests and counters must be lock-free");
}

class ReadAheadSource::Core final : public juce::Thread, public BackgroundRetirement::Job
{
public:
    static constexpr int pageFrames = 2048;
    enum PageState { Empty, Available, Writing, Reading };
    struct Page
    {
        juce::AudioBuffer<float> audio { 2, pageFrames };
        std::atomic<int> state { Empty }, count { 0 };
        std::atomic<uint64_t> generation { ~uint64_t{} };
        std::atomic<juce::int64> index { -1 };
    };
    struct Request { uint64_t generation = 0; juce::int64 position = 0; };

    explicit Core (std::unique_ptr<juce::AudioFormatReader> input, int cacheFrames)
        : juce::Thread ("RememberPlaybackReadAhead"), reader (std::move (input))
    {
        validReader = module.valid() && reader && std::isfinite (reader->sampleRate)
            && reader->sampleRate > 0 && reader->sampleRate <= 768000
            && reader->lengthInSamples > 0 && reader->numChannels > 0;
        if (! validReader) { initialError = "Unsupported or empty preview, or background module unavailable"; return; }
        rate = reader->sampleRate; length = reader->lengthInSamples;
        pageCount = (int) std::ceil ((cacheFrames > 0 ? juce::jlimit (4096.0, 384000.0, (double) cacheFrames) : juce::jlimit (16384.0, 384000.0, rate * 2)) / pageFrames) + 2;
        const auto bytes = (size_t) pageCount * pageFrames * 2 * sizeof (float);
        auto reserved = reservedReadAheadBytes.load();
        do
        {
            if (reserved + bytes > readAheadProcessBudget)
            { validReader = false; failed.store (true); pageCount = 0; initialError = "Playback cache is busy (64 MiB process limit). Retry after earlier readers finish"; return; }
        } while (! reservedReadAheadBytes.compare_exchange_weak (reserved, reserved + bytes));
        reservedBytes = bytes;
        try { pages = std::make_unique<Page[]> ((size_t) pageCount); }
        catch (...)
        {
            reservedReadAheadBytes.fetch_sub (reservedBytes); reservedBytes = 0;
            validReader = false; failed.store (true); pageCount = 0; initialError = "Cannot allocate playback cache"; return;
        }
        if (! startThread()) { validReader = false; failed.store (true); initialError = "Cannot start playback reader"; return; }
        if (! completion.capture ((uintptr_t) getThreadId())) { validReader = false; failed.store (true); initialError = "Cannot monitor native playback reader completion"; }
    }
    ~Core() override { jassert (readyToRelease()); reservedReadAheadBytes.fetch_sub (reservedBytes); }
    void requestStop() override { signalThreadShouldExit(); notify(); }
    bool readyToRelease() const override { return ! isThreadRunning() && completion.finished(); }

    bool request (Request& value) const
    {
        // One attempt in audio/probes; an in-progress seek produces silence
        // instead of spinning on the message thread.
        const auto before = seekGeneration.load (std::memory_order_acquire);
        if ((before & 1) != 0) return false;
        value.position = seekPosition.load(); value.generation = before;
        return before == seekGeneration.load (std::memory_order_acquire);
    }
    juce::int64 position (const Request& request) const
    {
        const auto before = consumedGeneration.load (std::memory_order_acquire);
        const auto cursor = consumedPosition.load();
        return before == request.generation && before == consumedGeneration.load (std::memory_order_acquire)
            ? cursor : request.position;
    }
    bool ready (int samples) const
    {
        if (! validReader || failed.load()) return false;
        Request command;
        if (! request (command)) return false;
        const auto start = position (command);
        const auto end = juce::jmin (length, start + juce::jmax (1, samples));
        for (auto cursor = start; cursor < end;)
        {
            const auto index = cursor / pageFrames;
            const auto& page = pages[(size_t) (index % pageCount)];
            const auto count = page.count.load();
            if (page.state.load (std::memory_order_acquire) != Available
                || page.generation.load() != command.generation || page.index.load() != index
                || count <= cursor % pageFrames) return false;
            cursor = juce::jmin (end, index * pageFrames + count);
        }
        return true;
    }
    void run() override
    {
        while (! threadShouldExit())
        {
            Request command;
            if (! request (command)) { wait (1); continue; }
            const auto first = position (command) / pageFrames;
            bool readAny = false;
            for (int offset = 0; offset < pageCount && ! threadShouldExit(); ++offset)
            {
                if (command.generation != seekGeneration.load()) break;
                const auto index = first + offset;
                const auto start = index * pageFrames;
                if (start >= length) break;
                auto& page = pages[(size_t) (index % pageCount)];
                auto state = page.state.load();
                if (state == Available && page.index.load() == index && page.generation.load() == command.generation) continue;
                if (state != Available && state != Empty) continue;
                if (! page.state.compare_exchange_strong (state, Writing, std::memory_order_acquire)) continue;
                const auto count = (int) juce::jmin ((juce::int64) pageFrames, length - start);
                const auto ok = reader->read (&page.audio, 0, count, start, true, true);
                if (! ok)
                {
                    failed.store (true);
                    page.state.store (Empty, std::memory_order_release);
                    return;
                }
                page.index.store (index); page.count.store (count); page.generation.store (command.generation);
                page.state.store (Available, std::memory_order_release);
                readAny = true;
            }
            wait (readAny ? 2 : (active.load() ? 5 : 250));
        }
    }

    BackgroundRetirement::ModuleReference module;
    BackgroundRetirement::NativeCompletion completion;
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<Page[]> pages;
    double rate = 0;
    juce::int64 length = 0;
    int pageCount = 0;
    size_t reservedBytes = 0;
    bool validReader = false;
    juce::String initialError;
    std::atomic<bool> failed { false }, active { false };
    std::atomic<uint64_t> seekGeneration { 0 }, consumedGeneration { ~uint64_t{} }, underruns { 0 };
    std::atomic<juce::int64> seekPosition { 0 }, consumedPosition { 0 };
};

ReadAheadSource::ReadAheadSource (std::unique_ptr<juce::AudioFormatReader> reader, int cacheFrames)
    : core (std::make_shared<Core> (std::move (reader), cacheFrames)) {}
ReadAheadSource::~ReadAheadSource() { BackgroundRetirement::retire (std::move (core)); }
bool ReadAheadSource::valid() const { return core->validReader; }
bool ReadAheadSource::ready (int samples) const { return core->ready (samples); }
bool ReadAheadSource::readFailed() const { return core->failed.load(); }
juce::String ReadAheadSource::error() const { return core->initialError.isNotEmpty() ? core->initialError : (readFailed() ? "Decoder/storage read failed" : ""); }
double ReadAheadSource::sampleRate() const { return core->rate; }
std::optional<double> ReadAheadSource::presentationPosition (double seconds) const
{
    Core::Request command;
    if (! core->request (command)) return {};
    return cursorClock.position (seconds, command.generation, core->rate);
}
juce::int64 ReadAheadSource::getTotalLength() const { return core->length; }
uint64_t ReadAheadSource::underruns() const { return core->underruns.load(); }
size_t ReadAheadSource::bufferBytes() const { return core->reservedBytes; }
size_t ReadAheadSource::processBufferBytes() { return reservedReadAheadBytes.load(); }
void ReadAheadSource::setActive (bool active) { core->active.store (active); if (active) core->notify(); }
void ReadAheadSource::requestPrefill() { core->notify(); }
juce::int64 ReadAheadSource::getNextReadPosition() const
{
    Core::Request command;
    return core->request (command) ? core->position (command) : core->seekPosition.load();
}
void ReadAheadSource::setNextReadPosition (juce::int64 position)
{
    position = juce::jlimit ((juce::int64) 0, core->length, position);
    if (getNextReadPosition() == position) return;
    // A single control writer (AudioTransportSource) publishes a coherent seek.
    core->seekGeneration.fetch_add (1, std::memory_order_acq_rel);
    core->seekPosition.store (position);
    core->seekGeneration.fetch_add (1, std::memory_order_release);
    // No notifier: seeking is also legal from an audio callback.
}
void ReadAheadSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    const auto blockTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    info.clearActiveBufferRegion();
    if (! valid()) return;
    Core::Request command;
    if (! core->request (command)) { core->underruns.fetch_add (1); return; }
    if (callbackGeneration != command.generation)
    { callbackGeneration = command.generation; callbackPosition = command.position; }
    const auto blockStart = callbackPosition;
    int copied = 0;
    while (copied < info.numSamples && callbackPosition < core->length)
    {
        const auto index = callbackPosition / Core::pageFrames;
        auto& page = core->pages[(size_t) (index % core->pageCount)];
        auto expected = (int) Core::Available;
        if (! page.state.compare_exchange_strong (expected, Core::Reading, std::memory_order_acquire)) break;
        const auto offset = (int) (callbackPosition % Core::pageFrames);
        const auto available = page.count.load() - offset;
        const auto count = juce::jmin (info.numSamples - copied, available);
        const bool matches = page.generation.load() == command.generation && page.index.load() == index && count > 0;
        if (matches)
        {
            for (int ch = 0; ch < juce::jmin (2, info.buffer->getNumChannels()); ++ch)
                info.buffer->copyFrom (ch, info.startSample + copied, page.audio, ch, offset, count);
            copied += count; callbackPosition += count;
        }
        page.state.store (Core::Available, std::memory_order_release);
        if (! matches) break;
    }
    if (copied < info.numSamples && callbackPosition < core->length) core->underruns.fetch_add (1);
    core->consumedPosition.store (callbackPosition);
    core->consumedGeneration.store (command.generation, std::memory_order_release);
    cursorClock.publish ((double) blockStart, (double) callbackPosition, command.generation, blockTime);
}
