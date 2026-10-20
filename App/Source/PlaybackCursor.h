#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>

// One audio writer, one UI reader. A failed snapshot never waits on the audio
// thread: the UI keeps its previous cursor until the next display frame.
class PlaybackCursorClock
{
public:
    void publish (double first, double last, uint64_t generation, double seconds)
    {
        sequence.fetch_add (1);
        start.store (first); end.store (last); seekGeneration.store (generation); timestamp.store (seconds);
        sequence.fetch_add (1);
    }

    std::optional<double> position (double now, uint64_t generation, double sampleRate) const
    {
        const auto before = sequence.load();
        if (before == 0 || (before & 1) != 0) return {};
        const auto first = start.load(), last = end.load(), seconds = timestamp.load();
        const auto seek = seekGeneration.load();
        if (sequence.load() != before || seek != generation) return {};
        // Interpolate only audio actually delivered. A storage stall or a lost
        // device cannot send the visual cursor beyond the last supplied sample.
        return std::clamp (first + std::max (0.0, now - seconds) * sampleRate, first, last) / sampleRate;
    }

private:
    static_assert (std::atomic<double>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<uint64_t> sequence { 0 }, seekGeneration { 0 };
    std::atomic<double> start { 0 }, end { 0 }, timestamp { 0 };
};

// Draw on display frames with a 120 Hz budget. Keep an absolute deadline so
// slightly early/jittered 120 Hz events do not accidentally halve the cadence.
class PlaybackFramePacer
{
public:
    bool advance (double seconds)
    {
        if (previous && seconds == *previous) return false;
        if (previous && seconds < *previous) reset();
        previous = seconds;
        if (next && seconds + 0.0005 < *next) return false;
        next = next.value_or (seconds) + 1.0 / 120.0;
        // A delayed UI frame must not start a burst of catch-up repaints.
        if (*next <= seconds) next = seconds + 1.0 / 120.0;
        return true;
    }
    void reset() { next.reset(); previous.reset(); }
private:
    std::optional<double> next, previous;
};
