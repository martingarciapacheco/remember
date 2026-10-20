#include "UndoTests.h"
#include "../App/Source/MainComponent.h"
#include "../Shared/OrganizationUndo.h"
#include <iostream>
#include <stdexcept>

namespace
{
using Context = ProjectScanner::UndoContext;
void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }
template<class Fn> void waitFor (Fn condition, const char* message = "Context undo timed out")
{
    const auto deadline = juce::Time::getMillisecondCounter() + 15000;
    while (! condition())
    {
        check (juce::Time::getMillisecondCounter() < deadline, message);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
}
template<class T> T* child (juce::Component& component)
{
    for (auto* c : component.getChildren()) if (auto* typed = dynamic_cast<T*> (c)) return typed;
    return nullptr;
}
void idle() { waitFor ([] { return ProjectScanner::pendingJobs() == 0; }); }
juce::Result edit (std::function<juce::Result()> work)
{
    bool done = false; auto result = juce::Result::ok();
    ProjectScanner::editAsync (std::move (work), [&] (juce::Result r) { result = r; done = true; });
    waitFor ([&] { return done; }); idle(); return result;
}
struct RestoreFixture
{
    juce::File config { LibraryStore::configFile() };
    juce::String configText { config.loadFileAsString() };
    bool existed = config.existsAsFile();
    std::vector<OrganizationUndo::Group> history;
    size_t bytes = 0;
    RestoreFixture()
    {
        idle(); std::lock_guard<std::mutex> guard (OrganizationUndo::lock);
        history.swap (OrganizationUndo::history); bytes = OrganizationUndo::retainedBytes; OrganizationUndo::retainedBytes = 0;
    }
    ~RestoreFixture()
    {
        idle();
        { std::lock_guard<std::mutex> guard (OrganizationUndo::lock);
          history.swap (OrganizationUndo::history); OrganizationUndo::retainedBytes = bytes; }
        if (existed) config.replaceWithText (configText); else config.deleteFile();
        ProjectScanner::refreshAsync(); idle();
    }
};
void clearHistory()
{
    idle(); std::lock_guard<std::mutex> guard (OrganizationUndo::lock);
    OrganizationUndo::history.clear(); OrganizationUndo::retainedBytes = 0;
}
}

void testContextUndo (const juce::File& root)
{
    RestoreFixture restore;
    const auto library = root.getChildFile ("undo-library"), projects = root.getChildFile ("undo-projects");
    check (library.createDirectory().wasOk() && projects.createDirectory().wasOk(), "Cannot create undo library");
    juce::Array<ProjectEntry> entries;
    for (int i = 0; i < 2; ++i)
    {
        ProjectEntry entry; entry.projectId = juce::Uuid().toString(); entry.name = "Undo " + juce::String (i);
        const auto folder = library.getChildFile (entry.projectId);
        check (folder.createDirectory().wasOk(), "Cannot create undo preview");
        entry.metadataFile = folder.getChildFile ("metadata.json"); entry.previewOgg = folder.getChildFile ("preview.ogg");
        juce::var data (new juce::DynamicObject());
        data.getDynamicObject()->setProperty ("projectId", entry.projectId);
        data.getDynamicObject()->setProperty ("projectName", entry.name);
        data.getDynamicObject()->setProperty ("folders", juce::Array<juce::var> { "Kept" });
        check (LibraryStore::writeJsonAtomically (entry.metadataFile, data).wasOk()
            && entry.previewOgg.replaceWithText ("preserved audio " + juce::String (i)), "Cannot write undo preview");
        entries.add (entry);
    }
    const auto first = entries[0], second = entries[1];
    const auto audioBefore = first.previewOgg.loadFileAsString(), metadataBefore = first.metadataFile.loadFileAsString();
    const auto trash = [&](const ProjectEntry& e) { return library.getChildFile (".trash").getChildFile (e.projectId); };
    check (ProjectScanner::saveSettings (library, { projects }, 60, 4, false).wasOk(), "Cannot configure undo library");
    ProjectScanner::refreshAsync();
    waitFor ([&] { return ProjectScanner::librarySnapshot()->sourceRoot == library && ProjectScanner::scan().size() == 2; }, "Undo fixture was not indexed");
    check (edit ([] { return ProjectScanner::createFolder ("Empty"); }).wasOk(), "Cannot create undo folder");
    clearHistory();
    {
        MainComponent scene;
        auto* list = child<ProjectListComponent> (scene); auto* sidebar = child<FoldersSidebarComponent> (scene);
        auto* player = child<WaveformPlayerComponent> (scene);
        check (list && sidebar && player, "Undo scene is incomplete");
        list->onEntrySelected = [] (const ProjectEntry&) {}; // No physical audio device in shortcut tests.
        auto* rows = child<juce::ListBox> (*list); auto* tree = child<juce::TreeView> (*sidebar);
        auto* search = child<juce::TextEditor> (*list);
        check (rows && tree && search, "Undo controls are missing");
        scene.setSize (1024, 768); scene.addToDesktop (0); scene.setVisible (true);
        waitFor ([&] { return list->getNumRows() == 2; }, "Undo scene did not publish two rows"); idle();
        const auto press = [&](int key, int mods = 0, juce::juce_wchar text = 0)
        { return scene.getPeer()->handleKeyPress (juce::KeyPress (key, mods, text)); };
        const auto undo = [&] { check (press ('Z', juce::ModifierKeys::ctrlModifier), "Ctrl+Z was not handled"); idle(); };
        const auto confirm = [&](int result)
        {
            waitFor ([] { return dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent()) != nullptr; }, "Deletion confirmation did not open");
            juce::Component::getCurrentlyModalComponent()->exitModalState (result); idle();
            waitFor ([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
        };
        list->selectEntry (first); rows->grabKeyboardFocus();
        check (press (juce::KeyPress::deleteKey), "Preview Delete was not handled"); confirm (0);
        check (first.metadataFile.existsAsFile() && ! ProjectScanner::canUndoOrganization (Context::Previews), "Cancelled deletion entered undo history");
        rows->grabKeyboardFocus(); press (juce::KeyPress::backspaceKey); confirm (1);
        waitFor ([&] { return trash (first).isDirectory() && list->getNumRows() == 1; }, "Single preview deletion did not publish");
        sidebar->selectFolder ("Empty"); tree->grabKeyboardFocus();
        check (press (juce::KeyPress::deleteKey), "Folder Delete was not handled"); confirm (1);
        waitFor ([] { return ! ProjectScanner::getAllFolderPaths().contains ("Empty"); }, "Folder deletion did not publish");
        tree->grabKeyboardFocus(); undo();
        waitFor ([] { return ProjectScanner::getAllFolderPaths().contains ("Empty"); }, "Folder Ctrl+Z did not restore the folder");
        check (trash (first).isDirectory(), "Folder Ctrl+Z restored a preview");
        undo(); check (trash (first).isDirectory(), "Empty folder history fell through to preview undo");
        sidebar->selectFolder ({}); rows->grabKeyboardFocus(); undo();
        waitFor ([&] { return list->getNumRows() == 2; }, "Preview Ctrl+Z did not restore the single preview");
        check (first.previewOgg.loadFileAsString() == audioBefore && first.metadataFile.loadFileAsString() == metadataBefore,
               "Preview undo changed audio or metadata");

        int released = 0;
        list->onBeforeStorageChange = [&] (const ProjectEntry&) { ++released; };
        rows->selectRow (0); rows->selectRow (1, false, false); rows->grabKeyboardFocus();
        check (rows->getNumSelectedRows() == 2 && press (juce::KeyPress::deleteKey), "Bulk preview delete was not handled");
        // Queue undo immediately, before the background deletion has completed.
        undo(); waitFor ([&] { return list->getNumRows() == 2; }, "Immediate bulk preview undo did not publish");
        check (released == 2 && first.metadataFile.existsAsFile() && second.metadataFile.existsAsFile()
            && ! ProjectScanner::canUndoOrganization (Context::Previews), "Bulk deletion did not undo as one queued action");

        check (edit ([] { return ProjectScanner::createFolder ("Menu folder"); }).wasOk(), "Cannot prepare menu-context folder");
        auto* menuRow = dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (0));
        check (menuRow && menuRow->onActions, "Preview actions menu is missing");
        menuRow->onActions (menuRow);
        auto* menu = juce::Component::getCurrentlyModalComponent();
        check (menu != nullptr, "Preview actions menu did not open");
        menu->exitModalState (11); idle();
        // Exercise the callback even with preview undo disabled: it must never
        // fall through to the only available folder operation.
        check (ProjectScanner::getAllFolderPaths().contains ("Menu folder"), "Preview menu undid a folder");
        check (edit ([] { return ProjectScanner::undoOrganization (Context::Folders); }).wasOk(), "Cannot clear menu-context fixture");

        check (edit ([&] { return ProjectScanner::trashEntry (first); }).wasOk(), "Cannot prepare mouse-context deletion");
        check (edit ([] { return ProjectScanner::createFolder ("Mouse folder"); }).wasOk(), "Cannot prepare mouse-context folder");
        const auto click = [&](juce::Component& target, juce::Point<int> local)
        {
            struct Probe : juce::MouseListener
            {
                int clicks = 0;
                void mouseDown (const juce::MouseEvent&) override { ++clicks; }
            } probe;
            target.addMouseListener (&probe, true);
            const auto point = scene.getLocalPoint (&target, local).toFloat();
            const auto now = juce::Time::currentTimeMillis();
            auto* peer = scene.getPeer();
            // Prime hit testing before the press, independent of pointer state
            // left by earlier native-window and drag fixtures.
            peer->handleMouseEvent (juce::MouseInputSource::mouse, point, {}, 1, 0, now - 1);
            peer->handleMouseEvent (juce::MouseInputSource::mouse, point, juce::ModifierKeys::leftButtonModifier, 1, 0, now);
            peer->handleMouseEvent (juce::MouseInputSource::mouse, point, {}, 1, 0, now + 1);
            target.removeMouseListener (&probe);
            check (probe.clicks == 1, "Native mouse fixture did not hit its intended undo area");
        };
        click (*sidebar, { 20, 20 }); scene.grabKeyboardFocus(); undo();
        waitFor ([] { return ! ProjectScanner::getAllFolderPaths().contains ("Mouse folder"); }, "Mouse interaction did not select folder undo context");
        check (trash (first).isDirectory(), "Mouse interaction with folders undid a preview");
        click (*list, { list->getWidth() / 2, list->getHeight() - 15 });
        scene.grabKeyboardFocus(); undo();
        check (first.metadataFile.existsAsFile(), "Last mouse interaction with previews was not remembered");

        check (edit ([&] { return ProjectScanner::trashEntry (first); }).wasOk(), "Cannot prepare text-editor deletion");
        search->grabKeyboardFocus(); search->setText ({}, false);
        press ('X', 0, 'x'); press ('Z', juce::ModifierKeys::ctrlModifier); idle();
        check (search->getText().isEmpty() && trash (first).isDirectory()
            && ProjectScanner::canUndoOrganization (Context::Previews), "Library undo intercepted text-editor Ctrl+Z");
        rows->grabKeyboardFocus(); undo();
        check (first.metadataFile.existsAsFile() && ! player->isAudioDeviceOpen(), "Preview undo opened audio or failed after editing text");
        list->onBeforeStorageChange = nullptr;
        scene.removeFromDesktop();
    }
    clearHistory();

    auto mismatched = first; mismatched.projectId = juce::Uuid().toString();
    check (edit ([&] { return ProjectScanner::trashEntry (mismatched); }).failed()
        && ! ProjectScanner::canUndoOrganization (Context::Previews), "Failed deletion entered undo or deleted a replacement");
    auto mismatchedSecond = second; mismatchedSecond.projectId = juce::Uuid().toString();
    check (edit ([&]
    {
        const auto result = ProjectScanner::trashEntry (first);
        return result.failed() ? result : ProjectScanner::trashEntry (mismatchedSecond);
    }).failed() && trash (first).isDirectory() && second.metadataFile.existsAsFile(), "Partial deletion lost its successful subset");
    check (edit ([] { return ProjectScanner::undoOrganization (Context::Previews); }).wasOk()
        && first.metadataFile.existsAsFile(), "Cannot undo only the successful part of a deletion");
    check (edit ([&]
    {
        const auto result = ProjectScanner::trashEntry (first);
        return result.failed() ? result : ProjectScanner::trashEntry (second);
    }).wasOk(), "Cannot prepare partial-restore group");
    const auto original = first.metadataFile.getParentDirectory();
    check (original.createDirectory().wasOk(), "Cannot occupy original preview location");
    check (edit ([] { return ProjectScanner::undoOrganization (Context::Previews); }).failed()
        && trash (first).isDirectory() && second.metadataFile.existsAsFile(), "Partial undo overwrote occupied files or lost a restorable preview");
    check (original.deleteFile(), "Cannot release occupied fixture location");
    check (edit ([] { return ProjectScanner::undoOrganization (Context::Previews); }).wasOk()
        && first.metadataFile.existsAsFile(), "Failed undo could not be retried");

    check (edit ([&] { return ProjectScanner::trashEntry (first); }).wasOk(), "Cannot prepare replacement-identity undo");
    const auto trashMetadata = trash (first).getChildFile ("metadata.json");
    check (LibraryStore::updateJson (trashMetadata, [] (juce::DynamicObject& data)
        { data.setProperty ("projectId", juce::Uuid().toString()); return juce::Result::ok(); }).wasOk(), "Cannot prepare replacement identity");
    check (edit ([] { return ProjectScanner::undoOrganization (Context::Previews); }).failed()
        && trash (first).isDirectory(), "Undo restored a replacement recording identity");
    check (LibraryStore::updateJson (trashMetadata, [&] (juce::DynamicObject& data)
        { data.setProperty ("projectId", first.projectId); return juce::Result::ok(); }).wasOk(), "Cannot restore fixture identity");
    check (edit ([] { return ProjectScanner::undoOrganization (Context::Previews); }).wasOk(), "Identity rejection lost undo receipt");

    check (edit ([&] { return ProjectScanner::trashEntry (first); }).wasOk(), "Cannot prepare stale receipt");
    check (LibraryStore::restoreEntry (library, trash (first)).wasOk()
        && LibraryStore::deleteEntry (library, original).wasOk(), "Cannot manually restore and re-delete fixture");
    check (edit ([] { return ProjectScanner::undoOrganization (Context::Previews); }).wasOk()
        && trash (first).isDirectory(), "Old undo restored a later deletion at the same path");
    check (LibraryStore::restoreEntry (library, trash (first)).wasOk(), "Cannot clean up manual deletion");
    check (edit ([&] { return ProjectScanner::trashEntry (first); }).wasOk(), "Cannot prepare cross-library undo");
    check (edit ([&] { return OrganizationUndo::undo (Context::Previews, root.getChildFile ("different-library")); }).failed()
        && trash (first).isDirectory(), "Undo crossed preview libraries");
    check (edit ([] { return ProjectScanner::undoOrganization (Context::Previews); }).wasOk(), "Cross-library rejection lost undo receipt");
    check (first.previewOgg.loadFileAsString() == audioBefore
        && LibraryStore::readJson (first.metadataFile).getProperty ("projectId", {}).toString() == first.projectId,
           "Restoration changed preview audio or identity");
    std::cout << "PASS: native contextual Ctrl+Z, folder/preview isolation, empty history, text editing, mouse/focus, immediate bulk undo, cancellation, identity, occupied-location retry, stale deletion receipt and library isolation\n";
}
