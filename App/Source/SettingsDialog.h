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
    class SectionTabs final : public RememberUI::EqualWidthTabs
    {
    public:
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
    RememberUI::TextButton storageInfo;
    juce::Component::SafePointer<juce::CallOutBox> storageInfoPopup;
    void updateStorageUsage();
    void compactLargePreviews();
    RememberUI::TextButton compactBtn { RememberUI::tr ("Compact previews above") };
    RememberUI::TextEditor compactLimit;
    RememberUI::Label compactUnits { {}, "MB" };
    bool compacting = false;
    void loadSettings (std::function<ProjectScanner::SettingsSnapshot()> reader);
    void saveAppearance();
    void saveLanguage();
    void refreshLanguage();
    bool languageSaving = false;
    RememberUI::Label languageCaption { {}, RememberUI::tr ("Language") };
    RememberUI::Label languageHint { {}, RememberUI::tr ("Shared by the app and all plugins. Saved immediately.") };
    RememberUI::ComboBox languageCombo;
    std::shared_ptr<std::atomic<uint64_t>> storageGeneration { std::make_shared<std::atomic<uint64_t>> (0) };
    std::shared_ptr<std::atomic<uint64_t>> validationGeneration { std::make_shared<std::atomic<uint64_t>> (0) };
    juce::File measuredFolder;
    std::function<void()> refreshStorageText;
    bool settingsLoaded = false;
    juce::String rebuildFingerprint;
    RememberUI::Label settingsProblem;
    RememberUI::TextButton retrySettings { RememberUI::tr ("Reload settings") };

    bool firstRun { false };
    bool appearanceSaving = false;
    bool zoomSaving = false;
    void saveZoom();
    RememberUI::Label zoomCaption { {}, RememberUI::tr ("Interface zoom") };
    RememberUI::Label zoomHint { {}, RememberUI::tr ("Text and controls in the app. Saved immediately.") };
    RememberUI::ComboBox zoomCombo;
    RememberUI::Label midiCaption { {}, RememberUI::tr ("MIDI audition") };
    RememberUI::Label midiHint { {}, RememberUI::tr ("Default instrument for Used sounds") };
    RememberUI::ComboBox midiVoiceCombo;
    RememberUI::Label offlineCaption { {}, RememberUI::tr ("Default offline rendering DAW") };
    RememberUI::Label offlineHint { {}, RememberUI::tr ("Used when opening Offline rendering. Saved with OK.") };
    RememberUI::ComboBox offlineDawCombo;
    RememberUI::Label appearanceCaption { {}, RememberUI::tr ("Appearance") };
    RememberUI::Label appearanceHint { {}, RememberUI::tr ("System follows light and dark automatically") };
    RememberUI::ComboBox appearanceCombo;

    RememberUI::Label  header;
    RememberUI::Label  signature { {}, RememberUI::tr ("Created by WABII") };
    RememberUI::Label  previewsCaption { {}, RememberUI::tr ("Previews folder") };
    RememberUI::Label  previewsHint    { {}, RememberUI::tr ("Where the plugin saves recordings") };
    RememberUI::FolderLink previewsPath;
    RememberUI::TextButton previewsBrowse { RememberUI::tr ("Change...") };

    // Recording settings block
    RememberUI::Label       recordCaption { {}, RememberUI::tr ("Recording") };
    RememberUI::Label       recordHint    { {}, RememberUI::tr ("Manual mode defaults to new instances; lock/replacement apply live") };
    RememberUI::Label       durationLbl   { {}, RememberUI::tr ("Duration") };
    RememberUI::Label       qualityLbl    { {}, RememberUI::tr ("Quality") };
    RememberUI::ComboBox    durationCombo;
    RememberUI::ComboBox    qualityCombo;
    RememberUI::ToggleButton monoToggle   { RememberUI::tr ("Record in mono (smaller file)") };
    RememberUI::ToggleButton manualToggle { RememberUI::tr ("Capture from any position by default in new plugins") }, lockToggle { RememberUI::tr ("Lock preview updates in all plugins") }, replaceToggle { RememberUI::tr ("Replace whole previews instead of keeping the old tail") };
    RememberUI::Label       sizeEstimate;
    RememberUI::Label       storageUsage;
    RememberUI::Label       pcmPolicy;

    RememberUI::Label  projectsCaption { {}, RememberUI::tr ("DAW project search folders") };
    RememberUI::Label  projectsHint    { {}, RememberUI::tr ("Add every folder where you keep your DAW project files") };

    // List of configured project root folders (each row has a delete button)
    class FoldersListModel;
    std::unique_ptr<FoldersListModel> foldersModel;
    RememberUI::RecordingListBox foldersList;
    RememberUI::TextButton addProjectsBtn { RememberUI::tr ("+ Add folder") };
    RememberUI::TextButton refreshProjectsBtn { RememberUI::tr ("Search folders") };
    RememberUI::Label searchStatus;
    std::shared_ptr<std::atomic<uint64_t>> searchGeneration { std::make_shared<std::atomic<uint64_t>> (0) };

    RememberUI::TextButton okBtn     { RememberUI::tr ("OK")     };
    RememberUI::TextButton cancelBtn { RememberUI::tr ("Cancel") };

    juce::File              previewsFolder;
    juce::Array<juce::File> projectsFolders;

    // Local copies of the recording settings; persisted on commit()
    int  recordSeconds { 60 };
    int  oggQuality    { 4 };
    bool recordMono    { false };

    std::unique_ptr<juce::FileChooser> chooser;

    juce::SharedResourcePointer<RememberUI::TooltipWindow> tooltips;
    RememberUI::LanguageCallback languageUpdates { [this] { refreshLanguage(); } };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
};
