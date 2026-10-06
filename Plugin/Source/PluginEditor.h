#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class RememberEditor : public juce::AudioProcessorEditor,
                       public juce::Timer
{
public:
    explicit RememberEditor (RememberProcessor& p);
    ~RememberEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    void showSettingsPanel();
    void refreshFromEngine();
    void paintProgressBar (juce::Graphics& g, juce::Rectangle<int> bounds, float progress);

    RememberProcessor& processor;

    juce::TextEditor  nameEditor;
    juce::Label       bpmKeyLabel;   // "128.0 BPM  •  C major"
    juce::Label       statusLabel;
    juce::Label       versionLabel;  // "v1.0.0-beta1 a1b2c3d" in the bottom-right
    juce::TextButton  settingsButton { "..." };

    std::unique_ptr<juce::FileChooser> fileChooser;
    float  recordProgress { 0.0f };
    bool   isRecording    { false };

    // Cached last-applied values so timerCallback only updates the UI on change
    int          lastStateInt    { -1 };
    float        lastProgress    { -1.0f };
    juce::String lastBpmKeyText;
    juce::String lastStatusText;

    // Palette — pure black/dark gray with purple accent
    static constexpr uint32_t kBgDeep   = 0xff0a0a0a;
    static constexpr uint32_t kSurface  = 0xff141414;
    static constexpr uint32_t kElevated = 0xff222222;
    static constexpr uint32_t kBorder   = 0xff2a2a2a;
    static constexpr uint32_t kAccent   = 0xff8b5cf6;
    static constexpr uint32_t kText     = 0xfff5f5f5;
    static constexpr uint32_t kSubtext  = 0xff8a8a8a;
    static constexpr uint32_t kSuccess  = 0xff34d399;
    static constexpr uint32_t kError    = 0xffef4444;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RememberEditor)
};
