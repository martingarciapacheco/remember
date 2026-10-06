#pragma once
#include <JuceHeader.h>
#include "RecordingEngine.h"

class RememberProcessor : public juce::AudioProcessor
{
public:
    explicit RememberProcessor (RecordingEngine::ProjectDetector = ProjectIdentity::detectHostProject);
    ~RememberProcessor() override = default;

    // ── AudioProcessor interface ─────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { engine.finishCapture(); }
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

    // ── Accessors for the editor ─────────────────────────────────────────────
    RecordingEngine& getEngine() { return engine; }
    juce::String     getProjectName() const { return engine.getProjectName(); }
    void             setProjectName (const juce::String& name);

private:
    RecordingEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RememberProcessor)
};
