#include "SettingsDialog.h"
#include "PreviewCompactionActions.h"
#include "ProjectScanner.h"
#include "ProjectListComponent.h"   // for Pal::
#include "../../Shared/LibraryStore.h"
#include "../../Shared/AppearanceSettings.h"
#include "../../Shared/ZoomSettings.h"

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
        g.fillAll (RememberUI::colour (Pal::Elevated));
        if (sel)
        {
            g.setColour (RememberUI::colour (Pal::Selected));
            g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h), RememberUI::controlRadius);
        }
        g.setColour (RememberUI::colour (Pal::Border));
        g.drawHorizontalLine (h - 1, RememberUI::controlRadius, (float) w - RememberUI::controlRadius);

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
                owner.searchProjectFolders();
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
    setSize (680, 848);
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

    styleLabel (signature, 12.0f, Pal::Subtext);
    signature.setJustificationType (juce::Justification::centred);
    signature.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (signature);

    styleLabel (appearanceCaption, 13.0f, Pal::Text, true);
    styleLabel (appearanceHint, 11.0f, Pal::Subtext);
    appearanceCombo.setTitle ("Appearance");
    appearanceCombo.addItem ("System (automatic)", 3);
    appearanceCombo.addItem ("Light", 1);
    appearanceCombo.addItem ("Dark", 2);
    appearanceCombo.setSelectedId (RememberUI::appearanceMode == RememberAppearance::System ? 3 : RememberUI::isDark() ? 2 : 1, juce::dontSendNotification);
    appearanceCombo.onChange = [this] { saveAppearance(); };
    addAndMakeVisible (appearanceCaption);
    addAndMakeVisible (appearanceHint);
    addAndMakeVisible (appearanceCombo);
    styleLabel (zoomCaption, 13.0f, Pal::Text, true);
    styleLabel (zoomHint, 11.0f, Pal::Subtext);
    RememberUI::populateZoom (zoomCombo);
    zoomCombo.setSelectedId (RememberUI::appZoom().percent, juce::dontSendNotification);
    zoomCombo.onChange = [this] { saveZoom(); };
    addAndMakeVisible (zoomCaption);
    addAndMakeVisible (zoomHint);
    addAndMakeVisible (zoomCombo);
    styleLabel (midiCaption, 13.0f, Pal::Text, true);
    styleLabel (midiHint, 11.0f, Pal::Subtext);
    midiVoiceCombo.setTitle ("Default MIDI audition instrument");
    midiVoiceCombo.addItem ("Simple piano", 1); midiVoiceCombo.addItem ("Saw", 2);
    midiVoiceCombo.addItem ("Square", 3); midiVoiceCombo.addItem ("Sine", 4);
    midiVoiceCombo.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (midiCaption); addAndMakeVisible (midiHint); addAndMakeVisible (midiVoiceCombo);

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
    styleLabel (sizeEstimate,  12.0f, Pal::Text, true);
    styleLabel (storageUsage,  11.0f, Pal::Subtext);
    styleLabel (pcmPolicy, 12.0f, Pal::Subtext);
    sizeEstimate.setTitle ("Estimated preview storage");
    pcmPolicy.setTitle ("Preview storage notice");
    for (auto* label : { &sizeEstimate, &pcmPolicy })
    {
        label->setJustificationType (juce::Justification::topLeft);
        label->setMinimumHorizontalScale (1.0f);
    }
    addAndMakeVisible (recordCaption);
    addAndMakeVisible (recordHint);
    addAndMakeVisible (durationLbl);
    addAndMakeVisible (qualityLbl);
    addAndMakeVisible (sizeEstimate);
    addAndMakeVisible (storageUsage);
    addAndMakeVisible (pcmPolicy);
    RememberUI::iconButton (storageInfo, "info", "About preview storage");
    storageInfo.setTooltip ("How previews, tail preservation and recovery use disk space");
    storageInfo.onClick = [this] { showStorageInfo(); };
    addAndMakeVisible (storageInfo);
    compactBtn.setTitle ("Compact large previews");
    compactBtn.setTooltip ("Re-encode previews above the total audio limit, including recovery, into one OGG each");
    compactBtn.onClick = [this] { compactLargePreviews(); };
    compactLimit.setTitle ("Compaction limit in MB"); compactLimit.setText ("10", false);
    compactLimit.setInputRestrictions (9, "0123456789."); compactLimit.setFont (RememberUI::font (13));
    compactLimit.setTooltip ("Total audio including recovery; 1 MB = 1,000,000 bytes. Saved with OK.");
    RememberUI::bindColour (compactLimit, juce::TextEditor::backgroundColourId, Pal::Content);
    RememberUI::bindColour (compactLimit, juce::TextEditor::textColourId, Pal::Text);
    styleLabel (compactUnits, 12, Pal::Subtext);
    addAndMakeVisible (compactBtn); addAndMakeVisible (compactLimit); addAndMakeVisible (compactUnits);


    // Duration presets
    durationCombo.setTitle ("Capture duration");
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
    qualityCombo.setTitle ("OGG quality");
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
    styleBtn (refreshProjectsBtn, Pal::Elevated, Pal::Text);
    styleLabel (searchStatus, 12.0f, Pal::Subtext);
    searchStatus.setTitle ("Project folder search status");
    searchStatus.setMinimumHorizontalScale (1.0f);
    refreshProjectsBtn.setTitle ("Search project folders");
    refreshProjectsBtn.setTooltip ("Search every listed folder and subfolder now. Folder settings are saved with OK.");
    projectsHint.setText ("Searches matching project names in these folders and all subfolders", juce::dontSendNotification);
    refreshProjectsBtn.onClick = [this] { searchProjectFolders(); };
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
    addAndMakeVisible (refreshProjectsBtn);
    addAndMakeVisible (searchStatus);
    addAndMakeVisible (okBtn);
    if (firstRun)
        cancelBtn.setVisible (false);   // explicit so resized() skips it
    else
        addAndMakeVisible (cancelBtn);

    for (auto* child : getChildren()) if (child != &cancelBtn) child->setEnabled (false);
    previewsPath.setText ("Loading settings...", juce::dontSendNotification);
    loadSettings (std::move (reader));
}

SettingsDialog::~SettingsDialog() { if (storageInfoPopup != nullptr) storageInfoPopup->dismiss(); searchGeneration->fetch_add (1); storageGeneration->fetch_add (1); validationGeneration->fetch_add (1); }

void SettingsDialog::saveZoom()
{
    if (! settingsLoaded || zoomSaving) return;
    const int percent = zoomCombo.getSelectedId();
    zoomSaving = true;
    zoomCombo.setEnabled (false);
    okBtn.setEnabled (false);
    ProjectScanner::editAsync ([percent] { return ZoomSettings::save (percent, false); },
        [safe = juce::Component::SafePointer<SettingsDialog> (this), percent] (juce::Result result)
        {
            if (result.wasOk()) RememberUI::appZoom().set (percent);
            if (safe == nullptr) return;
            safe->zoomSaving = false;
            safe->zoomCombo.setEnabled (true);
            safe->zoomCombo.setSelectedId (RememberUI::appZoom().percent, juce::dontSendNotification);
            if (result.failed())
            {
                safe->settingsProblem.setText ("Cannot save zoom: " + result.getErrorMessage(), juce::dontSendNotification);
                safe->settingsProblem.setVisible (true);
                safe->retrySettings.setVisible (true);
                safe->resized();
            }
            safe->refreshLabels();
        });
}

void SettingsDialog::saveAppearance()
{
    if (! settingsLoaded || appearanceSaving) return;
    const auto dark = appearanceCombo.getSelectedId() == 3 ? RememberAppearance::System
        : appearanceCombo.getSelectedId() == 2 ? RememberAppearance::Dark : RememberAppearance::Light;
    appearanceSaving = true;
    appearanceCombo.setEnabled (false);
    okBtn.setEnabled (false);
    ProjectScanner::editAsync ([dark] { return AppearanceSettings::save (dark); },
        [safe = juce::Component::SafePointer<SettingsDialog> (this), dark] (juce::Result result)
        {
            if (result.wasOk()) RememberUI::setAppearanceMode (dark);
            if (safe == nullptr) return;
            safe->appearanceSaving = false;
            safe->appearanceCombo.setEnabled (true);
            safe->appearanceCombo.setSelectedId (RememberUI::appearanceMode == RememberAppearance::System ? 3 : RememberUI::isDark() ? 2 : 1, juce::dontSendNotification);
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
    searchGeneration->fetch_add (1);
    searchStatus.setText ({}, juce::dontSendNotification);
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
            safe->compactLimit.setText (juce::String (settings->compactAboveMB, 2), false);
            safe->recordMono = settings->mono;
            safe->midiVoiceCombo.setSelectedId (settings->midiPreviewVoice, juce::dontSendNotification);
            // JUCE Button::setToggleState ignores changes while disabled.
            for (auto* child : safe->getChildren()) child->setEnabled (true);
            if (result.wasOk()) RememberUI::setAppearanceMode (settings->appearance);
            safe->appearanceCombo.setSelectedId (RememberUI::appearanceMode == RememberAppearance::System ? 3 : RememberUI::isDark() ? 2 : 1, juce::dontSendNotification);
            safe->appearanceCombo.setEnabled (result.wasOk());
            if (result.wasOk()) RememberUI::appZoom().set (settings->interfaceZoom);
            safe->zoomCombo.setSelectedId (RememberUI::appZoom().percent, juce::dontSendNotification);
            safe->zoomCombo.setEnabled (result.wasOk());
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
        if (safe != nullptr && token->load() == generation) safe->okBtn.setEnabled (result.wasOk() && *valid && ! safe->appearanceSaving && ! safe->zoomSaving);
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

    const auto size = [] (double value) { return juce::File::descriptionOfSizeInBytes ((juce::int64) value); };
    sizeEstimate.setText ("At 48 kHz: ~" + size (2.0 * bytes) + " of OGG audio, including recovery copy\n"
        + juce::String (recordSeconds) + " s " + (recordMono ? "mono" : "stereo") + ": ~" + size (bytes) + " per copy, plus fragment overhead", juce::dontSendNotification);
    sizeEstimate.setTooltip ("Approximate compressed audio size. Fragment headers, overlaps and old retained ranges add space. "
        "Duration, sample rate, quality and content affect actual size; existing legacy previews may still contain WAV files.");
    pcmPolicy.setText ("New previews keep OGG fragments without recoding the old tail.\n"
        "WAVs are temporary while saving. Use the info button for limits and recovery.", juce::dontSendNotification);
    pcmPolicy.setTooltip ("Current and previous preview packages are retained. Existing previews are read without bulk migration.");
}

void SettingsDialog::showStorageInfo()
{
    if (storageInfoPopup != nullptr) { storageInfoPopup->dismiss(); return; }
    class Information final : public juce::Component
    {
    public:
        Information()
        {
            title.setText ("How preview storage works", juce::dontSendNotification);
            title.setFont (RememberUI::font (16, true)); RememberUI::bindColour (title, juce::Label::textColourId, Pal::Text);
            text.setMultiLine (true); text.setReadOnly (true); text.setCaretVisible (false); text.setScrollbarsShown (true);
            text.setFont (RememberUI::font (13)); text.setTitle ("Preview storage information");
            RememberUI::bindColour (text, juce::TextEditor::backgroundColourId, Pal::Content);
            RememberUI::bindColour (text, juce::TextEditor::textColourId, Pal::Text);
            text.setText ("New captures are encoded once into short OGG fragments. Keeping the old tail reuses its compressed fragments; it does not repeatedly recode them. A short crossfade is applied during playback.\n\n"
                "Fragments and their playback index are stored together in a .rmp preview package. The current package and one previous package are kept for recovery. These are not ordinary OGG files. Update the app and plugin together; earlier builds cannot read new packages.\n\n"
                "Capture WAVs are temporary and removed after their jobs finish. A crash can leave temporary files. Older previews remain readable and may still contain permanent WAVs; adding this version does not bulk-delete or migrate your library.\n\n"
                "The estimate covers OGG audio, not an exact disk limit. Fragment headers, overlap and retained source ranges add space. Higher quality, duration, sample rate and stereo can increase size. Temporary capture needs about 23 MB for 60 s stereo at 48 kHz, or 92 MB at 192 kHz; mono uses half, and queued captures need additional space.\n\n"
                "Compact preview audio in a recording's actions, or Compact previews above a chosen MB limit here, replaces fragments and recovery audio with one ordinary OGG at the selected quality. It adds one lossy encoding generation and cannot be undone. A later partial capture converts its retained tail to fragments once; later captures reuse those fragments. Entry sizes and the MB threshold count all audio including recovery. Already compacted audio is not encoded again unless it changes.\n\n"
                "To bound storage and decoding, a package allows at most 256 source ranges, 16 overlapping layers and 32 MiB. Repeated tiny edits can reach a limit. Remember then preserves the previous preview and reports an error: compact explicitly, capture the full preview or enable Replace whole previews. No automatic lossy flattening is performed.", false);
            addAndMakeVisible (title); addAndMakeVisible (text); setSize (400, 410);
        }
        void resized() override { auto area = getLocalBounds().reduced (12); title.setBounds (area.removeFromTop (30)); text.setBounds (area); }
    private:
        RememberUI::Style style { *this };
        juce::Label title; juce::TextEditor text;
    };
    auto content = std::make_unique<Information>(); auto* look = &content->getLookAndFeel();
    storageInfoPopup = &juce::CallOutBox::launchAsynchronously (std::move (content), storageInfo.getScreenBounds(), nullptr);
    storageInfoPopup->setLookAndFeel (look);
}

void SettingsDialog::compactLargePreviews()
{
    if (! settingsLoaded || compacting) return;
    const auto text = compactLimit.getText(); const auto limit = text.getDoubleValue();
    if (! text.containsOnly ("0123456789.") || text.indexOfChar ('.') != text.lastIndexOfChar ('.')
        || ! std::isfinite (limit) || limit <= 0 || limit > 1000000)
    { pcmPolicy.setText ("Enter a positive compaction limit in MB", juce::dontSendNotification); return; }
    const auto snapshot = ProjectScanner::librarySnapshot();
    if (snapshot->loading || snapshot->sourceRoot == juce::File{} || snapshot->stale)
    { pcmPolicy.setText ("Wait for the current library to finish loading, then retry", juce::dontSendNotification); return; }
    const auto entries = snapshot->entries; const auto root = snapshot->sourceRoot;
    const auto quality = oggQuality;
    PreviewCompactionActions::show (PreviewCompactionActions::confirmation (
        "All previews in the current library above " + juce::String (limit, 2)
        + " MB of audio, including recovery. Locked/live pending recordings are skipped.\n" + root.getFullPathName()).withAssociatedComponent (this),
        [safe = juce::Component::SafePointer<SettingsDialog> (this), entries, root, limit, quality] (int response)
        {
            if (safe == nullptr || response != 1 || safe->compacting) return;
            safe->compacting = true; safe->compactBtn.setEnabled (false); safe->compactLimit.setEnabled (false);
            safe->okBtn.setEnabled (false);
            safe->pcmPolicy.setText ("Compacting eligible previews in the background...\nOriginal audio is kept until each replacement is validated.", juce::dontSendNotification);
            if (safe->onBeforeCompaction) for (const auto& entry : entries)
                if (PreviewCompactionActions::eligible (entry) && entry.audioBytes > limit * 1000000.0) safe->onBeforeCompaction (entry);
            auto summary = std::make_shared<PreviewCompactionActions::Summary>();
            ProjectScanner::editAsync ([root, entries, limit, quality, summary]
            {
                PreviewCompactionActions::run (root, entries, quality, (juce::int64) std::llround (limit * 1000000), *summary);
                return juce::Result::ok();
            }, [safe, summary] (juce::Result result)
            {
                if (safe == nullptr) return;
                safe->compacting = false; safe->compactBtn.setEnabled (true); safe->compactLimit.setEnabled (true);
                safe->okBtn.setEnabled (true);
                safe->pcmPolicy.setText (result.failed() ? result.getErrorMessage() : summary->text(), juce::dontSendNotification);
                safe->pcmPolicy.setTooltip (summary->errors.joinIntoString ("\n"));
                safe->measuredFolder = {}; safe->updateStorageUsage();
                if (! summary->errors.isEmpty()) PreviewCompactionActions::show (juce::MessageBoxOptions()
                    .withTitle ("Compaction needs attention").withAssociatedComponent (safe.getComponent())
                    .withMessage (summary->text() + "\n\n" + summary->errors.joinIntoString ("\n")).withButton ("OK"));
            });
        });
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
            + ", compressed " + size (usage->previewBytes) + ") | Free: "
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
            safe->searchProjectFolders();
        });
}

void SettingsDialog::searchProjectFolders()
{
    if (! settingsLoaded) return;
    const auto token = searchGeneration;
    const auto generation = token->fetch_add (1) + 1;
    refreshProjectsBtn.setEnabled (false);
    searchStatus.setText ("Searching folders and subfolders...", juce::dontSendNotification);
    ProjectScanner::searchDirectoriesAsync (previewsFolder, projectsFolders,
        [token, generation] { return token->load() != generation; },
        [safe = juce::Component::SafePointer<SettingsDialog> (this), token, generation]
        (juce::Result result, ProjectScanner::DirectorySearchResult summary)
        {
            if (safe == nullptr || token->load() != generation) return;
            safe->refreshProjectsBtn.setEnabled (true);
            const auto text = result.failed() ? result.getErrorMessage()
                : juce::String ((juce::int64) summary.candidates) + " project files; "
                    + juce::String (summary.available) + " linked, " + juce::String (summary.missing)
                    + " missing (" + juce::String (summary.elapsedMs / 1000.0, 1) + " s)";
            safe->searchStatus.setText (text, juce::dontSendNotification);
            safe->searchStatus.setTooltip (text);
        });
}

void SettingsDialog::commit()
{
    if (appearanceSaving || zoomSaving || compacting) return;
    const auto compactText = compactLimit.getText();
    const auto compactMB = compactText.getDoubleValue();
    if (! compactText.containsOnly ("0123456789.") || compactText.indexOfChar ('.') != compactText.lastIndexOfChar ('.')
        || ! std::isfinite (compactMB) || compactMB <= 0 || compactMB > 1000000)
    { storageUsage.setText ("Enter a positive compaction limit in MB", juce::dontSendNotification); return; }
    validationGeneration->fetch_add (1);
    for (auto* child : getChildren()) child->setEnabled (false);
    auto originalBackup = std::make_shared<juce::File>();
    ProjectScanner::editAsync ([previews = previewsFolder, roots = projectsFolders, fingerprint = rebuildFingerprint, originalBackup,
                               seconds = recordSeconds, quality = oggQuality, mono = recordMono, midiVoice = midiVoiceCombo.getSelectedId(), compactMB,
                               capture = CaptureOptions { manualToggle.getToggleState(), lockToggle.getToggleState(), replaceToggle.getToggleState() }]
    {
        bool validRoot = false;
        for (const auto& root : roots) if (root.isDirectory()) { validRoot = true; break; }
        if (! previews.isDirectory() || ! validRoot)
            return juce::Result::fail ("Select an existing previews folder and at least one accessible project search folder");
        const auto result = fingerprint.isEmpty() ? ProjectScanner::saveSettings (previews, roots, seconds, quality, mono, &capture, midiVoice, compactMB)
            : ProjectScanner::rebuildSettings (previews, roots, seconds, quality, mono, fingerprint, *originalBackup, &capture, midiVoice, compactMB);
        // Saving accepts the search roots independently of the dialog's draft
        // search, which is cancelled on close. Rebuild the live inventory even
        // if a native filesystem notification is delayed or unavailable.
        if (result.wasOk()) ProjectScanner::refreshAsync();
        return result;
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
    auto zoomRow = area.removeFromTop (32);
    zoomCaption.setBounds (zoomRow.removeFromLeft (110));
    zoomCombo.setBounds (zoomRow.removeFromLeft (150));
    zoomRow.removeFromLeft (16);
    zoomHint.setBounds (zoomRow);
    area.removeFromTop (12);
    auto midiRow = area.removeFromTop (32);
    midiCaption.setBounds (midiRow.removeFromLeft (110));
    midiVoiceCombo.setBounds (midiRow.removeFromLeft (150)); midiRow.removeFromLeft (16);
    midiHint.setBounds (midiRow); area.removeFromTop (12);

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
    auto recordHeading = area.removeFromTop (30);
    storageInfo.setBounds (recordHeading.removeFromRight (30));
    recordCaption.setBounds (recordHeading);
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

    monoToggle.setBounds (area.removeFromTop (30));
    manualToggle.setBounds (area.removeFromTop (30));
    lockToggle.setBounds (area.removeFromTop (30));
    replaceToggle.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    sizeEstimate.setBounds (area.removeFromTop (38));
    pcmPolicy.setBounds (area.removeFromTop (38));
    storageUsage.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);
    auto compactRow = area.removeFromTop (32);
    compactBtn.setBounds (compactRow.removeFromLeft (220)); compactRow.removeFromLeft (8);
    compactLimit.setBounds (compactRow.removeFromLeft (96)); compactRow.removeFromLeft (6);
    compactUnits.setBounds (compactRow.removeFromLeft (32));

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
    refreshProjectsBtn.setBounds (addBtnRow.removeFromRight (140));
    searchStatus.setBounds (area.removeFromBottom (24));

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
    signature.setBounds (bottom.reduced (8, 0));
}

void SettingsDialog::launch (bool firstRun, std::function<void()> onDone, std::function<void (const ProjectEntry&)> beforeCompaction)
{
    auto* dialog = new SettingsDialog (firstRun);
    dialog->onCompleted = onDone;
    dialog->onBeforeCompaction = std::move (beforeCompaction);

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
