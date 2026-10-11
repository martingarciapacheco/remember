#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "ProjectListComponent.h"
#include "WaveformPlayerComponent.h"
#include "FoldersSidebarComponent.h"

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer
{
public:
    MainComponent();
    ~MainComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress& key) override;
    void setUiActive (bool active) { player.setUiActive (active); }

private:
    RememberUI::Style uiStyle { *this };
    void showSettingsDialog();
    void checkFirstRun();

    FoldersSidebarComponent sidebar;
    ProjectListComponent    list;
    WaveformPlayerComponent player;

    juce::TextButton settingsButton { "Settings" };

    static constexpr int kPlayerHeight  = 184;
    static constexpr int kSidebarWidth  = 216;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
