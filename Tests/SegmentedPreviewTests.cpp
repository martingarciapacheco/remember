#include "SegmentedPreviewTests.h"
#include "../Shared/PreviewAudio.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/ProjectIdentity.h"
#include "../Shared/PreviewCompaction.h"
#include "../App/Source/PreviewCompactionActions.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
void require (bool ok, const juce::String& why) { if (! ok) throw std::runtime_error (why.toStdString()); }
juce::File source (const juce::File& root, const juce::String& name, double rate, int seconds, float gain)
{
    const auto file = root.getChildFile (name + ".wav"); juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2)
        .withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    require (writer != nullptr, "Cannot create segmented fixture");
    juce::AudioBuffer<float> block (2, 1024);
    for (int position = 0; position < (int) (rate * seconds);)
    {
        const int n = juce::jmin (1024, (int) (rate * seconds) - position);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i)
            block.setSample (ch, i, gain * (float) (std::sin (juce::MathConstants<double>::twoPi * (440 + ch * 251) * (position + i) / rate)
                + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 5000 * (position + i) / rate)));
        require (writer->writeFromAudioSampleBuffer (block, 0, n), "Cannot write segmented source"); position += n;
    }
    return file;
}
juce::AudioBuffer<float> read (const juce::File& file, double start, int frames)
{
    auto reader = PreviewAudio::open (file); require (reader != nullptr, "Cannot open segmented package");
    juce::AudioBuffer<float> audio (2, frames);
    require (reader->read (&audio, 0, frames, (juce::int64) std::llround (start * reader->sampleRate), true, true), "Cannot seek/read segmented package"); return audio;
}
}
void testSegmentedPreviews (const juce::File& root)
{
    const auto folder = root.getChildFile ("segmented"); require (folder.createDirectory().wasOk(), "Cannot create segmented folder");
    const auto original = source (folder, "original", 48000, 6, 0.3f), fresh = source (folder, "fresh", 48000, 2, 0.2f);
    const auto first = folder.getChildFile ("first.rmp");
    require (PreviewAudio::write (first, original, {}, 48000, 2, 288000, 288000, 4, 1).wasOk(), "Cannot encode initial fragments");
    {
        auto raw = PreviewAudio::open (original); juce::AudioBuffer<float> input (2, 288000);
        require (raw && raw->read (&input, 0, 288000, 0, true, true), "Cannot read source reference");
        const auto decoded = read (first, 0, 288000); double error = 0, energy = 0;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 288000; ++i)
        {
            const auto delta = decoded.getSample (ch, i) - input.getSample (ch, i);
            error += delta * delta; energy += input.getSample (ch, i) * input.getSample (ch, i);
        }
        require (std::sqrt (error / energy) < 0.04, "Fragment timing/decoding damaged source fidelity");
    }
    const auto reference = read (first, 4, 24000);
    auto previous = first;
    for (int i = 0; i < 60; ++i)
    {
        const auto next = folder.getChildFile ("update-" + juce::String (i) + ".rmp");
        const auto prefix = i < 30 ? 96000 : 90000 + (i % 7) * 777;
        const auto result = PreviewAudio::write (next, fresh, previous, 48000, 2, prefix, 288000, 4, 1);
        require (result.wasOk(), "Repeated splice failed: " + result.getErrorMessage());
        const auto tail = read (next, 4, 24000);
        for (int ch = 0; ch < 2; ++ch) for (int sample = 0; sample < tail.getNumSamples(); ++sample)
            require (tail.getSample (ch, sample) == reference.getSample (ch, sample), "Unmodified tail changed after repeated saves");
        require (next.getSize() < first.getSize() * 2, "Repeated saves caused unbounded fragment storage");
        previous = next;
    }
    const auto seam = read (previous, 1.99, 960);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < seam.getNumSamples(); ++i)
        require (std::isfinite (seam.getSample (ch, i)) && std::abs (seam.getSample (ch, i)) < 0.8f, "Invalid or additive seam");
    require (PreviewAudio::verify (previous).wasOk(), "Healthy fragments failed integrity check");
    const auto damaged = folder.getChildFile ("damaged.rmp"); require (previous.copyFileTo (damaged), "Cannot copy damaged fixture");
    {
        juce::MemoryBlock bytes; require (damaged.loadFileAsData (bytes), "Cannot read corruption fixture");
        static_cast<juce::uint8*> (bytes.getData())[bytes.getSize() - 8] ^= 0x40;
        require (damaged.replaceWithData (bytes.getData(), bytes.getSize()), "Cannot corrupt fragment");
    }
    require (PreviewAudio::verify (damaged).failed(), "Corrupt compressed fragment was accepted");
    const auto cancelled = folder.getChildFile ("cancelled.rmp");
    require (PreviewAudio::write (cancelled, fresh, first, 48000, 2, 96000, 288000, 4, 1, [] { return true; }).failed(), "Cancelled fragment write succeeded");
    const auto resampled = folder.getChildFile ("rate-change.rmp");
    require (PreviewAudio::write (resampled, fresh, first, 96000, 1, 192000, 576000, 4, 1).wasOk(), "Cannot preserve fragments across rate/mono change");
    auto reader = PreviewAudio::open (resampled);
    require (reader && reader->sampleRate == 96000 && reader->numChannels == 1 && reader->lengthInSamples == 576000, "Changed format lost its logical timeline");
    read (resampled, 3.7, 1000);
    const auto convertedTail = read (resampled, 4, 48000);
    for (int i = 0; i < 24000; ++i)
        require (std::abs (convertedTail.getSample (0, i * 2)
            - 0.5f * (reference.getSample (0, i) + reference.getSample (1, i))) < 0.003f, "Rate/channel conversion changed the retained musical tail");
    for (const auto file : { first, previous, resampled })
    {
        auto wholeReader = PreviewAudio::open (file), seekReader = PreviewAudio::open (file);
        juce::AudioBuffer<float> whole (2, (int) wholeReader->lengthInSamples), block (2, 777);
        require (wholeReader->read (&whole, 0, whole.getNumSamples(), 0, true, true), "Cannot decode whole preview");
        const auto expectedChannels = (int) wholeReader->numChannels;
        for (int position = 0; position < whole.getNumSamples(); position += 777)
        {
            const auto count = juce::jmin (777, whole.getNumSamples() - position);
            require (seekReader->read (&block, 0, count, position, true, true), "Cannot decode blocked preview");
            for (int ch = 0; ch < expectedChannels; ++ch) for (int i = 0; i < count; ++i)
                require (whole.getSample (ch, position + i) == block.getSample (ch, i), "Decoder output depends on read block size");
        }
        for (const int position : { 144001, 47000, 238999, 95000, 47, 192000 })
        {
            auto freshReader = PreviewAudio::open (file);
            require (freshReader->read (&block, 0, 777, position, true, true), "Cannot seek fresh decoder");
            for (int ch = 0; ch < expectedChannels; ++ch) for (int i = 0; i < 777; ++i)
                require (whole.getSample (ch, position + i) == block.getSample (ch, i), "Decoder cache changed random-seek output");
        }
    }
    // A deliberately fragmented but valid package exercises the explicit limit
    // without hundreds of lossy generations or a multi-minute fixture.
    const auto crowded = folder.getChildFile ("crowded.rmp");
    {
        juce::FileInputStream input (first); input.setPosition (8); const auto size = input.readInt();
        juce::MemoryBlock json; input.readIntoMemoryBlock (json, (size_t) size);
        auto index = juce::JSON::parse (json.toString()); juce::Array<juce::var> spans;
        for (int i = 0; i < 256; ++i)
        {
            juce::var span (new juce::DynamicObject()); auto* o = span.getDynamicObject();
            o->setProperty ("asset", 0); o->setProperty ("source", 0.0);
            o->setProperty ("start", i == 0 ? 0 : 1.01 + (i - 1) * 4.99 / 255);
            o->setProperty ("end", i == 0 ? 1.01 : 1.01 + i * 4.99 / 255);
            o->setProperty ("envelopes", juce::Array<juce::var>{}); spans.add (span);
        }
        index.getDynamicObject()->setProperty ("spans", spans); const auto updated = juce::JSON::toString (index, true);
        juce::FileOutputStream output (crowded); output.write ("RMPREV03", 8); output.writeInt ((int) updated.getNumBytesAsUTF8());
        output.write (updated.toRawUTF8(), updated.getNumBytesAsUTF8()); output.writeFromInputStream (input, -1);
    }
    require (PreviewAudio::verify (crowded).wasOk(), "Fragment-limit fixture is invalid");
    const auto hash = ProjectIdentity::fingerprint (crowded);
    const auto limited = PreviewAudio::write (folder.getChildFile ("limited.rmp"), fresh, crowded, 48000, 2, 48000, 288000, 4, 1);
    require (limited.failed() && limited.getErrorMessage().contains ("limit")
        && ProjectIdentity::fingerprint (crowded) == hash, "Fragment limit did not preserve existing audio");
    const auto relocated = folder.getChildFile ("relocated"); require (relocated.createDirectory().wasOk(), "Cannot create portable fixture");
    const auto portable = relocated.getChildFile ("preview.rmp"); require (previous.copyFileTo (portable), "Cannot relocate package");
    previous.deleteFile(); read (portable, 4, 1024);
    std::cout << "PASS: 60 equal/variable splices preserve tail bit-for-bit with bounded storage, seeking, fades, checksums, cancellation, portability, limits and rate/mono conversion\n";
}

void testPreviewCompaction (const juce::File& root)
{
    const auto library = root.getChildFile ("compact-library");
    require (library.createDirectory().wasOk(), "Cannot create compaction library");
    require (ProjectScanner::saveSettings (library, {}, 60, 4, false, nullptr, 0, 12.5).wasOk()
        && ProjectScanner::readSettings().compactAboveMB == 12.5
        && ProjectScanner::saveSettings (library, {}, 60, 4, false).wasOk()
        && ProjectScanner::readSettings().compactAboveMB == 12.5,
        "Compaction threshold was not persisted or was lost by unrelated settings edits");
    const auto id = juce::Uuid().toString(); const auto entry = library.getChildFile (id);
    require (entry.createDirectory().wasOk(), "Cannot create compaction entry");
    const auto input = source (root, "compact-source", 48000, 50, 0.3f);
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty ("projectId", id);
    patch.getDynamicObject()->setProperty ("projectName", "Compaction");
    patch.getDynamicObject()->setProperty ("durationSeconds", 50.0);
    patch.getDynamicObject()->setProperty ("sampleRate", 48000);
    patch.getDynamicObject()->setProperty ("channels", 2);
    juce::String revision;
    for (int i = 0; i < 2; ++i)
    {
        LibraryStore::AudioRevision audio (entry, true);
        require (audio.prepare().wasOk(), "Cannot prepare compaction fixture");
        require (PreviewAudio::write (audio.ogg, input, {}, 48000, 2, 2400000, 2400000, 4, 1).wasOk(), "Cannot write compaction fragments");
        require (LibraryStore::commitAudio (entry, audio, patch, revision).wasOk(), "Cannot publish compaction fixture");
        revision = audio.id;
    }
    const auto before = LibraryStore::measureEntryAudio (entry);
    const auto old = LibraryStore::audioFile (entry, false);
    const auto oldHash = ProjectIdentity::fingerprint (old);
    const auto entries = ProjectScanner::scan (library, {});
    require (entries.size() == 1 && entries[0].audioBytes == before, "Scanner does not total current and recovery audio");
    PreviewCompactionActions::Summary skipped;
    PreviewCompactionActions::run (library, entries, 4, before + 1, skipped);
    require (skipped.skipped == 1 && skipped.compacted == 0, "Bulk compaction ignored its total audio threshold");
    require (LibraryStore::compactPreview (library, entry, id, revision, 4, [] { return true; }).result.failed(), "Cancelled compaction published audio");
    require (LibraryStore::compactPreview (library, entry, id, "stale", 4).result.failed(), "Stale compaction was accepted");
    require (ProjectIdentity::fingerprint (old) == oldHash && LibraryStore::measureEntryAudio (entry) == before,
        "Cancelled/stale compaction changed originals");
    const auto result = LibraryStore::compactPreview (library, entry, id, revision, 4, {}, [&] (LibraryStore::CommitStage stage)
    {
        if (stage == LibraryStore::CommitStage::AudioVerified)
        {
            auto metadata = LibraryStore::readJson (entry.getChildFile ("metadata.json"));
            metadata.getDynamicObject()->setProperty ("notes", "Concurrent edit survives");
            metadata.getDynamicObject()->setProperty ("favorite", true);
            require (LibraryStore::writeJsonAtomically (entry.getChildFile ("metadata.json"), metadata).wasOk(), "Cannot simulate concurrent metadata edit");
        }
    });
    require (result.result.wasOk() && result.changed, "Compaction failed: " + result.result.getErrorMessage());
    const auto metadata = LibraryStore::readJson (entry.getChildFile ("metadata.json"));
    const auto compacted = LibraryStore::audioFile (entry, false);
    require (compacted.hasFileExtension ("ogg") && result.afterBytes == compacted.getSize() && result.afterBytes < before,
        "Compaction retained recovery/fragments/WAV or did not reduce storage");
    require ((int) metadata["audioStorageVersion"] == 4 && metadata["previousAudioRevision"].toString().isEmpty()
        && metadata["notes"].toString() == "Concurrent edit survives" && (bool) metadata["favorite"]
        && ! (bool) metadata["compactionCleanupPending"] && (int) metadata["oggQuality"] == 4,
        "Compaction lost concurrent metadata or retained recovery");
    require (LibraryStore::recoverEntry (entry).result.wasOk(), "Compacted revision cannot recover/verify");
    const auto current = metadata["audioRevision"].toString(), hash = ProjectIdentity::fingerprint (compacted);
    const auto repeated = LibraryStore::compactPreview (library, entry, id, current, 8);
    require (repeated.result.wasOk() && ! repeated.changed && ProjectIdentity::fingerprint (compacted) == hash,
        "Repeated compaction re-encoded unchanged OGG");
    // A retry can remove leftover history without another lossy generation.
    const auto leftover = entry.getChildFile ("preview.ogg"); require (compacted.copyFileTo (leftover), "Cannot create cleanup retry fixture");
    PreviewCompactionActions::Summary cleaned;
    auto currentEntries = ProjectScanner::scan (library, {});
    PreviewCompactionActions::run (library, currentEntries, 8, compacted.getSize() + 1, cleaned);
    require (cleaned.compacted == 1 && ! leftover.existsAsFile()
        && ProjectIdentity::fingerprint (LibraryStore::audioFile (entry, false)) == hash,
        "Bulk compaction failed to count recovery/cleanup without recoding");
    const auto usable = LibraryStore::audioFile (entry, false);
    const auto restored = root.getChildFile ("restored.rmp"), updated = root.getChildFile ("updated.rmp");
    require (PreviewAudio::write (restored, input, usable, 48000, 2, 48000, 2400000, 4, 1).wasOk(), "Partial capture cannot convert compacted tail");
    const auto reference = read (restored, 4, 4096);
    require (PreviewAudio::write (updated, input, restored, 48000, 2, 48000, 2400000, 4, 1).wasOk(), "Subsequent partial capture failed");
    const auto tail = read (updated, 4, 4096);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 4096; ++i)
        require (tail.getSample (ch, i) == reference.getSample (ch, i), "Subsequent capture re-encoded retained tail");
   #if JUCE_WINDOWS
    const auto busyId = juce::Uuid().toString(); const auto busyFolder = library.getChildFile (busyId);
    require (busyFolder.createDirectory().wasOk(), "Cannot create busy audio fixture");
    auto busyPatch = patch.clone(); busyPatch.getDynamicObject()->setProperty ("projectId", busyId);
    juce::String busyRevision;
    {
        LibraryStore::AudioRevision audio (busyFolder, true);
        require (audio.prepare().wasOk() && updated.copyFileTo (audio.ogg)
            && LibraryStore::commitAudio (busyFolder, audio, busyPatch, {}).wasOk(), "Cannot publish busy audio fixture");
        busyRevision = audio.id;
    }
    struct HeldAudio
    {
        HANDLE handle;
        ~HeldAudio() { close(); }
        void close() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle (handle); handle = INVALID_HANDLE_VALUE; } }
    } openAudio { CreateFileW (LibraryStore::audioFile (busyFolder, false).getFullPathName().toWideCharPointer(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
    require (openAudio.handle != INVALID_HANDLE_VALUE, "Cannot hold audio without delete sharing");
    const auto busy = LibraryStore::compactPreview (library, busyFolder, busyId, busyRevision, 4);
    require (busy.changed && busy.result.failed()
        && (bool) LibraryStore::readJson (busyFolder.getChildFile ("metadata.json"))["compactionCleanupPending"],
        "Busy old audio did not report incomplete cleanup after publication");
    const auto compactHash = ProjectIdentity::fingerprint (LibraryStore::audioFile (busyFolder, false));
    openAudio.close();
    const auto busyCurrent = LibraryStore::readJson (busyFolder.getChildFile ("metadata.json"))["audioRevision"].toString();
    const auto retry = LibraryStore::compactPreview (library, busyFolder, busyId, busyCurrent, 8);
    require (retry.changed && retry.result.wasOk()
        && ProjectIdentity::fingerprint (LibraryStore::audioFile (busyFolder, false)) == compactHash,
        "Cleanup retry lost audio or re-encoded it at a different quality");
   #endif
    std::cout << "MEASURE: 50s stereo 48kHz q4 fragmented=" << before / 2 << " single=" << usable.getSize()
              << " total-with-recovery=" << before << " bytes\n";
    for (int quality : { 2, 8 })
    {
        const auto package = root.getChildFile ("measure-" + juce::String (quality) + ".rmp");
        const auto ogg = root.getChildFile ("measure-" + juce::String (quality) + ".ogg");
        PreviewCompaction::Encoding encoding;
        require (PreviewAudio::write (package, input, {}, 48000, 2, 2400000, 2400000, quality, 1).wasOk()
            && PreviewCompaction::encode (package, ogg, quality, encoding).wasOk(), "Cannot measure OGG storage");
        std::cout << "MEASURE: 50s stereo 48kHz q" << quality << " fragmented=" << package.getSize() << " single=" << ogg.getSize() << " bytes\n";
    }
    std::cout << "PASS: atomic compaction, selected quality, concurrent metadata, recovery removal, cancellation, stale target, idempotence and future partial capture\n";
}
