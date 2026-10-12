#include "SettingsDialog.h"
#include "ProjectScanner.h"
#include "ProjectListComponent.h"   // for Pal::
#include "../../Shared/LibraryStore.h"
#include "../../Shared/AppearanceSettings.h"

// ── ListBoxModel for the project folders list ────────────────────────────────
class SettingsDialog::FoldersListModel : public juce::ListBoxModel
{
public:
    class FolderRow final : public juce::Component
    {
    public:
        FolderRow()
        {
            setInterceptsMouseClicks (false, true);
            remove.setTitle ("Remove search folder");
            remove.setTooltip ("Remove search folder");
            remove.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            RememberUI::bindColour (remove, juce::TextButton::textColourOffId, Pal::Subtext);
            remove.setWantsKeyboardFocus (false);
            addAndMakeVisible (remove);
        }
        void resized() override { remove.setBounds (getLocalBounds().removeFromRight (32)); }
        juce::TextButton remove { "X" };
    };

    explicit FoldersListModel (SettingsDialog& o) : owner (o) {}

    int getNumRows() override { return owner.projectsFolders.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (row < 0 || row >= owner.projectsFolders.size()) return;
        g.fillAll (RememberUI::colour (sel ? Pal::Elevated : Pal::BgDeep));
        g.setColour (RememberUI::colour (Pal::Border));
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);

        g.setColour (RememberUI::colour (Pal::Text));
        g.setFont (RememberUI::font (12.0f));
        // Leave room on the right for the per-row remove button (32px)
        g.drawText (owner.projectsFolders.getReference (row).getFullPathName(),
                    8, 0, w - 40, h, juce::Justification::centredLeft, true);
    }

    juce::Component* refreshComponentForRow (int row, bool /*sel*/,
                                             juce::Component* existing) override
    {
        auto* component = dynamic_cast<FolderRow*> (existing);
        if (component == nullptr) component = new FolderRow();

        if (row < 0 || row >= owner.projectsFolders.size())
        {
            component->setVisible (false);
            return component;
        }

        component->setVisible (true);
        component->remove.onClick = [this, row]
        {
            if (row >= 0 && row < owner.projectsFolders.size())
            {
                owner.projectsFolders.remove (row);
                owner.rebuildFoldersList();
                owner.refreshLabels();
            }
        };
        return component;
    }

private:
    SettingsDialog& owner;
};

// ── SettingsDialog ────────────────────────────────────────────────────────────

// Approximate Vorbis bitrate (kbps) for each quality index 0..10. Used only
// for the file-size estimate shown in the settings UI — the actual encoded
// size depends on the content.
static const int kVorbisBitrate[11] = { 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 500 };

// Vorbis at the same quality compresses mono to ~55-65% of stereo; use 0.6
// as the rule-of-thumb factor for the on-screen estimate.
static constexpr double kMonoSizeFactor = 0.6;

SettingsDialog::SettingsDialog (bool firstRunFlag, std::function<ProjectScanner::SettingsSnapshot()> reader) : firstRun (firstRunFlag)
{
    setSize (680, 760);
    for (auto* toggle : { &manualToggle, &lockToggle, &replaceToggle })
    { toggle->setTitle (toggle->getButtonText()); addAndMakeVisible (*toggle); }
    manualToggle.setTooltip ("Default for new Remember instances. Existing instances and saved DAW projects keep their own setting.");

    auto styleLabel = [] (juce::Label& l, float h, uint32_t col, bool bold = false)
    {
        l.setFont (RememberUI::font (h, bold));
        RememberUI::bindColour (l, juce::Label::textColourId, col);
    };

    header.setText (firstRun ? "Welcome to Remember" : "Settings",
                    juce::dontSendNotification);
    styleLabel (header, 22.0f, Pal::Text, true);
    addAndMakeVisible (header);

    styleLabel (appearanceCaption, 13.0f, Pal::Text, true);
    styleLabel (appearanceHint, 11.0f, Pal::Subtext);
    appearanceCombo.setTitle ("Appearance");
    appearanceCombo.addItem ("Light", 1);
    appearanceCombo.addItem ("Dark", 2);
    appearanceCombo.setSelectedId (RememberUI::isDark() ? 2 : 1, juce::dontSendNotification);
    appearanceCombo.onChange = [this] { saveAppearance(); };
    addAndMakeVisible (appearanceCaption);
    addAndMakeVisible (appearanceHint);
    addAndMakeVisible (appearanceCombo);

    styleLabel (previewsCaption, 13.0f, Pal::Text, true);
    styleLabel (previewsHint,    11.0f, Pal::Subtext);
    styleLabel (previewsPath,    12.0f, Pal::Accent);
    addAndMakeVisible (previewsCaption);
    addAndMakeVisible (previewsHint);
    addAndMakeVisible (previewsPath);

    // Recording settings ──────────────────────────────────────────────────────
    styleLabel (recordCaption, 13.0f, Pal::Text, true);
    styleLabel (recordHint,    11.0f, Pal::Subtext);
    styleLabel (durationLbl,   12.0f, Pal::Subtext);
    styleLabel (qualityLbl,    12.0f, Pal::Subtext);
    styleLabel (sizeEstimate,  11.5f, Pal::Accent);
    styleLabel (storageUsage,  11.0f, Pal::Subtext);
    styleLabel (pcmPolicy, 11.0f, Pal::Subtext);
    addAndMakeVisible (recordCaption);
    addAndMakeVisible (recordHint);
    addAndMakeVisible (durationLbl);
    addAndMakeVisible (qualityLbl);
    addAndMakeVisible (sizeEstimate);
    addAndMakeVisible (storageUsage);
    addAndMakeVisible (pcmPolicy);

    // Duration presets
    durationCombo.addItem ("15 s",  15);
    durationCombo.addItem ("30 s",  30);
    durationCombo.addItem ("60 s",  60);
    durationCombo.addItem ("90 s",  90);
    durationCombo.addItem ("120 s", 120);
    int initialDur = (recordSeconds == 15 || recordSeconds == 30
                      || recordSeconds == 90 || recordSeconds == 120)
                        ? recordSeconds : 60;
    recordSeconds = initialDur;   // keep state in sync if config had an unsupported value
    durationCombo.setSelectedId (initialDur, juce::dontSendNotification);
    durationCombo.onChange = [this]
    {
        recordSeconds = durationCombo.getSelectedId();
        updateSizeEstimate();
    };

    // Quality presets: Low=2 / Med=4 / High=6 / Max=8.
    // ComboBox IDs encode the quality index directly (shifted by +1 because
    // ComboBox forbids id == 0).
    auto kbpsStr = [] (int q) { return " (~" + juce::String (kVorbisBitrate[q]) + " kbps)"; };
    qualityCombo.addItem ("Low"  + kbpsStr (2), 2 + 1);
    qualityCombo.addItem ("Med"  + kbpsStr (4), 4 + 1);
    qualityCombo.addItem ("High" + kbpsStr (6), 6 + 1);
    qualityCombo.addItem ("Max"  + kbpsStr (8), 8 + 1);
    int initialQ = (oggQuality == 2 || oggQuality == 6 || oggQuality == 8) ? oggQuality : 4;
    oggQuality = initialQ;   // keep state in sync if config had an unsupported value
    qualityCombo.setSelectedId (initialQ + 1, juce::dontSendNotification);
    qualityCombo.onChange = [this]
    {
        oggQuality = qualityCombo.getSelectedId() - 1;
        updateSizeEstimate();
    };

    RememberUI::bindColour (monoToggle, juce::ToggleButton::textColourId, Pal::Subtext);
    monoToggle.setToggleState (recordMono, juce::dontSendNotification);
    monoToggle.onClick = [this]
    {
        recordMono = monoToggle.getToggleState();
        updateSizeEstimate();
    };
    addAndMakeVisible (durationCombo);
    addAndMakeVisible (qualityCombo);
    addAndMakeVisible (monoToggle);

    updateSizeEstimate();

    // Project folders ────────────────────────────────────────────────────────
    styleLabel (projectsCaption, 13.0f, Pal::Text, true);
    styleLabel (projectsHint,    11.0f, Pal::Subtext);
    addAndMakeVisible (projectsCaption);
    addAndMakeVisible (projectsHint);

    foldersModel = std::make_unique<FoldersListModel> (*this);
    foldersList.setModel (foldersModel.get());
    foldersList.setRowHeight (32);
    RememberUI::bindColour (foldersList, juce::ListBox::backgroundColourId, Pal::Elevated);
    foldersList.setOutlineThickness (0);
    addAndMakeVisible (foldersList);

    auto styleBtn = [] (juce::TextButton& b, uint32_t bg, uint32_t fg)
    {
        RememberUI::bindColour (b, juce::TextButton::buttonColourId, bg);
        RememberUI::bindColour (b, juce::TextButton::textColourOffId, fg);
    };

    styleBtn (previewsBrowse,  Pal::Elevated, Pal::Text);
    styleBtn (addProjectsBtn,  Pal::Elevated, Pal::Text);
    styleBtn (okBtn, Pal::Action,   Pal::OnAction);
    styleBtn (cancelBtn,       Pal::Elevated, Pal::Subtext);
    styleBtn (retrySettings,   Pal::Elevated, Pal::Text);
    styleLabel (settingsProblem, 12.0f, Pal::Subtext);
    settingsProblem.setJustificationType (juce::Justification::topLeft);
    settingsProblem.setMinimumHorizontalScale (1.0f);
    addChildComponent (settingsProblem);
    addChildComponent (retrySettings);
    retrySettings.onClick = [this] { loadSettings ({}); };

    previewsBrowse.onClick = [this] { pickPreviews(); };
    addProjectsBtn.onClick = [this] { addProjectFolder(); };
    okBtn         .onClick = [this] { commit(); };
    cancelBtn     .onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    addAndMakeVisible (previewsBrowse);
    addAndMakeVisible (addProjectsBtn);
    addAndMakeVisible (okBtn);
    if (firstRun)
        cancelBtn.setVisible (false);   // explicit so resized() skips it
    else
        addAndMakeVisible (cancelBtn);

    for (auto* child : getChildren()) if (child != &cancelBtn) child->setEnabled (false);
    previewsPath.setText ("Loading settings...", juce::dontSendNotification);
    loadSettings (std::move (reader));
}

SettingsDialog::~SettingsDialog() { storageGeneration->fetch_add (1); validationGeneration->fetch_add (1); }

void SettingsDialog::saveAppearance()
{
    if (! settingsLoaded || appearanceSaving) return;
    const bool dark = appearanceCombo.getSelectedId() == 2;
    appearanceSaving = true;
    appearanceCombo.setEnabled (false);
    okBtn.setEnabled (false);
    ProjectScanner::editAsync ([dark] { return AppearanceSettings::save (dark); },
        [safe = juce::Component::SafePointer<SettingsDialog> (this), dark] (juce::Result result)
        {
            if (result.wasOk()) RememberUI::setDark (dark);
            if (safe == nullptr) return;
            safe->appearanceSaving = false;
            safe->appearanceCombo.setEnabled (true);
            safe->appearanceCombo.setSelectedId (RememberUI::isDark() ? 2 : 1, juce::dontSendNotification);
            safe->refreshLabels();
            if (result.failed())
            {
                safe->settingsProblem.setText ("Cannot save appearance: " + result.getErrorMessage(), juce::dontSendNotification);
                safe->settingsProblem.setVisible (true);
                safe->retrySettings.setVisible (true);
                safe->resized();
            }
        });
}

void SettingsDialog::loadSettings (std::function<ProjectScanner::SettingsSnapshot()> reader)
{
    settingsLoaded = false;
    validationGeneration->fetch_add (1); storageGeneration->fetch_add (1); measuredFolder = {};
    for (auto* child : getChildren()) if (child != &cancelBtn) child->setEnabled (false);
    previewsPath.setText ("Loading settings...", juce::dontSendNotification);
    auto settings = std::make_shared<ProjectScanner::SettingsSnapshot>();
    ProjectScanner::queryAsync ([settings, reader = std::move (reader)]
    { *settings = reader ? reader() : ProjectScanner::readSettings(); return settings->result; },
        [safe = juce::Component::SafePointer<SettingsDialog> (this), settings] (juce::Result result)
        {
            if (safe == nullptr) return;
            safe->rebuildFingerprint = result.failed() && settings->canRebuild ? settings->fingerprint : juce::String{};
            const auto problem = result.failed() ? result.getErrorMessage()
                + (settings->canRebuild ? "\nReview the folders and recording options. Rebuild settings saves an original copy beside config.json."
                                        : "\nRestore access, then Reload settings. The file will be preserved.") : juce::String{};
            safe->settingsProblem.setText (problem, juce::dontSendNotification);
            safe->settingsProblem.setTooltip (problem); safe->settingsProblem.setVisible (problem.isNotEmpty());
            safe->settingsProblem.setEnabled (true);
            safe->retrySettings.setVisible (result.failed()); safe->retrySettings.setEnabled (true);
            safe->cancelBtn.setEnabled (true);
            safe->okBtn.setButtonText (safe->rebuildFingerprint.isNotEmpty() ? "Rebuild settings" : "OK");
            safe->resized();
            if (result.failed() && ! settings->canRebuild)
            { safe->previewsPath.setText ("Settings could not be read", juce::dontSendNotification); return; }
            safe->previewsFolder = settings->previews;
            safe->projectsFolders = settings->roots;
            safe->recordSeconds = settings->seconds;
            safe->oggQuality = settings->quality;
            safe->recordMono = settings->mono;
            // JUCE Button::setToggleState ignores changes while disabled.
            for (auto* child : safe->getChildren()) child->setEnabled (true);
            if (result.wasOk()) RememberUI::setDark (settings->darkAppearance);
            safe->appearanceCombo.setSelectedId (RememberUI::isDark() ? 2 : 1, juce::dontSendNotification);
            safe->appearanceCombo.setEnabled (result.wasOk());
            // Preserve all supported config values, rather than silently
            // overwriting a non-preset value when saving an unrelated setting.
            if (safe->durationCombo.indexOfItemId (settings->seconds) < 0)
                safe->durationCombo.addItem (juce::String (settings->seconds) + " s", settings->seconds);
            if (safe->qualityCombo.indexOfItemId (settings->quality + 1) < 0)
                safe->qualityCombo.addItem ("Quality " + juce::String (settings->quality), settings->quality + 1);
            safe->durationCombo.setSelectedId (settings->seconds, juce::dontSendNotification);
            safe->qualityCombo.setSelectedId (settings->quality + 1, juce::dontSendNotification);
            safe->monoToggle.setToggleState (settings->mono, juce::dontSendNotification);
            safe->manualToggle.setToggleState (settings->capture.manual, juce::dontSendNotification);
            safe->lockToggle.setToggleState (settings->capture.locked, juce::dontSendNotification);
            safe->replaceToggle.setToggleState (settings->capture.replace, juce::dontSendNotification);
            safe->settingsLoaded = true;
            safe->rebuildFoldersList();
            safe->updateSizeEstimate();
            safe->refreshLabels();
            safe->updateStorageUsage();
        });
}

void SettingsDialog::refreshLabels()
{
    previewsPath.setText (previewsFolder.getFullPathName().isEmpty()
                             ? "(not set)" : previewsFolder.getFullPathName(),
                          juce::dontSendNotification);

    okBtn.setEnabled (false);
    if (! settingsLoaded) return;
    const auto token = validationGeneration;
    const auto generation = token->fetch_add (1) + 1;
    auto valid = std::make_shared<bool> (false);
    ProjectScanner::queryAsync ([previews = previewsFolder, roots = projectsFolders, token, generation, valid]
    {
        if (token->load() != generation) return juce::Result::ok();
        bool anyProjectsValid = false;
        for (const auto& root : roots)
        {
            if (token->load() != generation) return juce::Result::ok();
            if (root.isDirectory()) { anyProjectsValid = true; break; }
        }
        *valid = anyProjectsValid && previews.isDirectory();
        return juce::Result::ok();
    }, [safe = juce::Component::SafePointer<SettingsDialog> (this), token, generation, valid] (juce::Result result)
    {
        if (safe != nullptr && token->load() == generation) safe->okBtn.setEnabled (result.wasOk() && *valid && ! safe->appearanceSaving);
    });
}

void SettingsDialog::rebuildFoldersList()
{
    foldersList.updateContent();
    foldersList.repaint();
}

void SettingsDialog::updateSizeEstimate()
{
    int q = juce::jlimit (0, 10, oggQuality);
    double bytes = (double) kVorbisBitrate[q] * 1000.0 * (double) recordSeconds
                   * (recordMono ? kMonoSizeFactor : 1.0) / 8.0;

    const auto pcm = 48000.0 * recordSeconds * (recordMono ? 1 : 2) * sizeof (float);
    sizeEstimate.setText ("At 48 kHz: float WAV " + juce::File::descriptionOfSizeInBytes ((juce::int64) pcm)
        + " + OGG ~" + juce::File::descriptionOfSizeInBytes ((juce::int64) bytes)
        + " per revision (WAV x4 at 192 kHz)", juce::dontSendNotification);
    sizeEstimate.setTooltip ("Estimated OGG size varies with content. The previous revision uses additional disk space; temporary capture WAV also uses disk while saving.");
}

void SettingsDialog::updateStorageUsage()
{
    if (previewsFolder == measuredFolder) return;
    measuredFolder = previewsFolder;
    storageUsage.setText ("Calculating library storage...", juce::dontSendNotification);
    const auto generation = storageGeneration->fetch_add (1) + 1;
    const auto token = storageGeneration;
    const auto folder = previewsFolder;
    auto usage = std::make_shared<LibraryStore::StorageUsage>();
    ProjectScanner::queryAsync ([folder, token, generation, usage]
    {
        *usage = LibraryStore::measureStorage (folder, [token, generation] { return token->load() != generation; });
        return juce::Result::ok();
    }, [safe = juce::Component::SafePointer<SettingsDialog> (this), token, generation, usage] (juce::Result)
    {
        if (safe == nullptr || token->load() != generation || usage->cancelled) return;
        const auto size = [] (juce::int64 bytes) { return juce::File::descriptionOfSizeInBytes (bytes); };
        const auto text = "Library incl. history: " + size (usage->bytes) + " (PCM " + size (usage->pcmBytes)
            + ", OGG " + size (usage->previewBytes) + ") | Free: "
            + (usage->freeBytes >= 0 ? size (usage->freeBytes) : juce::String ("unavailable"));
        safe->storageUsage.setText (text, juce::dontSendNotification);
        safe->storageUsage.setTooltip (text);
    });
}

void SettingsDialog::pickPreviews()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Select previews folder (where recordings are saved)",
        previewsFolder.getFullPathName().isNotEmpty()
            ? previewsFolder
            : juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<SettingsDialog> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            if (! fc.getResults().isEmpty())
                safe->previewsFolder = fc.getResult();
            safe->refreshLabels();
            safe->updateStorageUsage();
        });
}

void SettingsDialog::addProjectFolder()
{
    auto start = projectsFolders.isEmpty()
                     ? juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                     : projectsFolders.getReference (0);
    chooser = std::make_unique<juce::FileChooser> (
        "Add a folder to search for DAW projects",
        start);

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<SettingsDialog> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            if (fc.getResults().isEmpty()) return;
            auto picked = fc.getResult();
            // Skip duplicates
            for (auto& f : safe->projectsFolders)
                if (f == picked) return;
            safe->projectsFolders.add (picked);
            safe->rebuildFoldersList();
            safe->refreshLabels();
        });
}

void SettingsDialog::commit()
{
    if (appearanceSaving) return;
    validationGeneration->fetch_add (1);
    for (auto* child : getChildren()) child->setEnabled (false);
    auto originalBackup = std::make_shared<juce::File>();
    ProjectScanner::editAsync ([previews = previewsFolder, roots = projectsFolders, fingerprint = rebuildFingerprint, originalBackup,
                               seconds = recordSeconds, quality = oggQuality, mono = recordMono,
                               capture = CaptureOptions { manualToggle.getToggleState(), lockToggle.getToggleState(), replaceToggle.getToggleState() }]
    {
        bool validRoot = false;
        for (const auto& root : roots) if (root.isDirectory()) { validRoot = true; break; }
        if (! previews.isDirectory() || ! validRoot)
            return juce::Result::fail ("Select an existing previews folder and at least one accessible project search folder");
        return fingerprint.isEmpty() ? ProjectScanner::saveSettings (previews, roots, seconds, quality, mono, &capture)
            : ProjectScanner::rebuildSettings (previews, roots, seconds, quality, mono, fingerprint, *originalBackup, &capture);
    }, [safe = juce::Component::SafePointer<SettingsDialog> (this), originalBackup] (juce::Result result)
    {
        if (safe == nullptr) return;
        for (auto* child : safe->getChildren()) child->setEnabled (true);
        if (result.failed())
        {
            safe->settingsProblem.setText (result.getErrorMessage(), juce::dontSendNotification);
            safe->settingsProblem.setTooltip (result.getErrorMessage()); safe->settingsProblem.setVisible (true);
            safe->retrySettings.setVisible (true); safe->resized();
            safe->refreshLabels();
            return;
        }
        if (*originalBackup != juce::File{})
        {
            const auto message = "Settings rebuilt. Original copy: " + originalBackup->getFullPathName();
            safe->settingsProblem.setText (message, juce::dontSendNotification);
            safe->settingsProblem.setTooltip (message); safe->settingsProblem.setVisible (true);
            safe->rebuildFingerprint.clear(); safe->okBtn.setButtonText ("OK"); safe->resized();
        }
        auto completed = safe->onCompleted;
        if (completed) completed();
        if (safe != nullptr)
            if (auto* window = safe->findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (1);
    });
}

void SettingsDialog::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));

    // Header strip
    g.setColour (RememberUI::colour (Pal::Surface));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (RememberUI::colour (Pal::Border));
    g.fillRect (0, 48, getWidth(), 1);
}

void SettingsDialog::resized()
{
    auto area = getLocalBounds();

    auto headerArea = area.removeFromTop (48).reduced (16, 0);
    header.setBounds (headerArea);

    area.removeFromTop (16);
    area = area.reduced (16, 0);

    auto appearanceRow = area.removeFromTop (32);
    appearanceCaption.setBounds (appearanceRow.removeFromLeft (110));
    appearanceCombo.setBounds (appearanceRow.removeFromLeft (150));
    appearanceRow.removeFromLeft (16);
    appearanceHint.setBounds (appearanceRow);
    area.removeFromTop (12);

    // ── Previews block ──────────────────────────────────────────────────────
    auto block1 = area.removeFromTop (78);
    previewsCaption.setBounds (block1.removeFromTop (20));
    previewsHint   .setBounds (block1.removeFromTop (16));
    auto row1 = block1.removeFromTop (28);
    previewsBrowse.setBounds (row1.removeFromRight (100));
    row1.removeFromRight (10);
    previewsPath  .setBounds (row1);

    area.removeFromTop (12);

    // ── Recording block ─────────────────────────────────────────────────────
    recordCaption.setBounds (area.removeFromTop (20));
    recordHint   .setBounds (area.removeFromTop (16));
    area.removeFromTop (4);

    // Row 1: Duration | Quality   (two equal columns)
    auto comboRow = area.removeFromTop (28);
    int colW = (comboRow.getWidth() - 12) / 2;
    auto leftCol  = comboRow.removeFromLeft (colW);
    comboRow.removeFromLeft (12);
    auto rightCol = comboRow;
    durationLbl  .setBounds (leftCol .removeFromLeft (62));
    durationCombo.setBounds (leftCol);
    qualityLbl   .setBounds (rightCol.removeFromLeft (52));
    qualityCombo .setBounds (rightCol);

    area.removeFromTop (8);

    monoToggle.setBounds (area.removeFromTop (22));
    manualToggle.setBounds (area.removeFromTop (30));
    lockToggle.setBounds (area.removeFromTop (30));
    replaceToggle.setBounds (area.removeFromTop (30));
    sizeEstimate.setBounds (area.removeFromTop (24));
    pcmPolicy.setBounds (area.removeFromTop (22));
    storageUsage.setBounds (area.removeFromTop (24));

    area.removeFromTop (12);

    // ── Project folders block ───────────────────────────────────────────────
    projectsCaption.setBounds (area.removeFromTop (20));
    projectsHint   .setBounds (area.removeFromTop (16));
    area.removeFromTop (4);

    int problemHeight = 0;
    if (settingsProblem.isVisible())
    {
        juce::AttributedString text (settingsProblem.getText());
        text.setFont (settingsProblem.getFont());
        juce::TextLayout layout; layout.createLayout (text, (float) getWidth() - 42.0f);
        problemHeight = juce::jlimit (64, 128, (int) std::ceil (layout.getHeight()) + 12);
    }
    area = area.withTrimmedBottom (52 + problemHeight);
    auto addBtnRow   = area.removeFromBottom (32);
    addProjectsBtn.setBounds (addBtnRow.removeFromLeft (120));

    foldersList.setBounds (area);
    settingsProblem.setBounds (16, getHeight() - 52 - problemHeight, getWidth() - 32, problemHeight);

    auto bottom = getLocalBounds().removeFromBottom (52).reduced (16, 10);
    okBtn.setBounds (bottom.removeFromRight (rebuildFingerprint.isNotEmpty() ? 144 : 90));
    if (cancelBtn.isVisible())
    {
        bottom.removeFromRight (8);
        cancelBtn.setBounds (bottom.removeFromRight (90));
    }
    retrySettings.setBounds (bottom.removeFromLeft (128));
}

void SettingsDialog::launch (bool firstRun, std::function<void()> onDone)
{
    auto* dialog = new SettingsDialog (firstRun);
    dialog->onCompleted = onDone;

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle               = firstRun ? "Remember - Setup" : "Remember - Settings";
    opts.content.setOwned          (dialog);
    opts.componentToCentreAround   = nullptr;
    opts.escapeKeyTriggersCloseButton = ! firstRun;
    opts.useNativeTitleBar         = true;
    opts.resizable                 = false;
    opts.dialogBackgroundColour    = RememberUI::colour (Pal::BgDeep);
    opts.launchAsync();
}
