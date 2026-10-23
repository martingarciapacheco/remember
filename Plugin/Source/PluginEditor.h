#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
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
    // The native folder selector and other editor integrations use this same
    // asynchronous edit path; preparation applies it to future capture banks.
    void applyOutputFolder (const juce::File&);
    void applyAppearance (RememberAppearance mode);
    void applyAppearance (bool dark) { applyAppearance (dark ? RememberAppearance::Dark : RememberAppearance::Light); }
    void applyZoom (int percent);
    void applyLanguage (RememberUI::Language);

private:
    RememberUI::Style uiStyle { *this };
    int zoomPercent = 100;
    float zoomScale() const { return zoomPercent / 100.0f; }
    int logicalWidth() const { return juce::roundToInt (getWidth() / zoomScale()); }
    void showSettingsPanel();
    void chooseOutputFolder();
    void showAssociationMenu();
    void chooseProjectFile (const juce::String& expectedId);
    void refreshSettingsRequest();
    void refreshFromEngine();
    void paintProgressBar (juce::Graphics& g, juce::Rectangle<int> bounds, float progress);

    RememberProcessor& processor;

    RememberUI::TextEditor  nameEditor;
    RememberUI::Label       bpmKeyLabel;   // "128.0 BPM  •  C major"
    RememberUI::Label       statusLabel;
    RememberUI::Label       versionLabel;  // "v1.0.0-beta1 a1b2c3d" in the bottom-right
    RememberUI::TextButton  settingsButton { "..." };
    RememberUI::Label       associationLabel;
    RememberUI::ToggleButton manualToggle { RememberUI::tr ("Capture from any position") }, lockToggle { RememberUI::tr ("Lock preview updates") }, replaceToggle { RememberUI::tr ("Replace the whole preview") };
    RememberUI::Label captureStatus;
    void saveCaptureOptions();
    RememberUI::TextButton  associationButton { RememberUI::tr ("Associate") };

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::shared_ptr<SettingsManager::Request> settingsRequest;
    juce::File settingsFolder;
    juce::String settingsMessage;
    bool initialSettings = true, savingSettings = false, settingsFailed = false, editorReady = false;
    float  recordProgress { 0.0f };
    bool   isRecording    { false };

    // Cached last-applied values so timerCallback only updates the UI on change
    int          lastStateInt    { -1 };
    float        lastProgress    { -1.0f };
    juce::String lastBpmKeyText;
    juce::String lastStatusText;

    static constexpr uint32_t kBgDeep = Pal::BgDeep, kSurface = Pal::Surface,
        kElevated = Pal::Elevated, kBorder = Pal::Border, kAccent = Pal::Accent,
        kText = Pal::Text, kSubtext = Pal::Subtext, kSuccess = Pal::Success, kError = Pal::Error;

    RememberUI::TooltipWindow tooltips { this }; // parented to the editor for host scaling/lifetime
    RememberUI::LanguageCallback languageUpdates { [this] { resized(); lastStateInt = -1; refreshFromEngine(); repaint(); } };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RememberEditor)
};
