#include "WorkflowTests.h"
#include "../App/Source/LibraryFilter.h"
#include "../App/Source/PlaybackQueue.h"
#include "../App/Source/LoudnessMeter.h"
#include "../App/Source/WaveformEnvelope.h"
#include "../App/Source/ProjectListComponent.h"
#include "../App/Source/WaveformPlayerComponent.h"
#include "../App/Source/WorkflowForm.h"
#include "../App/Source/PreviewFolderSetup.h"
#include "../App/Source/MainComponent.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/OrganizationUndo.h"
#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }
template<class Fn> void waitFor (Fn fn)
{
    const auto limit = juce::Time::getMillisecondCounter() + 15000;
    while (! fn()) { check (juce::Time::getMillisecondCounter() < limit, "Workflow async fixture timed out"); juce::MessageManager::getInstance()->runDispatchLoopUntil (5); }
}
template<class T> T* find (juce::Component& root, const juce::String& title)
{
    if (auto* value = dynamic_cast<T*> (&root); value && value->getTitle() == title) return value;
    for (auto* child : root.getChildren()) if (auto* result = find<T> (*child, title)) return result;
    return nullptr;
}
WorkflowForm* form()
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* window = dynamic_cast<juce::DialogWindow*> (desktop.getComponent (i)))
            if (auto* content = dynamic_cast<WorkflowForm*> (window->getContentComponent())) return content;
    return nullptr;
}
void snapshot (juce::Component& component, const juce::String& name)
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts");
    check (folder.createDirectory().wasOk(), "Cannot create workflow snapshot directory");
    juce::FileOutputStream stream (folder.getChildFile (name + ".png")); juce::PNGImageFormat png;
    check (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk(), "Cannot replace workflow snapshot");
    check (png.writeImageToStream (component.createComponentSnapshot (component.getLocalBounds(), true, 1, juce::SoftwareImageType{}), stream), "Cannot render workflow snapshot");
}
void accept (WorkflowForm& dialog)
{
    for (auto* child : dialog.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Apply") { button->onClick(); return; }
    throw std::runtime_error ("Form has no Apply button");
}
void popup (juce::Component& root, const juce::String& title, int action)
{
    auto* button = find<juce::TextButton> (root, title); check (button && button->onClick, "Workflow menu trigger missing");
    if (auto* context = dynamic_cast<RememberUI::ContextMenuButton*> (button)) context->onContextMenu();
    else button->onClick();
    auto* menu = juce::Component::getCurrentlyModalComponent(); check (menu != nullptr, "Workflow menu did not open");
    if (title == "Preview actions") snapshot (*menu, "workflow-row-actions");
    menu->exitModalState (action);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}
void writeAudio (const juce::File& file, float amplitude)
{
    juce::OggVorbisAudioFormat ogg; std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = ogg.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (16).withQualityOptionIndex (4));
    check (writer != nullptr, "Cannot write workflow audio"); juce::AudioBuffer<float> audio (2, 96000);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample (ch, i, amplitude * std::sin (2 * juce::MathConstants<double>::pi * 1000 * i / 48000));
    check (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "Audio fixture write failed");
}
}
void testWorkflowFeatures (const juce::File& root)
{
    const auto library = root.getChildFile ("workflow"), projects = root.getChildFile ("projects");
    check (library.createDirectory().wasOk() && projects.createDirectory().wasOk(), "Cannot create workflow library");
    CaptureOptions capture { true, true, true };
    check (ProjectScanner::saveSettings (library, { projects }, 30, 4, false, &capture).wasOk(), "Cannot save capture toggles");
    const auto loaded = ProjectScanner::readSettings();
    check (loaded.capture.manual && loaded.capture.locked && loaded.capture.replace, "Capture options did not round-trip");
    const auto configBefore = LibraryStore::configFile().loadFileAsString();
    check (PreviewFolderSetup::saveFolder (root.getChildFile ("does-not-exist")).failed()
        && LibraryStore::configFile().loadFileAsString() == configBefore, "Invalid preview folder changed settings");
    check (PreviewFolderSetup::saveFolder (library).wasOk(), "Preview setup could not save a folder");
    const auto afterSetup = ProjectScanner::readSettings();
    check (afterSetup.previews == library && afterSetup.roots == loaded.roots && afterSetup.capture.manual
        && afterSetup.capture.locked && afterSetup.capture.replace && afterSetup.seconds == 30,
        "Preview setup overwrote other recording or search settings");
    juce::Array<ProjectEntry> entries;
    for (int i = 0; i < 4; ++i)
    {
        const auto folder = library.getChildFile (juce::Uuid().toString()); check (folder.createDirectory().wasOk(), "Cannot create workflow entry");
        writeAudio (folder.getChildFile ("preview.ogg"), 0.1f);
        juce::var metadata (new juce::DynamicObject()); auto& obj = *metadata.getDynamicObject();
        obj.setProperty ("projectId", folder.getFileName()); obj.setProperty ("projectName", "Sketch " + juce::String (i));
        obj.setProperty ("daw", "Ableton Live"); obj.setProperty ("bpm", 120.0 + i * 5); obj.setProperty ("key", "A minor");
        obj.setProperty ("recordedAt", "2026-10-" + juce::String (10 + i) + "T12:00:00Z"); obj.setProperty ("favorite", i == 1);
        obj.setProperty ("tags", juce::Array<juce::var> { "Techno" }); obj.setProperty ("folders", juce::Array<juce::var> { "Ideas/Ambient" });
        obj.setProperty ("status", "In progress"); obj.setProperty ("notes", "Finish the bass");
        if (i == 0)
        {
            const auto project = projects.getChildFile ("Sketch 0.als"); check (project.replaceWithText ("fixture"), "Cannot write project");
            obj.setProperty ("projectPath", project.getFullPathName()); obj.setProperty ("projectFileId", ProjectIdentity::fileId (project));
        }
        check (LibraryStore::writeJsonAtomically (folder.getChildFile ("metadata.json"), metadata).wasOk(), "Cannot write workflow metadata");
    }
    entries = ProjectScanner::scan (library, { projects }); check (entries.size() == 4, "Workflow entries missing");
    LibraryFilter filter; filter.query = "sketch bass"; filter.bpmFrom = 124; filter.bpmTo = 126; filter.key = "A minor";
    filter.tags = "techno"; filter.folder = "Ideas"; filter.daw = "Ableton Live"; filter.favorites = true; filter.status = "In progress";
    filter.dateFrom = "2026-10-11"; filter.dateTo = "2026-10-11";
    int matches = 0; for (const auto& e : entries) if (filter.matches (e)) ++matches;
    check (matches == 1, "Combined musical/date/tag filters are not ANDed");
    const auto roundtrip = LibraryFilter::fromJson (filter.json());
    check (juce::JSON::toString (filter.json()) == juce::JSON::toString (roundtrip.json()), "Smart collection lost filters");
    LibraryFilter folderGroup; folderGroup.folders = { "Ideas", "Other" };
    auto groupedEntry = entries[0]; groupedEntry.folders = { "Other/Child" };
    check (folderGroup.matches (groupedEntry), "Selected folder union omitted the second subtree");
    check (LibraryFilter::fromJson (folderGroup.json()).matches (groupedEntry), "Smart collection lost multiple folders");
    groupedEntry.folders = { "Ideas elsewhere" };
    check (! folderGroup.matches (groupedEntry), "Folder union matched a shared name prefix outside its subtree");
    filter = {}; filter.sort = LibraryFilter::BpmAscending; filter.order (entries);
    check (entries[0].bpm == 120 && entries[3].bpm == 135, "BPM sorting failed");
    filter.missing = true; matches = 0; for (const auto& e : entries) if (filter.matches (e)) ++matches;
    check (matches == 3, "Missing-project view includes verified project or misses unassigned files");
    PlaybackQueue queue; queue.setEntries (entries); queue.select (entries[0]);
    check (queue.next()->projectId == entries[1].projectId && queue.previous()->projectId == entries[0].projectId, "Next/previous history failed");
    queue.setShuffle (true); std::set<std::string> visited { entries[0].projectId.toStdString() };
    for (int i = 0; i < 3; ++i) { auto e = queue.next(); check (e != nullptr && visited.insert (e->projectId.toStdString()).second, "Shuffle repeated before exhausting the collection"); }
    check (queue.next (true) == nullptr, "Repeat-off does not stop at end"); queue.repeat = PlaybackQueue::All;
    check (queue.next (true) != nullptr, "Repeat-all did not restart queue"); queue.repeat = PlaybackQueue::One;
    const auto repeated = queue.next (true); check (repeated != nullptr && queue.next (true)->projectId == repeated->projectId, "Repeat-one skipped preview");
    for (const auto shuffle : { false, true })
    {
        PlaybackQueue single; single.setEntries (entries); single.setShuffle (shuffle); single.select (entries[0]);
        auto following = single.next(); check (following != nullptr, "Cannot seed Single listening history");
        single.previous(); single.repeat = PlaybackQueue::Single;
        check (single.next (true) == nullptr && single.next (true) == nullptr,
               "Single advanced or repeated automatically, including through listening history");
        check (single.next()->projectId == following->projectId, "Single blocked manual Next or consumed the listening history");
        check (single.previous()->projectId == entries[0].projectId, "Single blocked manual Previous");
    }
    queue.setEntries ({}); check (queue.next() == nullptr, "Empty filtered collection borrowed another track");
    for (const auto rate : { 8000, 44100, 48000, 96000, 192000 })
    {
        juce::AudioBuffer<float> audio (2, rate * 2); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample (ch, i, 0.1f * std::sin (2 * juce::MathConstants<double>::pi * 1000 * i / rate));
        LoudnessMeter quiet (rate, 2); quiet.add (audio, audio.getNumSamples());
        audio.applyGain (2); LoudnessMeter loud (rate, 2); loud.add (audio, audio.getNumSamples());
        check (std::abs (loud.integrated() - quiet.integrated() - 6.0206) < 0.01, "Loudness meter gain invariance failed");
        check (std::abs (loud.gain() * 2 - quiet.gain()) < 0.001, "Normalization does not match perceived loudness");
        if (rate == 48000) check (std::abs (quiet.integrated() + 20.0) < 0.15, "K-weighted 1 kHz stereo reference is incorrect");
    }
    {
        WaveformPlayerComponent player; player.setVolume (0.5);
        check (std::abs (player.getOutputGain() - 0.25f) < 0.001, "Volume curve failed");
        player.setMuted (true); check (player.getOutputGain() == 0, "Mute failed"); player.setMuted (false);
        check (std::abs (player.getVolume() - 0.5) < 0.001 && ! player.isAudioDeviceOpen(), "Unmute lost volume or opened idle audio");
        player.setMuted (true); player.setVolume (0.8); check (! player.isMuted() && player.getOutputGain() > 0, "Changing volume did not unmute");
        player.setVolume (0); player.setMuted (false); check (std::abs (player.getVolume() - 0.8) < 0.001, "Unmute at zero did not restore the previous volume");
        const auto audioHash = ProjectIdentity::fingerprint (entries[0].previewOgg);
        player.setNormalized (true); player.loadEntry (entries[0]);
        waitFor ([&] { return player.hasLoadedEntry() && player.getOutputGain() > 1.0f; });
        check (! player.isAudioDeviceOpen(), "Normalization opened an idle audio device");
        player.setNormalized (false); check (std::abs (player.getOutputGain() - 0.64f) < 0.001, "Disabling normalization did not restore the selected volume");
        popup (player, "Repeat: off", 4);
        auto* repeat = find<juce::TextButton> (player, "Repeat: single");
        check (repeat && repeat->getTooltip().contains ("once, then stop"), "Repeat menu did not select or explain Single");
        waitFor ([] { return (int) LibraryStore::readJson (LibraryStore::configFile())["repeatPlayback"] == 3; });
        {
            WaveformPlayerComponent reopened;
            waitFor ([&] { return find<juce::TextButton> (reopened, "Repeat: single") != nullptr; });
            check (! reopened.isAudioDeviceOpen(), "Restoring Single opened an audio device");
        }
        popup (player, "Repeat: single", 1);
        check (ProjectIdentity::fingerprint (entries[0].previewOgg) == audioHash, "Playback volume/normalization changed the stored audio");
    }
    {
        WaveformEnvelope envelope;
        check (envelope.read (entries[0].previewOgg, [] { return false; }).wasOk(), "Cannot build detailed waveform");
        check (envelope.channels[0].size() > 1000 && envelope.channels[0].size() <= 32768, "Waveform resolution is too coarse or unbounded");
        for (const auto& channel : envelope.channels) for (const auto& peak : channel)
            check (std::isfinite (peak.low) && std::isfinite (peak.high) && peak.low <= peak.high,
                "Waveform peak envelope is invalid");
        const auto first = envelope.channels[0][0];
        check (envelope.read (entries[0].previewOgg, [] { return false; }).wasOk()
            && envelope.channels[0][0].low == first.low && envelope.channels[0][0].high == first.high, "Reading waveform twice accumulated old peaks");
    }
    auto edited = entries[1], concurrent = edited; concurrent.favorite = false;
    check (concurrent.save ({ "favorite" }).wasOk(), "Concurrent favorite update failed");
    edited.notes = "New note"; edited.status = "Finished"; edited.bpmOverride = 128.5; edited.keyOverride = "G major";
    check (edited.save ({ "notes", "status", "bpmOverride", "keyOverride" }).wasOk(), "Detail edits failed");
    auto saved = LibraryStore::readJson (edited.metadataFile);
    check (! (bool) saved["favorite"] && (double) saved["bpm"] == 125.0 && (double) saved["bpmOverride"] == 128.5, "Metadata overrides replaced analysis or concurrent fields");
    auto rescanned = ProjectScanner::scan (library, { projects });
    for (const auto& e : rescanned) if (e.projectId == edited.projectId) check (e.bpm == 128.5 && e.key == "G major" && e.bpmSource == "manual", "Manual corrections not used by library");
    edited.tags.add ("Undo this"); check (edited.save().wasOk(), "Cannot add undo tag");
    auto concurrentPatch = juce::var (new juce::DynamicObject()); concurrentPatch.getDynamicObject()->setProperty ("tags", juce::Array<juce::var> { "Techno", "Undo this", "Concurrent tag" });
    check (LibraryStore::patchJson (edited.metadataFile, concurrentPatch).wasOk(), "Cannot add concurrent tag");
    check (ProjectScanner::canUndoOrganization() && ProjectScanner::undoOrganization().wasOk(), "Organization undo failed");
    const auto undone = OrganizationUndo::strings (LibraryStore::readJson (edited.metadataFile)["tags"]);
    check (! undone.contains ("Undo this") && undone.contains ("Techno") && undone.contains ("Concurrent tag"), "Undo overwrote concurrent tags");
    // Real native row details form, accepted after a concurrent edit.
    ProjectScanner::refreshAsync(); waitFor ([&] { auto s = ProjectScanner::librarySnapshot(); return ! s->loading && s->sourceRoot == library && s->entries.size() == 4; });
    {
        ProjectListComponent list; list.setSize (640, 400);
        juce::ListBox* box = nullptr; for (auto* child : list.getChildren()) if (auto* candidate = dynamic_cast<juce::ListBox*> (child)) box = candidate;
        check (box != nullptr, "Library list is missing"); box->selectRow (0); box->selectRow (1, false, false);
        check (list.selectedEntries().size() == 2, "Multiple selection failed");
        auto* row = dynamic_cast<ProjectRowComponent*> (box->getComponentForRowNumber (0)); check (row && row->onDetails, "Details action missing"); row->onDetails();
        waitFor ([] { return form() != nullptr; });
        auto* dialog = form(); auto* notes = find<juce::TextEditor> (*dialog, "Notes"); auto* bpm = find<juce::TextEditor> (*dialog, "Manual BPM");
        check (notes && bpm, "Details form omitted note/BPM controls"); notes->setText ("UI note"); bpm->setText ("133.5");
        auto* automatic = find<juce::ToggleButton> (*dialog, "Automatic key recognition");
        auto* note = find<juce::ComboBox> (*dialog, "Key note");
        auto* scale = find<juce::ComboBox> (*dialog, "Key scale");
        check (automatic && note && scale, "Details form omitted automatic/note/scale controls");
        check (scale->getItemText (0) == "Major" && scale->getItemText (1) == "Minor" && scale->getNumItems() > 7, "Scale menu order/options changed");
        check (automatic->getToggleState() && ! note->isEnabled() && ! scale->isEnabled(), "Automatic key did not disable manual fields");
        check (note->getText() == "C" && scale->getText() == "Minor", "Manual key defaults must be C / Minor");
        automatic->setToggleState (false, juce::dontSendNotification); automatic->onClick();
        check (note->isEnabled() && scale->isEnabled(), "Manual key fields stayed disabled");
        note->setSelectedId (3, juce::dontSendNotification); scale->setSelectedId (3, juce::dontSendNotification);
        snapshot (*dialog, "workflow-details"); dialog->setSize (480, 360); snapshot (*dialog, "workflow-details-minimum");
        for (auto* child : dialog->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*> (child)) viewport->setViewPosition (0, 180);
        snapshot (*dialog, "workflow-details-minimum-key");
        accept (*dialog);
        waitFor ([] { return form() == nullptr; });
        waitFor ([&] { for (const auto& e : list.visibleEntries()) if (e.notes == "UI note" && e.bpm == 133.5 && e.keyOverride == "D dorian") return true; return false; });
        row = dynamic_cast<ProjectRowComponent*> (box->getComponentForRowNumber (0));
        row->onDetails(); waitFor ([] { return form() != nullptr; });
        dialog = form();
        automatic = find<juce::ToggleButton> (*dialog, "Automatic key recognition");
        note = find<juce::ComboBox> (*dialog, "Key note"); scale = find<juce::ComboBox> (*dialog, "Key scale");
        check (automatic && ! automatic->getToggleState() && note->getText() == "D" && scale->getText() == "Dorian", "Reopening lost manual scale");
        automatic->setToggleState (true, juce::dontSendNotification); automatic->onClick();
        check (! note->isEnabled() && ! scale->isEnabled(), "Returning to automatic key left manual fields active");
        snapshot (*dialog, "workflow-details-automatic");
        accept (*dialog); waitFor ([] { return form() == nullptr; });
        waitFor ([&] { for (const auto& e : list.visibleEntries()) if (e.notes == "UI note" && e.keyOverride.isEmpty() && e.key == "A minor") return true; return false; });
        row = dynamic_cast<ProjectRowComponent*> (box->getComponentForRowNumber (0));
        check (row && row->getTooltip().isEmpty(), "Row still shows floating project tooltip");
        auto* status = find<juce::TextButton> (*row, "Change preview status");
        check (status && status->isEnabled(), "Inline status button is missing");
        const auto statusFile = list.visibleEntries()[0].metadataFile;
        status->onClick();
        auto* statusMenu = juce::Component::getCurrentlyModalComponent();
        check (statusMenu != nullptr, "Inline status menu did not open");
        snapshot (*statusMenu, "workflow-status-menu");
        // Recycle the visible component while its popup is still open.
        auto rebound = list.visibleEntries()[1];
        row->setEntry (rebound, false);
        row->onStatusChanged = [] (juce::String) { throw std::runtime_error ("Status menu edited a recycled row"); };
        statusMenu->exitModalState (4);
        waitFor ([&] { return LibraryStore::readJson (statusFile)["status"].toString() == "Archived"; });
        list.refresh();
        waitFor ([&] { return list.visibleEntries()[0].status == "Archived"; });
        check (find<juce::TextButton> (list, "Actions for selected projects") == nullptr, "Original Actions toolbar button remains");
        // Batch menu and inverse deltas exercise the real native controls.
        box->selectRow (0); box->selectRow (1, false, false);
        popup (list, "Preview actions", 2); waitFor ([] { return form() != nullptr; });
        auto* tags = find<juce::TextEditor> (*form(), "Tags (comma-separated)"); check (tags != nullptr, "Batch tags field missing"); tags->setText ("Batch tag"); accept (*form());
        waitFor ([] { int n = 0; for (const auto& e : ProjectScanner::librarySnapshot()->entries) if (e.tags.contains ("Batch tag")) ++n; return n == 2; });
        popup (list, "Preview actions", 11);
        waitFor ([] { for (const auto& e : ProjectScanner::librarySnapshot()->entries) if (e.tags.contains ("Batch tag")) return false; return true; });
        auto* filters = find<juce::TextButton> (list, "Filter projects"); check (filters != nullptr, "Filters trigger missing"); filters->onClick(); waitFor ([] { return form() != nullptr; });
        auto* tagInput = find<TagFilterEditor> (*form(), "All these tags");
        check (tagInput != nullptr, "Filter tags lack suggestions");
        tagInput->setText ("tech", false); tagInput->showSuggestions();
        auto* suggestions = find<juce::ListBox> (*form(), "Existing tags");
        check (suggestions && suggestions->getModel()->getNumRows() == 1, "Existing tags did not filter by typed text");
        snapshot (*form(), "workflow-tag-suggestions");
        form()->setSize (480, 360); tagInput->showSuggestions();
        snapshot (*form(), "workflow-tag-suggestions-minimum");
        check (suggestions->getBottom() < tagInput->getY(), "Compact tag suggestions are clipped below the viewport");
        form()->setSize (580, 540); tagInput->showSuggestions();
        suggestions->getModel()->returnKeyPressed (0);
        check (tagInput->getText() == "Techno, ", "Choosing an existing tag did not complete the current token");
        tagInput->setText ("Techno, unlikely-tag", false); tagInput->showSuggestions();
        suggestions->getModel()->returnKeyPressed (0);
        check (tagInput->getText() == "Techno, unlikely-tag", "Empty suggestions selected a nonexistent tag");
        tagInput->setText ({}, false); suggestions->setVisible (false);
        find<juce::TextEditor> (*form(), "Minimum BPM")->setText ("128"); find<juce::TextEditor> (*form(), "Maximum BPM")->setText ("134");
        snapshot (*form(), "workflow-filters"); accept (*form()); waitFor ([&] { return list.getNumRows() == 3; });
        popup (list, "Save or remove smart collection", 1); waitFor ([] { return form() != nullptr; });
        find<juce::TextEditor> (*form(), "Collection name")->setText ("Production shortlist"); accept (*form());
        auto* collection = find<juce::ComboBox> (list, "Smart collections");
        waitFor ([&] { return collection && collection->getNumItems() == 2; });
        popup (list, "Preview actions", 10); check (list.getNumRows() == 4, "Clear filters did not restore library");
        collection->setSelectedId (2, juce::dontSendNotification); collection->onChange();
        auto* clear = find<juce::TextButton> (list, "Clear filters");
        check (clear && clear->getX() > find<juce::TextButton> (list, "Save or remove smart collection")->getRight(), "Clear filters is not beside save");
        clear->onClick(); check (list.getNumRows() == 4 && collection->getSelectedId() == 1, "Clear filters button did not reset the saved collection");
        collection->setSelectedId (2, juce::dontSendNotification); collection->onChange(); check (list.getNumRows() == 3, "Saved smart collection did not apply its BPM range");
        auto* views = find<juce::ComboBox> (list, "Library view"); check (views != nullptr, "Missing/trash views unavailable");
        views->setSelectedId (2, juce::dontSendNotification); views->onChange(); check (list.getNumRows() == 3, "Missing project panel ignored filter or included verified file");
        snapshot (list, "workflow-missing-projects");
    }
    check (LibraryStore::deleteEntry (library, edited.metadataFile.getParentDirectory()).wasOk(), "Move to trash failed");
    auto trash = ProjectScanner::scanTrash (library); check (trash.size() == 1 && trash[0].projectId == edited.projectId, "Trash lost original identity");
    check (edited.save().failed(), "Stale editor resurrected a trashed entry");
    const auto deleted = juce::Time::fromISO8601 (trash[0].deletedAt);
    check (LibraryStore::purgeExpiredTrash (library, deleted + juce::RelativeTime::days (29)).wasOk() && trash[0].metadataFile.existsAsFile(), "Trash purged before 30 days");
    ProjectScanner::refreshAsync(); waitFor ([] { return ProjectScanner::librarySnapshot()->trash.size() == 1; });
    {
        ProjectListComponent trashList; trashList.setSize (640, 400);
        auto* view = find<juce::ComboBox> (trashList, "Library view"); check (view != nullptr, "Trash view selector missing");
        view->setSelectedId (3, juce::dontSendNotification); view->onChange(); check (trashList.getNumRows() == 1, "Trash panel lost the deleted entry");
        snapshot (trashList, "workflow-trash");
        juce::ListBox* rows = nullptr; for (auto* child : trashList.getChildren()) if (auto* candidate = dynamic_cast<juce::ListBox*> (child)) rows = candidate;
        auto* row = rows ? dynamic_cast<ProjectRowComponent*> (rows->getComponentForRowNumber (0)) : nullptr;
        check (row && row->onRestore, "Trash row has no restore action"); row->onRestore();
        waitFor ([&] { return trashList.getNumRows() == 0; });
    }
    check (edited.metadataFile.existsAsFile(), "Restoration lost entry or metadata");
    check (LibraryStore::readJson (edited.metadataFile)["notes"].toString() == "New note", "Restoration lost organization");
    check (LibraryStore::deleteEntry (library, edited.metadataFile.getParentDirectory()).wasOk(), "Second trash move failed");
    trash = ProjectScanner::scanTrash (library);
    const auto secondDeleted = juce::Time::fromISO8601 (trash[0].deletedAt);
    check (LibraryStore::purgeExpiredTrash (library, secondDeleted + juce::RelativeTime::days (30)).wasOk() && ! trash[0].metadataFile.existsAsFile(), "Expired trash was not removed at 30 days");
    {
        const auto assignmentLibrary = root.getChildFile ("assignment-library");
        for (const auto* name : { "KE PENA PROD", "MOJO" })
        {
            const auto folder = assignmentLibrary.getChildFile (name);
            check (folder.createDirectory().wasOk(), "Cannot create assignment fixture");
            writeAudio (folder.getChildFile ("preview.ogg"), juce::String (name) == "MOJO" ? 0.6f : 0.1f);
            juce::var data (new juce::DynamicObject());
            data.getDynamicObject()->setProperty ("projectId", folder.getFileName());
            data.getDynamicObject()->setProperty ("projectName", folder.getFileName());
            check (LibraryStore::writeJsonAtomically (folder.getChildFile ("metadata.json"), data).wasOk(), "Cannot write assignment metadata");
        }
        const auto project = projects.getChildFile ("KE PENA PROD.rpp");
        check (project.replaceWithText ("assignment fixture"), "Cannot write assigned project");
        // Keep the project outside automatic search roots until manual assignment.
        check (ProjectScanner::saveSettings (assignmentLibrary, {}, 30, 4, false).wasOk(), "Cannot configure assignment library");
        ProjectScanner::refreshAsync();
        waitFor ([&] { const auto s = ProjectScanner::librarySnapshot(); return s->sourceRoot == assignmentLibrary && ! s->loading && s->entries.size() == 2; });
        MainComponent scene;
        scene.setSize (1024, 768);
        ProjectListComponent* list = nullptr;
        WaveformPlayerComponent* player = nullptr;
        for (auto* child : scene.getChildren())
        {
            if (auto* value = dynamic_cast<ProjectListComponent*> (child)) list = value;
            if (auto* value = dynamic_cast<WaveformPlayerComponent*> (child)) player = value;
        }
        check (list && player, "Assignment scene lacks list/player");
        juce::ListBox* rows = nullptr;
        for (auto* child : list->getChildren()) if (auto* value = dynamic_cast<juce::ListBox*> (child)) rows = value;
        check (rows != nullptr, "Assignment scene lacks rows");
        ProjectEntry mojo, missing;
        int missingRow = -1, mojoRow = -1;
        for (int i = 0; i < list->visibleEntries().size(); ++i)
        {
            const auto& e = list->visibleEntries()[i];
            if (e.name == "MOJO") { mojo = e; mojoRow = i; }
            else { missing = e; missingRow = i; }
        }
        check (missingRow >= 0 && mojoRow >= 0, "Assignment fixture entries missing");
        const auto mojoMetadata = mojo.metadataFile.loadFileAsString();
        const auto mojoAudio = ProjectIdentity::fingerprint (mojo.previewOgg);
        const auto missingAudio = ProjectIdentity::fingerprint (missing.previewOgg);
        player->loadEntry (mojo);
        waitFor ([&] { return player->hasLoadedEntry(); });
        // Keyboard selection does not play/load the selected recording.
        rows->selectRow (missingRow);
        auto target = player->projectAssignmentTarget();
        check (target && target->metadataFile == missing.metadataFile && target->projectId == missing.projectId,
            "Assign path targeted the last played preview instead of the selected recording");
        snapshot (scene, "assignment-selected");
        scene.setSize (640, 480); snapshot (scene, "assignment-selected-minimum");
        rows->deselectAllRows(); check (! player->projectAssignmentTarget(), "Assignment fell back to last playback without selection");
        rows->selectRow (missingRow); rows->selectRow (mojoRow, false, false);
        check (! player->projectAssignmentTarget(), "Ambiguous assignment accepted multiple selected recordings");
        rows->selectRow (mojoRow);
        // The picker keeps the identity captured when it opened, even if the
        // selection/playback changes before its asynchronous completion.
        target->projectPath = project.getFullPathName();
        bool committed = false;
        ProjectScanner::editAsync ([target] { return target->save ({ "projectPath" }); }, [&] (juce::Result r)
        { check (r.wasOk(), "Selected recording assignment failed"); committed = true; });
        waitFor ([&] { return committed; });
        const auto assigned = ProjectScanner::scan (assignmentLibrary, {});
        check (assigned.size() == 2, "Assignment created a duplicate entry");
        const auto saved = LibraryStore::readJson (missing.metadataFile);
        check (saved["projectId"].toString() == missing.projectId && saved["projectPath"].toString() == project.getFullPathName(),
            "Assignment did not preserve the selected recording identity");
        check (mojo.metadataFile.loadFileAsString() == mojoMetadata && ProjectIdentity::fingerprint (mojo.previewOgg) == mojoAudio
            && ProjectIdentity::fingerprint (missing.previewOgg) == missingAudio, "Assignment changed another recording or preview audio");
        std::cout << "PASS: Assign path pins selected recording with different last playback, rejects empty/multiple selection and preserves both previews without duplicates\n";
    }
    std::cout << "PASS: shared capture settings, combined filters, smart serialization, musical overrides, native details, multiselect, queue/history/shuffle/repeat, volume/mute, loudness, trash restore and 30-day retention\n";
}
