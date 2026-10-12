#include "MainComponent.h"
#include "ProjectScanner.h"
#include "SettingsDialog.h"
#include "BuildInfo.h"

// Pal:: colours are defined in ProjectListComponent.h

MainComponent::MainComponent()
{
    setSize (960, 680);
    setWantsKeyboardFocus (true);

    settingsButton.setTooltip ("Open settings (previews & projects folders)");
    RememberUI::bindColour (settingsButton, juce::TextButton::buttonColourId, Pal::Elevated);
    RememberUI::bindColour (settingsButton, juce::TextButton::textColourOffId, Pal::Subtext);
    settingsButton.onClick = [this] { showSettingsDialog(); };
    settingsButton.setTitle ("Settings");
    settingsButton.setTooltip (juce::String ("Open settings (previews and project folders)\n") + REMEMBER_VERSION_LINE);
    addAndMakeVisible (settingsButton);

    // Clicking a row loads AND auto-plays the preview
    list.onEntrySelected = [this] (const ProjectEntry& e) { player.loadEntry (e); if (player.autoplaySelection()) player.play(); };
    list.onEntryPlay     = [this] (const ProjectEntry& e) { player.loadEntry (e); player.play(); };
    list.onVisibleEntriesChanged = [this] (const juce::Array<ProjectEntry>& entries) { player.setQueue (entries); };
    player.onEntryLoaded = [this] (const ProjectEntry& entry) { list.selectEntry (entry); };
    player.setQueue (list.visibleEntries());
    list.onBeforeStorageChange = [this] (const ProjectEntry& e) { player.releaseEntry (e.metadataFile); };

    addAndMakeVisible (sidebar);
    addAndMakeVisible (list);
    addAndMakeVisible (player);

    // Sidebar selects a folder → list filters by it
    sidebar.onFolderSelected = [this] (juce::String path)
    {
        list.setFolderFilter (path);
    };

    // Drop an entry on a folder → add the folder to that entry, then refresh
    sidebar.onEntryDroppedOnFolder = [this] (juce::File metaFile, juce::String folderPath)
    {
        // Re-load the entry from the scan() snapshot so we know its current state
        for (auto& e : ProjectScanner::scan())
        {
            if (e.metadataFile == metaFile)
            {
                auto safe = juce::Component::SafePointer<MainComponent> (this);
                ProjectScanner::editAsync ([e, folderPath] { return ProjectScanner::addEntryToFolder (e, folderPath); },
                    [safe] (juce::Result result)
                    {
                        if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (
                            juce::AlertWindow::WarningIcon, "Cannot assign folder", result.getErrorMessage());
                    });
                break;
            }
        }
        list.refresh();
        sidebar.refresh();
    };

    // Folder structure changed (create/rename/delete) → main list may need re-filter
    sidebar.onFoldersChanged = [this] { list.refresh(); };

    // When the main list reloads (e.g. plugin saved a new entry), recompute
    // sidebar counts.
    list.onRefreshed = [this] { sidebar.refresh(); };

    checkFirstRun();

    // The index verifies/resolves paths before publishing. The list reports
    // missing projects from that publication without a second scan or dialogs.
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == 'Z')
    {
        ProjectScanner::editAsync ([] { return ProjectScanner::undoOrganization(); },
            [safe = juce::Component::SafePointer<MainComponent> (this)] (juce::Result result)
            { if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Cannot undo organization", result.getErrorMessage()); });
        return true;
    }
    // Space: play/pause currently loaded preview
    if (key == juce::KeyPress::spaceKey)
    {
        player.togglePlay();
        return true;
    }
    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == juce::KeyPress::rightKey) { player.next(); return true; }
    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == juce::KeyPress::leftKey) { player.previous(); return true; }
    // Enter is handled inside ProjectListComponent::returnKeyPressed (plays the row)
    // R: rename selected (no modifier — search box catches it when typing)
    if (key.getKeyCode() == 'R' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        list.renameSelected();
        return true;
    }
    // T: edit tags for selected
    if (key.getKeyCode() == 'T' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        list.editTagsForSelected();
        return true;
    }
    // F: toggle favorite for selected entry
    if (key.getKeyCode() == 'F' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        list.toggleFavoriteForSelected();
        return true;
    }
    // Delete: remove selected entry (with confirmation)
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        list.deleteSelected();
        return true;
    }
    return false;
}

void MainComponent::checkFirstRun()
{
    auto needsSetup = std::make_shared<bool> (false);
    auto dark = std::make_shared<bool> (false);
    ProjectScanner::queryAsync ([needsSetup, dark]
    {
        const auto settings = ProjectScanner::readSettings();
        if (settings.result.failed()) return settings.result;
        // A configured drive/folder can be temporarily unavailable. Its index
        // error offers Settings/Refresh; it is not a new profile to initialise.
        *needsSetup = settings.previews == juce::File{};
        *dark = settings.darkAppearance;
        return juce::Result::ok();
    },
        [safe = juce::Component::SafePointer<MainComponent> (this), needsSetup, dark] (juce::Result result)
        {
            if (safe == nullptr || result.failed()) return;
            RememberUI::setDark (*dark);
            if (! *needsSetup) return;
            SettingsDialog::launch (true, [safe]
            { if (safe != nullptr) safe->list.refresh(); });
        });
}

void MainComponent::showSettingsDialog()
{
    SettingsDialog::launch (false, [safe = juce::Component::SafePointer<MainComponent> (this)]
    { if (safe != nullptr) safe->list.refresh(); });
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));
    g.setColour (RememberUI::colour (Pal::Surface));
    g.fillRect (0, 0, kSidebarWidth, getHeight() - kPlayerHeight);
    g.setFont (RememberUI::font (22, true));
    g.setColour (RememberUI::colour (Pal::Text));
    g.drawText ("Remember", 20, 14, kSidebarWidth - 32, 28, juce::Justification::centredLeft);
    g.setFont (RememberUI::font (12));
    g.setColour (RememberUI::colour (Pal::Subtext));
    g.drawText ("Your music, remembered.", 20, 42, kSidebarWidth - 32, 22, juce::Justification::centredLeft);
    g.setFont (RememberUI::font (22, true));
    g.setColour (RememberUI::colour (Pal::Text));
    g.drawText ("Library", kSidebarWidth + 24, 14, 180, 28, juce::Justification::centredLeft);
    g.setFont (RememberUI::font (12));
    g.setColour (RememberUI::colour (Pal::Subtext));
    g.drawText ("Browse projects and listen to previews", kSidebarWidth + 24, 42,
                getWidth() - kSidebarWidth - 120, 22, juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (80);
    settingsButton.setBounds (header.removeFromRight (104).reduced (12, 23));
    player.setBounds (area.removeFromBottom (kPlayerHeight));
    sidebar.setBounds (area.removeFromLeft (kSidebarWidth));
    list.setBounds (area);
}
