#include "RecoveryUiTests.h"
#include "../App/Source/ProjectListComponent.h"
#include "../App/Source/RecoveryChoiceDialog.h"
#include "../App/Source/WaveformPlayerComponent.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
template<class Predicate> void awaitUi (Predicate predicate)
{
    const auto until = juce::Time::getMillisecondCounter() + 15000;
    while (! predicate())
    {
        check (juce::Time::getMillisecondCounter() < until, "Recovery UI timed out");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
}
template<class Type> Type* childOf (juce::Component& parent)
{
    for (auto* child : parent.getChildren()) if (auto* typed = dynamic_cast<Type*> (child)) return typed;
    return nullptr;
}
juce::TextButton* button (juce::Component& parent, const juce::String& label)
{
    for (auto* child : parent.getChildren())
        if (auto* found = dynamic_cast<juce::TextButton*> (child); found && found->getButtonText() == label) return found;
    return nullptr;
}
RecoveryChoiceDialog* modalRecovery()
{
    auto* window = dynamic_cast<juce::DialogWindow*> (juce::Component::getCurrentlyModalComponent());
    return window ? dynamic_cast<RecoveryChoiceDialog*> (window->getContentComponent()) : nullptr;
}
void click (juce::Button& component)
{
    bool delivered = false;
    component.triggerClick();
    juce::MessageManager::callAsync ([&] { delivered = true; });
    awaitUi ([&] { return delivered; });
}
void image (juce::Component& component, const char* name)
{
    const auto artifacts = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts");
    check (artifacts.createDirectory().wasOk(), "Cannot create recovery artifacts");
    juce::FileOutputStream output (artifacts.getChildFile (name));
    juce::PNGImageFormat png;
    check (output.openedOk() && output.setPosition (0) && output.truncate().wasOk()
        && png.writeImageToStream (component.createComponentSnapshot (component.getLocalBounds(), true, 1, juce::SoftwareImageType{}), output),
           "Cannot render recovery dialog");
}
}

void testRecoveryUi (const juce::File& root)
{
   #if JUCE_WINDOWS
    check (root.isAChildOf (juce::File::getCurrentWorkingDirectory()), "Recovery UI root is not isolated");
    struct RestoreConfig
    {
        juce::File file { LibraryStore::configFile() };
        juce::MemoryBlock bytes;
        bool existed = file.existsAsFile();
        RestoreConfig() { if (existed) check (file.loadFileAsData (bytes), "Cannot preserve recovery UI settings"); }
        ~RestoreConfig()
        {
            if (existed)
            {
                juce::FileOutputStream output (file, 0);
                if (output.openedOk() && output.setPosition (0) && output.truncate().wasOk())
                { if (bytes.getSize() > 0) output.write (bytes.getData(), bytes.getSize()); output.flush(); }
            }
            else file.deleteFile();
            ProjectScanner::refreshAsync();
        }
    } restore;
    const auto library = root.getChildFile ("storage-recovery/ui-ambiguous");
    const auto projects = root.getChildFile ("recovery-ui-projects");
    const auto folder = library.getChildFile (".pending").getChildFile (juce::Uuid().toString());
    check (folder.createDirectory().wasOk() && projects.createDirectory().wasOk(), "Cannot create recovery UI fixture");
    for (int i = 0; i < 2; ++i)
    {
        juce::ChildProcess writer;
        check (writer.start (juce::StringArray { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                               "--storage-crash", folder.getFullPathName(), "1" })
            && writer.waitForProcessToFinish (15000) && writer.getExitCode() == 77,
               "Cannot interrupt recovery UI fixture writer at its receipt");
    }
    const auto review = LibraryStore::recoverEntry (folder);
    check (review.choices.size() == 2, "Recovery UI fixture is not ambiguous");
    check (ProjectScanner::saveSettings (library, { projects }, 45, 9, true).wasOk(), "Cannot configure recovery UI library");
    ProjectScanner::refreshAsync();
    awaitUi ([&]
    {
        const auto snapshot = ProjectScanner::librarySnapshot();
        return snapshot && snapshot->entries.size() == 1 && snapshot->entries[0].metadataFile.getParentDirectory() == folder;
    });
    const auto manifest = folder.getChildFile ("metadata.json");
    WaveformPlayerComponent player;
    juce::File played, storageTarget;
    auto makeList = [&]
    {
        auto list = std::make_unique<ProjectListComponent>(); list->setSize (640, 480);
        list->onEntryPlay = [&] (const ProjectEntry& entry) { played = entry.previewOgg; player.loadEntry (entry); };
        list->onBeforeStorageChange = [&] (const ProjectEntry& entry)
        { storageTarget = entry.metadataFile; player.releaseEntry (entry.metadataFile); };
        awaitUi ([&] { return list->getNumRows() == 1; });
        return list;
    };
    auto list = makeList();
    auto row = std::unique_ptr<ProjectRowComponent> (dynamic_cast<ProjectRowComponent*> (list->refreshComponentForRow (0, true, nullptr)));
    check (row && row->onRecover, "Ambiguous row has no recovery action");
    row->onRecover (false);
    ProjectEntry rebound; rebound.projectId = juce::Uuid().toString(); rebound.name = "Another row";
    rebound.metadataFile = library.getChildFile ("unrelated/metadata.json"); row->setEntry (rebound, true);
    awaitUi ([] { return modalRecovery() != nullptr; });
    auto* dialog = modalRecovery();
    auto* combo = childOf<juce::ComboBox> (*dialog);
    auto* restoreButton = button (*dialog, "Restore selected");
    auto* listenButton = button (*dialog, "Listen to selected");
    check (combo && combo->getNumItems() == 2 && combo->getSelectedId() == 0
           && restoreButton && ! restoreButton->isEnabled() && listenButton && ! listenButton->isEnabled()
           && ! manifest.exists(), "Recovery dialog guessed an initial revision or changed files on open");
    image (*dialog, "recovery-choice-unselected.png");
    combo->setSelectedId (2, juce::sendNotificationSync);
    check (restoreButton->isEnabled() && listenButton->isEnabled(), "Explicit revision selection did not enable actions");
    click (*listenButton);
    awaitUi ([&] { return player.hasLoadedEntry(); });
    check (played == review.choices[1].previewOgg && ! player.isAudioDeviceOpen() && ! manifest.exists(),
           "Recovery audition loaded another revision, opened output during load or changed the manifest");
    image (*dialog, "recovery-choice-selected.png");
    click (*button (*dialog, "Cancel"));
    awaitUi ([] { return juce::Component::getNumCurrentlyModalComponents() == 0 && ProjectScanner::pendingJobs() == 0; });
    check (! manifest.exists() && review.choices[0].previewOgg.existsAsFile() && review.choices[1].previewOgg.existsAsFile(),
           "Cancel discarded an ambiguous orphan");

    row->onRecover (false); awaitUi ([] { return modalRecovery() != nullptr; });
    dialog = modalRecovery(); childOf<juce::ComboBox> (*dialog)->setSelectedId (2, juce::sendNotificationSync);
    button (*dialog, "Restore selected")->triggerClick();
    list.reset(); // Owner dies before the queued button action is dispatched.
    awaitUi ([] { return juce::Component::getNumCurrentlyModalComponents() == 0 && ProjectScanner::pendingJobs() == 0; });
    check (! manifest.exists(), "Queued recovery choice wrote after its owner was destroyed");
    row.reset();

    list = makeList();
    row.reset (dynamic_cast<ProjectRowComponent*> (list->refreshComponentForRow (0, true, nullptr)));
    row->onRecover (false); awaitUi ([] { return modalRecovery() != nullptr; });
    dialog = modalRecovery(); childOf<juce::ComboBox> (*dialog)->setSelectedId (2, juce::sendNotificationSync);
    struct Gate { juce::WaitableEvent entered, release { true }; };
    auto gate = std::make_shared<Gate>();
    struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release.signal(); } } unblock { gate };
    ProjectScanner::editAsync ([gate] { gate->entered.signal(); gate->release.wait (15000); return juce::Result::ok(); }, {});
    awaitUi ([&] { return gate->entered.wait (0); });
    click (*button (*dialog, "Restore selected")); // Accept a queued edit before closing the list.
    check (! manifest.exists() && storageTarget == manifest && ! player.hasLoadedEntry(),
           "Recovery ignored the held edit worker, reused another row or retained its preview reader");
    list.reset(); row.reset(); gate->release.signal();
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0 && juce::Component::getNumCurrentlyModalComponents() == 0; });
    const auto saved = LibraryStore::readJson (manifest);
    check (saved.getProperty ("audioRevision", "").toString() == review.choices[1].revision
           && saved.getProperty ("projectId", "").toString() == folder.getFileName()
           && ! rebound.metadataFile.exists() && review.choices[0].previewOgg.existsAsFile(),
           "Accepted recovery lost the selected identity after UI closure or discarded another revision");
    std::cout << "PASS: actual ambiguous row opens revision chooser without a default; selected OGG loads through read-ahead, cancel/rebind/queued action/owner closure are safe, accepted held edit completes after UI destruction; software renders exported\n";
   #else
    juce::ignoreUnused (root);
   #endif
}
