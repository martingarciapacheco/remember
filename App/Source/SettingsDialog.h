#pragma once
#include <JuceHeader.h>

// Modal settings panel. Lets the user pick:
//   - previewsFolder       (where the plugin writes recordings)
//   - projectsRootFolders  (multiple search roots for DAW projects)
//
// Both values are persisted to config.json. Existing per-entry projectPath
// values in metadata.json are unaffected by changes here.
class SettingsDialog : public juce::Component
{
public:
    explicit SettingsDialog (bool firstRun);

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Called after the user presses OK (and config has been written).
    std::function<void()> onCompleted;

    // Launches this dialog as a modal DialogWindow.
    static void launch (bool firstRun, std::function<void()> onDone);

private:
    void pickPreviews();
    void addProjectFolder();
    void commit();
    void refreshLabels();
    void rebuildFoldersList();

    bool firstRun { false };

    juce::Label  header;
    juce::Label  previewsCaption { {}, "Previews folder" };
    juce::Label  previewsHint    { {}, "Where the plugin saves recordings" };
    juce::Label  previewsPath;
    juce::TextButton previewsBrowse { "Change..." };

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

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
};
