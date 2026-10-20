#include "StorageTests.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/ProjectIdentity.h"
#include "../App/Source/ProjectScanner.h"
#include <stdexcept>
#include <iostream>
#include <thread>
#include <future>

namespace
{
void check (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
juce::var object() { return juce::var (new juce::DynamicObject()); }
void writeAudio (const juce::File& file, bool pcm)
{
    juce::AudioBuffer<float> data (2, 8000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < data.getNumSamples(); ++i) data.setSample (channel, i, 0.2f);
    juce::WavAudioFormat wav;
    juce::OggVorbisAudioFormat ogg;
    auto& format = pcm ? static_cast<juce::AudioFormat&> (wav) : static_cast<juce::AudioFormat&> (ogg);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (8000)
        .withNumChannels (2).withBitsPerSample (pcm ? 32 : 16).withQualityOptionIndex (4)
        .withSampleFormat (pcm ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                              : juce::AudioFormatWriterOptions::SampleFormat::automatic));
    check (writer != nullptr && writer->writeFromAudioSampleBuffer (data, 0, data.getNumSamples()), "Cannot write storage audio fixture");
}
}

int storageWriter (const juce::File& metadataFile)
{
    if (! metadataFile.isAChildOf (juce::File::getCurrentWorkingDirectory()) || metadataFile.getFileName() != "metadata.json") return 2;
    for (int i = 0; i < 20; ++i)
    {
        auto result = LibraryStore::updateJson (metadataFile, [] (juce::DynamicObject& data)
        {
            data.setProperty ("concurrentCounter", (int) data.getProperty ("concurrentCounter") + 1);
            return juce::Result::ok();
        });
        if (result.failed()) return 3;
    }
    return 0;
}

void testStorage (const juce::File& root)
{
    {
        // Distinct entries still share several initially absent parents. No
        // entry lock can serialize those mkdir operations across writers.
        for (int round = 0; round < 8; ++round)
        {
            struct Writers
            {
                juce::WaitableEvent release { true };
                std::atomic<int> ready { 0 };
                std::vector<std::thread> threads;
                ~Writers() { release.signal(); join(); }
                void join() { for (auto& thread : threads) if (thread.joinable()) thread.join(); }
            };
            const auto common = root.getChildFile ("directory-races").getChildFile (juce::String (round)).getChildFile ("shared/.pending");
            std::vector<juce::Result> results (8, juce::Result::ok());
            Writers writers; // Joins before captured paths/results are destroyed.
            for (int i = 0; i < 8; ++i)
                writers.threads.emplace_back ([&, i]
                {
                    writers.ready.fetch_add (1); writers.release.wait (10000);
                    results[(size_t) i] = LibraryStore::ensureDirectory (common.getChildFile (juce::String (i) + "/.audio/revision"));
                });
            const auto until = juce::Time::getMillisecondCounter() + 5000;
            while (writers.ready.load() != 8)
            { check (juce::Time::getMillisecondCounter() < until, "Concurrent directory fixture did not start its writers"); juce::Thread::sleep (1); }
            writers.release.signal(); writers.join();
            for (int i = 0; i < 8; ++i)
            {
                check (results[(size_t) i].wasOk(), "Concurrent creators rejected an existing shared directory");
                const auto directory = common.getChildFile (juce::String (i) + "/.audio/revision");
                check (directory.isDirectory() && LibraryStore::ensureDirectory (directory).wasOk(), "Directory creation was not idempotent");
            }
        }
        const auto occupied = root.getChildFile ("directory-races/not-a-directory");
        check (occupied.replaceWithText ("preserve this file"), "Cannot create occupied-path fixture");
        check (LibraryStore::ensureDirectory (occupied.getChildFile ("child")).failed()
               && occupied.loadFileAsString() == "preserve this file", "Directory recovery ignored a genuine file/path collision");
        std::cout << "PASS: 64 concurrent directory creators, shared absent parents and genuine file collisions\n";
    }
    {
        const auto project = root.getChildFile ("HashCancellation.flp");
        const auto contents = juce::String::repeatedString ("bounded fingerprint fixture\n", 50000);
        check (project.replaceWithText (contents), "Cannot write hash fixture");
        int checks = 0;
        check (ProjectIdentity::fingerprint (project, [&] { return ++checks > 10; }).isEmpty(),
               "Cancellation published a partial project hash");
        check (checks > 10, "Fingerprint never checked cancellation between reads");
        check (ProjectIdentity::fingerprint (project) == juce::SHA256 (project).toHexString(),
               "Checked fingerprint changed the SHA256 of a complete file");
        check (ProjectIdentity::fingerprint (root.getChildFile ("MissingHash.flp")).isEmpty(),
               "An unreadable project published a valid-looking fingerprint");
        // Cancellation after a short read must still discard its digest even
        // if the native stream or JUCE's read-ahead has already reached EOF.
        const auto small = root.getChildFile ("PrefetchedCancellation.flp");
        check (small.replaceWithText (juce::String::repeatedString ("x", 1024)), "Cannot write small hash fixture");
        checks = 0;
        check (ProjectIdentity::fingerprint (small, [&] { return ++checks >= 3; }).isEmpty(),
               "Read-ahead EOF hid an incomplete logical hash");
        const auto empty = root.getChildFile ("EmptyHash.flp");
        {
            juce::FileOutputStream writer (empty);
            check (writer.openedOk() && writer.setPosition (0) && writer.truncate().wasOk(), "Cannot create empty hash fixture");
        }
        const auto emptyHash = ProjectIdentity::fingerprint (empty);
        std::cout << "HASH_EMPTY bytes=" << empty.getSize() << " digest=" << emptyHash << std::endl;
        check (emptyHash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
               "SHA256 of an empty file changed");
        for (const auto bytes : { 1, 55, 56, 63, 64, 65, 1024, 65535, 65536, 65537 })
        {
            const auto boundary = root.getChildFile ("HashBoundary-" + juce::String (bytes) + ".flp");
            std::vector<unsigned char> data ((size_t) bytes);
            for (size_t i = 0; i < data.size(); ++i) data[i] = (unsigned char) ((i * 37 + 31) & 255);
            check (boundary.replaceWithData (data.data(), data.size()), "Cannot create hash boundary fixture");
            check (ProjectIdentity::fingerprint (boundary) == juce::SHA256 (data.data(), data.size()).toHexString(),
                   "Native file hash differs at a SHA256/read boundary");
        }
        for (const auto grow : { false, true })
        {
            const auto changing = root.getChildFile (grow ? "GrowingHash.flp" : "TruncatedHash.flp");
            std::vector<unsigned char> data (3 * 65536 + 1, 0x5a);
            check (changing.replaceWithData (data.data(), data.size()), "Cannot create changing hash fixture");
            checks = 0;
            const auto changedHash = ProjectIdentity::fingerprint (changing, [&]
            {
                if (++checks == 3)
                {
                    juce::FileOutputStream writer (changing);
                    check (writer.openedOk(), "Cannot mutate an open hash input");
                    if (grow) check (writer.setPosition ((juce::int64) data.size()) && writer.writeByte ('x'), "Cannot grow hash input");
                    else check (writer.setPosition (0) && writer.truncate().wasOk(), "Cannot truncate hash input");
                    writer.flush(); check (writer.getStatus().wasOk(), "Cannot flush changed hash input");
                }
                return false;
            });
            check (checks >= 3 && changedHash.isEmpty(), "A growing/truncated input published a complete-looking digest");
        }
        const auto large = root.getChildFile ("BufferedHash.flp");
        {
            juce::FileOutputStream output (large); check (output.openedOk(), "Cannot create large hash fixture");
            std::array<char, 65536> block {};
            for (size_t i = 0; i < block.size(); ++i) block[i] = (char) ((i * 37 + 31) & 255);
            for (int i = 0; i < 512; ++i) check (output.write (block.data(), block.size()), "Cannot write large hash fixture");
            output.flush(); check (output.getStatus().wasOk(), "Cannot flush large hash fixture");
        }
        auto begun = juce::Time::getMillisecondCounterHiRes();
        const auto reference = juce::SHA256 (large).toHexString();
        const auto directMs = juce::Time::getMillisecondCounterHiRes() - begun;
        begun = juce::Time::getMillisecondCounterHiRes();
        const auto buffered = ProjectIdentity::fingerprint (large);
        const auto bufferedMs = juce::Time::getMillisecondCounterHiRes() - begun;
        check (reference == buffered && buffered.length() == 64, "Buffered SHA256 differs on a 32 MiB file");
        std::vector<std::future<juce::String>> concurrent;
        for (int i = 0; i < 8; ++i)
            concurrent.push_back (std::async (std::launch::async, [large] { return ProjectIdentity::fingerprint (large); }));
        for (auto& result : concurrent) check (result.get() == reference, "Concurrent fingerprints shared or corrupted hash state");
        std::cout << "HASH bytes=" << large.getSize() << " JUCE_directMs=" << directMs << " bufferedMs=" << bufferedMs
                  << " (same file, OS cache available; reference measured first)\n";
        std::cout << "PASS: cancelled/unreadable/changed fingerprints are discarded; empty and SHA256/read boundaries, 32 MiB and eight concurrent hashes agree\n";
    }
    const auto library = root.getChildFile ("storage-library"); library.createDirectory();
    const auto folder = library.getChildFile (juce::Uuid().toString()); folder.createDirectory();
    const auto metadataFile = folder.getChildFile ("metadata.json");
    const auto project = root.getChildFile ("Storage.flp"); project.replaceWithText ("original project");
    auto metadata = object();
    auto* data = metadata.getDynamicObject();
    data->setProperty ("projectId", folder.getFileName());
    data->setProperty ("projectName", "Storage");
    data->setProperty ("projectPath", project.getFullPathName());
    data->setProperty ("projectFileId", ProjectIdentity::fileId (project));
    data->setProperty ("projectFingerprint", ProjectIdentity::fingerprint (project));
    data->setProperty ("daw", "FL Studio");
    data->setProperty ("futureField", "preserved");
    check (LibraryStore::patchJson (metadataFile, metadata, true).wasOk(), "Initial metadata commit failed");
    LibraryStore::AudioRevision first (folder);
    check (first.prepare().wasOk(), "Audio revision prepare failed");
    writeAudio (first.ogg, false); writeAudio (first.pcm, true);
    check (LibraryStore::commitAudio (folder, first, object(), {}).wasOk(), "First audio commit failed");
    auto entries = ProjectScanner::scan (library, { root });
    check (entries.size() == 1 && entries[0].previewOgg == first.ogg, "Scanner did not follow committed revision");
    auto older = entries[0], favoriteEdit = entries[0];
    favoriteEdit.favorite = true; check (favoriteEdit.save().wasOk(), "Favorite edit failed");
    older.tags.add ("new-tag"); check (older.save().wasOk(), "Tag edit failed");
    auto saved = LibraryStore::readJson (metadataFile);
    check ((bool) saved.getProperty ("favorite", false), "Stale tag edit erased favorite");
    check (saved.getProperty ("futureField", "").toString() == "preserved", "Edit erased unknown field");
    check (saved.getProperty ("audioRevision", "").toString() == first.id, "Edit changed audio revision");
    auto parallel = ProjectScanner::scan (library, { root });
    auto tagsA = parallel[0], tagsB = parallel[0];
    tagsA.tags.add ("A"); tagsB.tags.add ("B");
    check (tagsA.save().wasOk() && tagsB.save().wasOk(), "Concurrent tag edits failed");
    auto allTags = LibraryStore::readJson (metadataFile).getProperty ("tags", juce::var{});
    check (allTags.isArray() && allTags.getArray()->size() == 3, "Tag deltas lost another snapshot's addition");

    const auto before = metadataFile.loadFileAsString();
    LibraryStore::AudioRevision incomplete (folder); incomplete.prepare(); writeAudio (incomplete.ogg, false);
    check (LibraryStore::commitAudio (folder, incomplete, object(), first.id).failed(), "Incomplete revision was published");
    check (metadataFile.loadFileAsString() == before, "Failed audio commit changed manifest");
    incomplete.discard();
    const auto manualProject = root.getChildFile ("ManualAssociation.flp");
    manualProject.replaceWithText ("manually assigned project");
    auto assigned = object(); assigned.getDynamicObject()->setProperty ("projectPath", manualProject.getFullPathName());
    check (LibraryStore::patchJson (metadataFile, assigned).wasOk(), "Manual association fixture failed");
    LibraryStore::AudioRevision second (folder); second.prepare(); writeAudio (second.ogg, false); writeAudio (second.pcm, true);
    auto patch = object(); patch.getDynamicObject()->setProperty ("bpm", 123.0);
    patch.getDynamicObject()->setProperty ("projectPath", project.getFullPathName());
    check (LibraryStore::commitAudio (folder, second, patch, first.id).wasOk(), "Second audio commit failed");
    check (LibraryStore::commitAudio (folder, first, patch, first.id).failed(), "Stale audio merge replaced a newer revision");
    check (LibraryStore::audioFile (folder, false) == second.ogg, "Manifest does not retain newer revision");
    check ((bool) LibraryStore::readJson (metadataFile).getProperty ("favorite", false), "Audio publication lost organization");
    check (LibraryStore::readJson (metadataFile).getProperty ("projectPath", "").toString() == manualProject.getFullPathName(),
           "Audio publication rolled back an intervening path edit");
    juce::File abandonedDirectory;
    {
        LibraryStore::AudioRevision abandoned (folder); abandoned.prepare();
        abandonedDirectory = abandoned.directory;
        writeAudio (abandoned.ogg, false);
    }
    check (! abandonedDirectory.exists(), "Failed/unpublished job leaked its revision");
   #if JUCE_WINDOWS
    {
        const auto prior = metadataFile.loadFileAsString();
        LibraryStore::AudioRevision blocked (folder); blocked.prepare();
        writeAudio (blocked.ogg, false); writeAudio (blocked.pcm, true);
        struct RestoreAccess { juce::File file; ~RestoreAccess() { file.setReadOnly (false); } } restore { metadataFile };
        check (metadataFile.setReadOnly (true), "Cannot create read-only commit fixture");
        check (LibraryStore::commitAudio (folder, blocked, patch, second.id).failed(), "Read-only manifest commit was ignored");
        check (metadataFile.loadFileAsString() == prior, "Failed manifest replacement changed the prior revision");
    }
   #endif
    check (LibraryStore::updateJson (metadataFile, [] (juce::DynamicObject& value)
    {
        value.setProperty ("favorite", false);
        return juce::Result::fail ("simulated transaction failure");
    }).failed(), "Mutation failure was ignored");
    check ((bool) LibraryStore::readJson (metadataFile).getProperty ("favorite", false), "Failed mutation leaked to disk");

    juce::ChildProcess writerA, writerB;
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    check (writerA.start (juce::StringArray { executable, "--storage-writer", metadataFile.getFullPathName() })
        && writerB.start (juce::StringArray { executable, "--storage-writer", metadataFile.getFullPathName() }), "Cannot start storage writers");
    check (writerA.waitForProcessToFinish (15000) && writerB.waitForProcessToFinish (15000), "Storage writers timed out");
    check (writerA.getExitCode() == 0 && writerB.getExitCode() == 0, "Storage writer failed");
    check ((int) LibraryStore::readJson (metadataFile).getProperty ("concurrentCounter", 0) == 40, "Interprocess updates were lost");

    const auto oldId = ProjectIdentity::fileId (project), oldHash = ProjectIdentity::fingerprint (project);
    const auto moved = root.getChildFile ("StorageMoved.flp"); check (project.moveFileTo (moved), "Cannot move identity fixture");
    project.replaceWithText ("unrelated replacement");
    check (ProjectIdentity::resolve ("Storage", "FL Studio", { root }, project.getFullPathName(), oldId, oldHash) == moved.getFullPathName(),
           "Existing replacement path overrode original identity");
    auto atomicFile = root.getChildFile ("Atomic.flp"); atomicFile.replaceWithText ("same saved content");
    const auto atomicId = ProjectIdentity::fileId (atomicFile), atomicHash = ProjectIdentity::fingerprint (atomicFile);
    auto replacement = root.getChildFile ("atomic-save.tmp"); replacement.replaceWithText ("same saved content");
    check (replacement.replaceFileIn (atomicFile), "Cannot simulate atomic project save");
    check (ProjectIdentity::resolve ("Atomic", "FL Studio", { root }, atomicFile.getFullPathName(), atomicId, atomicHash) == atomicFile.getFullPathName(),
           "Atomic save with verified content was rejected");

    const auto usage = LibraryStore::measureStorage (library);
    check (usage.pcmBytes >= first.pcm.getSize() + second.pcm.getSize()
        && usage.previewBytes >= first.ogg.getSize() + second.ogg.getSize()
        && usage.bytes > usage.pcmBytes + usage.previewBytes, "Storage measurement omitted hidden revisions/metadata");
    check (LibraryStore::measureStorage (library, [] { return true; }).cancelled, "Storage enumeration ignored cancellation");
    check (LibraryStore::deleteEntry (library, folder).wasOk(), "Entry deletion failed");
    check (LibraryStore::patchJson (metadataFile, metadata, true).failed(), "A writer resurrected a deleted entry");
    check (older.save().failed(), "A stale edit recreated deleted metadata");
    std::cout << "PASS: field deltas, interprocess updates, complete audio commits, fault preservation, deletion tombstone, reused paths, atomic saves\n";
}
