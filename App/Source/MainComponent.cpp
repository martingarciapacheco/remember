#include "MainComponent.h"
#include "ProjectScanner.h"
#include "SettingsDialog.h"
#include "BuildInfo.h"

// Pal:: colours are defined in ProjectListComponent.h

MainComponent::MainComponent()
{
    setSize (860, 640);
    setWantsKeyboardFocus (true);

    settingsButton.setTooltip ("Open settings (previews & projects folders)");
    settingsButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (Pal::Elevated));
    settingsButton.setColour (juce::TextButton::textColourOffId, juce::Colour (Pal::Subtext));
    settingsButton.onClick = [this] { showSettingsDialog(); };
    addAndMakeVisible (settingsButton);

    // Clicking a row loads AND auto-plays the preview
    list.onEntrySelected = [this] (const ProjectEntry& e) { player.loadEntry (e); player.play(); };
    list.onEntryPlay     = [this] (const ProjectEntry& e) { player.loadEntry (e); player.play(); };

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
                ProjectScanner::addEntryToFolder (e, folderPath);
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

    // After the UI is up, verify project paths in the background. If the user
    // is going through first-run setup (no previews folder yet), this will be
    // a no-op until they finish — the onCompleted callback re-triggers refresh.
    juce::MessageManager::callAsync ([this] { list.verifyProjectPathsAsync(); });
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Space: play/pause currently loaded preview
    if (key == juce::KeyPress::spaceKey)
    {
        player.togglePlay();
        return true;
    }
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
    if (! ProjectScanner::getPreviewsFolder().isDirectory())
        juce::MessageManager::callAsync ([this]
        {
            SettingsDialog::launch (true, [this] { list.refresh(); list.verifyProjectPathsAsync(); });
        });
}

void MainComponent::showSettingsDialog()
{
    SettingsDialog::launch (false, [this] { list.refresh(); list.verifyProjectPathsAsync(); });
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Pal::BgDeep));

    // Header bar
    g.setColour (juce::Colour (Pal::Surface));
    g.fillRect (0, 0, getWidth(), 44);

    // Header bottom border
    g.setColour (juce::Colour (Pal::Border));
    g.fillRect (0, 44, getWidth(), 1);

    // Logo / title
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (17.0f).withStyle ("Bold")));
    g.setColour (juce::Colour (Pal::Accent));
    g.drawText ("REMEMBER", 16, 0, 160, 44, juce::Justification::centredLeft);

    // Build version (next to logo, small + muted)
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.5f)));
    g.setColour (juce::Colour (Pal::Subtext).withAlpha (0.7f));
    g.drawText (juce::String ("v") + REMEMBER_VERSION + "  " + REMEMBER_GIT_HASH,
                140, 0, 200, 44, juce::Justification::centredLeft);

    // Separator between list and player
    g.setColour (juce::Colour (Pal::Border));
    g.fillRect (0, getHeight() - kPlayerHeight, getWidth(), 1);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Header
    auto header = area.removeFromTop (44);
    settingsButton.setBounds (header.removeFromRight (90).reduced (8, 8));

    // Player panel at the bottom (full width)
    player.setBounds (area.removeFromBottom (kPlayerHeight));

    // Sidebar on the left, list in the rest
    sidebar.setBounds (area.removeFromLeft (kSidebarWidth));
    list   .setBounds (area);
}
