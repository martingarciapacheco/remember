#pragma once
#include <JuceHeader.h>
#include <cmath>

// Immutable embedded click and atomic controls. The audio thread does no I/O,
// allocation or timer scheduling; beat phase comes from the preview transport.
class MetronomeSource final : public juce::AudioSource
{
public:
    explicit MetronomeSource (juce::AudioTransportSource&);
    void prepareToPlay (int, double) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    void setEnabled (bool value) { enabled.store (value); }
    bool isEnabled() const { return enabled.load(); }
    void setTempo (double value) { bpm.store (std::isfinite (value) && value >= 10 && value <= 1000 ? value : 0); }
    void setVolume (float value) { volume.store (juce::jlimit (0.0f, 1.0f, value)); }
    float getVolume() const { return volume.load(); }
    void setMasterGain (float value) { master.store (value); }
    // Deterministic mixing also permits offline verification without a device.
    void mixClick (const juce::AudioSourceChannelInfo&, double startSeconds, double secondsPerFrame) const;
private:
    juce::AudioTransportSource& transport;
    juce::AudioBuffer<float> click;
    double clickRate = 0;
    std::atomic<bool> enabled { false };
    std::atomic<double> bpm { 0 };
    std::atomic<float> volume { 0.5f }, master { 1.0f };
};
