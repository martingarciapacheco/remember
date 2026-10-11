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
    static void launch (bool firstRun, std::function<void()> onDone);

private:
    RememberUI::Style uiStyle { *this };
    void pickPreviews();
    void addProjectFolder();
    void commit();
    void refreshLabels();
    void rebuildFoldersList();
    void updateSizeEstimate();
    void updateStorageUsage();
    void loadSettings (std::function<ProjectScanner::SettingsSnapshot()> reader);
    std::shared_ptr<std::atomic<uint64_t>> storageGeneration { std::make_shared<std::atomic<uint64_t>> (0) };
    std::shared_ptr<std::atomic<uint64_t>> validationGeneration { std::make_shared<std::atomic<uint64_t>> (0) };
    juce::File measuredFolder;
    bool settingsLoaded = false;
    juce::String rebuildFingerprint;
    juce::Label settingsProblem;
    juce::TextButton retrySettings { "Reload settings" };

    bool firstRun { false };

    juce::Label  header;
    juce::Label  previewsCaption { {}, "Previews folder" };
    juce::Label  previewsHint    { {}, "Where the plugin saves recordings" };
    juce::Label  previewsPath;
    juce::TextButton previewsBrowse { "Change..." };

    // Recording settings block
    juce::Label       recordCaption { {}, "Recording" };
    juce::Label       recordHint    { {}, "Apply on preparation/reload; queued recordings keep their previous settings" };
    juce::Label       durationLbl   { {}, "Duration" };
    juce::Label       qualityLbl    { {}, "Quality" };
    juce::ComboBox    durationCombo;
    juce::ComboBox    qualityCombo;
    juce::ToggleButton monoToggle   { "Record in mono (smaller file)" };
    juce::Label       sizeEstimate;
    juce::Label       storageUsage;
    juce::Label       pcmPolicy { {}, "Float PCM + OGG: current and previous revisions kept; older revisions cleaned automatically" };

    juce::Label  projectsCaption { {}, "DAW project search folders" };
    juce::Label  projectsHint    { {}, "Add every folder where you keep your DAW project files" };

    // List of configured project root folders (each row has a delete button)
    class FoldersListModel;
    std::unique_ptr<FoldersListModel> foldersModel;
    juce::ListBox    foldersList;
    juce::TextButton addProjectsBtn { "+ Add folder" };

    juce::TextButton okBtn     { "OK"     };
    juce::TextButton cancelBtn { "Cancel" };

    juce::File              previewsFolder;
    juce::Array<juce::File> projectsFolders;

    // Local copies of the recording settings; persisted on commit()
    int  recordSeconds { 60 };
    int  oggQuality    { 4 };
    bool recordMono    { false };

    std::unique_ptr<juce::FileChooser> chooser;

    juce::SharedResourcePointer<juce::TooltipWindow> tooltips;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
};
