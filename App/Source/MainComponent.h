#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "ProjectListComponent.h"
#include "WaveformPlayerComponent.h"
#include "FoldersSidebarComponent.h"
#include "DawLauncher.h"

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override { list.removeSearchKeyListener (this); }

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress& key) override;
    void setUiActive (bool active) { player.setUiActive (active); }
    void taskbarPrevious() { player.previous(); }
    void taskbarTogglePlay() { player.togglePlay(); }
    void taskbarNext() { player.next(); }
    bool taskbarPlaying() const { return player.isPlaying(); }
    bool taskbarCanPlay() const { return player.canTogglePlayback(); }
    bool taskbarHasQueue() const { return ! list.visibleEntries().isEmpty(); }

private:
    bool libraryShortcut (const juce::KeyPress& key);
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override { return libraryShortcut (key); }
    RememberUI::Style uiStyle { *this };
    void showSettingsDialog();
    void checkFirstRun();

    FoldersSidebarComponent sidebar;
    ProjectListComponent    list;
    WaveformPlayerComponent player;
    DawLauncher dawLauncher;

    juce::TextButton settingsButton { "Settings" };

    static constexpr int kPlayerHeight  = 184;
    static constexpr int kSidebarWidth  = 216;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
