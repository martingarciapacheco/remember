#include "RecoveryTests.h"
#include "AtomicJsonTests.h"
#include "../Shared/LibraryStore.h"
#include "../App/Source/ProjectScanner.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
void check (bool value, const char* reason) { if (! value) throw std::runtime_error (reason); }
juce::var object() { return juce::var (new juce::DynamicObject()); }
juce::var metadata (const juce::File& folder) { return LibraryStore::readJson (folder.getChildFile ("metadata.json")); }
void audio (const juce::File& file, bool pcm, float value, int channels = 2)
{
    juce::WavAudioFormat wav;
    juce::OggVorbisAudioFormat ogg;
    juce::AudioFormat& format = pcm ? static_cast<juce::AudioFormat&> (wav) : static_cast<juce::AudioFormat&> (ogg);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (8000)
        .withNumChannels (channels).withBitsPerSample (pcm ? 32 : 16).withQualityOptionIndex (4)
        .withSampleFormat (pcm ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                              : juce::AudioFormatWriterOptions::SampleFormat::automatic));
    check (writer != nullptr, "Recovery fixture cannot create audio writer");
    juce::AudioBuffer<float> samples (channels, 4800);
    for (int ch = 0; ch < channels; ++ch) for (int i = 0; i < 4800; ++i) samples.setSample (ch, i, value);
    check (writer->writeFromAudioSampleBuffer (samples, 0, 4800), "Recovery fixture cannot write audio");
}
juce::var patch (const juce::File& folder, float value)
{
    auto result = object(); auto* o = result.getDynamicObject();
    o->setProperty ("projectId", folder.getFileName());
    o->setProperty ("projectName", "Recovery fixture");
    o->setProperty ("pending", folder.getParentDirectory().getFileName() == ".pending");
    o->setProperty ("recordedAt", "2026-10-07T12:00:00Z");
    o->setProperty ("durationSeconds", 0.6); o->setProperty ("sampleRate", 8000); o->setProperty ("channels", 2);
    o->setProperty ("bpm", value < 0.5f ? 90 : 149); o->setProperty ("key", value < 0.5f ? "A minor" : "D major");
    return result;
}
juce::String publish (const juce::File& folder, float value)
{
    LibraryStore::AudioRevision revision (folder);
    check (revision.prepare().wasOk(), "Recovery fixture cannot prepare audio");
    audio (revision.pcm, true, value); audio (revision.ogg, false, value);
    check (LibraryStore::commitAudio (folder, revision, patch (folder, value), metadata (folder).getProperty ("audioRevision", "").toString()).wasOk(),
           "Recovery fixture cannot publish audio");
    return revision.id;
}
float sample (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr && reader->lengthInSamples == 4800, "Recovered audio is incomplete or unreadable");
    juce::AudioBuffer<float> samples (2, 1);
    check (reader->read (&samples, 0, 1, 100, true, true), "Cannot read recovered sample");
    return samples.getSample (0, 0);
}
void crash (const juce::File& folder, const juce::String& phase)
{
    juce::ChildProcess child;
    check (child.start (juce::StringArray { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
            "--storage-crash", folder.getFullPathName(), phase }), "Cannot start recovery crash fixture");
    if (! child.waitForProcessToFinish (15000)) { child.kill(); throw std::runtime_error ("Recovery crash fixture timed out"); }
    if (child.getExitCode() != 77) throw std::runtime_error ("Recovery child failed: " + child.readAllProcessOutput().toStdString());
}
juce::File library (const juce::File& root, const juce::String& name)
{
    const auto path = root.getChildFile (name);
    check (LibraryStore::ensureDirectory (path).wasOk(), "Cannot create recovery library"); return path;
}
juce::File entry (const juce::File& root) { return root.getChildFile (juce::Uuid().toString()); }
void corruptPcm (const juce::File& file)
{
    juce::MemoryBlock bytes; check (file.loadFileAsData (bytes) && bytes.getSize() > 1000, "Cannot read checksum fixture");
    static_cast<unsigned char*> (bytes.getData())[bytes.getSize() / 2] ^= 4;
    check (file.replaceWithData (bytes.getData(), bytes.getSize()), "Cannot corrupt PCM fixture");
}
bool ageDirectory (const juce::File& folder)
{
    const auto time = juce::Time::getCurrentTime() - juce::RelativeTime::days (2);
   #if JUCE_WINDOWS
    const auto handle = CreateFileW (folder.getFullPathName().toWideCharPointer(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const auto ticks = (juce::uint64) (time.toMilliseconds() + 11644473600000LL) * 10000ULL;
    const FILETIME stamp { (DWORD) ticks, (DWORD) (ticks >> 32) };
    const bool result = SetFileTime (handle, nullptr, nullptr, &stamp) != 0;
    CloseHandle (handle); return result;
   #else
    return folder.setLastModificationTime (time);
   #endif
}
}

int storageCrash (const juce::File& folder, const juce::String& phase)
{
    // This destructive subprocess mode is confined to our generated fixtures.
    auto ancestor = folder;
    while (ancestor.getParentDirectory() != ancestor && ancestor.getFileName() != "storage-recovery") ancestor = ancestor.getParentDirectory();
    if (! folder.isAChildOf (juce::File::getCurrentWorkingDirectory()) || ancestor.getFileName() != "storage-recovery"
        || ! ancestor.getParentDirectory().getFileName().startsWith ("remember-") || juce::Uuid (folder.getFileName()).isNull()) return 2;
    try
    {
        if (phase.startsWith ("promote:"))
        {
            const auto root = folder.getParentDirectory().getParentDirectory();
            const auto stage = phase.fromFirstOccurrenceOf (":", false, false).getIntValue();
            const auto result = LibraryStore::recoverPending (root, folder, true, [stage] (LibraryStore::PromotionStage where)
            { if ((int) where == stage) std::_Exit (77); });
            if (result.failed()) throw std::runtime_error (result.getErrorMessage().toStdString());
            return 3;
        }
        const auto stage = phase.getIntValue();
        const bool ownedPromotion = phase.startsWith ("owned-promote:");
        LibraryStore::OwnerLease owner;
        if (folder.getParentDirectory().getFileName() == ".pending")
            check (LibraryStore::claimPending (folder.getParentDirectory().getParentDirectory(), folder, owner).wasOk(), "Child cannot claim pending capture");
        LibraryStore::AudioRevision revision (folder);
        if (const auto prepared = revision.prepare(); prepared.failed())
            throw std::runtime_error ("Child cannot prepare revision at " + revision.directory.getFullPathName().toStdString()
                + ": " + prepared.getErrorMessage().toStdString());
        check (folder.getChildFile ("candidate-id.txt").replaceWithText (revision.id), "Cannot write candidate marker");
        audio (revision.pcm, true, 0.6f); if (stage == -2) std::_Exit (77);
        audio (revision.ogg, false, 0.6f); if (stage == -1) std::_Exit (77);
        auto next = patch (folder, 0.6f); next.getDynamicObject()->setProperty ("owner", owner.proof());
        if (folder.getParentDirectory().getFileName() == ".pending") next.getDynamicObject()->setProperty ("projectName", "");
        const auto result = LibraryStore::commitAudio (folder, revision, next, metadata (folder).getProperty ("audioRevision", "").toString(), {},
            [stage, ownedPromotion] (LibraryStore::CommitStage where) { if (! ownedPromotion && (int) where == stage) std::_Exit (77); });
        if (result.failed()) throw std::runtime_error (result.getErrorMessage().toStdString());
        if (ownedPromotion)
        {
            auto identity = object(); identity.getDynamicObject()->setProperty ("projectId", folder.getFileName());
            identity.getDynamicObject()->setProperty ("projectName", "Explicitly kept recording");
            identity.getDynamicObject()->setProperty ("projectAssociation", "manual-recording");
            identity.getDynamicObject()->setProperty ("projectAssociationConfidence", "explicit");
            const auto stop = phase.fromFirstOccurrenceOf (":", false, false).getIntValue();
            const auto kept = LibraryStore::promoteOwnedPending (folder.getParentDirectory().getParentDirectory(), folder, owner, identity,
                [stop] (LibraryStore::PromotionStage where) { if ((int) where == stop) std::_Exit (77); });
            if (kept.failed()) throw std::runtime_error (kept.getErrorMessage().toStdString());
        }
        return 3;
    }
    catch (const std::exception& error) { std::cerr << error.what(); return 4; }
}

void testRecovery (const juce::File& fixtureRoot)
{
    testAtomicJson (fixtureRoot);
    const auto root = library (fixtureRoot, "storage-recovery");
    {
        const auto lib = library (root, "array-roots"), folder = entry (lib);
        const auto confirmed = publish (folder, 0.2f);
        const auto manifest = folder.getChildFile ("metadata.json");
        const auto savedManifest = manifest.loadFileAsString();
        const auto savedReceipt = folder.getChildFile (".audio").getChildFile (confirmed).getChildFile ("revision.json").loadFileAsString();
        check (manifest.replaceWithText ("[1,2,3]"), "Cannot create array manifest fixture");
        bool called = false;
        check (LibraryStore::updateJson (manifest, [&] (juce::DynamicObject&) { called = true; return juce::Result::ok(); }).failed()
               && ! called && manifest.loadFileAsString() == "[1,2,3]", "Array metadata reached an object mutation or was silently reset");
        LibraryStore::AudioRevision rejected (folder);
        check (LibraryStore::commitAudio (folder, rejected, juce::var (juce::Array<juce::var> { 1, 2 }), confirmed).failed(),
               "Array patch reached an object-only audio writer");
        check (folder.getChildFile (".metadata-backup.json").replaceWithText ("[4,5]"), "Cannot create array backup fixture");
        const auto recovered = LibraryStore::recoverEntry (folder);
        check (recovered.result.wasOk() && recovered.repaired && recovered.revision == confirmed
               && std::abs (sample (LibraryStore::audioFile (folder, true)) - 0.2f) < 0.00001f,
               "Array manifest/backup did not recover the confirmed audio from its receipt");
        const auto receipt = folder.getChildFile (".audio").getChildFile (confirmed).getChildFile ("revision.json");
        check (receipt.replaceWithText ("[6,7]"), "Cannot create array receipt fixture");
        const auto before = manifest.loadFileAsString();
        check (LibraryStore::recoverEntry (folder).result.failed() && manifest.loadFileAsString() == before,
               "Array receipt was accepted or recovery rewrote the source without verified audio");
        check (receipt.replaceWithText (savedReceipt) && manifest.replaceWithText (savedManifest), "Cannot restore array fixtures");
        check (LibraryStore::recoverEntry (folder).result.wasOk(), "Repairing the invalid receipt did not restore verified audio");
        std::cout << "PASS: array-root manifests/settings patches/receipts are rejected without null object mutation; valid committed audio recovers through history\n";
    }
   #if JUCE_WINDOWS
    {
        const auto lib = library (root, "ambiguous-pending"), folder = entry (lib.getChildFile (".pending"));
        crash (folder, "1"); crash (folder, "1"); // Two real writers exit after their first receipts.
        const auto manifest = folder.getChildFile ("metadata.json");
        const auto alternatives = LibraryStore::recoverEntry (folder);
        check (alternatives.result.failed() && alternatives.choices.size() == 2 && alternatives.selectionToken == "missing"
               && ! manifest.exists(), "Ambiguous orphan receipts were silently selected or did not expose verified choices");
        const auto selected = alternatives.choices[1], other = alternatives.choices[0];
        check (selected.revision != other.revision && selected.previewOgg.existsAsFile()
               && std::abs (selected.durationSeconds - 0.6) < 0.00001 && selected.channels == 2,
               "Recovery choices lost distinct identity or actual PCM format/duration");
        const auto receiptFile = folder.getChildFile (".audio").getChildFile (selected.revision).getChildFile ("revision.json");
        const auto originalReceipt = receiptFile.loadFileAsString();
        const auto otherReceipt = other.previewOgg.getSiblingFile ("revision.json").loadFileAsString();
        auto rejected = selected; rejected.revision = juce::Uuid().toString();
        check (LibraryStore::recoverEntryRevision (folder, rejected, alternatives.selectionToken).result.failed() && ! manifest.exists(),
               "An arbitrary recovery revision was accepted");
        check (LibraryStore::recoverEntryRevision (folder, selected, alternatives.selectionToken, false, [] { return true; }).result.failed()
               && ! manifest.exists(), "Cancelled explicit recovery published a choice");
        {
            juce::FileOutputStream output (manifest, 0);
            check (output.openedOk() && output.setPosition (0) && output.truncate().wasOk(), "Cannot create present-empty recovery manifest");
            output.flush();
        }
        const auto emptyReview = LibraryStore::recoverEntry (folder);
        check (emptyReview.choices.size() == 2 && emptyReview.selectionToken != alternatives.selectionToken
               && LibraryStore::recoverEntryRevision (folder, selected, alternatives.selectionToken).result.failed()
               && manifest.existsAsFile() && manifest.getSize() == 0, "Missing/present-empty manifests shared a stale review token");
        check (manifest.replaceWithText ("{"), "Cannot seed held recovery manifest");
        {
            const auto handle = CreateFileW (manifest.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                             0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            check (handle != INVALID_HANDLE_VALUE, "Cannot hold recovery manifest exclusively");
            struct Close { HANDLE handle; ~Close() { CloseHandle (handle); } } close { handle };
            const auto failed = LibraryStore::recoverEntry (folder);
            check (failed.result.failed() && failed.choices.isEmpty()
                   && failed.result.getErrorMessage().contains ("Cannot read recovery manifest"),
                   "Unreadable recovery manifest was treated as absent and offered overwrite choices");
        }
        check (manifest.loadFileAsString() == "{" && manifest.deleteFile(), "Failed manifest read changed the original");
        auto changedReceipt = LibraryStore::readJson (receiptFile);
        changedReceipt.getDynamicObject()->getProperty ("metadata").getDynamicObject()->setProperty ("key", "changed after review");
        check (LibraryStore::writeJsonAtomically (receiptFile, changedReceipt).wasOk(), "Cannot change selected receipt");
        check (LibraryStore::recoverEntryRevision (folder, selected, alternatives.selectionToken).result.failed() && ! manifest.exists(),
               "A changed receipt bypassed the frozen recovery fingerprint");
        check (receiptFile.replaceWithText (originalReceipt), "Cannot restore selected receipt");
        juce::var pointer = object(); pointer.getDynamicObject()->setProperty ("audioRevision", selected.revision);
        const auto selectedPcm = LibraryStore::audioFile (folder, pointer, true);
        juce::MemoryBlock pcmBytes; check (selectedPcm.loadFileAsData (pcmBytes), "Cannot preserve selected PCM fixture");
        corruptPcm (selectedPcm);
        check (LibraryStore::recoverEntryRevision (folder, selected, alternatives.selectionToken).result.failed() && ! manifest.exists(),
               "Corrupt selected audio silently fell back to another candidate");
        check (selectedPcm.replaceWithData (pcmBytes.getData(), pcmBytes.getSize()), "Cannot restore selected PCM");
        auto publication = LibraryStore::readJson (other.previewOgg.getSiblingFile ("revision.json")).getProperty ("metadata", juce::var{});
        check (LibraryStore::writeJsonAtomically (manifest, publication).wasOk(), "Cannot publish concurrent valid recovery fixture");
        const auto publicationText = manifest.loadFileAsString();
        check (LibraryStore::recoverEntryRevision (folder, selected, alternatives.selectionToken).result.failed()
               && manifest.loadFileAsString() == publicationText && LibraryStore::recoverEntry (folder).revision == other.revision,
               "Stale explicit recovery replaced a concurrently valid publication");
        check (manifest.deleteFile(), "Cannot reset stale publication fixture");
        {
            LibraryStore::OwnerLease live;
            check (LibraryStore::claimPending (lib, folder, live).wasOk(), "Cannot reclaim ambiguous pending fixture");
            check (LibraryStore::recoverEntryRevision (folder, selected, alternatives.selectionToken, true).result.failed() && ! manifest.exists(),
                   "Explicit recovery stole a live pending capture despite its confirmation flag");
        }
        auto owner = LibraryStore::readJson (folder.getChildFile (".owner.json"));
        owner.getDynamicObject()->setProperty ("machine", "unverifiable-recovery-fixture");
        check (LibraryStore::writeJsonAtomically (folder.getChildFile (".owner.json"), owner).wasOk(), "Cannot make pending owner unverifiable");
        check (LibraryStore::recoverEntryRevision (folder, selected, alternatives.selectionToken).result.failed() && ! manifest.exists(),
               "Unknown owner did not require explicit recovery confirmation");
        auto organised = object(); organised.getDynamicObject()->setProperty ("projectId", folder.getFileName());
        organised.getDynamicObject()->setProperty ("favorite", true); organised.getDynamicObject()->setProperty ("futureField", "keep me");
        organised.getDynamicObject()->setProperty ("tags", juce::Array<juce::var> { juce::var ("reviewed") });
        organised.getDynamicObject()->setProperty ("dataRevision", 4);
        check (LibraryStore::writeJsonAtomically (manifest, organised).wasOk(), "Cannot add organisation to damaged manifest");
        const auto reviewed = LibraryStore::recoverEntry (folder);
        check (reviewed.choices.size() == 2 && reviewed.selectionToken != alternatives.selectionToken,
               "Updated organisation did not produce a new recovery review token");
        const auto chosen = LibraryStore::recoverEntryRevision (folder, selected, reviewed.selectionToken, true);
        const auto saved = metadata (folder);
        check (chosen.result.wasOk() && chosen.repaired && chosen.revision == selected.revision
               && saved.getProperty ("audioRecovery", "").toString() == "explicit-revision"
               && (bool) saved.getProperty ("favorite", false) && saved.getProperty ("futureField", "").toString() == "keep me"
               && (int) saved.getProperty ("dataRevision", 0) == 5
               && saved.getProperty ("key", "").toString() == juce::JSON::parse (originalReceipt).getProperty ("metadata", juce::var{}).getProperty ("key", "").toString(),
               "Explicit choice lost current organisation, selected analysis or revision progression");
        check (other.previewOgg.existsAsFile() && other.previewOgg.getSiblingFile ("revision.json").loadFileAsString() == otherReceipt,
               "Selecting one revision discarded another complete orphan");
        check (LibraryStore::recoverPending (lib, folder).failed() && LibraryStore::recoverPending (lib, folder, true).wasOk(),
               "Unknown-owner approval did not remain explicit during keep");
        const auto permanent = lib.getChildFile (folder.getFileName());
        check (LibraryStore::recoverEntry (permanent).revision == selected.revision && ! folder.exists()
               && permanent.getChildFile (".audio").getChildFile (other.revision).isDirectory(),
               "Keeping the chosen orphan lost its exact revision or unchosen files");
        std::cout << "PASS: two actual receipt-stage writer exits expose distinct choices; explicit selection rejects cancellation, stale publication/receipt, corrupt audio, active/unknown owners; selected analysis/organisation survive and other audio is retained\n";
    }
    for (int stage = -2; stage <= 4; ++stage)
    {
        const auto lib = library (root, "crash-" + juce::String (stage)), folder = entry (lib);
        const auto original = publish (folder, 0.2f);
        check (LibraryStore::updateJson (folder.getChildFile ("metadata.json"), [] (juce::DynamicObject& o)
        { o.setProperty ("favorite", true); o.setProperty ("customField", "preserved"); return juce::Result::ok(); }).wasOk(), "Cannot edit crash fixture");
        const auto before = folder.getChildFile ("metadata.json").loadFileAsString();
        crash (folder, juce::String (stage));
        const auto candidate = folder.getChildFile ("candidate-id.txt").loadFileAsString();
        const auto proof = LibraryStore::readJson (folder.getChildFile (".audio").getChildFile (candidate).getChildFile ("owner.json"));
        check (LibraryStore::ownerState (proof) == LibraryStore::OwnerState::Orphaned, "OS did not release crashed owner's token");
        const auto recovered = LibraryStore::recoverEntry (folder);
        check (recovered.result.wasOk() && ! recovered.repaired, "Recovery replaced a valid published manifest");
        check (recovered.revision == (stage >= 3 ? candidate : original), "Crash crossed the wrong audio commit point");
        if (stage < 3) check (folder.getChildFile ("metadata.json").loadFileAsString() == before, "Crash changed old manifest before publication");
        check (std::abs (sample (LibraryStore::audioFile (folder, true)) - (stage >= 3 ? 0.6f : 0.2f)) < 0.00001f, "Crash lost committed audio");
        check ((bool) metadata (folder).getProperty ("favorite", false) && metadata (folder).getProperty ("customField", "").toString() == "preserved",
               "Crash lost organisation or unknown metadata");
    }
    std::cout << "PASS: seven abrupt process exits across PCM, OGG, receipt, backup and manifest commit points\n";
    for (int stage = 0; stage <= 2; ++stage)
    {
        const auto lib = library (root, "owned-promotion-" + juce::String (stage)), folder = entry (lib.getChildFile (".pending"));
        crash (folder, "owned-promote:" + juce::String (stage));
        const auto target = lib.getChildFile (folder.getFileName());
        if (stage == 0) check (LibraryStore::recoverPending (lib, folder).wasOk(), "Writer-marked keep was not recoverable after death");
        else check (LibraryStore::finishPendingRecovery (lib, target).wasOk(), "Writer-moved keep was not resumable after death");
        const auto kept = metadata (target);
        check (! (bool) kept.getProperty ("pending", true)
            && kept.getProperty ("projectAssociation", "").toString() == "manual-recording"
            && kept.getProperty ("projectAssociationConfidence", "").toString() == "explicit"
            && std::abs (sample (LibraryStore::audioFile (target, true)) - 0.6f) < 0.00001f,
            "Interrupted explicit keep lost audio or manual association");
    }
    std::cout << "PASS: three actual writer exits across explicit keep marking, rename and completion preserve manual association\n";
   #endif
    {
        const auto lib = library (root, "integrity"), folder = entry (lib);
        const auto first = publish (folder, 0.2f), second = publish (folder, 0.6f);
        check (LibraryStore::updateJson (folder.getChildFile ("metadata.json"), [] (juce::DynamicObject& o)
        {
            o.setProperty ("favorite", true); o.setProperty ("tags", juce::Array<juce::var> { "latest" });
            o.setProperty ("projectPath", "X:/latest-association.flp"); o.setProperty ("customField", "latest"); return juce::Result::ok();
        }).wasOk(), "Cannot edit integrity fixture");
        corruptPcm (LibraryStore::audioFile (folder, true));
        const auto before = folder.getChildFile ("metadata.json").loadFileAsString();
        check (LibraryStore::recoverEntry (folder, [] { return true; }).result.failed()
            && folder.getChildFile ("metadata.json").loadFileAsString() == before, "Cancelled recovery changed the manifest");
        const auto repaired = LibraryStore::recoverEntry (folder);
        check (repaired.result.wasOk() && repaired.repaired && repaired.revision == first, "Readable PCM corruption bypassed receipt checksum");
        auto m = metadata (folder);
        check ((int) m.getProperty ("bpm", 0) == 90 && m.getProperty ("key", "").toString() == "A minor", "Audio analysis did not follow recovered revision");
        check ((bool) m.getProperty ("favorite", false) && m.getProperty ("projectPath", "").toString() == "X:/latest-association.flp"
            && m.getProperty ("tags", {}).getArray()->getReference (0).toString() == "latest" && m.getProperty ("customField", "").toString() == "latest",
               "Recovery rolled back latest organisation/identity");
        check (std::abs (sample (LibraryStore::audioFile (folder, true)) - 0.2f) < 0.00001f, "Fallback PCM changed");
        // No valid previous pointer or backup: scan committed receipts, even
        // when their expectedRevision differs from the damaged current one.
        const auto third = publish (folder, 0.6f);
        corruptPcm (LibraryStore::audioFile (folder, true));
        check (LibraryStore::updateJson (folder.getChildFile ("metadata.json"), [] (juce::DynamicObject& o)
        { o.setProperty ("previousAudioRevision", ""); return juce::Result::ok(); }).wasOk(), "Cannot remove fallback pointer");
        check (folder.getChildFile (".metadata-backup.json").replaceWithText ("{"), "Cannot damage backup");
        check (LibraryStore::recoverEntry (folder).revision == first, "Committed history fallback rejected an older valid receipt");
        check (folder.getChildFile ("metadata.json").replaceWithText ("{"), "Cannot damage manifest");
        check (LibraryStore::recoverEntry (folder).result.wasOk() && metadata (folder).getProperty ("audioRevision", "").toString() == first,
               "Missing valid manifest/backup did not recover from committed receipt");
        check (LibraryStore::deleteEntry (lib, folder).wasOk(), "Cannot delete recovery fixture");
        check (LibraryStore::recoverEntry (folder).result.failed(), "Recovery resurrected a tombstoned entry");
        juce::ignoreUnused (second, third);
    }
    std::cout << "PASS: checksum corruption, cancelled repair, history fallback, analysis/organisation consistency and tombstones\n";
   #if JUCE_WINDOWS
    {
        const auto lib = library (root, "first-pending"), pending = lib.getChildFile (".pending"), folder = entry (pending);
        crash (folder, "1"); // Receipt persisted; first manifest never existed.
        check (! folder.getChildFile ("metadata.json").existsAsFile(), "Receipt-stage child published first manifest");
        auto entries = ProjectScanner::scan (lib, {});
        check (entries.size() == 1 && entries[0].pending && entries[0].pendingOwner == LibraryStore::OwnerState::Orphaned,
               "Index did not expose complete orphaned pending receipt");
        check (LibraryStore::recoverPending (lib, folder).wasOk(), "Cannot explicitly keep orphaned pending recording");
        entries = ProjectScanner::scan (lib, {});
        check (entries.size() == 1 && ! entries[0].pending && entries[0].metadataFile.getParentDirectory() == lib.getChildFile (folder.getFileName()),
               "Recovered pending entry was duplicated or not permanent");
        const auto incomplete = entry (pending); crash (incomplete, "-2");
        check (LibraryStore::recoverEntry (incomplete).result.failed() && incomplete.isDirectory(), "Incomplete PCM-only capture was invented or discarded");
        entries = ProjectScanner::scan (lib, {});
        bool visible = false;
        for (const auto& item : entries) if (item.metadataFile.getParentDirectory() == incomplete)
            visible = item.pending && item.storageError.isNotEmpty() && item.previewOgg == juce::File{};
        check (visible, "Incomplete orphan was hidden instead of exposing repair/discard actions");
    }
    for (int stage = 0; stage <= 2; ++stage)
    {
        const auto lib = library (root, "promotion-" + juce::String (stage)), folder = entry (lib.getChildFile (".pending"));
        crash (folder, "4"); crash (folder, "promote:" + juce::String (stage));
        const auto target = lib.getChildFile (folder.getFileName());
        if (stage == 0) check (LibraryStore::recoverPending (lib, folder).wasOk(), "Marked pending promotion was not retryable");
        if (stage == 1)
        {
            auto proof = LibraryStore::readJson (target.getChildFile (".owner.json"));
            proof.getDynamicObject()->setProperty ("machine", "different-session-fixture");
            check (LibraryStore::writeJsonAtomically (target.getChildFile (".owner.json"), proof).wasOk(), "Cannot simulate changed ownership visibility");
            check (LibraryStore::pendingOwnerState (target, metadata (target)) == LibraryStore::OwnerState::Unknown,
                   "Promotion fixture did not make the old owner unqueryable");
        }
        auto entries = ProjectScanner::scan (lib, {});
        check (entries.size() == 1 && ! entries[0].pending && target.isDirectory() && ! folder.exists(), "Interrupted promotion did not resume exactly once");
        check (LibraryStore::recoverEntry (target).result.wasOk(), "Promotion lost verified audio");
    }
    {
        const auto lib = library (root, "legacy-promotion"), folder = entry (lib.getChildFile (".pending"));
        publish (folder, 0.2f); crash (folder, "promote:1");
        const auto entries = ProjectScanner::scan (lib, {});
        check (entries.size() == 1 && ! entries[0].pending, "Explicit unknown-owner approval did not survive interrupted promotion");
    }
    std::cout << "PASS: first-capture orphan discovery, incomplete-file preservation and three interrupted promotion stages\n";
   #endif
    {
        const auto lib = library (root, "ownership"), folder = entry (lib.getChildFile (".pending"));
        LibraryStore::OwnerLease owner, other;
        check (LibraryStore::claimPending (lib, folder, owner).wasOk(), "Cannot claim pending fixture");
        publish (folder, 0.2f);
        check (LibraryStore::recoverPending (lib, folder, true).failed() && LibraryStore::deleteEntry (lib, folder).failed()
            && LibraryStore::claimPending (lib, folder, other).failed() && LibraryStore::discardOwnedPending (lib, folder, other).failed(),
               "Another session stole, promoted or deleted a live pending capture");
        check (LibraryStore::discardOwnedPending (lib, folder, owner).wasOk(), "Owner could not clean its normal pending capture");
        check (! LibraryStore::isDeleted (folder), "Normal pending cleanup prevented restoring its saved identity");
        const auto legacy = entry (lib.getChildFile (".pending")); publish (legacy, 0.2f);
        check (LibraryStore::recoverPending (lib, legacy).failed(), "Unverified legacy owner bypassed explicit confirmation");
        const auto target = lib.getChildFile (legacy.getFileName()); check (target.createDirectory().wasOk(), "Cannot create collision fixture");
        check (target.getChildFile ("keep.txt").replaceWithText ("preserved"), "Cannot write collision marker");
        check (LibraryStore::recoverPending (lib, legacy, true).failed() && target.getChildFile ("keep.txt").loadFileAsString() == "preserved",
               "Recovery overwrote an existing permanent entry");
        check (target.deleteRecursively(), "Cannot remove fixture collision");
        check (LibraryStore::recoverPending (lib, legacy, true).wasOk(), "Explicit legacy pending recovery failed");
        check (LibraryStore::claimPending (lib, legacy, other).failed() && ! legacy.exists(),
               "Restored pending identity duplicated a kept permanent entry");
        const auto unapproved = entry (lib); publish (unapproved, 0.2f);
        check (LibraryStore::updateJson (unapproved.getChildFile ("metadata.json"), [] (juce::DynamicObject& o)
        { o.setProperty ("pending", true); o.setProperty ("pendingRecovery", "promoting"); return juce::Result::ok(); }).wasOk(), "Cannot write unapproved marker");
        check (LibraryStore::finishPendingRecovery (lib, unapproved).failed(), "Promotion marker invented unknown-owner approval");
    }
    {
        const auto lib = library (root, "readers"), folder = entry (lib);
        const auto first = publish (folder, 0.2f), second = publish (folder, 0.6f);
        const auto oldest = folder.getChildFile (".audio").getChildFile (first);
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> openReader (formats.createReaderFor (oldest.getChildFile ("preview.ogg")));
        check (openReader != nullptr, "Cannot hold obsolete reader");
        const auto third = publish (folder, 0.2f);
        LibraryStore::AudioRevision active (folder); check (active.prepare().wasOk(), "Cannot prepare live aged revision");
        check (ageDirectory (active.directory), "Cannot age live revision");
        LibraryStore::pruneAudioRevisions (folder);
        check (active.directory.isDirectory() && folder.getChildFile (".audio").getChildFile (second).isDirectory()
            && folder.getChildFile (".audio").getChildFile (third).isDirectory(), "Pruning removed active/current/previous audio");
        juce::AudioBuffer<float> samples (2, 1);
        check (openReader->read (&samples, 0, 1, 100, true, true), "Pruning invalidated an open immutable reader");
        openReader.reset(); LibraryStore::pruneAudioRevisions (folder);
        check (! oldest.exists(), "Pruning failed to reclaim an orphaned obsolete revision");
    }
    std::cout << "PASS: live/unknown owner protection, explicit keep, target collisions, normal cleanup and open-reader pruning\n";
    {
        const auto lib = library (root, "legacy-mono"), folder = entry (lib);
        check (LibraryStore::ensureDirectory (folder).wasOk(), "Cannot prepare legacy mono fixture");
        audio (folder.getChildFile ("preview.wav"), true, 0.2f, 2);
        audio (folder.getChildFile ("preview.ogg"), false, 0.2f, 1);
        check (LibraryStore::writeJsonAtomically (folder.getChildFile ("metadata.json"), patch (folder, 0.2f)).wasOk(), "Cannot create legacy mono manifest");
        const auto recovered = LibraryStore::recoverEntry (folder);
        check (recovered.result.wasOk() && ! recovered.repaired, "Historical mono OGG/stereo WAV was rejected as corrupt");
        LibraryStore::AudioRevision mismatch (folder); check (mismatch.prepare().wasOk(), "Cannot prepare mismatched new revision");
        audio (mismatch.pcm, true, 0.2f, 2); audio (mismatch.ogg, false, 0.2f, 1);
        check (LibraryStore::commitAudio (folder, mismatch, patch (folder, 0.2f), {}).failed(), "Legacy exception admitted mismatched new audio");
        std::cout << "PASS: legacy mono OGG/stereo PCM compatibility; new revisions still require matching channels\n";
    }
    {
        const auto lib = library (root, "observer-unwind"), folder = entry (lib);
        const auto previous = publish (folder, 0.2f);
        juce::String committed;
        bool threw = false;
        {
            LibraryStore::AudioRevision revision (folder); committed = revision.id;
            check (revision.prepare().wasOk(), "Cannot prepare observer-unwind fixture");
            audio (revision.pcm, true, 0.6f); audio (revision.ogg, false, 0.6f);
            try
            {
                LibraryStore::commitAudio (folder, revision, patch (folder, 0.6f), previous, {}, [] (LibraryStore::CommitStage stage)
                { if (stage == LibraryStore::CommitStage::ManifestWritten) throw std::runtime_error ("simulated post-publication interruption"); });
            }
            catch (const std::runtime_error&) { threw = true; }
        }
        check (threw && LibraryStore::recoverEntry (folder).revision == committed
            && std::abs (sample (LibraryStore::audioFile (folder, true)) - 0.6f) < 0.00001f,
               "Post-publication unwinding discarded committed audio");
        std::cout << "PASS: post-publication exception cannot discard the committed revision\n";
    }
}
