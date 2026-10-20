#include "MainComponent.h"
#include "ProjectScanner.h"
#include "SettingsDialog.h"
#include "PreviewFolderSetup.h"
#include "UsedSoundsDialog.h"
#include "StatisticsDialog.h"
#include "BuildInfo.h"

// Pal:: colours are defined in ProjectListComponent.h

MainComponent::MainComponent()
{
    setSize (960, 680);
    setWantsKeyboardFocus (true);
    list.addSearchKeyListener (this);
    sidebar.addMouseListener (this, true);
    list.addMouseListener (this, true);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    settingsButton.setTooltip ("Open settings (previews & projects folders)");
    RememberUI::bindColour (settingsButton, juce::TextButton::buttonColourId, Pal::Elevated);
    RememberUI::bindColour (settingsButton, juce::TextButton::textColourOffId, Pal::Subtext);
    settingsButton.onClick = [this] { showSettingsDialog(); };
    RememberUI::iconButton (settingsButton, "settings", "Settings");
    settingsButton.setTooltip ("Settings");
    addAndMakeVisible (settingsButton);
    RememberUI::iconButton (statisticsButton, "statistics", "Statistics");
    statisticsButton.setTooltip ("Statistics");
    RememberUI::bindColour (statisticsButton, juce::TextButton::buttonColourId, Pal::Elevated);
    RememberUI::bindColour (statisticsButton, juce::TextButton::textColourOffId, Pal::Subtext);
    statisticsButton.onClick = [this] { StatisticsDialog::launch (&player); };
    addAndMakeVisible (statisticsButton);

    // Clicking a row loads AND auto-plays the preview
    list.onEntrySelected = [this] (const ProjectEntry& e) { player.loadEntry (e); if (player.autoplaySelection()) player.play(); };
    list.onEntryPlay     = [this] (const ProjectEntry& e) { player.loadEntry (e); player.play(); };
    list.onUsedSounds = [this] (const ProjectEntry& e)
    {
        UsedSoundsDialog::launch (e, {}, {}, {}, &player);
    };
    list.onVisibleEntriesChanged = [this] (const juce::Array<ProjectEntry>& entries) { player.setQueue (entries); };
    player.onEntryLoaded = [this] (const ProjectEntry& entry) { list.selectEntry (entry); };
    player.assignmentSelection = [this] { return list.selectedEntries(); };
    player.setQueue (list.visibleEntries());
    list.onBeforeStorageChange = [this] (const ProjectEntry& e) { player.releaseEntry (e.metadataFile); };

    addAndMakeVisible (sidebar);
    addAndMakeVisible (list);
    addAndMakeVisible (player);
    addAndMakeVisible (dawLauncher);
    dawLauncher.onLayoutChanged = [this] { resized(); };

    list.onFiltersCleared = [this] { sidebar.selectFolder ({}); };

    // Sidebar selects a folder → list filters by it
    sidebar.onFoldersSelected = [this] (juce::StringArray paths)
    {
        list.setFolderFilters (paths);
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
    };

    // Folder structure changed (create/rename/delete) → main list may need re-filter
    // editAsync wakes the index once. Its publication updates both views;
    // forcing Refresh here would also rebuild the DAW project inventory.

    // When the main list reloads (e.g. plugin saved a new entry), recompute
    // sidebar counts.
    list.onRefreshed = [this] { sidebar.refresh(); };

    checkFirstRun();

    // The index verifies/resolves paths before publishing. The list reports
    // missing projects from that publication without a second scan or dialogs.
}

MainComponent::~MainComponent()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    sidebar.removeMouseListener (this);
    list.removeMouseListener (this);
    list.removeSearchKeyListener (this);
}

void MainComponent::rememberUndoContext (juce::Component* component)
{
    if (component == nullptr) return;
    if (component == &sidebar || sidebar.isParentOf (component)) lastUndoContext = ProjectScanner::UndoContext::Folders;
    else if (component == &list || list.isParentOf (component)) lastUndoContext = ProjectScanner::UndoContext::Previews;
}

void MainComponent::mouseDown (const juce::MouseEvent& event)
{
    lastFocusedComponent = juce::Component::getCurrentlyFocusedComponent();
    rememberUndoContext (event.eventComponent);
}

void MainComponent::observeUndoFocus (juce::Component* component)
{
    if (component == lastFocusedComponent.getComponent()) return;
    lastFocusedComponent = component;
    rememberUndoContext (component);
}

void MainComponent::globalFocusChanged (juce::Component* component)
{
    observeUndoFocus (component);
}

bool MainComponent::libraryShortcut (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('F', juce::ModifierKeys::ctrlModifier, 0)) { list.focusSearch(); return true; }
    if (key == juce::KeyPress (',', juce::ModifierKeys::ctrlModifier, 0)) { showSettingsDialog(); return true; }
    if (key == juce::KeyPress ('O', juce::ModifierKeys::ctrlModifier, 0)) { list.openSelectedInDaw(); return true; }
    if (key == juce::KeyPress ('F', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0))
    { list.toggleFavoritesFilter(); return true; }
    return false;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (libraryShortcut (key)) return true;
    // Desktop focus notifications are deferred. Observe a keyboard focus move
    // before the next shortcut, without overriding a newer click that kept focus.
    observeUndoFocus (juce::Component::getCurrentlyFocusedComponent());
    // Text editors own typing and cursor movement; only explicit library
    // commands above may change the app while the user is entering text.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent();
        dynamic_cast<juce::TextEditor*> (focused) != nullptr && isParentOf (focused)) return false;
    if (key == juce::KeyPress ('M')) { player.toggleMute(); return true; }
    if (key == juce::KeyPress ('S')) { player.toggleShuffle(); return true; }
    if (key == juce::KeyPress ('L')) { player.cycleRepeat(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0))
    { player.seekBySeconds (-5); return true; }
    if (key == juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0))
    { player.seekBySeconds (5); return true; }
    if (key == juce::KeyPress ('Z', juce::ModifierKeys::ctrlModifier, 0))
    {
        const auto context = lastUndoContext;
        ProjectScanner::editAsync ([context] { return ProjectScanner::undoOrganization (context); },
            [safe = juce::Component::SafePointer<MainComponent> (this)] (juce::Result result)
            { if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Cannot undo", result.getErrorMessage()); });
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
    auto dark = std::make_shared<RememberAppearance> (RememberAppearance::System);
    auto zoom = std::make_shared<int> (100);
    ProjectScanner::queryAsync ([needsSetup, dark, zoom]
    {
        const auto settings = ProjectScanner::readSettings();
        if (settings.result.failed()) return settings.result;
        // A configured drive/folder can be temporarily unavailable. Its index
        // error offers Settings/Refresh; it is not a new profile to initialise.
        *needsSetup = settings.previews == juce::File{};
        *dark = settings.appearance;
        *zoom = settings.interfaceZoom;
        return juce::Result::ok();
    },
        [safe = juce::Component::SafePointer<MainComponent> (this), needsSetup, dark, zoom] (juce::Result result)
        {
            if (safe == nullptr || result.failed()) return;
            RememberUI::setAppearanceMode (*dark);
            RememberUI::appZoom().set (*zoom);
            if (! *needsSetup) return;
            PreviewFolderSetup::launch (safe.getComponent(), [safe]
            { if (safe != nullptr) safe->list.refresh(); });
        });
}

void MainComponent::showSettingsDialog()
{
    SettingsDialog::launch (false, [safe = juce::Component::SafePointer<MainComponent> (this)]
    { if (safe != nullptr) safe->list.refresh(); },
        [safe = juce::Component::SafePointer<MainComponent> (this)] (const ProjectEntry& entry)
        { if (safe != nullptr) safe->player.releaseEntry (entry.metadataFile); });
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));
    g.setColour (RememberUI::colour (Pal::Surface));
    juce::Path navigation;
    constexpr int margin = RememberUI::appOuterMargin;
    navigation.addRoundedRectangle ((float) margin, (float) margin, (float) kSidebarWidth,
                                    (float) (getHeight() - kPlayerHeight - margin), RememberUI::recordingRowRadius);
    g.fillPath (navigation);
    g.setFont (RememberUI::font (26, true));
    g.setColour (RememberUI::colour (Pal::Text));
    g.drawText ("Remember", margin + RememberUI::folderIconLeading, margin, kSidebarWidth - 32, 64, juce::Justification::centredLeft);
    g.setFont (RememberUI::font (22, true));
    g.setColour (RememberUI::colour (Pal::Text));
    g.drawText ("Library", margin + kSidebarWidth + RememberUI::appSectionGap + 8, margin + 14, 180, 28, juce::Justification::centredLeft);
    g.setFont (RememberUI::font (12));
    g.setColour (RememberUI::colour (Pal::Subtext));
    g.drawText ("Browse projects and listen to previews", margin + kSidebarWidth + RememberUI::appSectionGap + 8, margin + 42,
                getWidth() - margin - kSidebarWidth - 162, 22, juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    constexpr int margin = RememberUI::appOuterMargin;
    auto header = area.removeFromTop (64 + margin).withTrimmedTop (margin).withTrimmedRight (margin);
    settingsButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 34));
    header.removeFromRight (8);
    statisticsButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 34));
    player.setBounds (area.removeFromBottom (kPlayerHeight));
    auto sidebarArea = area.removeFromLeft (kSidebarWidth + margin).withTrimmedLeft (margin);
    constexpr int padding = RememberUI::appSidebarPadding;
    const auto launcherHeight = dawLauncher.preferredHeight (sidebarArea.getWidth() - 2 * padding);
    dawLauncher.setBounds (sidebarArea.removeFromBottom (launcherHeight + 2 * padding).reduced (padding));
    sidebar.setBounds (sidebarArea);
    list.setBounds (area);
}
