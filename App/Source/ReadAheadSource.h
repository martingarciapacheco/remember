#pragma once
#include <JuceHeader.h>
#include <memory>
#include "PlaybackCursor.h"

// Reader I/O belongs exclusively to a retained worker. Control/audio operations
// use atomic requests and bounded cache pages; they never join or call a reader.
class ReadAheadSource final : public juce::PositionableAudioSource
{
public:
    explicit ReadAheadSource (std::unique_ptr<juce::AudioFormatReader>);
    ~ReadAheadSource() override;
    void prepareToPlay (int, double) override {}
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    void setNextReadPosition (juce::int64) override;
    juce::int64 getNextReadPosition() const override;
    juce::int64 getTotalLength() const override;
    bool isLooping() const override { return false; }
    bool valid() const;
    bool ready (int samples = 2048) const;
    bool readFailed() const;
    juce::String error() const;
    double sampleRate() const;
    // UI probe using Time::getMillisecondCounterHiRes() * 0.001. Empty until
    // audio is supplied after a seek, or if a concurrent snapshot is incomplete.
    std::optional<double> presentationPosition (double seconds) const;
    uint64_t underruns() const;
    size_t bufferBytes() const;
    static size_t processBufferBytes();
    // Control thread only: enables frequent fill polling during playback and
    // wakes an idle worker for a requested start/seek.
    void setActive (bool);
    void requestPrefill();
private:
    class Core;
    std::shared_ptr<Core> core;
    // The callback is the sole owner of these cursor fields. Seek only publishes
    // a new request, so a concurrent seek cannot overwrite callback state.
    uint64_t callbackGeneration = ~uint64_t{};
    juce::int64 callbackPosition = 0;
    PlaybackCursorClock cursorClock;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReadAheadSource)
};
