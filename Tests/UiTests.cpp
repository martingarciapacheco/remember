#include "UiTests.h"
#include "AppearanceTests.h"
#include "TypographyTests.h"
#include "../App/Source/FoldersSidebarComponent.h"
#include "../App/Source/TagsEditorDialog.h"
#include "../App/Source/MainComponent.h"
#include "../App/Source/SettingsDialog.h"
#include "../App/Source/PreviewCompactionActions.h"
#include "../App/Source/PreviewFolderSetup.h"
#include "../App/Source/RecoveryChoiceDialog.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/AppearanceSettings.h"
#include <iostream>
#include <stdexcept>
#include <set>

namespace
{
struct SavedConfig
{
    juce::File file { LibraryStore::configFile() };
    juce::String text { file.loadFileAsString() };
    bool existed = file.existsAsFile();
    ~SavedConfig()
    {
        if (existed) file.replaceWithText (text); else file.deleteFile();
        ProjectScanner::refreshAsync();
    }
};
void check (bool passed, const char* message) { if (! passed) throw std::runtime_error (message); }
template<class Condition> void awaitUi (Condition condition, int timeoutMs = 10000, const char* reason = "UI fixture timed out")
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! condition())
    {
        check (juce::Time::getMillisecondCounter() < until, reason);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
}

template<class Type> Type* childOf (juce::Component& parent)
{
    for (auto* child : parent.getChildren()) if (auto* typed = dynamic_cast<Type*> (child)) return typed;
    return nullptr;
}
void saveImage (juce::Component& component, const char* name, bool software = false)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts");
    check (folder.createDirectory().wasOk(), "Cannot create UI artifact folder");
    juce::FileOutputStream stream (folder.getChildFile (name));
    juce::PNGImageFormat format;
    const auto snapshot = software ? component.createComponentSnapshot (component.getLocalBounds(), true, 1.0f, juce::SoftwareImageType{})
                                   : component.createComponentSnapshot (component.getLocalBounds());
    check (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
        && format.writeImageToStream (snapshot, stream),
           "Cannot write UI snapshot");
}
}

void testFrontendDesign (const juce::File& root)
{
    testFrontendTypography();
    testAppearanceContrast();
    testAutomaticAppearance();
    SavedConfig restoreConfig;
    const auto library = root.getChildFile ("design-library");
    const auto projects = root.getChildFile ("projects");
    check (projects.createDirectory().wasOk(), "Cannot create design project folder");
    const auto project = projects.getChildFile ("Midnight sketches - a long project name for layout review.als");
    check (project.replaceWithText ("design project fixture"), "Cannot create design project");
    check (library.createDirectory().wasOk(), "Cannot create design library");
    check (ProjectScanner::saveSettings (library, { projects }, 60, 4, false).wasOk(), "Cannot configure design fixture");
    const auto directory = library.getChildFile ("recording");
    check (directory.createDirectory().wasOk(), "Cannot create design recording");
    juce::var metadata (new juce::DynamicObject());
    auto* data = metadata.getDynamicObject();
    data->setProperty ("projectId", juce::Uuid().toString());
    data->setProperty ("projectName", "Midnight sketches - a long project name for layout review");
    data->setProperty ("daw", "Ableton Live");
    data->setProperty ("projectPath", project.getFullPathName());
    data->setProperty ("bpm", 124.0); data->setProperty ("key", "A minor");
    data->setProperty ("recordedAt", "2026-10-11T12:00:00Z");
    data->setProperty ("favorite", true);
    data->setProperty ("tags", juce::Array<juce::var> { "Ambient", "Work in progress" });
    data->setProperty ("folders", juce::Array<juce::var> { "Sketches/Ideas" });
    check (LibraryStore::writeJsonAtomically (directory.getChildFile ("metadata.json"), metadata).wasOk(), "Cannot write design metadata");
    {
        juce::OggVorbisAudioFormat ogg;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (directory.getChildFile ("preview.ogg"));
        auto writer = ogg.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (16));
        check (writer != nullptr, "Cannot create design audio");
        juce::AudioBuffer<float> audio (2, 96000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                audio.setSample (ch, i, (float) ((ch == 0 ? 0.7 : 0.5) * std::sin (i * 0.0288)
                    * (0.55 + 0.45 * std::sin (i * (ch == 0 ? 0.00011 : 0.00016) + ch))));
        check (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "Cannot write design audio");
    }
    ProjectScanner::refreshAsync();
    awaitUi ([&] { return ProjectScanner::scan().size() == 1; });
    {
        // Saving a newly added root must resolve nested projects even after
        // closing Settings, without a Search folders or Refresh click.
        const auto autoLibrary = root.getChildFile ("automatic-search-library");
        const auto autoRoots = root.getChildFile ("automatic-search-projects");
        const auto nested = autoRoots.getChildFile ("nested/deep");
        const auto autoProject = nested.getChildFile ("Automatic search.als");
        const auto autoEntry = autoLibrary.getChildFile ("recording");
        check (autoEntry.createDirectory().wasOk() && nested.createDirectory().wasOk()
            && autoProject.replaceWithText ("automatic search fixture"), "Cannot create automatic folder search fixture");
        auto autoMetadata = metadata.clone();
        autoMetadata.getDynamicObject()->setProperty ("projectId", juce::Uuid().toString());
        autoMetadata.getDynamicObject()->setProperty ("projectName", "Automatic search");
        autoMetadata.getDynamicObject()->setProperty ("projectPath", "");
        check (LibraryStore::writeJsonAtomically (autoEntry.getChildFile ("metadata.json"), autoMetadata).wasOk()
            && directory.getChildFile ("preview.ogg").copyFileTo (autoEntry.getChildFile ("preview.ogg")), "Cannot create missing-project preview");
        check (ProjectScanner::saveSettings (autoLibrary, { projects }, 60, 4, false).wasOk(), "Cannot configure automatic search fixture");
        ProjectScanner::refreshAsync();
        awaitUi ([&] { auto snapshot = ProjectScanner::librarySnapshot(); return snapshot->sourceRoot == autoLibrary
            && snapshot->entries.size() == 1 && snapshot->missingProjects == 1; });
        auto draft = ProjectScanner::readSettings(); draft.roots.add (autoRoots);
        auto settings = std::make_unique<SettingsDialog> (false, [draft] { return draft; });
        awaitUi ([&] { return settings->hasLoadedSettings(); });
        juce::TextButton* ok = nullptr;
        for (auto* child : settings->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "OK") ok = button;
        check (ok != nullptr, "Automatic search fixture cannot save Settings");
        awaitUi ([&] { return ok->isEnabled(); });
        bool saved = false; settings->onCompleted = [&] { saved = true; };
        ok->onClick(); awaitUi ([&] { return saved; }); settings.reset();
        awaitUi ([&] { const auto snapshot = ProjectScanner::librarySnapshot(); return snapshot->sourceRoot == autoLibrary
            && snapshot->entries.size() == 1 && snapshot->entries[0].projectAvailable
            && snapshot->entries[0].projectPath == autoProject.getFullPathName(); }, 10000,
            "Saving a new search root did not automatically resolve its nested project after Settings closed");
        check (ProjectScanner::saveSettings (library, { projects }, 60, 4, false).wasOk(), "Cannot restore design fixture");
        ProjectScanner::refreshAsync();
        awaitUi ([&] { return ProjectScanner::librarySnapshot()->sourceRoot == library; });
        std::cout << "PASS: saved search folder resolves nested projects after Settings closes without manual refresh\n";
    }
    auto scene = std::make_unique<MainComponent>();
    auto* list = childOf<ProjectListComponent> (*scene);
    auto* player = childOf<WaveformPlayerComponent> (*scene);
    check (list && player, "Design scene lacks list or player");
    auto* launchers = childOf<DawLauncher> (*scene);
    check (launchers != nullptr, "Installed DAW launchers are missing");
    awaitUi ([&] { return launchers->hasDiscoveredApplications(); }, 15000, "DAW discovery did not finish");
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    check (RememberUI::appearanceMode == RememberAppearance::System
        && RememberUI::isDark() == juce::Desktop::getInstance().isDarkModeActive(), "App did not start in system appearance");
    auto lightReady = std::make_shared<bool> (false);
    ProjectScanner::editAsync ([] { return AppearanceSettings::save (RememberAppearance::Light); },
        [lightReady] (juce::Result result)
        {
            check (result.wasOk(), "Cannot configure light snapshot appearance");
            RememberUI::setAppearanceMode (RememberAppearance::Light); *lightReady = true;
        });
    awaitUi ([&] { return *lightReady; });
    auto* launcherViewport = childOf<juce::Viewport> (*launchers);
    check (launcherViewport != nullptr, "DAW launchers cannot scroll");
    for (auto* child : launcherViewport->getViewedComponent()->getChildren())
        check (child->getTitle().startsWith ("Open "), "DAW launcher lost its accessible name");
    juce::TextButton* assignButton = nullptr;
    juce::TextButton* openButton = nullptr;
    for (auto* child : player->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getButtonText() == "Assign path") assignButton = button;
            if (button->getButtonText() == "Open in DAW") openButton = button;
        }
    check (assignButton && ! assignButton->isEnabled(), "Assign path is available without a selected entry");
    check (openButton && ! openButton->isEnabled(), "Open in DAW is available without a selected entry");
    check (openButton->getTooltip().isEmpty(), "Disabled Open in DAW still shows a tooltip");
    auto* search = childOf<juce::TextEditor> (*list);
    auto* rows = childOf<juce::ListBox> (*list);
    check (search && rows, "Design scene lacks search or rows");
    // Loading the real OGG renders a waveform without opening a physical device.
    list->onEntrySelected = [] (const ProjectEntry&) {};
    list->onEntryPlay = list->onEntrySelected;
    for (auto size : { juce::Point<int> (640, 480), juce::Point<int> (1024, 768) })
    {
        scene->setSize (size.x, size.y);
        auto* foldersPanel = childOf<FoldersSidebarComponent> (*scene);
        check (foldersPanel && foldersPanel->getX() == 6 && foldersPanel->getWidth() == 216,
            "Folder surface lost its outer inset or original width");
        auto* folderTree = childOf<juce::TreeView> (*foldersPanel);
        check (folderTree != nullptr, "Folder tree is missing");
        const auto folderImage = foldersPanel->createComponentSnapshot (foldersPanel->getLocalBounds(), true, 1, juce::SoftwareImageType{});
        const int allCentre = folderTree->getY() + 17;
        int selectionLeft = 0, selectionRight = folderImage.getWidth() - 1;
        while (selectionLeft < folderImage.getWidth() && folderImage.getPixelAt (selectionLeft, allCentre) != RememberUI::colour (Pal::Selected)) ++selectionLeft;
        while (selectionRight >= 0 && folderImage.getPixelAt (selectionRight, allCentre) != RememberUI::colour (Pal::Selected)) --selectionRight;
        check (selectionLeft == 10 && folderImage.getWidth() - 1 - selectionRight == selectionLeft,
            "All selection has unequal side margins");
        for (auto* child : foldersPanel->getChildren())
        {
            if (auto* label = dynamic_cast<juce::Label*> (child))
                check (label->getX() == RememberUI::folderIconLeading, "Folders does not align with All's icon");
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Sort")
                check (button->getRight() == selectionRight + 1, "Folder Sort does not align with All selection");
        }
        check (launchers->getX() == 22 && launchers->getRight() <= 222
            && launchers->getBottom() <= player->getY(), "DAW launchers moved away from below the folders");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
        check (search->getWidth() >= 160 && search->getHeight() >= 32, "Minimum window clipped the search target");
        check (search->getJustificationType() == juce::Justification::centredLeft, "Search text is not vertically centred");
        check (rows->getX() == search->getX(), "Preview list does not share the search leading edge");
        check (list->getY() == 70 && search->getY() == 8 && list->getX() == foldersPanel->getRight(),
            "Library did not follow the inset folder surface while retaining compact spacing");
        juce::TextButton* settingsControl = nullptr;
        for (auto* child : scene->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle() == "Settings") settingsControl = button;
        check (settingsControl && settingsControl->getProperties()["rememberIcon"].toString() == "settings"
            && settingsControl->getTooltip() == "Settings" && settingsControl->getWidth() == 34,
            "Settings lacks its compact gear and pointer tooltip");
        const int rightEdge = scene->getWidth() - 6;
        check (settingsControl->getRight() == rightEdge && list->getX() + rows->getRight() == rightEdge,
            "Settings and preview surface do not share the player surface trailing edge");
        for (auto* child : list->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Refresh")
                check (list->getX() + button->getRight() == rightEdge, "Refresh does not share the player surface trailing edge");
        const auto playerImage = player->createComponentSnapshot (player->getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
        check (playerImage.getPixelAt (player->getWidth() / 2, 6) == RememberUI::colour (Pal::Content)
            && playerImage.getPixelAt (6, player->getHeight() / 2) == RememberUI::colour (Pal::Content),
            "Player still paints a lighter outline than the preview surface");
        const auto marginsImage = scene->createComponentSnapshot (scene->getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
        check (marginsImage.getPixelAt (5, 60) == RememberUI::colour (Pal::BgDeep)
            && marginsImage.getPixelAt (60, 5) == RememberUI::colour (Pal::BgDeep)
            && marginsImage.getPixelAt (7, 7) == RememberUI::colour (Pal::BgDeep)
            && marginsImage.getPixelAt (7, 60) == RememberUI::colour (Pal::Surface),
            "Folder surface is not inset equally from the top and left with rounded upper corners");
        juce::Label* count = nullptr;
        juce::TextButton* filters = nullptr;
        for (auto* child : list->getChildren())
        {
            if (child->getTitle() == "Recording count") count = dynamic_cast<juce::Label*> (child);
            if (child->getTitle() == "Filter projects") filters = dynamic_cast<juce::TextButton*> (child);
        }
        check (count && filters && count->getText() == "1 item", "Recording count is missing or stale");
        juce::TextButton* clear = nullptr;
        juce::TextButton* save = nullptr;
        for (auto* child : list->getChildren())
        {
            if (child->getTitle() == "Clear filters") clear = dynamic_cast<juce::TextButton*> (child);
            if (child->getTitle() == "Save or remove smart collection") save = dynamic_cast<juce::TextButton*> (child);
        }
        check (clear && save && clear->getX() == save->getRight() + 8
            && list->getLocalBounds().contains (clear->getBounds()) && ! clear->getBounds().intersects (count->getBounds()),
            "Clear filters is clipped or overlaps the count");
        check (list->getX() + count->getRight() == rightEdge, "Recording count does not share the outer trailing edge");
        check (count->getY() == filters->getY() && count->getHeight() == filters->getHeight()
            && ! count->getBounds().intersects (filters->getBounds()), "Recording count is not aligned with Filters");
        check (rows->getY() - filters->getBottom() == 8, "Toolbar still reserves an empty counter row");
        juce::ComboBox* sortControl = nullptr;
        juce::ComboBox* viewControl = nullptr;
        juce::TextButton* favorites = nullptr;
        for (auto* child : list->getChildren())
        {
            if (child->getTitle() == "Sort projects") sortControl = dynamic_cast<juce::ComboBox*> (child);
            if (child->getTitle() == "Library view") viewControl = dynamic_cast<juce::ComboBox*> (child);
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Favorites") favorites = button;
        }
        check (sortControl && viewControl && favorites && sortControl->getRight() == favorites->getRight(),
            "Sort does not share Favorites' trailing edge");
        check (viewControl->getRight() + 8 == sortControl->getX() && viewControl->getY() == sortControl->getY(),
            "Library view is not beside sort");
        check (sortControl->getWidth() >= 152 && sortControl->getWidth() < 180,
            "Sort lost its compact width for BPM descending");
        sortControl->setSelectedId (5, juce::dontSendNotification);
        saveImage (*scene, size.x == 640 ? "apple-sort-longest-minimum.png" : "apple-sort-longest.png", true);
        sortControl->setSelectedId (1, juce::dontSendNotification);
        check (sortControl->getY() - search->getBottom() == rows->getY() - filters->getBottom(),
            "Search/filter gap differs from the filter/preview gap");
        check (list->getX() + rows->getX() - foldersPanel->getRight() == 6
            && player->getY() + 6 - (list->getY() + rows->getBottom()) == 6,
            "Horizontal panel separation differs from the gap above transport");
        check (launchers->getX() - foldersPanel->getX() == 16 && launchers->getWidth() == 184,
            "DAW launcher side padding changed");
        auto* dawViewport = childOf<juce::Viewport> (*launchers);
        if (! dawViewport->getHorizontalScrollBar().isVisible())
            check (player->getY() - launchers->getBottom() == 16, "DAW bottom padding differs from side padding");
        const auto originalDark = RememberUI::isDark();
        for (bool dark : { false, true })
        {
            RememberUI::setDark (dark);
            const auto previewSurface = rows->createComponentSnapshot (rows->getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
            const auto transportSurface = player->createComponentSnapshot (player->getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
            check (previewSurface.getPixelAt (12, rows->getHeight() - 12) == transportSurface.getPixelAt (player->getWidth() / 2, 6),
                "Preview and transport surfaces have different background colours");
        }
        RememberUI::setDark (originalDark);
        check (rows->getX() == filters->getX(), "Preview list does not share the Filters leading edge");
        for (auto* component : scene->getChildren())
            check (scene->getLocalBounds().contains (component->getBounds()), "Main layout places a component outside its bounds");
        rows->deselectAllRows();
        saveImage (*scene, size.x == 640 ? "apple-app-unselected-minimum.png" : "apple-app-unselected.png", true);
        rows->selectRow (0);
        saveImage (*scene, size.x == 640 ? "apple-app-minimum.png" : "apple-app.png", true);
        saveImage (*search, size.x == 640 ? "search-placeholder-minimum.png" : "search-placeholder.png", true);
        search->setText ("Midnight sketches", false);
        saveImage (*search, size.x == 640 ? "search-typed-minimum.png" : "search-typed.png", true);
        search->setText ({}, false);
        const auto listImage = rows->createComponentSnapshot (rows->getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
        check (listImage.getPixelAt (0, 0) == RememberUI::colour (Pal::BgDeep), "Preview surface retained a square outer corner");
        if (auto* row = rows->getComponentForRowNumber (0))
        {
            const auto image = row->createComponentSnapshot (row->getLocalBounds(), true, 1.0f, juce::SoftwareImageType{});
            check (image.getPixelAt (0, row->getHeight() / 2) == RememberUI::colour (Pal::Selected)
                && image.getPixelAt (row->getWidth() - 1, row->getHeight() / 2) == RememberUI::colour (Pal::Selected),
                "Selection does not cover both horizontal edges of the preview surface");
        }
        // Capture the native tooltip with the same semantic look and feel.
        auto tips = juce::SharedResourcePointer<RememberUI::TooltipWindow>::getSharedObjectWithoutCreating();
        if (tips.has_value())
        {
            (*tips)->setLookAndFeel (&scene->getLookAndFeel());
            (*tips)->displayTip ({ 50, 50 }, RememberUI::shortcutTooltip ("Previous preview", "Ctrl+Left",
                "Restart the current preview after 3 seconds; otherwise play the previous preview."));
            saveImage (**tips, size.x == 640 ? "keyboard-tooltip-minimum.png" : "keyboard-tooltip.png", true);
            (*tips)->hideTip();
            (*tips)->setLookAndFeel (nullptr);
        }
    }
    {
        DawLauncher launcherLayout;
        awaitUi ([&] { return launcherLayout.hasDiscoveredApplications(); }, 15000);
        for (int count : { 1, 4, 8 })
        {
            std::vector<DawLauncher::Application> apps;
            for (int i = 0; i < count; ++i) apps.push_back ({ "DAW " + juce::String (i), {}, {} });
            launcherLayout.setApplications (apps);
            launcherLayout.setSize (184, launcherLayout.preferredHeight (184));
            auto* viewport = childOf<juce::Viewport> (launcherLayout);
            check (viewport && viewport->getViewArea().getHeight() >= 32, "DAW overflow scrollbar clips the launcher buttons");
            auto* body = viewport->getViewedComponent();
            for (int i = 1; i < body->getNumChildComponents(); ++i)
                check (body->getChildComponent (i)->getX() - body->getChildComponent (i - 1)->getRight() == 16,
                    "DAW launcher horizontal spacing differs from side padding");
            viewport->setViewPosition (10000, 0);
            check (body->getChildComponent (count - 1)->getRight() <= viewport->getViewArea().getRight(), "Last overflow DAW cannot be reached");
        }
    }
    {
        PreviewFolderSetup setup ([] {});
        saveImage (setup, "apple-preview-folder-setup.png", true);
    }
    scene->setSize (1920, 1080);
    check (list->getX() + rows->getRight() == 1914 && list->getX() == 222,
        "Wide layout lost the player trailing edge or shifted Library origin");
    for (auto* child : scene->getChildren())
        if (child->getTitle() == "Settings") check (child->getRight() == 1914, "Wide Settings lost its shared trailing edge");
    saveImage (*scene, "apple-app-wide.png", true);
    scene->setSize (1024, 768);
    player->loadEntry (ProjectScanner::scan()[0]);
    check (assignButton->isEnabled(), "Assign path is unavailable for a selected entry");
    check (openButton->isEnabled(), "Open in DAW is unavailable for a selected entry with a verified project");
    check (openButton->getTooltip().contains ("Ctrl+O"), "Available Open in DAW lost its shortcut tooltip");
    juce::TextButton* repeatButton = nullptr;
    for (auto* child : player->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle().startsWith ("Repeat:")) repeatButton = button;
    check (repeatButton != nullptr, "Player has no Repeat options");
    RememberUI::ContextMenuButton* metronomeButton = nullptr;
    for (auto* child : player->getChildren())
        if (auto* button = dynamic_cast<RememberUI::ContextMenuButton*> (child); button && button->getTitle() == "Metronome") metronomeButton = button;
    check (metronomeButton && ! player->isMetronomeEnabled() && metronomeButton->getX() == repeatButton->getRight(), "Metronome is not disabled by default beside Repeat");
    player->setMetronomeEnabled (true);
    metronomeButton->onContextMenu();
    auto* metroPopover = juce::Component::getCurrentlyModalComponent();
    check (metroPopover != nullptr && player->isMetronomeEnabled(), "Metronome context action changed its enabled state");
    auto* metronomeSlider = childOf<juce::Slider> (*metroPopover);
    for (auto* content : metroPopover->getChildren())
        if (metronomeSlider == nullptr) metronomeSlider = childOf<juce::Slider> (*content);
    check (metronomeSlider != nullptr, "Metronome context action lacks a volume bar");
    metronomeSlider->setValue (35, juce::sendNotificationSync);
    check (std::abs (player->getMetronomeVolume() - 0.35) < 0.001, "Metronome volume bar does not update playback");
    saveImage (*metroPopover, "apple-metronome-volume.png", true);
    if (auto* callout = dynamic_cast<juce::CallOutBox*> (metroPopover)) callout->dismiss();
    else metroPopover->exitModalState (0);
    awaitUi ([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
    player->setMetronomeEnabled (false);

    auto* repeatContext = dynamic_cast<RememberUI::ContextMenuButton*> (repeatButton);
    check (repeatContext != nullptr, "Repeat lacks a separate context action");
    for (const auto* mode : { "all", "one", "single", "off" })
    {
        repeatButton->onClick();
        check (repeatButton->getTitle() == "Repeat: " + juce::String (mode)
               && juce::Component::getCurrentlyModalComponent() == nullptr, "Left Repeat click did not cycle without a menu");
    }
    const auto point = repeatContext->getLocalBounds().toFloat().getCentre();
    const auto time = juce::Time::getCurrentTime();
    juce::MouseEvent rightClick (juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        repeatContext, repeatContext, time, point, time, 1, false);
    repeatContext->mouseDown (rightClick); repeatContext->mouseUp (rightClick);
    check (repeatButton->getTitle() == "Repeat: off", "Right Repeat click cycled the current mode");
    auto* repeatMenu = juce::Component::getCurrentlyModalComponent();
    check (repeatMenu != nullptr, "Repeat options did not open");
    saveImage (*repeatMenu, "apple-repeat-options.png", true);
    repeatMenu->exitModalState (4);
    awaitUi ([&] { return repeatButton->getTitle() == "Repeat: single"; });
    scene->setSize (640, 480); saveImage (*scene, "apple-single-minimum.png", true);
    scene->setSize (1024, 768); saveImage (*scene, "apple-single.png", true);
    {
        RememberUI::ZoomView zoomScene (std::make_unique<MainComponent>());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        for (auto percent : RememberUI::zoomPercentages)
            for (auto size : { juce::Point<int> (640, 480), juce::Point<int> (1024, 768) })
            {
                RememberUI::appZoom().set (percent);
                zoomScene.setSize (size.x, size.y);
                auto* content = zoomScene.getContent();
                check (content->getWidth() >= 640 && content->getHeight() >= 480, "Zoom lost minimum logical layout");
                auto* viewport = childOf<juce::Viewport> (zoomScene);
                viewport->setViewPosition (10000, 10000);
                const auto bottom = viewport->getViewArea().getBottom();
                check (bottom >= juce::roundToInt (content->getHeight() * percent / 100.0f) - 2, "Large zoom cannot scroll to bottom controls");
                viewport->setViewPosition (0, 0);
                const auto filename = "zoom-app-" + juce::String (percent) + "-" + juce::String (size.x) + ".png";
                saveImage (zoomScene, filename.toRawUTF8(), true);
            }
        RememberUI::appZoom().set (100);
    }
    awaitUi ([&] { return player->hasLoadedEntry(); });
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    check (! player->isAudioDeviceOpen(), "Design snapshot opened an audio device");
    scene->setSize (640, 480); saveImage (*scene, "apple-app-waveform-minimum.png", true);
    scene->setSize (1024, 768);
    saveImage (*scene, "apple-app-waveform.png", true);
    {
        // Dispatch through a native peer so focused editors/listeners get the
        // first chance to consume keys, as they do in the standalone app.
        scene->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        scene->setVisible (true);
        scene->getPeer()->grabFocus();
        scene->grabKeyboardFocus();
        auto press = [&] (int code, int modifiers = 0, juce::juce_wchar text = 0)
        { return scene->getPeer()->handleKeyPress (juce::KeyPress (code, modifiers, text)); };
        check (press ('F', juce::ModifierKeys::ctrlModifier) && search->hasKeyboardFocus (false), "Ctrl+F did not focus search");
        search->setText ({}, false);
        press ('M', 0, 'm'); press ('S', 0, 's'); press ('L', 0, 'l');
        check (search->getText() == "msl" && ! player->isMuted(), "Transport shortcuts intercepted typing");
        search->setText ({}, false);
        const int favoritesKey = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
        juce::TextButton* favorites = nullptr;
        juce::TextButton* shuffle = nullptr;
        for (auto* child : list->getChildren()) if (auto* b = dynamic_cast<juce::TextButton*> (child); b && b->getTitle() == "Filter favorites") favorites = b;
        for (auto* child : player->getChildren()) if (auto* b = dynamic_cast<juce::TextButton*> (child); b && b->getTitle() == "Shuffle previews") shuffle = b;
        check (favorites && shuffle, "Keyboard controls are missing");
        check (press ('F', favoritesKey) && favorites->getToggleState() && search->getText().isEmpty(), "Ctrl+Shift+F did not filter from search");
        press ('F', favoritesKey); check (! favorites->getToggleState(), "Favorites shortcut did not restore its filter");
        rows->deselectAllRows();
        check (press ('O', juce::ModifierKeys::ctrlModifier), "Ctrl+O was not handled with an empty selection");
        check (press (',', juce::ModifierKeys::ctrlModifier, ',') && juce::Component::getCurrentlyModalComponent(), "Ctrl+, typed into search instead of opening Settings");
        juce::Component::getCurrentlyModalComponent()->exitModalState (0);
        awaitUi ([] { return juce::Component::getNumCurrentlyModalComponents() == 0; });
        scene->getPeer()->grabFocus();
        scene->grabKeyboardFocus();
        press ('M'); check (player->isMuted() && player->getOutputGain() == 0, "M did not mute");
        press ('M'); check (! player->isMuted(), "M did not unmute");
        const bool wasShuffled = shuffle->getToggleState();
        press ('S'); check (shuffle->getToggleState() != wasShuffled, "S did not shuffle");
        press ('S'); check (shuffle->getToggleState() == wasShuffled, "S did not restore shuffle");
        const auto repeatTitle = repeatButton->getTitle();
        press ('L'); check (repeatButton->getTitle() != repeatTitle && repeatButton->getTooltip().contains ("(L)"), "L did not change repeat and its hint");
        press ('L'); press ('L'); press ('L'); check (repeatButton->getTitle() == repeatTitle, "Repeat shortcut did not cycle all modes");
        juce::Label* time = nullptr;
        for (auto* child : player->getChildren()) if (auto* label = dynamic_cast<juce::Label*> (child); label && label->getText().contains (" / ")) time = label;
        check (time != nullptr, "Loaded preview has no time label");
        for (int i = 0; i < 10; ++i) press (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier);
        check (time->getText().upToFirstOccurrenceOf (" / ", false, false) == time->getText().fromFirstOccurrenceOf (" / ", false, false), "Alt+Right did not clamp to the end");
        for (int i = 0; i < 10; ++i) press (juce::KeyPress::leftKey, juce::ModifierKeys::altModifier);
        check (time->getText().startsWith ("0:00 / ") && ! player->isAudioDeviceOpen(), "Alt+Left did not clamp without opening audio");
        scene->removeFromDesktop();
        std::cout << "PASS: native keyboard search/settings/open-empty/favorites, editor typing, mute/shuffle/repeat and bounded seeks without audio\n";
    }
    search->setText ("no matching project", true);
    awaitUi ([&] { return list->getNumRows() == 0; });
    check (! rows->isVisible(), "An empty list hides the no-results message");
    saveImage (*scene, "apple-app-search-empty.png", true);
    search->setText ({}, true);
    awaitUi ([&] { return list->getNumRows() == 1; });
    check (rows->isVisible(), "Clearing search did not restore the list");
    rows->selectRow (0);
    {
        SettingsDialog settings (false);
        awaitUi ([&] { return settings.hasLoadedSettings(); });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
        auto* folders = childOf<juce::ListBox> (settings);
        std::vector<juce::ToggleButton*> recordingSwitches;
        for (auto* child : settings.getChildren())
            if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child)) recordingSwitches.push_back (toggle);
        std::sort (recordingSwitches.begin(), recordingSwitches.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
        check (recordingSwitches.size() == 4, "Settings recording switches missing");
        for (size_t i = 1; i < recordingSwitches.size(); ++i)
            check (recordingSwitches[i]->getBounds().getCentreY() - recordingSwitches[i - 1]->getBounds().getCentreY() == 30,
                "Record in mono and the remaining switches have unequal spacing");
        check (folders != nullptr, "Settings has no folder list");
        juce::TextButton* compact = nullptr;
        juce::TextEditor* compactLimit = nullptr;
        for (auto* child : settings.getChildren())
        {
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle() == "Compact large previews") compact = button;
            if (auto* editor = dynamic_cast<juce::TextEditor*> (child); editor && editor->getTitle() == "Compaction limit in MB") compactLimit = editor;
        }
        check (compact && compactLimit && compact->isEnabled() && compactLimit->getText().getDoubleValue() == 10,
            "Settings lacks the default 10 MB compaction controls");
        check (compact->getRight() < compactLimit->getX() && compactLimit->getBottom() <= folders->getY(),
            "Compaction controls overlap each other or project folders");
        const auto beforeCompactionPrompt = LibraryStore::configFile().loadFileAsString();
        compact->onClick();
        awaitUi ([] { return dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent()) != nullptr; });
        auto* compactionPrompt = dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent());
        check (compactionPrompt->findColour (juce::AlertWindow::backgroundColourId) == RememberUI::colour (Pal::BgDeep),
            "Compaction confirmation does not follow the shared appearance");
        const auto promptOptions = PreviewCompactionActions::confirmation ("All previews including recovery");
        check (compactionPrompt->getName() == promptOptions.getTitle() && promptOptions.getMessage().contains ("including recovery")
            && promptOptions.getMessage().contains ("lossy") && promptOptions.getMessage().contains ("cannot be undone"),
            "Compaction confirmation omits total size, quality loss or permanent deletion");
        saveImage (*compactionPrompt, "apple-preview-compaction-confirm.png", true);
        compactionPrompt->exitModalState (0);
        awaitUi ([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
        check (LibraryStore::configFile().loadFileAsString() == beforeCompactionPrompt,
            "Cancelling compaction changed saved settings");
        juce::Label* estimate = nullptr;
        juce::Label* wavNotice = nullptr;
        juce::ComboBox* duration = nullptr;
        juce::ComboBox* quality = nullptr;
        juce::ToggleButton* mono = nullptr;
        juce::ToggleButton* replace = nullptr;
        for (auto* child : settings.getChildren())
        {
            if (auto* label = dynamic_cast<juce::Label*> (child))
            {
                if (label->getTitle() == "Estimated preview storage") estimate = label;
                if (label->getTitle() == "Preview storage notice") wavNotice = label;
            }
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child))
            {
                if (combo->getTitle() == "Capture duration") duration = combo;
                if (combo->getTitle() == "OGG quality") quality = combo;
            }
            if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
            {
                if (toggle->getButtonText().startsWith ("Record in mono")) mono = toggle;
                if (toggle->getButtonText().startsWith ("Replace whole previews")) replace = toggle;
            }
        }
        check (estimate && wavNotice && duration && quality && mono && replace, "Settings lacks accessible preview storage estimates");
        check (estimate->getText().contains ("recovery copy") && wavNotice->isVisible()
            && wavNotice->getText().contains ("OGG fragments")
            && wavNotice->getBottom() <= folders->getY(), "WAV and recovery storage notice is missing or overlaps folders");
        const auto configBeforeEstimate = LibraryStore::configFile().loadFileAsString();
        const auto originalEstimate = estimate->getText();
        const int originalDuration = duration->getSelectedId(), originalQuality = quality->getSelectedId();
        const bool originalMono = mono->getToggleState(), originalReplace = replace->getToggleState();
        duration->setSelectedId (60, juce::sendNotificationSync);
        mono->setToggleState (false, juce::dontSendNotification); mono->onClick();
        const auto stereoSize = estimate->getText().fromFirstOccurrenceOf ("~", false, false).getDoubleValue();
        duration->setSelectedId (120, juce::sendNotificationSync);
        const auto longSize = estimate->getText().fromFirstOccurrenceOf ("~", false, false).getDoubleValue();
        check (longSize > stereoSize * 1.9 && longSize < stereoSize * 2.1, "Doubling duration did not double preview storage estimate");
        mono->setToggleState (true, juce::dontSendNotification); mono->onClick();
        const auto monoSize = estimate->getText().fromFirstOccurrenceOf ("~", false, false).getDoubleValue();
        check (monoSize <= longSize * 0.65 && monoSize > longSize * 0.5, "Mono storage estimate does not reflect reduced OGG size");
        const auto beforeQuality = estimate->getText();
        quality->setSelectedId (9, juce::sendNotificationSync);
        check (estimate->getText() != beforeQuality, "OGG quality did not update storage estimate");
        replace->setToggleState (! originalReplace, juce::dontSendNotification);
        check (wavNotice->isVisible() && wavNotice->getText().contains ("WAVs are temporary"), "Replacement lost the temporary WAV explanation");
        saveImage (settings, "apple-settings-storage-estimate.png", true);
        juce::TextButton* information = nullptr;
        for (auto* child : settings.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle() == "About preview storage") information = button;
        check (information && information->getProperties()["rememberIcon"].toString() == "info"
            && information->getWidth() >= 30 && information->getY() < duration->getY(), "Preview information lacks its accessible i icon near recording options");
        information->onClick();
        auto* popup = dynamic_cast<juce::CallOutBox*> (juce::Component::getCurrentlyModalComponent());
        check (popup != nullptr, "Storage information did not open an anchored callout");
        juce::TextEditor* explanation = nullptr;
        for (auto* child : popup->getChildren())
            if (auto* editor = childOf<juce::TextEditor> (*child)) explanation = editor;
        check (explanation && explanation->isReadOnly() && explanation->getText().contains (".rmp")
            && explanation->getText().contains ("256") && explanation->getText().contains ("temporary")
            && explanation->getText().contains ("recode"), "Storage information omitted format, limits or temporary files");
        saveImage (*popup, "apple-settings-storage-info-light.png", true);
        RememberUI::setAppearanceMode (RememberAppearance::Dark);
        saveImage (*popup, "apple-settings-storage-info-dark.png", true);
        RememberUI::setAppearanceMode (RememberAppearance::Light);
        popup->dismiss();
        awaitUi ([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
        duration->setSelectedId (originalDuration, juce::sendNotificationSync);
        quality->setSelectedId (originalQuality, juce::sendNotificationSync);
        mono->setToggleState (originalMono, juce::dontSendNotification); mono->onClick();
        replace->setToggleState (originalReplace, juce::dontSendNotification);
        check (estimate->getText() == originalEstimate && LibraryStore::configFile().loadFileAsString() == configBeforeEstimate,
            "Draft storage estimates changed saved settings or failed to restore");
        const auto foldersImage = folders->createComponentSnapshot (folders->getLocalBounds(), true, 1, juce::SoftwareImageType{});
        check (foldersImage.getPixelAt (0, 0) == RememberUI::colour (Pal::BgDeep), "Settings folder list retained square corners");
        auto* row = folders->getComponentForRowNumber (0);
        check (row != nullptr, "Settings folder row is missing");
        auto* remove = childOf<juce::TextButton> (*row);
        check (remove && remove->getWidth() == 32 && remove->getRight() == row->getWidth(), "Folder remove action spans or clips its row");
        juce::TextButton* searchFolders = nullptr;
        juce::Label* searchStatus = nullptr;
        for (auto* child : settings.getChildren())
        {
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle() == "Search project folders") searchFolders = button;
            if (auto* label = dynamic_cast<juce::Label*> (child); label && label->getTitle() == "Project folder search status") searchStatus = label;
        }
        check (searchFolders && searchStatus && searchFolders->isEnabled()
               && searchFolders->getRight() > settings.getWidth() / 2, "Settings lacks its right-aligned folder search");
        const auto nested = projects.getChildFile ("nested/deep");
        check (nested.createDirectory().wasOk() && nested.getChildFile ("New project.als").replaceWithText ("nested fixture"),
               "Cannot create recursive search fixture");
        const auto configBeforeSearch = LibraryStore::configFile().loadFileAsString();
        searchFolders->onClick();
        check (! searchFolders->isEnabled() && searchStatus->getText().contains ("Searching"), "Search has no immediate progress feedback");
        saveImage (settings, "apple-settings-searching.png", true);
        awaitUi ([&] { return searchFolders->isEnabled(); });
        check (searchStatus->getText().contains ("2 project files") && searchStatus->getText().contains ("1 linked"),
               "Manual folder search missed a nested project or its association");
        check (LibraryStore::configFile().loadFileAsString() == configBeforeSearch, "Searching draft folders saved pending settings");
        saveImage (settings, "apple-settings.png", true);
        juce::ComboBox* appearance = nullptr;
        for (auto* child : settings.getChildren())
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child); combo && combo->getTitle() == "Appearance") appearance = combo;
        check (appearance && appearance->isEnabled(), "Settings lacks an enabled appearance option");
        juce::ComboBox* zoom = nullptr;
        for (auto* child : settings.getChildren())
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child); combo && combo->getTitle() == "Interface zoom") zoom = combo;
        check (zoom && zoom->isEnabled() && zoom->getNumItems() == 7, "Settings lacks zoom presets");
        zoom->setSelectedId (125, juce::sendNotificationSync);
        awaitUi ([&] { return RememberUI::appZoom().percent == 125 && zoom->isEnabled(); });
        check (ProjectScanner::readSettings().interfaceZoom == 125, "App failed to persist zoom");
        appearance->setSelectedId (2, juce::sendNotificationSync);
        awaitUi ([&] { return RememberUI::isDark() && appearance->isEnabled(); });
        check (LibraryStore::readJson (LibraryStore::configFile())["appearanceMode"].toString() == "dark", "App did not persist dark appearance");
        check (search->findColour (juce::TextEditor::backgroundColourId) == RememberUI::colour (Pal::Elevated), "Existing search did not change appearance");
        check (rows->getSelectedRow() == 0 && player->hasLoadedEntry(), "Appearance switch lost selection or loaded audio");
        saveImage (settings, "apple-settings-dark.png", true);
        check (ProjectScanner::saveSettings (library, { projects }, 60, 4, false).wasOk()
            && ProjectScanner::readSettings().appearance == RememberAppearance::Dark, "Saving recording options discarded appearance");
        saveImage (*scene, "apple-app-dark.png", true);
        scene->setSize (640, 480);
        saveImage (*scene, "apple-app-dark-minimum.png", true);
        scene->setSize (1024, 768);
    }
    {
        RememberUI::setDark (false);
        auto reopenedApp = std::make_unique<MainComponent>();
        awaitUi ([] { return RememberUI::isDark(); });
    }
    {
        auto tags = TagsEditorDialog::create ("Midnight sketches", { "Ambient", "Work in progress" }, [] (juce::StringArray) {});
        saveImage (*tags, "apple-tags-dark.png", true);
    }
    {
        LibraryStore::RecoveryChoice choice;
        choice.revision = "revision-for-layout-review"; choice.recordedAt = "2026-10-11T12:00:00Z";
        choice.durationSeconds = 60; choice.channels = 2; choice.bpm = 124; choice.key = "A minor";
        auto recovery = RecoveryChoiceDialog::create ({ choice }, [] (auto) {}, [] (auto) {}, false);
        saveImage (*recovery, "apple-recovery-dark.png", true);
    }
    {
        SettingsDialog reopened (false);
        awaitUi ([&] { return reopened.hasLoadedSettings(); });
        juce::ComboBox* zoom = nullptr;
        for (auto* child : reopened.getChildren())
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child); combo && combo->getTitle() == "Interface zoom") zoom = combo;
        check (zoom && zoom->getSelectedId() == 125, "Reopened settings lost zoom after other edits");
        zoom->setSelectedId (100, juce::sendNotificationSync);
        awaitUi ([&] { return RememberUI::appZoom().percent == 100 && zoom->isEnabled(); });
        juce::ComboBox* appearance = nullptr;
        for (auto* child : reopened.getChildren())
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child); combo && combo->getTitle() == "Appearance") appearance = combo;
        check (appearance && appearance->getSelectedId() == 2, "Reopened settings lost dark appearance");
        appearance->setSelectedId (1, juce::sendNotificationSync);
        awaitUi ([&] { return ! RememberUI::isDark() && appearance->isEnabled(); });
        check (LibraryStore::readJson (LibraryStore::configFile())["appearanceMode"].toString() == "light", "App did not persist light appearance");
        check (search->findColour (juce::TextEditor::textColourId) == RememberUI::colour (Pal::Text), "Switching back left dark text colours");
        appearance->setSelectedId (3, juce::sendNotificationSync);
        awaitUi ([&] { return appearance->isEnabled() && RememberUI::appearanceMode == RememberAppearance::System; });
        check (RememberUI::isDark() == juce::Desktop::getInstance().isDarkModeActive()
            && LibraryStore::readJson (LibraryStore::configFile())["appearanceMode"].toString() == "system",
            "App did not persist or resume automatic system appearance");
        saveImage (reopened, "apple-settings-system.png", true);
    }
    {
        auto tags = TagsEditorDialog::create ("Midnight sketches", { "Ambient", "Work in progress" }, [] (juce::StringArray) {});
        saveImage (*tags, "apple-tags.png", true);
        LibraryStore::RecoveryChoice choice;
        choice.revision = "revision-for-layout-review"; choice.recordedAt = "2026-10-11T12:00:00Z";
        choice.durationSeconds = 60; choice.channels = 2; choice.bpm = 124; choice.key = "A minor";
        auto recovery = RecoveryChoiceDialog::create ({ choice }, [] (auto) {}, [] (auto) {}, false);
        saveImage (*recovery, "apple-recovery.png", true);
    }
    player->releaseEntry (ProjectScanner::scan()[0].metadataFile);
    check (! openButton->isEnabled() && openButton->getTooltip().isEmpty(), "Clearing selection retained the Open in DAW tooltip");
    check (! assignButton->isEnabled(), "Assign path stayed available after releasing the entry");
    scene.reset();
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0 && WaveformPlayerComponent::pendingLoads() == 0; });
    std::cout << "PASS: frontend minimum/large layout, waveform without device, no-results and restored search, settings/tags/recovery snapshots\n";
}

void testSidebarUi (const juce::File& root)
{
    // Choose a real popup action, destroy its owner before JUCE dispatches the
    // completion, and prove the completion has actually been delivered.
    {
        auto sidebar = std::make_unique<FoldersSidebarComponent>();
        sidebar->setSize (220, 500);
        auto* tree = childOf<juce::TreeView> (*sidebar);
        check (tree && tree->getRootItem()->getNumSubItems() > 0, "Sidebar has no All item");
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), {},
            juce::ModifierKeys::rightButtonModifier, 1, 0, 0, 0, 0, tree, tree, now, {}, now, 1, false);
        tree->getRootItem()->getSubItem (0)->itemClicked (event);
        juce::Component::SafePointer<juce::Component> popup (juce::Component::getCurrentlyModalComponent());
        check (popup != nullptr, "Folder popup did not open");
        bool completed = false;
        juce::ModalComponentManager::getInstance()->attachCallback (popup,
            juce::ModalCallbackFunction::create ([&] (int result) { check (result == 10, "Popup selected result was lost"); completed = true; }));
        check (popup->keyPressed (juce::KeyPress (juce::KeyPress::upKey))
            && popup->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)), "Folder popup did not accept its last action");
        sidebar.reset();
        awaitUi ([&] { return completed; });
        check (juce::Component::getNumCurrentlyModalComponents() == 0, "Closed sidebar launched a second popup");
    }

    SavedConfig restore;
    const auto library = root.getChildFile ("ui-library"), projects = root.getChildFile ("ui-projects");
    check (projects.createDirectory().wasOk(), "Cannot create UI projects");
    const auto project = projects.getChildFile ("UI.flp");
    check (project.replaceWithText ("UI fixture"), "Cannot create UI project");
    const auto entry = library.getChildFile ("entry");
    check (entry.createDirectory().wasOk(), "Cannot create UI entry");
    juce::var metadata (new juce::DynamicObject());
    auto* data = metadata.getDynamicObject();
    data->setProperty ("projectId", juce::Uuid().toString()); data->setProperty ("projectName", "UI");
    data->setProperty ("projectPath", project.getFullPathName()); data->setProperty ("daw", "FL Studio");
    data->setProperty ("folders", juce::Array<juce::var> { "A", "A/B", "A/B" });
    juce::Array<juce::var> tags;
    juce::StringArray initial;
    for (int i = 0; i < 512; ++i)
    {
        const auto tag = "Tag " + juce::String (i).paddedLeft ('0', 3);
        tags.add (tag); tags.add (tag.toUpperCase());
        if (i < 256) initial.add (tag);
    }
    const auto longTag = juce::String::repeatedString ("Long tag ", 70);
    initial.add (longTag);
    data->setProperty ("tags", tags);
    check (LibraryStore::writeJsonAtomically (entry.getChildFile ("metadata.json"), metadata).wasOk()
           && entry.getChildFile ("preview.ogg").replaceWithText ("index fixture"), "Cannot write UI entry");
    check (ProjectScanner::saveSettings (library, { projects }, 60, 4, false).wasOk(), "Cannot configure UI library");
    juce::var patch (new juce::DynamicObject()); juce::Array<juce::var> paths;
    for (int i = 0; i < 100; ++i) for (int j = 0; j < 10; ++j)
        paths.add ("Batch " + juce::String (i).paddedLeft ('0', 3) + "/Child " + juce::String (j));
    patch.getDynamicObject()->setProperty ("explicitFolders", paths);
    check (LibraryStore::patchJson (LibraryStore::configFile(), patch).wasOk(), "Cannot configure UI folders");
    ProjectScanner::refreshAsync();
    awaitUi ([&] { const auto entries = ProjectScanner::scan(); return entries.size() == 1 && entries[0].metadataFile == entry.getChildFile ("metadata.json")
        && ProjectScanner::getAllFolderPaths().size() == 1102; });
    check (ProjectScanner::getAllUniqueTags().size() == 512, "Case variants multiplied the tag inventory");
    auto sidebar = std::make_unique<FoldersSidebarComponent>(); sidebar->setSize (220, 500);
    auto* tree = childOf<juce::TreeView> (*sidebar);
    std::set<juce::String> names;
    std::function<void(juce::TreeViewItem*)> walk = [&] (juce::TreeViewItem* item)
    {
        for (int i = 0; i < item->getNumSubItems(); ++i)
        {
            auto* child = item->getSubItem (i);
            check (names.insert (child->getUniqueName()).second, "Folder tree duplicated an item"); walk (child);
        }
    };
    walk (tree->getRootItem()); check (names.size() == 1103, "Folder tree omitted paths");
    sidebar->selectFolder ("A/B"); sidebar->refresh();
    check (tree->getSelectedItem (0) && tree->getSelectedItem (0)->getUniqueName() == "A/B", "Tree rebuild lost selected identity");
    tree->getRootItem()->getSubItem (1)->setOpen (true);
    check (tree->getRootItem()->getSubItem (1)->getTooltip() == "A (1 entry)"
        && tree->getRootItem()->getSubItem (1)->getSubItem (0)->getTooltip() == "A/B (1 entry)", "Ancestor membership inflated folder counts");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (tree->getNumRowsInTree() >= 103, "Folder rows are not visible after publication/layout dispatch");
    saveImage (*sidebar, "folders-large.png");
    {
        const auto now = juce::Time::getCurrentTime();
        const auto point = juce::Point<float> (2, (float) tree->getRootItem()->getSubItem (0)->getItemPosition (true).getCentreY());
        juce::MouseEvent gutterClick (juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, tree, tree, now, point, now, 1, false);
        sidebar->mouseDown (gutterClick); sidebar->mouseUp (gutterClick);
        check (sidebar->getSelectedFolders().isEmpty() && tree->getSelectedItem (0)->getUniqueName() == "__ALL__",
            "All's newly unindented gutter is not clickable");
        saveImage (*sidebar, "folders-all-unindented.png");
        sidebar->selectFolder ("A/B");
    }

    const auto clickFolder = [&](juce::TreeViewItem* item, int modifiers)
    {
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), {},
            juce::ModifierKeys (modifiers | juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            tree, tree, now, {}, now, 1, false);
        const bool selectedBeforeDown = item->isSelected();
        if (! selectedBeforeDown) item->setSelected (true, ! event.mods.isCtrlDown());
        item->itemClicked (event);
        if (selectedBeforeDown) item->setSelected (! event.mods.isCtrlDown() || ! item->isSelected(), ! event.mods.isCtrlDown());
        sidebar->mouseUp (event);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    };
    auto rootItem = [&] (int index) { return tree->getRootItem()->getSubItem (index); };
    clickFolder (rootItem (2), 0); // Batch 000
    clickFolder (rootItem (4), juce::ModifierKeys::altModifier); // Batch 002
    check (sidebar->getSelectedFolders().size() == 3 && tree->getNumSelectedItems() == 3
        && sidebar->getSelectedFolders().contains ("Batch 001"), "Alt-click did not select the contiguous visible range");
    clickFolder (rootItem (6), juce::ModifierKeys::ctrlModifier);
    check (sidebar->getSelectedFolders().size() == 4 && sidebar->getSelectedFolders().contains ("Batch 004"), "Ctrl-click did not add a separate folder");
    clickFolder (rootItem (3), juce::ModifierKeys::ctrlModifier);
    check (sidebar->getSelectedFolders().size() == 3 && ! sidebar->getSelectedFolders().contains ("Batch 001"), "Ctrl-click did not toggle a selected folder");
    sidebar->refresh();
    check (sidebar->getSelectedFolders().size() == 3 && tree->getNumSelectedItems() == 3, "Refresh lost multiple folder identities");
    {
        const auto selected = sidebar->getSelectedFolders();
        const auto current = sidebar->getSelectedFolder();
        int notifications = 0;
        sidebar->onFoldersSelected = [&] (juce::StringArray) { ++notifications; };
        for (auto* target : { rootItem (5), rootItem (2), rootItem (0) })
        {
            // Match TreeView's mouse-down ordering, including its selection
            // before the item's context callback on an unselected row.
            if (! target->isSelected()) target->setSelected (true, true);
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), {},
                juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1, 0, 0, 0, 0,
                tree, tree, now, {}, now, 1, false);
            target->itemClicked (event);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            check (sidebar->getSelectedFolders() == selected && sidebar->getSelectedFolder() == current
                && tree->getNumSelectedItems() == selected.size() && notifications == 0,
                "Folder context menu changed selection or the library filter");
            juce::PopupMenu::dismissAllActiveMenus();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        sidebar->onFoldersSelected = nullptr;
    }
    rootItem (5)->setSelected (true, true); // native keyboard/accessibility selection
    awaitUi ([&] { return sidebar->getSelectedFolder() == "Batch 003" && sidebar->getSelectedFolders().size() == 1; });
    {
        ProjectListComponent selectedList;
        selectedList.setSize (700, 500);
        sidebar->onFoldersSelected = [&] (juce::StringArray paths) { selectedList.setFolderFilters (paths); };
        clickFolder (rootItem (2), 0);
        check (selectedList.getNumRows() == 0, "Empty selected folder did not filter the library");
        clickFolder (rootItem (1), juce::ModifierKeys::ctrlModifier); // union with A
        check (selectedList.getNumRows() == 1, "Multiple folders did not combine their recordings");
        auto* nested = rootItem (1)->getSubItem (0);
        clickFolder (nested, juce::ModifierKeys::ctrlModifier);
        check (selectedList.getNumRows() == 1, "Overlapping selected folders duplicated recordings");
        clickFolder (rootItem (0), juce::ModifierKeys::ctrlModifier);
        check (sidebar->getSelectedFolders().isEmpty() && tree->getNumSelectedItems() == 1
            && selectedList.getNumRows() == 1, "All did not reset multiple folder filtering");
        sidebar->onFoldersSelected = nullptr;
    }
    sidebar->selectFolder ("A/B");

    const auto originalMetadata = LibraryStore::readJson (entry.getChildFile ("metadata.json"));
    const auto folderDrag = [&](const juce::String& path)
    { return juce::DragAndDropTarget::SourceDetails ("folder:" + path, sidebar.get(), {}); };
    check (tree->getRootItem()->isInterestedInDragSource (folderDrag ("A/B")), "Empty tree area rejects folder extraction");
    tree->getRootItem()->itemDropped (folderDrag ("A/B"), 1);
    awaitUi ([] { return ProjectScanner::getAllFolderPaths().contains ("B") && ! ProjectScanner::getAllFolderPaths().contains ("A/B"); });
    sidebar->refresh();
    check (sidebar->getSelectedFolder() == "B", "Extraction lost the selected folder");
    check (tree->getRootItem()->getSubItem (1)->getUniqueName() == "B", "Root drop did not preserve insertion position");
    check (ProjectScanner::getSidebarSort() == ProjectScanner::SidebarSort::Manual, "Drag did not persist manual ordering");
    tree->getRootItem()->itemDropped (folderDrag ("A"), 1);
    awaitUi ([] { return ProjectScanner::getAllFolderPaths()[0] == "A"; });
    sidebar->refresh();
    check (tree->getRootItem()->getSubItem (1)->getUniqueName() == "A"
        && tree->getRootItem()->getSubItem (2)->getUniqueName() == "B", "Sibling reorder nested a folder instead");
    ProjectScanner::editAsync ([] { return ProjectScanner::undoOrganization(); });
    awaitUi ([] { return ProjectScanner::getAllFolderPaths()[0] == "B"; });
    sidebar->refresh();
    tree->getRootItem()->itemDropped (folderDrag ("A"), 1);
    awaitUi ([] { return ProjectScanner::getAllFolderPaths()[0] == "A"; });
    sidebar->refresh();
    auto* destination = tree->getRootItem()->getSubItem (3);
    const auto destinationPath = destination->getUniqueName();
    check (destination->isInterestedInDragSource (folderDrag ("B")), "Folder centre rejects nesting");
    destination->itemDropped (folderDrag ("B"), 0);
    awaitUi ([&] { return ProjectScanner::getAllFolderPaths().contains (destinationPath + "/B"); });
    sidebar->refresh();
    check (sidebar->getSelectedFolder() == destinationPath + "/B", "Nesting lost selected identity");
    const auto movedMetadata = LibraryStore::readJson (entry.getChildFile ("metadata.json"));
    check (movedMetadata.getProperty ("folders", {}).getArray()->contains (destinationPath + "/B"), "Move lost recording memberships");
    check (movedMetadata.getProperty ("projectId", {}) == originalMetadata.getProperty ("projectId", {}), "Move changed recording identity");
    auto checkedInvalid = std::make_shared<bool> (false);
    ProjectScanner::queryAsync ([destinationPath]
    {
        return ProjectScanner::positionFolder (destinationPath, destinationPath + "/B");
    }, [checkedInvalid] (juce::Result result) { check (result.failed(), "Cycle was accepted"); *checkedInvalid = true; });
    awaitUi ([&] { return *checkedInvalid; });

    // The header creates a sibling of the selection, with an explicit child
    // action retained in the contextual menu.
    for (auto* child : sidebar->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle() == "New folder")
        {
            button->onClick();
            auto* dialog = dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent());
            check (dialog != nullptr, "New sibling dialog did not open");
            dialog->getTextEditor ("name")->setText ("Sibling");
            dialog->exitModalState (1);
            awaitUi ([&] { return ProjectScanner::getAllFolderPaths().contains (destinationPath + "/Sibling"); });
            check (! ProjectScanner::getAllFolderPaths().contains (destinationPath + "/B/Sibling"), "New folder was created inside the selection");
        }
    saveImage (*sidebar, "folders-manual-order.png");

    // Measure content construction with no native peer before measuring launch.
    // Each profile runs cold-to-warm in this order; do not attribute native peer
    // startup or driver/font initialization to the controls alone.
    for (const auto count : { 100, 512, 10000 })
    {
        juce::StringArray large;
        for (int i = 0; i < count; ++i) large.add ("Tag " + juce::String (i).paddedLeft ('0', 5));
        const auto start = juce::Time::getMillisecondCounterHiRes();
        juce::StringArray contentSaved;
        auto contentOnly = TagsEditorDialog::create ("Tags content benchmark", large,
            [&] (juce::StringArray tags) { contentSaved = std::move (tags); });
        const auto constructMs = juce::Time::getMillisecondCounterHiRes() - start;
        // Instrumented JUCE text shaping for 10,000 labels can exceed ten
        // seconds. This bounds background readiness, not UI construction or
        // responsiveness; the suite-level CTest deadline is unchanged.
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; }, 60000);
        const auto readyMs = juce::Time::getMillisecondCounterHiRes() - start;
        int controls = 0;
        for (auto* child : contentOnly->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
            controls += viewport->getViewedComponent()->getNumChildComponents();
        std::cout << "TAGS contentOnly=" << count << " constructMs=" << constructMs << " readyMs=" << readyMs << " liveControls=" << controls << std::endl;
        check (controls > 0 && controls <= 256, "Tag content keeps unbounded offscreen controls");
        if (count == 10000) saveImage (*contentOnly, "tags-10000-top.png", true);
        bool lastReachable = false;
        for (auto* child : contentOnly->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
        {
            for (int step = 0; step <= 100; ++step)
            {
                viewport->setViewPosition (0, viewport->getViewedComponent()->getHeight() * step / 100);
                check (viewport->getViewedComponent()->getNumChildComponents() <= 128, "Scrolling retains offscreen tag controls");
            }
            for (auto* visible : viewport->getViewedComponent()->getChildren())
                if (auto* tag = dynamic_cast<juce::TextButton*> (visible))
                    lastReachable |= tag->getButtonText() == "#Tag " + juce::String (count - 1).paddedLeft ('0', 5) + "  x";
        }
        check (lastReachable, "A virtual tag collection lost its last tag");
        if (count == 10000)
        {
            saveImage (*contentOnly, "tags-10000-bottom.png", true);
            auto* text = childOf<juce::TextEditor> (*contentOnly);
            text->setText ("# New   large tag", false);
            const auto editStart = juce::Time::getMillisecondCounterHiRes(); text->onReturnKey();
            const auto editMs = juce::Time::getMillisecondCounterHiRes() - editStart;
            juce::Viewport* currentView = nullptr;
            for (auto* child : contentOnly->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
                if (! currentView || viewport->getY() < currentView->getY()) currentView = viewport;
            check (currentView != nullptr, "Current tag viewport is missing");
            currentView->setViewPosition (0, 0);
            juce::TextButton* firstTag = nullptr;
            for (auto* child : currentView->getViewedComponent()->getChildren())
                if (auto* tag = dynamic_cast<juce::TextButton*> (child); tag && tag->getButtonText() == "#Tag 00000  x") firstTag = tag;
            check (firstTag != nullptr, "First large tag cannot be reached");
            firstTag->onClick();
            // Destroy/recycle that button before dispatching its captured action.
            currentView->setViewPosition (0, currentView->getViewedComponent()->getHeight());
            bool actionDelivered = false; juce::MessageManager::callAsync ([&] { actionDelivered = true; });
            awaitUi ([&] { return actionDelivered; });
            for (auto* child : contentOnly->getChildren()) if (auto* action = dynamic_cast<juce::TextButton*> (child); action && action->getButtonText() == "OK") action->onClick();
            check (contentSaved.size() == 10000 && contentSaved.contains ("New large tag") && contentSaved.contains ("Tag 09999")
                   && ! contentSaved.contains ("Tag 00000"), "A recycled large-tag action lost data or changed the wrong tag");
            std::cout << "TAGS edit10000Ms=" << editMs << " committed=" << contentSaved.size() << " recycledAction=preserved\n";
        }
    }
    {
        juce::Array<juce::var> suggestions;
        for (int i = 0; i < 10000; ++i) suggestions.add ("Suggestion " + juce::String (i).paddedLeft ('0', 5));
        check (LibraryStore::updateJson (entry.getChildFile ("metadata.json"), [&] (juce::DynamicObject& object)
            { object.setProperty ("tags", suggestions); return juce::Result::ok(); }).wasOk(), "Cannot publish large tag suggestions");
        ProjectScanner::refreshAsync(); awaitUi ([] { return ProjectScanner::getAllUniqueTags().size() == 10000; });
        juce::StringArray saved;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        auto quick = TagsEditorDialog::create ("10,000 quick-add suggestions", {}, [&] (juce::StringArray value) { saved = std::move (value); });
        const auto constructMs = juce::Time::getMillisecondCounterHiRes() - start;
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; }, 60000);
        const auto readyMs = juce::Time::getMillisecondCounterHiRes() - start;
        juce::TextButton* last = nullptr; juce::Viewport* view = nullptr; int controls = 0;
        for (auto* child : quick->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
        {
            controls += viewport->getViewedComponent()->getNumChildComponents();
            viewport->setViewPosition (0, viewport->getViewedComponent()->getHeight());
            for (auto* item : viewport->getViewedComponent()->getChildren())
                if (auto* tag = dynamic_cast<juce::TextButton*> (item); tag && tag->getButtonText() == "#Suggestion 09999") { last = tag; view = viewport; }
        }
        check (controls > 0 && controls <= 128 && last && view, "Large Quick add loses its last suggestion or keeps unbounded controls");
        saveImage (*quick, "tags-suggestions-10000-bottom.png", true);
        last->onClick(); view->setViewPosition (0, 0);
        bool dispatched = false; juce::MessageManager::callAsync ([&] { dispatched = true; }); awaitUi ([&] { return dispatched; });
        for (auto* child : quick->getChildren()) if (auto* action = dynamic_cast<juce::TextButton*> (child); action && action->getButtonText() == "OK") action->onClick();
        check (saved.size() == 1 && saved[0] == "Suggestion 09999", "A recycled Quick add action selected the wrong tag");
        quick.reset(); awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
        check (LibraryStore::writeJsonAtomically (entry.getChildFile ("metadata.json"), metadata).wasOk(), "Cannot restore tag inventory fixture");
        ProjectScanner::refreshAsync(); awaitUi ([] { return ProjectScanner::getAllUniqueTags().size() == 512; });
        std::cout << "TAGS suggestions=10000 constructMs=" << constructMs << " readyMs=" << readyMs << " liveControls=" << controls << " lastCommit=preserved\n";
    }
    {
        struct Gate
        {
            juce::WaitableEvent entered, release { true };
            ~Gate() { release.signal(); }
        };
        auto gate = std::make_shared<Gate>();
        struct Unblock { std::shared_ptr<Gate> gate; ~Unblock() { gate->release.signal(); } } unblock { gate };
        ProjectScanner::queryAsync ([gate] { gate->entered.signal(); gate->release.wait (10000); return juce::Result::ok(); });
        check (gate->entered.wait (1000), "Cannot hold tag layout query worker");
        juce::StringArray many;
        for (int i = 0; i < 1000; ++i) many.add ("Pending " + juce::String (i).paddedLeft ('0', 4));
        juce::StringArray saved;
        auto editing = TagsEditorDialog::create ("Pending layout", many, [&] (juce::StringArray tags) { saved = std::move (tags); });
        auto closing = TagsEditorDialog::create ("Close pending layout", many, {});
        closing.reset();
        auto* pendingInput = childOf<juce::TextEditor> (*editing);
        check (pendingInput && pendingInput->isEnabled(), "Pending tag layout disables editing");
        pendingInput->setText ("# New pending tag", false); pendingInput->onReturnKey();
        bool dispatched = false; juce::MessageManager::callAsync ([&] { dispatched = true; }); awaitUi ([&] { return dispatched; });
        gate->release.signal(); awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
        bool newTag = false;
        for (auto* child : editing->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
        {
            viewport->setViewPosition (0, viewport->getViewedComponent()->getHeight());
            for (auto* item : viewport->getViewedComponent()->getChildren())
                if (auto* tag = dynamic_cast<juce::TextButton*> (item)) newTag |= tag->getButtonText() == "#New pending tag  x";
        }
        check (newTag, "An older tag layout overwrote the latest edit after its worker was released");
        for (auto* child : editing->getChildren()) if (auto* action = dynamic_cast<juce::TextButton*> (child); action && action->getButtonText() == "OK") action->onClick();
        check (saved.size() == 1001 && saved.contains ("New pending tag"), "Pending layout lost existing or newly edited tags at commit");
        std::cout << "PASS: tag layout stays off UI with its worker held; editing, stale-result rejection and close during pending layout retain all tags\n";
    }
    {
        auto closing = TagsEditorDialog::create ("Close after chip action", { "Closing tag" }, {});
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
        juce::TextButton* chip = nullptr;
        for (auto* child : closing->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
            for (auto* item : viewport->getViewedComponent()->getChildren())
                if (auto* tag = dynamic_cast<juce::TextButton*> (item); tag && tag->getButtonText() == "#Closing tag  x") chip = tag;
        check (chip != nullptr, "Closing tag action is missing");
        chip->onClick(); closing.reset();
        bool delivered = false; juce::MessageManager::callAsync ([&] { delivered = true; }); awaitUi ([&] { return delivered; });
        std::cout << "PASS: chip action queued before dialog destruction is safely cancelled\n";
    }
    juce::StringArray committed;
    const auto begun = juce::Time::getMillisecondCounterHiRes();
    TagsEditorDialog::launch ("Large tag fixture", initial, [&] (juce::StringArray value) { committed = std::move (value); });
    juce::Component::SafePointer<juce::DialogWindow> window;
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        if (auto* candidate = dynamic_cast<juce::DialogWindow*> (juce::Desktop::getInstance().getComponent (i)); candidate && candidate->getName() == "Edit tags") window = candidate;
    check (window != nullptr, "Tag dialog did not open"); window->setAlpha (0.0f);
    juce::Component::SafePointer<juce::Component> content (window->getContentComponent());
    check (content != nullptr, "Tag dialog content is missing");
    const auto openedMs = juce::Time::getMillisecondCounterHiRes() - begun;
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    int overflowing = 0;
    for (auto* child : content->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
    {
        auto* viewed = viewport->getViewedComponent();
        check (viewed && viewed->getNumChildComponents() > 0 && viewed->getNumChildComponents() <= 128,
               "Tag viewport has no visible controls or retains offscreen buttons");
        check (viewed->getHeight() > viewport->getHeight(), "Large tag set has no scroll range on initial opening");
        for (auto* button : viewed->getChildren()) check (button->getWidth() > 0 && button->getRight() <= viewed->getWidth(), "Tag button is invisible or outside its viewport");
        viewport->setViewPosition (0, viewed->getHeight());
        check (viewport->getViewPositionY() > 0 && viewport->getViewArea().intersects (viewed->getChildComponent (viewed->getNumChildComponents() - 1)->getBounds()),
               "Last tag cannot be reached by scrolling");
        ++overflowing;
    }
    check (overflowing == 2, "Tag dialog needs independent current/suggestion scroll areas");
    saveImage (*content, "tags-large-bottom.png");
    auto* input = childOf<juce::TextEditor> (*content); check (input && input->onReturnKey, "Tag input is missing");
    input->setText ("# New   tag", false); const auto editBegun = juce::Time::getMillisecondCounterHiRes(); input->onReturnKey();
    const auto editedMs = juce::Time::getMillisecondCounterHiRes() - editBegun;
    auto findTagButton = [&] (const juce::String& text) -> juce::TextButton*
    {
        check (content != nullptr, "Tag dialog closed before the fixture interaction finished");
        for (auto* child : content->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
            for (auto* item : viewport->getViewedComponent()->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (item); button && button->getButtonText() == text) return button;
        return nullptr;
    };
    auto* suggestion = findTagButton ("#Tag 511"); check (suggestion && suggestion->onClick, "Last tag suggestion is missing");
    suggestion->onClick(); awaitUi ([&]
    {
        for (auto* child : content->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
            viewport->setViewPosition (0, viewport->getViewedComponent()->getHeight());
        return findTagButton ("#Tag 511  x") != nullptr;
    });
    for (auto* child : content->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child)) viewport->setViewPosition (0, 0);
    auto* removable = findTagButton ("#Tag 000  x"); check (removable && removable->onClick, "Existing tag chip is missing");
    removable->onClick(); awaitUi ([&] { return findTagButton ("#Tag 000  x") == nullptr; });
    for (auto* child : content->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
        viewport->setViewPosition (0, viewport->getViewedComponent()->getHeight());
    auto* longButton = findTagButton ("#" + longTag + "  x");
    check (longButton && longButton->getTooltip().contains (longTag), "Truncated tag lost its complete tooltip/identity");
    for (auto* child : content->getChildren()) if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "OK") button->onClick();
    awaitUi ([&] { return window == nullptr; });
    check (committed.size() == initial.size() + 1 && committed.contains ("New tag") && committed.contains (longTag)
        && committed.contains ("Tag 511") && ! committed.contains ("Tag 000"), "Large tag commit lost data or normalisation");
    std::cout << "PASS: late selected folder popup after owner destruction, 1102 folder paths, retained selection, 512 unique tags, initial scroll/layout and long tag commit; open="
              << openedMs << " ms, edit=" << editedMs << " ms\n";
}

void testRowInteractions (const juce::File& root)
{
    ProjectEntry original, replacement;
    original.name = "Original action"; original.projectId = juce::Uuid().toString();
    original.metadataFile = root.getChildFile ("original-row.json");
    replacement.name = "Replacement action"; replacement.projectId = juce::Uuid().toString();
    replacement.metadataFile = root.getChildFile ("replacement-row.json");
    {
        ProjectRowComponent row; row.setSize (600, 52);
        juce::File committedAt;
        juce::String committedText;
        row.onRenameCommitted = [&, target = original.metadataFile] (juce::String name)
        { committedAt = target; committedText = name; };
        row.setEntry (original, true); row.startInlineRename();
        auto* editor = childOf<juce::TextEditor> (row);
        check (editor && editor->onReturnKey, "Row edit has no text/commit control");
        editor->setText ("Typing through relocation", false);
        auto relocated = original; relocated.metadataFile = root.getChildFile ("relocated-row.json");
        row.onRenameCommitted = [&, target = relocated.metadataFile] (juce::String name)
        { committedAt = target; committedText = name; };
        row.setEntry (relocated, true);
        check (editor->isVisible() && editor->getText() == "Typing through relocation", "Same-ID refresh reset the active text");
        editor->onReturnKey();
        check (committedAt == relocated.metadataFile && committedText == "Typing through relocation",
               "Same-ID relocation committed the rename to its old metadata path");
    }
    // A real popup is selected through JUCE key handling. Rebinding its row
    // between opening and completion must keep the clicked action/identity.
    // Enabled actions are rename, tags, repair and delete, in that order.
    for (int choice = 0; choice < 5; ++choice)
    {
        auto row = std::make_unique<ProjectRowComponent>(); row->setSize (600, 52);
        struct Result { int action = 0, completion = 0; };
        auto result = std::make_shared<Result>();
        row->onRenameRequested = [result] { result->action = 1; };
        row->onEditTags = [result] { result->action = 2; };
        row->onRecover = [result] (bool) { result->action = 6; };
        row->onDelete = [result] { result->action = 4; };
        row->setEntry (original, true);
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), {},
            juce::ModifierKeys::rightButtonModifier, 1, 0, 0, 0, 0, row.get(), row.get(), now, {}, now, 1, false);
        row->mouseDown (event);
        juce::Component::SafePointer<juce::Component> popup (juce::Component::getCurrentlyModalComponent());
        check (popup != nullptr, "Row context popup did not open");
        juce::ModalComponentManager::getInstance()->attachCallback (popup,
            juce::ModalCallbackFunction::create ([result] (int selected) { result->completion = selected; }));
        row->onRenameRequested = [result] { result->action = -1; };
        row->onEditTags = [result] { result->action = -2; };
        row->onRecover = [result] (bool) { result->action = -6; };
        row->onDelete = [result] { result->action = -4; };
        auto changed = replacement;
        if (choice == 4) changed.metadataFile = original.metadataFile; // Same path, another ID.
        row->setEntry (changed, false);
        const auto steps = choice == 4 ? 1 : choice + 1;
        for (int i = 0; i < steps; ++i)
            check (popup->keyPressed (juce::KeyPress (juce::KeyPress::downKey)), "Row popup ignored navigation");
        check (popup->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)), "Row popup did not select an action");
        awaitUi ([&] { return result->completion != 0; });
        const int expected[] { 1, 2, 6, 4, 1 };
        check (result->completion == expected[choice], "Row popup selected the wrong enabled action");
        check (result->action == expected[choice], "Recycled popup called the new row's action or renamed its replacement ID");
        check (juce::Component::getNumCurrentlyModalComponents() == 0, "Completed row popup remained modal");
    }
    {
        SavedConfig restore;
        const auto library = root.getChildFile ("row-action-library");
        const auto first = library.getChildFile ("first"), second = library.getChildFile ("second");
        const auto firstMeta = first.getChildFile ("metadata.json"), secondMeta = second.getChildFile ("metadata.json");
        const auto firstId = juce::Uuid().toString(), secondId = juce::Uuid().toString();
        auto createEntry = [&] (const juce::File& folder, juce::String id, juce::String name, juce::String date)
        {
            check (folder.createDirectory().wasOk(), "Cannot create row-action entry");
            juce::var metadata (new juce::DynamicObject()); auto* object = metadata.getDynamicObject();
            object->setProperty ("projectId", id); object->setProperty ("projectName", name);
            object->setProperty ("recordedAt", date); object->setProperty ("tags", juce::Array<juce::var> { "Initial" });
            check (LibraryStore::writeJsonAtomically (folder.getChildFile ("metadata.json"), metadata).wasOk()
                   && folder.getChildFile ("preview.ogg").replaceWithText ("row index marker"), "Cannot write row-action entry");
        };
        createEntry (first, firstId, "First tags", "2040-01-01T00:00:00Z");
        createEntry (second, secondId, "Second tags", "2030-01-01T00:00:00Z");
        check (ProjectScanner::saveSettings (library, {}, 60, 4, false).wasOk(), "Cannot configure row-action library");
        ProjectScanner::refreshAsync();
        awaitUi ([&] { const auto snapshot = ProjectScanner::librarySnapshot(); return snapshot->sourceRoot == library
            && snapshot->entries.size() == 2 && snapshot->entries[0].projectId == firstId; });
        juce::String selectedId;
        auto list = std::make_unique<ProjectListComponent>(); list->setSize (600, 300);
        list->onEntrySelected = [&] (const ProjectEntry& selected) { selectedId = selected.projectId; };
        auto* rows = childOf<juce::ListBox> (*list);
        check (rows != nullptr, "Row-action list has no ListBox");
        auto* firstRow = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (0));
        check (firstRow && firstRow->onEditTags, "Row-action list lacks its first tags callback");
        firstRow->onEditTags();
        juce::Component::SafePointer<juce::DialogWindow> tags (dynamic_cast<juce::DialogWindow*> (juce::Component::getCurrentlyModalComponent()));
        check (tags != nullptr && tags->getContentComponent(), "Real list tags dialog did not open");
        auto* content = tags->getContentComponent();
        auto* editor = childOf<juce::TextEditor> (*content);
        check (editor && editor->onReturnKey, "Tags dialog has no editable input");
        editor->setText ("Clicked first", false); editor->onReturnKey();
        juce::var newer (new juce::DynamicObject()); newer.getDynamicObject()->setProperty ("recordedAt", "2050-01-01T00:00:00Z");
        check (LibraryStore::patchJson (secondMeta, newer).wasOk(), "Cannot reorder row-action library");
        juce::var concurrent (new juce::DynamicObject()); concurrent.getDynamicObject()->setProperty ("favorite", true);
        concurrent.getDynamicObject()->setProperty ("tags", juce::Array<juce::var> { "Initial", "Concurrent" });
        check (LibraryStore::patchJson (firstMeta, concurrent).wasOk(), "Cannot change tags/favorite concurrently");
        ProjectScanner::refreshAsync();
        awaitUi ([&]
        {
            if (list->getNumRows() != 2) return false;
            auto* top = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (0));
            if (! top || ! top->onSelect) return false;
            top->onSelect(); return selectedId == secondId;
        });
        juce::TextButton* save = nullptr;
        for (auto* child : content->getChildren()) if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "OK") save = button;
        check (save && save->onClick, "Tags dialog lacks its OK action"); save->onClick();
        awaitUi ([&]
        {
            const auto savedTags = LibraryStore::readJson (firstMeta).getProperty ("tags", juce::var{});
            if (tags != nullptr || ! savedTags.isArray()) return false;
            for (const auto& tag : *savedTags.getArray()) if (tag.toString() == "Clicked first") return true;
            return false;
        });
        const auto edited = LibraryStore::readJson (firstMeta), untouched = LibraryStore::readJson (secondMeta);
        const auto editedTags = edited.getProperty ("tags", juce::var{});
        check ((bool) edited.getProperty ("favorite", false) && editedTags.size() == 3
               && editedTags[0].toString() == "Initial" && editedTags[1].toString() == "Concurrent"
               && editedTags[2].toString() == "Clicked first"
               && untouched.getProperty ("tags", juce::var{}).size() == 1,
               "Late tags commit changed another row or lost a concurrent favorite/tag");
        // Select a real menu action, then close its list before completion.
        // The captured list SafePointer must suppress the secondary dialog.
        auto* clicked = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (0));
        check (clicked != nullptr, "Reordered row is missing before close");
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), {},
            juce::ModifierKeys::rightButtonModifier, 1, 0, 0, 0, 0, clicked, clicked, now, {}, now, 1, false);
        clicked->mouseDown (event);
        juce::Component::SafePointer<juce::Component> popup (juce::Component::getCurrentlyModalComponent());
        check (popup != nullptr, "List popup did not open before close");
        auto completed = std::make_shared<int> (0);
        juce::ModalComponentManager::getInstance()->attachCallback (popup,
            juce::ModalCallbackFunction::create ([completed] (int selected) { *completed = selected; }));
        check (popup->keyPressed (juce::KeyPress (juce::KeyPress::downKey))
            && popup->keyPressed (juce::KeyPress (juce::KeyPress::downKey))
            && popup->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)), "List popup did not select tags");
        list.reset(); awaitUi ([&] { return *completed != 0; });
        check (*completed == 2 && juce::Component::getNumCurrentlyModalComponents() == 0,
               "Closing the list lost a selected popup result or opened a secondary tags dialog");
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    }
    std::cout << "PASS: real row popups retain rename/tags/repair/delete actions after reuse, reject another ID at the same path, same-ID relocation commits current text at the new path, late tags preserve concurrent fields after reorder, and selected popup completion is safe after list closure\n";
}

void testLibraryScene (const juce::File& root)
{
    const auto diagnostics = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts/scene-index.log");
    check (diagnostics.getParentDirectory().createDirectory().wasOk(), "Cannot create scene diagnostics");
    diagnostics.deleteFile();
    juce::FileLogger logger (diagnostics, "Scene index diagnostics", 0);
    struct RestoreLogger
    {
        juce::Logger* previous = juce::Logger::getCurrentLogger();
        ~RestoreLogger() { juce::Logger::setCurrentLogger (previous); }
    } restoreLogger;
    juce::Logger::setCurrentLogger (&logger);
    SavedConfig restore;
    const auto library = root.getChildFile ("scene-library"), projects = root.getChildFile ("scene-projects");
    check (library.createDirectory().wasOk() && projects.createDirectory().wasOk(), "Cannot create full-scene library");
    const auto project = projects.getChildFile ("Scene.flp");
    check (project.replaceWithText ("scene project"), "Cannot create full-scene project");
    const auto projectFileId = ProjectIdentity::fileId (project);
    int created = 0;
    std::unique_ptr<MainComponent> scene;
    ProjectListComponent* list = nullptr;
    FoldersSidebarComponent* sidebar = nullptr;
    juce::ListBox* rows = nullptr;
    juce::TextEditor* search = nullptr;
    juce::String clickedId;
    for (const auto count : { 100, 1000, 10000 })
    {
        for (; created < count; ++created)
        {
            const auto directory = library.getChildFile (juce::String (created).paddedLeft ('0', 5));
            check (directory.createDirectory().wasOk(), "Cannot create scene entry");
            juce::var metadata (new juce::DynamicObject()); auto* object = metadata.getDynamicObject();
            object->setProperty ("projectId", juce::Uuid().toString());
            object->setProperty ("projectName", "Scene " + juce::String (created).paddedLeft ('0', 5));
            object->setProperty ("projectPath", project.getFullPathName());
            object->setProperty ("projectFileId", projectFileId);
            object->setProperty ("daw", "FL Studio");
            object->setProperty ("recordedAt", juce::Time (1791374400000LL + created * 1000LL).toISO8601 (true));
            object->setProperty ("tags", juce::Array<juce::var> { "Group " + juce::String (created % 8) });
            object->setProperty ("folders", juce::Array<juce::var> { "Groups/" + juce::String (created % 10) });
            const auto manifest = directory.getChildFile ("metadata.json");
            // Fixture creation is excluded from UI latency. Durability is
            // covered by the storage suites, rather than 10,000 fsyncs here.
            juce::FileOutputStream output (manifest, 0);
            check (output.openedOk() && output.writeText (juce::JSON::toString (metadata), false, false, "\n") && output.getStatus().wasOk()
                   && directory.getChildFile ("preview.ogg").replaceWithText ("scene index marker"), "Cannot write scene entry");
        }
        check (ProjectScanner::saveSettings (library, { projects }, 60, 4, false).wasOk(), "Cannot configure full scene");
        ProjectScanner::refreshAsync();
        awaitUi ([&] { return ProjectScanner::librarySnapshot()->sourceRoot == library && ProjectScanner::librarySnapshot()->entries.size() == count; }, 60000);
        const auto opening = juce::Time::getMillisecondCounterHiRes();
        if (! scene)
        {
            scene = std::make_unique<MainComponent>();
            list = childOf<ProjectListComponent> (*scene); sidebar = childOf<FoldersSidebarComponent> (*scene);
            check (list && sidebar, "Full scene lacks list/sidebar");
            rows = childOf<juce::ListBox> (*list); search = childOf<juce::TextEditor> (*list);
            check (rows && search, "Full scene lacks list/search widgets");
            // Device/audio behavior has its own fixtures. Observe row identity
            // through the actual list callback without opening a physical driver.
            list->onEntrySelected = [&] (const ProjectEntry& entry) { clickedId = entry.projectId; };
            list->onEntryPlay = list->onEntrySelected;
        }
        scene->setSize (860, 640);
        awaitUi ([&] { return list->getNumRows() == count; });
        const auto openedMs = juce::Time::getMillisecondCounterHiRes() - opening;
        rows->scrollToEnsureRowIsOnscreen (count - 1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        auto* last = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (count - 1));
        check (last && last->isVisible() && last->onSelect, "Last row is unreachable in the native ListBox");
        last->onSelect();
        check (clickedId == ProjectScanner::librarySnapshot()->entries[count - 1].projectId, "Scrolled row selected another entry");
        int liveRows = 0;
        std::function<void(juce::Component&)> visit = [&](juce::Component& component)
        {
            if (dynamic_cast<ProjectRowComponent*> (&component)) ++liveRows;
            for (auto* child : component.getChildren()) visit (*child);
        };
        visit (*scene);
        check (liveRows > 0 && liveRows < 40, "Native ListBox created a component for every library entry");
        const auto filtering = juce::Time::getMillisecondCounterHiRes();
        search->setText ("Scene " + juce::String (count - 1).paddedLeft ('0', 5), true);
        awaitUi ([&] { return list->getNumRows() == 1; });
        const auto searchMs = juce::Time::getMillisecondCounterHiRes() - filtering;
        search->setText ({}, true); awaitUi ([&] { return list->getNumRows() == count; });
        const auto folderFiltering = juce::Time::getMillisecondCounterHiRes();
        sidebar->selectFolder ("Groups/3");
        check (list->getNumRows() == count / 10, "Main sidebar/list wiring lost its folder filter");
        const auto folderMs = juce::Time::getMillisecondCounterHiRes() - folderFiltering;
        sidebar->selectFolder ({});
        std::cout << "SCENE entries=" << count << " openOrDispatchMs=" << openedMs << " liveRows=" << liveRows
                  << " searchWithDebounceMs=" << searchMs << " folderMs=" << folderMs << '\n';
    }
    check (scene && rows && list && sidebar, "Scene fixture did not run");
    rows->selectRow (5000); rows->scrollToEnsureRowIsOnscreen (5000);
    auto* selected = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (5000));
    check (selected && selected->onSelect, "Cannot select middle scene row"); selected->onSelect();
    const auto target = ProjectScanner::librarySnapshot()->entries[5000];
    check (clickedId == target.projectId && scene->keyPressed (juce::KeyPress ('F')), "Main favorite hotkey did not reach selected entry");
    awaitUi ([&] { return (bool) LibraryStore::readJson (target.metadataFile).getProperty ("favorite", false); }, 10000, "Scene favorite commit did not complete");
    juce::TextButton* favorites = nullptr;
    for (auto* child : list->getChildren()) if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText().contains ("Favorites")) favorites = button;
    check (favorites != nullptr, "Favorites filter button is missing");
    favorites->triggerClick(); awaitUi ([&] { return list->getNumRows() == 1; }, 10000, "Scene favorite filter did not isolate one entry");
    favorites->triggerClick(); awaitUi ([&] { return list->getNumRows() == 10000; }, 10000, "Scene favorites toggle did not restore all entries");
    juce::var patch (new juce::DynamicObject()); patch.getDynamicObject()->setProperty ("recordedAt", "2050-01-01T00:00:00Z");
    check (LibraryStore::patchJson (target.metadataFile, patch).wasOk(), "Cannot reorder selected scene entry");
    try
    {
        awaitUi ([&] { return ProjectScanner::librarySnapshot()->entries[0].projectId == target.projectId && rows->getSelectedRow() == 0; }, 10000, "Scene publication did not preserve reordered selection by project ID");
    }
    catch (...)
    {
        const auto current = ProjectScanner::librarySnapshot();
        std::cerr << "SCENE reorder target=" << target.projectId << " publishedFirst=" << current->entries[0].projectId
            << " publishedDate=" << current->entries[0].recordedAt << " selectedRow=" << rows->getSelectedRow()
            << " visibleFirst=" << list->visibleEntries()[0].projectId << " targetManifest=" << target.metadataFile.loadFileAsString() << std::endl;
        throw;
    }
    selected = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (0));
    check (selected && selected->onSelect, "Reordered selected row is missing"); selected->onSelect();
    check (clickedId == target.projectId, "Publication reordered selection by row number instead of identity");
    auto* folderTree = childOf<juce::TreeView> (*sidebar);
    check (folderTree != nullptr, "Full-scene sidebar has no tree");
    scene->setSize (640, 480);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    check (folderTree->getNumRowsInTree() >= 2, "Full-scene folders have no visible rows");
    saveImage (*scene, "scene-10000-minimum.png");
    scene->setSize (1024, 768);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    saveImage (*scene, "scene-10000.png");
    auto sharedTip = juce::SharedResourcePointer<RememberUI::TooltipWindow>::getSharedObjectWithoutCreating();
    check (sharedTip.has_value(), "Full scene never enabled its configured tooltips");
    const auto closing = juce::Time::getMillisecondCounterHiRes(); scene.reset();
    const auto closeMs = juce::Time::getMillisecondCounterHiRes() - closing;
    check (closeMs < 200, "Closing a full library waited for its index worker");
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0 && WaveformPlayerComponent::pendingLoads() == 0; }, 10000, "Scene closure left accepted jobs or readers pending");
    check (juce::Component::getNumCurrentlyModalComponents() == 0, "Full scene opened an unexpected settings/missing-project dialog");
    std::cout << "PASS: native full scene 100/1000/10000, recycled/last rows, search/folders/favorites, hotkey and reordered ID selection, minimum/large renders and deferred-close safety; close=" << closeMs << " ms\n";
}
