#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "ProjectScanner.h"

// Modal settings panel. Lets the user pick:
//   - previewsFolder       (where the plugin writes recordings)
//   - projectsRootFolders  (multiple search roots for DAW projects)
//
// Both values are persisted to config.json. Existing per-entry projectPath
// values in metadata.json are unaffected by changes here.
class SettingsDialog : public juce::Component
{
public:
    explicit SettingsDialog (bool firstRun, std::function<ProjectScanner::SettingsSnapshot()> reader = {});
    bool hasLoadedSettings() const { return settingsLoaded; }
    ~SettingsDialog() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Called after the user presses OK (and config has been written).
    std::function<void()> onCompleted;

    // Launches this dialog as a modal DialogWindow.
    static void launch (bool firstRun, std::function<void()> onDone,
                        std::function<void (const ProjectEntry&)> beforeCompaction = {});
    std::function<void (const ProjectEntry&)> onBeforeCompaction;

private:
    class SectionTabs final : public juce::TabbedButtonBar
    {
    public:
        SectionTabs() : TabbedButtonBar (juce::TabbedButtonBar::TabsAtTop) {}
        std::function<void()> changed;
        void currentTabChanged (int, const juce::String&) override { if (changed) changed(); }
    };
    SectionTabs sections;
    RememberUI::Style uiStyle { *this };
    void pickPreviews();
    void addProjectFolder();
    void searchProjectFolders();
    void commit();
    void refreshLabels();
    void rebuildFoldersList();
    void updateSizeEstimate();
    void showStorageInfo();
    juce::TextButton storageInfo;
    juce::Component::SafePointer<juce::CallOutBox> storageInfoPopup;
    void updateStorageUsage();
    void compactLargePreviews();
    juce::TextButton compactBtn { "Compact previews above" };
    juce::TextEditor compactLimit;
    juce::Label compactUnits { {}, "MB" };
    bool compacting = false;
    void loadSettings (std::function<ProjectScanner::SettingsSnapshot()> reader);
    void saveAppearance();
    std::shared_ptr<std::atomic<uint64_t>> storageGeneration { std::make_shared<std::atomic<uint64_t>> (0) };
    std::shared_ptr<std::atomic<uint64_t>> validationGeneration { std::make_shared<std::atomic<uint64_t>> (0) };
    juce::File measuredFolder;
    bool settingsLoaded = false;
    juce::String rebuildFingerprint;
    juce::Label settingsProblem;
    juce::TextButton retrySettings { "Reload settings" };

    bool firstRun { false };
    bool appearanceSaving = false;
    bool zoomSaving = false;
    void saveZoom();
    juce::Label zoomCaption { {}, "Interface zoom" };
    juce::Label zoomHint { {}, "Text and controls in the app. Saved immediately." };
    juce::ComboBox zoomCombo;
    juce::Label midiCaption { {}, "MIDI audition" };
    juce::Label midiHint { {}, "Default instrument for Used sounds" };
    juce::ComboBox midiVoiceCombo;
    juce::Label appearanceCaption { {}, "Appearance" };
    juce::Label appearanceHint { {}, "System follows light and dark automatically" };
    juce::ComboBox appearanceCombo;

    juce::Label  header;
    juce::Label  signature { {}, "Created by WABII" };
    juce::Label  previewsCaption { {}, "Previews folder" };
    juce::Label  previewsHint    { {}, "Where the plugin saves recordings" };
    juce::Label  previewsPath;
    juce::TextButton previewsBrowse { "Change..." };

    // Recording settings block
    juce::Label       recordCaption { {}, "Recording" };
    juce::Label       recordHint    { {}, "Manual mode defaults to new instances; lock/replacement apply live" };
    juce::Label       durationLbl   { {}, "Duration" };
    juce::Label       qualityLbl    { {}, "Quality" };
    juce::ComboBox    durationCombo;
    juce::ComboBox    qualityCombo;
    juce::ToggleButton monoToggle   { "Record in mono (smaller file)" };
    juce::ToggleButton manualToggle { "Capture from any position by default in new plugins" }, lockToggle { "Lock preview updates in all plugins" }, replaceToggle { "Replace whole previews instead of keeping the old tail" };
    juce::Label       sizeEstimate;
    juce::Label       storageUsage;
    juce::Label       pcmPolicy;

    juce::Label  projectsCaption { {}, "DAW project search folders" };
    juce::Label  projectsHint    { {}, "Add every folder where you keep your DAW project files" };

    // List of configured project root folders (each row has a delete button)
    class FoldersListModel;
    std::unique_ptr<FoldersListModel> foldersModel;
    RememberUI::RecordingListBox foldersList;
    juce::TextButton addProjectsBtn { "+ Add folder" };
    juce::TextButton refreshProjectsBtn { "Search folders" };
    juce::Label searchStatus;
    std::shared_ptr<std::atomic<uint64_t>> searchGeneration { std::make_shared<std::atomic<uint64_t>> (0) };

    juce::TextButton okBtn     { "OK"     };
    juce::TextButton cancelBtn { "Cancel" };

    juce::File              previewsFolder;
    juce::Array<juce::File> projectsFolders;

    // Local copies of the recording settings; persisted on commit()
    int  recordSeconds { 60 };
    int  oggQuality    { 4 };
    bool recordMono    { false };

    std::unique_ptr<juce::FileChooser> chooser;

    juce::SharedResourcePointer<RememberUI::TooltipWindow> tooltips;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
};
