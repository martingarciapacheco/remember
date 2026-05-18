#pragma once
#include <JuceHeader.h>
#include "RecordingEngine.h"

class RememberProcessor : public juce::AudioProcessor
{
public:
    RememberProcessor();
    ~RememberProcessor() override = default;

    // ── AudioProcessor interface ─────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Remember"; }

    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Host passes track name here (Ableton, etc.)
    void updateTrackProperties (const TrackProperties& properties) override;

    // ── Accessors for the editor ─────────────────────────────────────────────
    RecordingEngine& getEngine() { return engine; }
    juce::String     getProjectName() const { return projectName; }
    void             setProjectName (const juce::String& name);

private:
    RecordingEngine engine;
    juce::String    projectName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RememberProcessor)
};
