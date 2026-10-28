#pragma once
#include "ProjectScanner.h"
#include "PreviewSelection.h"

namespace PreviewExport
{
// Worker only. Published files are immutable: DAWs may retain these paths.
inline juce::Result prepare (const ProjectEntry& entry, const juce::File& library,
                             juce::File& exported, const std::function<bool()>& cancelled)
{
    exported = {};
    if (library == juce::File{}) return juce::Result::fail ("Configure the previews folder before exporting");
    if (entry.auditionReader || entry.pending || entry.storageError.isNotEmpty())
        return juce::Result::fail ("This preview cannot be exported");
    juce::File audioFile;
    auto reader = PreviewSelection::open (entry, audioFile);
    if (! reader) return juce::Result::fail ("Cannot read preview for export");
    juce::FileInputStream input (audioFile);
    if (! input.openedOk()) return juce::Result::fail ("Preview is unavailable");
    const auto digest = juce::SHA256 (input).toHexString();
    if (cancelled()) return juce::Result::fail ("Export cancelled");
    const auto directory = library.getChildFile ("Exports");
    auto result = LibraryStore::ensureDirectory (directory);
    if (result.failed()) return result;
    // Leave room for TemporaryFile's suffix on Windows, including deep library
    // locations. The full digest remains the immutable content identity.
    const auto nameBudget = juce::jlimit (0, 64, 220 - directory.getFullPathName().length() - 70);
    const auto name = juce::File::createLegalFileName (entry.name).substring (0, nameBudget);
    const auto destination = directory.getChildFile ((name.isEmpty() ? juce::String{} : name + "-") + digest + ".wav");
    // A content hash and an interprocess lock prevent stale files and competing writers.
    juce::InterProcessLock lock ("RememberPreviewExport-" + juce::SHA256 (destination.getFullPathName().toRawUTF8(),
        destination.getFullPathName().getNumBytesAsUTF8()).toHexString());
    if (! lock.enter (5000)) return juce::Result::fail ("Another export is busy; select the preview again to retry");
    struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock { lock };
    if (! reader || reader->lengthInSamples <= 0 || reader->numChannels < 1 || reader->numChannels > 2)
        return juce::Result::fail ("Cannot read preview for export");
    if (destination.existsAsFile())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> existing (wav.createReaderFor (destination.createInputStream().release(), true));
        if (! existing || existing->lengthInSamples != reader->lengthInSamples || existing->sampleRate != reader->sampleRate
            || existing->numChannels != reader->numChannels)
            return juce::Result::fail ("The exported WAV was modified. Move it out of Exports and select the preview again.");
        exported = destination; return juce::Result::ok();
    }
    juce::TemporaryFile temporary (destination);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> output (temporary.getFile().createOutputStream());
    auto writer = wav.createWriterFor (output, juce::AudioFormatWriterOptions{}.withSampleRate (reader->sampleRate)
        .withNumChannels ((int) reader->numChannels).withBitsPerSample (24));
    if (! writer) return juce::Result::fail ("Cannot create exported WAV");
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, 8192);
    for (juce::int64 position = 0; position < reader->lengthInSamples;)
    {
        if (cancelled()) return juce::Result::fail ("Export cancelled");
        const auto count = (int) juce::jmin ((juce::int64) buffer.getNumSamples(), reader->lengthInSamples - position);
        if (! reader->read (&buffer, 0, count, position, true, true) || ! writer->writeFromAudioSampleBuffer (buffer, 0, count))
            return juce::Result::fail ("Cannot write complete preview WAV");
        position += count;
    }
    if (! writer->flush()) return juce::Result::fail ("Cannot flush exported WAV");
    writer.reset();
    if (cancelled()) return juce::Result::fail ("Export cancelled");
    juce::FileInputStream latest (audioFile);
    if (! latest.openedOk() || juce::SHA256 (latest).toHexString() != digest)
        return juce::Result::fail ("Preview changed while exporting. Select it again to retry.");
    if (! temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail ("Cannot publish exported WAV");
    exported = destination; return juce::Result::ok();
}
}
