#include "PreviewSelection.h"

namespace PreviewSelection
{
namespace
{
bool usable (const std::unique_ptr<juce::AudioFormatReader>& reader)
{
    return reader && reader->sampleRate > 0 && reader->lengthInSamples > 0
        && reader->numChannels > 0 && reader->numChannels <= 2;
}
juce::Result identity (const ProjectEntry& entry, const juce::DynamicObject& object)
{
    return entry.projectId.isNotEmpty() && object.getProperty ("projectId").toString() == entry.projectId
        ? juce::Result::ok() : juce::Result::fail ("The selected recording changed; refresh the library.");
}
juce::Result association (const ProjectEntry& entry, const juce::DynamicObject& object,
                          const juce::String& expectedFileId)
{
    const auto path = object.getProperty ("projectPath").toString();
    const auto storedId = object.getProperty ("projectFileId").toString();
    if (expectedFileId.isEmpty() || ! juce::File::isAbsolutePath (entry.projectPath)
        || ! juce::File::isAbsolutePath (path) || juce::File (path) != juce::File (entry.projectPath)
        || (storedId.isNotEmpty() && storedId != expectedFileId)
        || (entry.projectFileId.isNotEmpty() && entry.projectFileId != expectedFileId)
        || ProjectIdentity::fileId (juce::File (path)) != expectedFileId)
        return juce::Result::fail ("The associated project changed; refresh the library.");
    return juce::Result::ok();
}
}
juce::Result select (const ProjectEntry& entry, const juce::File& file, bool manual)
{
    const auto reader = PreviewAudio::open (file);
    if (! usable (reader)) return juce::Result::fail ("The selected file is not a supported audio file.");
    const auto expectedFileId = ! manual && juce::File::isAbsolutePath (entry.projectPath)
        ? ProjectIdentity::fileId (juce::File (entry.projectPath)) : juce::String{};
    return LibraryStore::updateJson (entry.metadataFile, [&] (juce::DynamicObject& object)
    {
        auto result = identity (entry, object);
        if (result.failed()) return result;
        // Latest manifest, under the edit lock: a stale render job must not
        // replace a manual decision made after that render was queued.
        const auto provenance = object.getProperty ("selectedPreviewSource").toString();
        if (! manual && (provenance == "manual" || (provenance.isNotEmpty()
            && ! (bool) object.getProperty ("selectedPreviewEnabled"))))
            return juce::Result::fail ("Manual preview selection is protected.");
        if (! manual)
        {
            result = association (entry, object, expectedFileId);
            if (result.failed()) return result;
        }
        object.setProperty ("selectedPreviewPath", file.getFullPathName());
        object.setProperty ("selectedPreviewSource", manual ? "manual" : "render");
        object.setProperty ("selectedPreviewEnabled", true);
        return juce::Result::ok();
    });
}
juce::Result disable (const ProjectEntry& entry)
{
    return LibraryStore::updateJson (entry.metadataFile, [&] (juce::DynamicObject& object)
    {
        auto result = identity (entry, object);
        if (result.wasOk())
        {
            object.setProperty ("selectedPreviewEnabled", false);
            object.setProperty ("selectedPreviewSource", "manual");
        }
        // Disabling an automatic render is itself an explicit manual choice.
        return result;
    });
}
bool inheritRender (ProjectEntry& entry, const juce::var& history)
{
    if (entry.pending || ! entry.projectAvailable || entry.selectedPreviewSource.isNotEmpty()
        || ! juce::File::isAbsolutePath (entry.projectPath)) return false;
    auto path = juce::File (entry.projectPath).getFullPathName();
#if JUCE_WINDOWS
    path = path.toLowerCase();
#endif
    const auto key = juce::SHA256 (path.toRawUTF8(), path.getNumBytesAsUTF8()).toHexString();
    const auto record = history.getProperty (key, juce::var{});
    const auto audio = record.getProperty ("previewPath", "").toString();
    const auto expectedFileId = record.getProperty ("projectFileId", "").toString();
    if (! (bool) record.getProperty ("useAsPreview", false) || ! juce::File::isAbsolutePath (audio)
        || ! juce::File (audio).existsAsFile() || expectedFileId.isEmpty()
        || expectedFileId != ProjectIdentity::fileId (juce::File (entry.projectPath))) return false;
    bool applied = false;
    const auto result = LibraryStore::updateJson (entry.metadataFile, [&] (juce::DynamicObject& object)
    {
        const auto valid = identity (entry, object); if (valid.failed()) return valid;
        if (object.getProperty ("selectedPreviewSource").toString().isNotEmpty()) return juce::Result::ok();
        const auto associated = association (entry, object, expectedFileId);
        if (associated.failed()) return associated;
        object.setProperty ("selectedPreviewPath", audio); object.setProperty ("selectedPreviewSource", "render");
        object.setProperty ("selectedPreviewEnabled", true); applied = true; return juce::Result::ok();
    });
    if (result.failed() || ! applied) return false;
    entry.selectedPreviewPath = audio; entry.selectedPreviewSource = "render";
    entry.selectedPreviewEnabled = true; entry.selectedPreviewAvailable = true; return true;
}
std::unique_ptr<juce::AudioFormatReader> open (const ProjectEntry& entry, juce::File& actual)
{
    if (entry.selectedPreviewEnabled && juce::File::isAbsolutePath (entry.selectedPreviewPath))
    {
        actual = juce::File (entry.selectedPreviewPath);
        auto reader = PreviewAudio::open (actual);
        if (usable (reader)) return reader;
    }
    actual = entry.previewOgg;
    return PreviewAudio::open (actual);
}
}
