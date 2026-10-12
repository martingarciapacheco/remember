#include "AssociationTests.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Plugin/Source/PluginProcessor.h"
#include "../Plugin/Source/PluginEditor.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>

namespace
{
using Association = RecordingEngine::Association;
void check (bool value, const char* reason) { if (! value) throw std::runtime_error (reason); }
template <typename Predicate> void await (Predicate predicate, const char* reason)
{
    const auto end = juce::Time::getMillisecondCounter() + 7000;
    while (! predicate()) { check (juce::Time::getMillisecondCounter() < end, reason); juce::Thread::sleep (2); }
}
struct Head : juce::AudioPlayHead
{
    bool playing = true; double time = 0;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setIsPlaying (playing); p.setTimeInSeconds (time); p.setBpm (120); return p; }
};
void capture (RecordingEngine& engine, float level, double seconds = 1.2)
{
    Head head; const auto total = (int) std::llround (8000 * seconds);
    for (int done = 0; done < total;)
    {
        const auto count = juce::jmin (128, total - done); juce::AudioBuffer<float> audio (2, count);
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (audio.getWritePointer (ch), level, count);
        head.time = done / 8000.0; engine.processBlock (audio, &head); done += count;
    }
    head.playing = false; head.time = seconds; juce::AudioBuffer<float> stop (2, 128); stop.clear(); engine.processBlock (stop, &head);
}
void saved (RecordingEngine& engine, bool pending = false)
{
    await ([&] { return engine.getState() == (pending ? RecordingEngine::State::Pending : RecordingEngine::State::Done)
        || engine.getState() == RecordingEngine::State::Error; }, "Association capture did not finish");
    if (engine.getState() == RecordingEngine::State::Error) throw std::runtime_error (engine.getLastError().toStdString());
}
void sourceIs (RecordingEngine& engine, Association source, bool confirmed = true)
{
    await ([&] { const auto info = engine.getAssociationInfo(); return info.source == source && info.confirmed == confirmed; }, "Association source/confirmation did not change");
}
juce::var meta (const juce::File& folder) { return LibraryStore::readJson (folder.getChildFile ("metadata.json")); }
void audioIs (const juce::File& folder, float level, int sample = 100, int length = 0)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
    check (reader && (length == 0 || reader->lengthInSamples == length), "Association lost or truncated its PCM");
    juce::AudioBuffer<float> point (2, 1);
    check (reader->read (&point, 0, 1, sample, true, true) && std::abs (point.getSample (0, 0) - level) < 0.00001f,
           "Association wrote a different recording or changed its tail");
}
void retire() { await ([] { return BackgroundRetirement::pending() == 0; }, "Association resources did not retire"); }
}

void testAssociation (const juce::File& fixtureRoot)
{
    const auto root = fixtureRoot.getChildFile ("association"); check (root.createDirectory().wasOk(), "Cannot create association fixture");
    const auto library = root.getChildFile ("library"); check (library.createDirectory().wasOk(), "Cannot create association library");
    juce::var isolatedRoots (new juce::DynamicObject());
    isolatedRoots.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { root.getFullPathName() });
    check (LibraryStore::patchJson (LibraryStore::configFile(), isolatedRoots, true).wasOk(), "Cannot isolate association search roots");
    SettingsManager::Snapshot settings; settings.previews = library; settings.seconds = 15;
    juce::XmlElement kept ("RememberState"); juce::String keepId;
    {
        RecordingEngine engine ([] { return HostProject{}; }); engine.prepare (8000, 128, settings);
        engine.setProjectName ("Idea"); capture (engine, 0.2f, 2.4); saved (engine, true);
        keepId = engine.getAssociationInfo().id; engine.keepRecording ("Kept idea"); sourceIs (engine, Association::ManualRecording);
        await ([&] { return meta (library.getChildFile (keepId)).getProperty ("projectAssociation", "").toString() == "manual-recording"; }, "Keep did not publish permanent metadata");
        engine.saveState (kept); audioIs (library.getChildFile (keepId), 0.2f, 100, 19200);
        engine.setProjectName ("Renamed idea");
        await ([&] { return meta (library.getChildFile (keepId)).getProperty ("projectName", "").toString() == "Renamed idea"; }, "Kept recording cannot be renamed");
        engine.saveState (kept);
    }
    retire(); check (library.getChildFile (keepId).isDirectory(), "Closing discarded a kept recording");
    {
        RecordingEngine engine ([] { return HostProject { true, "Another title", {}, "REAPER" }; });
        engine.setOutputFolder (library); engine.restoreState (kept); engine.prepare (8000, 128, settings);
        sourceIs (engine, Association::ManualRecording); check (engine.getProjectName() == "Renamed idea", "A title overrode restored manual keep");
        capture (engine, 0.6f); saved (engine); audioIs (library.getChildFile (keepId), 0.6f);
        audioIs (library.getChildFile (keepId), 0.2f, 14000, 19200);
    }
    retire(); std::cout << "PASS: explicit keep, rename, close, restore and partial tail without a DAW file\n";

    const auto projects = root.getChildFile ("projects"); check (projects.createDirectory().wasOk(), "Cannot create projects");
    isolatedRoots.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { projects.getFullPathName() });
    check (LibraryStore::patchJson (LibraryStore::configFile(), isolatedRoots).wasOk(), "Cannot scope manual-file search roots");
    const auto selected = projects.getChildFile ("Selected.rpp"); check (selected.replaceWithText ("selected project"), "Cannot create selected project");
    juce::XmlElement linked ("RememberState"); juce::String linkId;
    {
        RecordingEngine engine ([] { return HostProject { true, "Unrelated title", {}, "REAPER" }; }); engine.prepare (8000, 128, settings);
        capture (engine, 0.4f, 2.4); saved (engine, true);
        linkId = engine.getAssociationInfo().id; engine.associateProjectFile (selected); sourceIs (engine, Association::ManualFile);
        await ([&] { return LibraryStore::audioFile (library.getChildFile (linkId), false).existsAsFile(); }, "Manual file did not promote pending audio");
        audioIs (library.getChildFile (linkId), 0.4f, 100, 19200);
        check (engine.getAssociationInfo().confidence == "explicit" && engine.getAssociationInfo().verified, "Manual file has no explicit/verified evidence");
        engine.saveState (linked);
        engine.associateProjectFile (projects.getChildFile ("missing.rpp"));
        await ([&] { return engine.getLastError().startsWith ("Associate project file"); }, "Invalid selection has no actionable error");
        check (engine.getAssociationInfo().id == linkId && engine.getAssociationInfo().path == selected.getFullPathName(), "Invalid selection replaced a valid association");
    }
    retire();
    const auto moved = projects.getChildFile ("Moved.rpp"); check (selected.moveFileTo (moved), "Cannot move selected fixture");
    {
        RecordingEngine engine ([] { return HostProject{}; }); engine.setOutputFolder (library); engine.restoreState (linked); engine.prepare (8000, 128, settings);
        sourceIs (engine, Association::ManualFile);
        await ([&] { return engine.getAssociationInfo().verified && engine.getAssociationInfo().path == moved.getFullPathName(); }, "Restored manual file was not relocated by identity");
        check (engine.getAssociationInfo().id == linkId, "Moving a manual file lost its recording identity");
        capture (engine, 0.8f); saved (engine); audioIs (library.getChildFile (linkId), 0.4f, 14000, 19200);
        engine.saveState (linked);
    }
    retire(); std::cout << "PASS: manual file preserves pending audio, ignores title-only captions, restores and follows a moved identity\n";

    const auto outside = root.getChildFile ("outside"); check (outside.createDirectory().wasOk(), "Cannot create outside root");
    check (moved.moveFileTo (outside.getChildFile ("Original.rpp")) && moved.replaceWithText ("different project at reused path"), "Cannot reuse selected path");
    {
        RecordingEngine engine ([] { return HostProject{}; }); engine.setOutputFolder (library); engine.restoreState (linked); engine.prepare (8000, 128, settings);
        await ([&] { return engine.getState() == RecordingEngine::State::Error; }, "Reused manual path was silently accepted");
        const auto revision = meta (library.getChildFile (linkId)).getProperty ("audioRevision", "").toString();
        capture (engine, 0.95f);
        check (meta (library.getChildFile (linkId)).getProperty ("audioRevision", "").toString() == revision && ! engine.getAssociationInfo().verified,
               "Unverified manual path recorded into the original entry");
        engine.associateProjectFile (moved); sourceIs (engine, Association::ManualFile);
        await ([&] { return engine.getAssociationInfo().verified; }, "Explicit reselect did not verify replacement");
        check (engine.getAssociationInfo().id != linkId, "A reused file path adopted another project's entry");
        capture (engine, 0.95f); saved (engine); audioIs (library.getChildFile (linkId), 0.8f);
    }
    retire(); std::cout << "PASS: reused manual path blocks capture; explicit replacement forks and preserves the original\n";

    const auto a = root.getChildFile ("A"), b = root.getChildFile ("B"); check (a.createDirectory().wasOk() && b.createDirectory().wasOk(), "Cannot create homonym roots");
    const auto songA = a.getChildFile ("Same.rpp"), songB = b.getChildFile ("Same.rpp");
    check (songA.replaceWithText ("project A") && songB.replaceWithText ("project B"), "Cannot create homonyms");
    juce::var patch (new juce::DynamicObject()); patch.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { a.getFullPathName() });
    check (LibraryStore::patchJson (LibraryStore::configFile(), patch, true).wasOk(), "Cannot set search roots");
    {
        RecordingEngine engine ([] { return HostProject { true, "Same", {}, "REAPER" }; }); engine.prepare (8000, 128, settings);
        sourceIs (engine, Association::TitleMatch); check (engine.getAssociationInfo().confidence == "inferred", "Title-only evidence was overstated");
        capture (engine, 0.3f, 2.4); saved (engine); const auto originalId = engine.getAssociationInfo().id;
        patch.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { a.getFullPathName(), b.getFullPathName() });
        check (LibraryStore::patchJson (LibraryStore::configFile(), patch).wasOk(), "Cannot expose Save As homonym");
        sourceIs (engine, Association::Ambiguous, false);
        check (engine.getAssociationInfo().id != originalId && engine.getAssociationInfo().path.isEmpty(), "Homonymous Save As stayed on the previous path");
        capture (engine, 0.7f); saved (engine, true);
        engine.associateProjectFile (songB); sourceIs (engine, Association::ManualFile); capture (engine, 0.9f); saved (engine);
        audioIs (library.getChildFile (originalId), 0.3f, 100, 19200);
        check (engine.getAssociationInfo().path == songB.getFullPathName(), "Manual resolution picked the other homonym");
        ProjectIdentity::CandidateIndex index; check (index.rebuild ({ a, b }), "Cannot index homonyms");
        check (index.resolve ("Same", "REAPER").isEmpty() && index.wasLastResolutionAmbiguous(), "Resolver lost ambiguity");
        check (index.resolve ("Same", "REAPER").isEmpty() && index.wasLastResolutionAmbiguous(), "Negative cache lost ambiguity status");
    }
    retire(); std::cout << "PASS: title-only homonymous Save As becomes ambiguous; manual choice resolves it without overwriting the original\n";

    {
        RecordingEngine engine ([] { return HostProject{}; }); engine.prepare (8000, 128, settings);
        engine.setProjectName ("Failed keep"); capture (engine, 0.5f); saved (engine, true);
        const auto id = engine.getAssociationInfo().id; const auto collision = library.getChildFile (id);
        check (collision.replaceWithText ("occupied"), "Cannot create permanent collision");
        engine.keepRecording(); await ([&] { return engine.getState() == RecordingEngine::State::Error; }, "Keep collision was not reported");
        check (! engine.isProjectConfirmed() && collision.loadFileAsString() == "occupied", "Keep replaced a destination or claimed success");
        check (collision.deleteFile(), "Cannot release fixture collision");
        engine.keepRecording(); sourceIs (engine, Association::ManualRecording); audioIs (library.getChildFile (id), 0.5f);
        engine.useAutomaticAssociation ("stale-id");
        await ([&] { return engine.getLastError().startsWith ("Change association"); }, "Stale menu action was not rejected");
        check (engine.getAssociationInfo().id == id, "Stale association menu changed another recording");
        engine.useAutomaticAssociation(); sourceIs (engine, Association::Unassigned, false);
        check (library.getChildFile (id).isDirectory(), "Automatic mode removed a permanent recording");
    }
    retire(); std::cout << "PASS: keep collision preserves audio, retry succeeds, stale actions are rejected and automatic mode retains permanent data\n";
    juce::String failedId;
    {
        RecordingEngine engine ([] { return HostProject{}; }); engine.prepare (8000, 128, settings);
        capture (engine, 0.45f); saved (engine, true); failedId = engine.getAssociationInfo().id;
        check (library.getChildFile (failedId).replaceWithText ("occupied"), "Cannot create failed-close collision");
        engine.keepRecording(); await ([&] { return engine.getState() == RecordingEngine::State::Error; }, "Failed keep did not report its destination collision");
    }
    retire();
    const auto failedPending = library.getChildFile (".pending").getChildFile (failedId);
    audioIs (failedPending, 0.45f);
    check (LibraryStore::pendingOwnerState (failedPending, meta (failedPending)) == LibraryStore::OwnerState::Orphaned,
           "Closing failed keep lost its recoverable orphan or retained its owner");
    std::cout << "PASS: closing after failed explicit keep preserves recoverable audio\n";
    juce::String existingFileId, originalKeepId;
    const auto existingFile = projects.getChildFile ("Existing.rpp");
    check (existingFile.replaceWithText ("existing linked project"), "Cannot create existing linked file");
    {
        RecordingEngine engine ([&] { return HostProject { true, "Existing", existingFile.getFullPathName(), "REAPER" }; });
        engine.prepare (8000, 128, settings); sourceIs (engine, Association::HostPath);
        capture (engine, 0.15f, 2.4); saved (engine); existingFileId = engine.getAssociationInfo().id;
    }
    retire();
    {
        RecordingEngine engine ([] { return HostProject{}; }); engine.prepare (8000, 128, settings);
        capture (engine, 0.55f); saved (engine, true); engine.keepRecording ("Original kept idea");
        sourceIs (engine, Association::ManualRecording); originalKeepId = engine.getAssociationInfo().id;
        engine.associateProjectFile (existingFile); sourceIs (engine, Association::ManualFile);
        check (engine.getAssociationInfo().id == existingFileId, "Kept association duplicated an existing file's entry");
        audioIs (library.getChildFile (existingFileId), 0.55f);
        audioIs (library.getChildFile (existingFileId), 0.15f, 14000, 19200);
        audioIs (library.getChildFile (originalKeepId), 0.55f);
        check (meta (library.getChildFile (originalKeepId)).getProperty ("projectPath", "").toString().isEmpty(),
               "Original kept audio acquired a duplicate file association");
    }
    retire(); std::cout << "PASS: a kept recording adopts an existing file entry, merges its prefix, retains the existing tail and preserves the original idea\n";
    {
        const auto lib = root.getChildFile ("app-kept"); check (lib.createDirectory().wasOk(), "Cannot create app-kept library");
        const auto id = juce::Uuid().toString();
        const auto pending = lib.getChildFile (".pending").getChildFile (id);
        {
            LibraryStore::OwnerLease owner; check (LibraryStore::claimPending (lib, pending, owner).wasOk(), "Cannot seed pending owner");
            LibraryStore::AudioRevision revision (pending); check (revision.prepare().wasOk(), "Cannot prepare app-kept audio");
            for (int format = 0; format < 2; ++format)
            {
                juce::WavAudioFormat wav; juce::OggVorbisAudioFormat ogg;
                juce::AudioFormat& codec = format == 0 ? static_cast<juce::AudioFormat&> (wav) : static_cast<juce::AudioFormat&> (ogg);
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (format == 0 ? revision.pcm : revision.ogg);
                auto writer = codec.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (8000).withNumChannels (2)
                    .withBitsPerSample (format == 0 ? 32 : 16).withQualityOptionIndex (4)
                    .withSampleFormat (format == 0 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint : juce::AudioFormatWriterOptions::SampleFormat::automatic));
                check (writer != nullptr, "Cannot create app-kept codec"); juce::AudioBuffer<float> data (2, 19200);
                for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (data.getWritePointer (ch), 0.35f, 19200);
                check (writer->writeFromAudioSampleBuffer (data, 0, 19200), "Cannot write app-kept audio");
            }
            juce::var metadata (new juce::DynamicObject()); metadata.getDynamicObject()->setProperty ("projectId", id);
            metadata.getDynamicObject()->setProperty ("projectName", "App kept"); metadata.getDynamicObject()->setProperty ("pending", true);
            metadata.getDynamicObject()->setProperty ("owner", owner.proof());
            check (LibraryStore::commitAudio (pending, revision, metadata, {}).wasOk(), "Cannot commit app-kept fixture");
        }
        check (LibraryStore::recoverPending (lib, pending).wasOk(), "App could not keep orphan");
        juce::XmlElement oldState ("RememberState"); oldState.setAttribute ("projectId", id);
        RecordingEngine engine ([] { return HostProject{}; }); auto otherSettings = settings; otherSettings.previews = lib;
        engine.setOutputFolder (lib); engine.restoreState (oldState); engine.prepare (8000, 128, otherSettings);
        engine.keepRecording ("App kept"); sourceIs (engine, Association::ManualRecording);
        capture (engine, 0.65f); saved (engine); audioIs (lib.getChildFile (id), 0.65f);
        audioIs (lib.getChildFile (id), 0.35f, 14000, 19200);
        check (! LibraryStore::audioFile (pending, true).existsAsFile(), "Restored plugin duplicated an app-kept ID in pending storage");
    }
    retire(); std::cout << "PASS: explicit keep reuses an app-kept ID from older plugin state without duplicating pending storage\n";
    {
        juce::var config (new juce::DynamicObject()); config.getDynamicObject()->setProperty ("previewsFolder", library.getFullPathName());
        check (LibraryStore::patchJson (LibraryStore::configFile(), config, true).wasOk(), "Cannot configure editor fixture");
        RememberProcessor processor ([] { return HostProject{}; }); auto& engine = processor.getEngine(); engine.prepare (8000, 128, settings);
        capture (engine, 0.25f); saved (engine, true); engine.keepRecording (juce::String::fromUTF8 ("Idea - asociación manual"));
        sourceIs (engine, Association::ManualRecording);
        std::unique_ptr<RememberEditor> editor (static_cast<RememberEditor*> (processor.createEditor())); editor->timerCallback();
        const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
        check (snapshot.isValid() && snapshot.getWidth() == 440 && snapshot.getHeight() == 496, "Association editor has invalid bounds");
        int captureSwitches = 0;
        for (auto* child : editor->getChildren())
        {
            check (editor->getLocalBounds().contains (child->getBounds()), "Association editor clips a control");
            if (dynamic_cast<juce::ToggleButton*> (child) != nullptr) ++captureSwitches;
        }
        check (captureSwitches == 3, "Association editor omitted capture/lock/replacement switches");
        const auto artifact = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts/association-editor.png");
        check (LibraryStore::ensureDirectory (artifact.getParentDirectory()).wasOk(), "Cannot create editor artifact directory");
        juce::FileOutputStream stream (artifact);
        check (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
               && juce::PNGImageFormat().writeImageToStream (snapshot, stream), "Cannot render association editor");
        editor.reset();
    }
    retire(); std::cout << "PASS: actual plugin editor renders association source, action and manual name; artifacts/association-editor.png\n";
    {
        const auto projectsRoot = root.getChildFile ("checked-search");
        const auto previews = root.getChildFile ("checked-search-library");
        const auto missingRoot = root.getChildFile ("checked-search-offline");
        check (projectsRoot.createDirectory().wasOk() && previews.createDirectory().wasOk(), "Cannot create checked-search fixture");
        const auto project = projectsRoot.getChildFile ("SearchChecked.rpp");
        check (project.replaceWithText ("checked-search project"), "Cannot create checked-search DAW file");
        juce::var config (new juce::DynamicObject());
        config.getDynamicObject()->setProperty ("previewsFolder", previews.getFullPathName());
        config.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { projectsRoot.getFullPathName() });
        check (LibraryStore::patchJson (LibraryStore::configFile(), config, true).wasOk(), "Cannot configure checked search");
        RememberProcessor processor ([] { return HostProject { true, "SearchChecked", {}, "REAPER" }; });
        auto& engine = processor.getEngine(); auto captureSettings = settings; captureSettings.previews = previews;
        engine.prepare (8000, 128, captureSettings); sourceIs (engine, Association::TitleMatch);
        capture (engine, 0.3f, 2.4); saved (engine);
        const auto id = engine.getAssociationInfo().id, initialRevision = meta (previews.getChildFile (id)).getProperty ("audioRevision", "").toString();
        config.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { projectsRoot.getFullPathName(), missingRoot.getFullPathName() });
        check (LibraryStore::patchJson (LibraryStore::configFile(), config).wasOk(), "Cannot make a checked-search root offline");
        await ([&] { return engine.getAssociationInfo().problem.contains (missingRoot.getFullPathName()); }, "Candidate failure was not published to the plugin");
        check (engine.getAssociationInfo().id == id && engine.getAssociationInfo().confirmed && ! engine.getAssociationInfo().verified,
               "Candidate failure discarded the previously confirmed recording identity");
        capture (engine, 0.9f);
        check (meta (previews.getChildFile (id)).getProperty ("audioRevision", "").toString() == initialRevision,
               "An unverifiable host overwrote the last valid preview");
        audioIs (previews.getChildFile (id), 0.3f, 100, 19200);
        std::unique_ptr<RememberEditor> editor (static_cast<RememberEditor*> (processor.createEditor())); editor->timerCallback();
        bool warning = false;
        for (auto* child : editor->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                warning |= label->getText().contains ("search unavailable") && label->getTooltip().contains (missingRoot.getFullPathName());
        check (warning, "Plugin editor hid the actionable candidate-search error");
        check (missingRoot.createDirectory().wasOk(), "Cannot restore checked-search root");
        await ([&] { const auto info = engine.getAssociationInfo(); return info.problem.isEmpty() && info.verified; },
               "Restoring candidate access did not unblock recording");
        capture (engine, 0.7f); saved (engine);
        check (engine.getAssociationInfo().id == id, "Candidate recovery switched the saved recording identity");
        audioIs (previews.getChildFile (id), 0.7f, 100, 19200); audioIs (previews.getChildFile (id), 0.3f, 15000, 19200);
        const auto anotherOfflineRoot = root.getChildFile ("checked-search-still-offline");
        config.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { projectsRoot.getFullPathName(), anotherOfflineRoot.getFullPathName() });
        check (LibraryStore::patchJson (LibraryStore::configFile(), config).wasOk(), "Cannot seed manual override search error");
        await ([&] { return engine.getAssociationInfo().problem.isNotEmpty(); }, "Cannot observe manual override search error");
        engine.associateProjectFile (project, id); sourceIs (engine, Association::ManualFile);
        await ([&] { const auto info = engine.getAssociationInfo(); return info.problem.isEmpty() && info.verified; },
               "An explicitly verified project file did not resolve the tree error");
        juce::Thread::sleep (1200); // Include a stable discovery poll using the still-offline search root.
        check (engine.getAssociationInfo().problem.isEmpty() && engine.getAssociationInfo().verified,
               "Unrelated search-root failure invalidated a directly verified manual file");
        capture (engine, 0.5f); saved (engine); audioIs (previews.getChildFile (id), 0.5f, 100, 19200);
        editor.reset();
    }
    retire(); std::cout << "PASS: failed search preserves identity/audio, publishes native editor error, blocks unverified capture, recovers and accepts a verified manual file\n";
}
