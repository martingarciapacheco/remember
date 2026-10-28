#include "OfflineRendering.h"
#include "../../Shared/LibraryStore.h"
#include "../../Shared/ProjectIdentity.h"
#include <filesystem>
#include <unordered_set>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <tlhelp32.h>
#endif

namespace OfflineRendering
{
namespace
{
juce::String outputStamp (const juce::File& file, const Cancel& cancel = {})
{
    if (! file.exists()) return "missing";
    if (! file.existsAsFile() || file.isSymbolicLink()) return {};
    const auto identity = ProjectIdentity::fileId (file);
    if (identity.isEmpty()) return {};
    const auto hash = ProjectIdentity::fingerprint (file, cancel);
    if (hash.isEmpty()) return {};
    return identity + ":" + juce::String (file.getSize())
        + ":" + juce::String (file.getLastModificationTime().toMilliseconds()) + ":" + hash;
}
}
RememberOptions optionsFromConfig (const juce::var& config)
{
    const auto value = config.getProperty ("offlineRendering", juce::var{});
    RememberOptions out;
    out.saveBesideProject = (bool) value.getProperty ("saveBesideProject", true);
    out.includeSubfolders = (bool) value.getProperty ("includeSubfolders", false);
    out.useRendersAsPreviews = (bool) value.getProperty ("useRendersAsPreviews", false);
    out.sourceIsFolder = (bool) value.getProperty ("sourceIsFolder", false);
    out.customDestination = value.getProperty ("customDestination", "").toString();
    out.sourcePath = value.getProperty ("sourcePath", "").toString();
    const auto storedFormats = value.getProperty ("formats", juce::var{});
    if (const auto* formats = storedFormats.getArray())
        for (const auto& format : *formats) if (juce::StringArray { "wav", "mp3", "ogg", "flac", "m4a" }.contains (format.toString())) out.formats.addIfNotAlreadyThere (format.toString());
    return out;
}
juce::Result saveOptions (const RememberOptions& options)
{
    return LibraryStore::updateJson (LibraryStore::configFile(), [&] (juce::DynamicObject& config)
    {
        auto value = config.getProperty ("offlineRendering").clone();
        if (! value.isObject()) value = juce::var (new juce::DynamicObject());
        auto& object = *value.getDynamicObject();
        object.setProperty ("saveBesideProject", options.saveBesideProject);
        object.setProperty ("includeSubfolders", options.includeSubfolders);
        object.setProperty ("useRendersAsPreviews", options.useRendersAsPreviews);
        object.setProperty ("sourceIsFolder", options.sourceIsFolder);
        object.setProperty ("customDestination", options.customDestination);
        object.setProperty ("sourcePath", options.sourcePath);
        juce::Array<juce::var> formats; for (const auto& format : options.formats) formats.add (format);
        object.setProperty ("formats", formats);
        config.setProperty ("offlineRendering", value);
        return juce::Result::ok();
    }, true);
}
FlStudioSettings readFlStudioSettings (const juce::String& version, SettingReader read)
{
    FlStudioSettings out; out.registryVersion = version;
    if (version.isEmpty() || version.length() > 3 || version.retainCharacters ("0123456789") != version)
    { out.result = juce::Result::fail ("Invalid FL Studio settings version."); return out; }
    if (! read)
    {
#if JUCE_WINDOWS
        const auto prefix = "HKEY_CURRENT_USER\\Software\\Image-Line\\FL Studio " + version + "\\General\\WAVRenderForm\\";
        read = [prefix, version] (const juce::String& name)
        {
            if (name == "SampleRate") return juce::WindowsRegistry::getValue (
                "HKEY_CURRENT_USER\\Software\\Image-Line\\FL Studio " + version + "\\Devices\\Audio output\\Sample rate");
            return juce::WindowsRegistry::getValue (prefix + name);
        };
#else
        out.result = juce::Result::fail ("FL Studio settings discovery is not supported on this platform."); return out;
#endif
    }
    for (const auto* name : { "SampleRate", "LMBox", "IPBox", "WFBox", "MP3BitrateSlider", "OggBitrateSlider",
        "FlacBitDepthBox", "FlacCompressionSlider", "DitherBox", "NoPolyBox", "PlugHQBox",
        "OpenRenderLocationBox", "BatchBox", "AcidBox", "NoteMarkersBox", "LCBox", "InputFXBox",
        "MasterFXBox", "PlaylistMarkersBox", "LoopMarkersBox", "ChannelsBox", "SplitMasterBox", "SplitCurrentBox" })
    {
        const auto stored = read (name).trim();
        if (stored.isNotEmpty() && stored.length() <= (juce::String (name) == "SampleRate" ? 6 : 3)
            && stored.retainCharacters ("0123456789") == stored)
            out.values.set (juce::Identifier (name), stored.getIntValue());
        else out.missing.add (name);
    }
    if (out.values.isEmpty()) out.result = juce::Result::fail ("FL Studio export settings are unavailable.");
    return out;
}
Sources collectProjects (const juce::File& source, bool recursive, const Cancel& cancelled)
{
    Sources out;
    const auto abort = [&] { return cancelled && cancelled(); };
    const auto fail = [&] (const char* reason)
    { out.projects.clear(); out.result = juce::Result::fail (reason); return out; };
    if (abort()) return fail ("Offline rendering scan cancelled.");
    if (source.existsAsFile())
    {
        if (! source.hasFileExtension ("flp")) return fail ("Select an FL Studio project (.flp).");
        out.projects.add (source); return out;
    }
    if (! source.isDirectory()) return fail ("Offline rendering source is unavailable.");
    // Iterative traversal keeps directory depth off the call stack. Do not
    // follow directory links, which could escape the selection or form cycles.
    juce::Array<juce::File> folders { source };
    while (! folders.isEmpty())
    {
        if (abort()) return fail ("Offline rendering scan cancelled.");
        const auto folder = folders.removeAndReturn (folders.size() - 1);
        const auto folderId = ProjectIdentity::fileId (folder);
        const auto unavailable = [&]
        {
            out.projects.clear();
            out.result = juce::Result::fail ("Cannot list offline rendering source: " + folder.getFullPathName());
            return out;
        };
        if (folderId.isEmpty()) return unavailable();
#if JUCE_WINDOWS
        const auto native = std::filesystem::path (folder.getFullPathName().toWideCharPointer());
#else
        const auto native = std::filesystem::u8path (folder.getFullPathName().toRawUTF8());
#endif
        std::error_code error;
        std::filesystem::directory_iterator iterator (native, error), end;
        if (error) return unavailable();
        for (; iterator != end; iterator.increment (error))
        {
            if (error) return unavailable();
            if (abort()) return fail ("Offline rendering scan cancelled.");
#if JUCE_WINDOWS
            const juce::File file (juce::String (iterator->path().c_str()));
#else
            const juce::File file (juce::String::fromUTF8 (iterator->path().u8string().c_str()));
#endif
            const auto status = iterator->status (error);
            if (error) return unavailable();
            if (std::filesystem::is_directory (status))
            {
                if (recursive && ! file.isSymbolicLink()) folders.add (file);
            }
            else if (std::filesystem::is_regular_file (status) && file.hasFileExtension ("flp")) out.projects.add (file);
        }
        if (error || folderId != ProjectIdentity::fileId (folder)) return unavailable();
    }
    struct Order { int compareElements (const juce::File& a, const juce::File& b) const
        { return a.getFullPathName().compareNatural (b.getFullPathName()); } } order;
    out.projects.sort (order);
    return out;
}

Command flStudioCommand (const juce::File& executable, const juce::File& project,
                         const juce::File& destination, const juce::StringArray& formats, const Cancel& cancel)
{
    Command out;
    out.executable = executable; out.project = project; out.destination = destination;
    const auto fail = [&] (const char* reason)
    { out.result = juce::Result::fail (reason); return out; };
    if (! executable.existsAsFile()) return fail ("FL Studio executable is unavailable.");
    if (! project.existsAsFile() || ! project.hasFileExtension ("flp"))
        return fail ("Select an FL Studio project (.flp).");
    out.projectFileId = ProjectIdentity::fileId (project);
    if (out.projectFileId.isEmpty()) return fail ("The associated project changed; refresh the library.");
    if (! destination.isDirectory()) return fail ("Offline rendering destination is unavailable.");
    if (formats.isEmpty()) return fail ("Select an audio output format.");
    const juce::StringArray supported { "wav", "mp3", "ogg", "flac", "m4a" };
    juce::StringArray unique;
    for (const auto& format : formats)
    {
        if (! supported.contains (format)) return fail ("Unsupported offline audio format.");
        unique.addIfNotAlreadyThere (format);
    }
    // Explicit single-project rendering avoids FL's implicit folder recursion.
    out.arguments = { executable.getFullPathName(), "/E" + unique.joinIntoString (","),
                      "/R", project.getFullPathName(), "/O" + destination.getFullPathName() };
    out.formats = unique;
    for (const auto& format : unique)
    {
        const auto file = destination.getChildFile (project.getFileNameWithoutExtension() + "." + format);
        out.outputs.add (file);
        const auto stamp = outputStamp (file, cancel);
        if (stamp.isEmpty()) return fail ("Cannot verify existing render outputs. Check access and select the projects again.");
        out.outputStamps[file.getFullPathName()] = stamp;
        if (file.exists()) out.existingOutputs.add (file);
    }
    return out;
}

BatchPlan planBatch (const juce::File& executable, const Sources& sources, const RememberOptions& options,
                     const juce::StringArray& formats, const Cancel& cancelled)
{
    BatchPlan out;
    const auto fail = [&] (juce::Result result)
    { out.result = result; out.jobs.clear(); out.existingOutputs.clear(); out.duplicateOutputs.clear(); return out; };
    if (sources.result.failed()) return fail (sources.result);
    if (sources.projects.isEmpty()) return fail (juce::Result::fail ("No FL Studio projects were found in the selected source."));
    if (! options.saveBesideProject && ! juce::File::isAbsolutePath (options.customDestination))
        return fail (juce::Result::fail ("Select a destination folder for the rendered audio."));
    std::unordered_set<std::string> seen, existing, duplicates;
    for (const auto& project : sources.projects)
    {
        if (cancelled && cancelled()) return fail (juce::Result::fail ("Offline rendering scan cancelled."));
        const auto destination = options.saveBesideProject ? project.getParentDirectory() : juce::File (options.customDestination);
        auto job = flStudioCommand (executable, project, destination, formats, cancelled);
        if (job.result.failed()) return fail (juce::Result::fail (project.getFileName() + ": " + job.result.getErrorMessage()));
        const auto key = [] (const juce::File& file)
        {
            auto path = file.getFullPathName();
#if JUCE_WINDOWS
            path = path.toLowerCase();
#endif
            return path.toStdString();
        };
        for (const auto& file : job.existingOutputs)
            if (existing.insert (key (file)).second) out.existingOutputs.add (file);
        for (const auto& file : job.outputs)
        {
            const auto path = key (file);
            if (! seen.insert (path).second && duplicates.insert (path).second) out.duplicateOutputs.add (file);
        }
        out.jobs.push_back (std::move (job));
    }
    return out;
}

RenderResult render (const Command& command, bool approved, const Cancel& cancelled, ProcessRunner runner)
{
    RenderResult out;
    const auto begun = juce::Time::getMillisecondCounterHiRes();
    const auto abort = [&] { return cancelled && cancelled(); };
    const auto finish = [&] (juce::Result result)
    { out.result = result; out.cancelled = out.cancelled || abort(); out.seconds = (juce::Time::getMillisecondCounterHiRes() - begun) / 1000.0; return out; };
    if (command.result.failed()) return finish (command.result);
    const auto validated = flStudioCommand (command.executable, command.project, command.destination, command.formats, cancelled);
    if (validated.result.failed()) return finish (validated.result);
    if (validated.outputs != command.outputs || validated.arguments != command.arguments
        || validated.projectFileId != command.projectFileId)
        return finish (juce::Result::fail ("The planned render job changed. Select the projects again."));
    if (abort()) { out.cancelled = true; return finish (juce::Result::fail ("Offline rendering cancelled.")); }
    if (command.outputs.isEmpty() || command.arguments.isEmpty()) return finish (juce::Result::fail ("No render job is planned."));
    if (! command.existingOutputs.isEmpty() && ! approved)
        return finish (juce::Result::fail ("Confirm before overwriting existing renders."));
    juce::InterProcessLock flLock ("RememberOfflineFLStudio");
    if (! flLock.enter (0)) return finish (juce::Result::fail ("Another FL Studio render is running."));
    struct FlUnlock { juce::InterProcessLock& lock; ~FlUnlock() { lock.exit(); } } flUnlock { flLock };
    if (! runner)
        if (auto closed = requireFlStudioClosed(); closed.failed()) return finish (closed);
    juce::InterProcessLock lock ("RememberOfflineOutput-" + juce::SHA256 (
        command.destination.getFullPathName().toRawUTF8(), command.destination.getFullPathName().getNumBytesAsUTF8()).toHexString());
    if (! lock.enter (0)) return finish (juce::Result::fail ("Another render is using this destination."));
    struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock { lock };
    const auto unchanged = [&]
    {
        for (const auto& file : command.outputs)
        {
            const auto expected = command.outputStamps.find (file.getFullPathName());
            if (expected == command.outputStamps.end() || expected->second != outputStamp (file, cancelled)) return false;
        }
        return true;
    };
    if (! unchanged()) return finish (juce::Result::fail ("Render outputs changed. Review the overwrite confirmation again."));
    const auto stage = command.destination.getChildFile (".remember-render-" + juce::Uuid().toString());
    if (auto result = stage.createDirectory(); result.failed()) return finish (result);
    bool safeToDeleteStage = true;
    struct Cleanup
    {
        juce::File folder, parent;
        bool& safe;
        ~Cleanup() { if (safe && folder.getParentDirectory() == parent && folder.getFileName().startsWith (".remember-render-")
                          && ! folder.isSymbolicLink()) folder.deleteRecursively(); }
    } cleanup { stage, command.destination, safeToDeleteStage };
    const auto staged = flStudioCommand (command.executable, command.project, stage, command.formats);
    if (staged.result.failed()) return finish (staged.result);
    if (! runner) runner = [&safeToDeleteStage] (const juce::StringArray& args, const Cancel& cancel)
    {
        juce::ChildProcess process;
        if (! process.start (args, 0)) return juce::Result::fail ("Cannot start FL Studio offline rendering.");
        while (process.isRunning())
        {
            if (cancel && cancel())
            {
                if (! process.kill() || ! process.waitForProcessToFinish (5000))
                {
                    safeToDeleteStage = false; // A live child may still be writing here.
                    return juce::Result::fail ("Could not stop the FL Studio render process.");
                }
                return juce::Result::fail ("Offline rendering cancelled.");
            }
            process.waitForProcessToFinish (100);
        }
        return process.getExitCode() == 0 ? juce::Result::ok() : juce::Result::fail ("FL Studio reported a rendering error.");
    };
    const auto result = runner (staged.arguments, cancelled);
    if (abort()) { out.cancelled = true; return finish (juce::Result::fail ("Offline rendering cancelled.")); }
    if (result.failed()) return finish (result);
    if (ProjectIdentity::fileId (command.project) != command.projectFileId)
        return finish (juce::Result::fail ("The associated project changed; refresh the library."));
    // Saved FL settings can request stems or other additional outputs. Never
    // silently discard those files or publish them without collision review.
    for (const auto& file : stage.findChildFiles (juce::File::findFiles, true))
        if (file.hasFileExtension ("wav;mp3;ogg;flac;m4a") && ! staged.outputs.contains (file))
        {
            safeToDeleteStage = false;
            return finish (juce::Result::fail ("Additional render outputs need review and were kept in: " + stage.getFullPathName()));
        }
    for (const auto& file : staged.outputs)
    {
        auto reader = PreviewAudio::open (file);
        if (! reader || reader->lengthInSamples <= 0 || reader->numChannels == 0)
            return finish (juce::Result::fail ("FL Studio did not produce a readable audio file: " + file.getFileName()));
        juce::AudioBuffer<float> edge ((int) juce::jmin (reader->numChannels, 2u), 1);
        if (! reader->read (&edge, 0, 1, 0, true, true)
            || ! reader->read (&edge, 0, 1, reader->lengthInSamples - 1, true, true))
            return finish (juce::Result::fail ("The rendered audio is incomplete: " + file.getFileName()));
    }
    if (! unchanged()) return finish (juce::Result::fail ("Render outputs changed. Review the overwrite confirmation again."));
    // Keep both the generated audio and exact originals until the complete job
    // commits. A cancelled/failed second format must not replace only the first.
    juce::Array<juce::File> backups;
    for (int i = 0; i < command.outputs.size(); ++i)
    {
        const auto target = command.outputs[i];
        const auto backup = stage.getChildFile ("original-" + juce::String (i));
        if (target.exists() && ! target.copyFileTo (backup))
            return finish (juce::Result::fail ("Cannot preserve existing render before publication: " + target.getFullPathName()));
        if (backup.existsAsFile() && ProjectIdentity::fingerprint (backup, cancelled)
            != command.outputStamps.at (target.getFullPathName()).fromLastOccurrenceOf (":", false, false))
            return finish (juce::Result::fail ("Cannot preserve existing render before publication: " + target.getFullPathName()));
        backups.add (backup);
    }
    if (! unchanged()) return finish (juce::Result::fail ("Render outputs changed. Review the overwrite confirmation again."));
    juce::StringArray publishedStamps;
    const auto rollback = [&] (const juce::Result& failure)
    {
        juce::Array<juce::File> remaining;
        for (int i = out.published.size(); --i >= 0;)
        {
            const auto target = out.published[i];
            // An external edit after publication belongs to that editor. Never
            // undo over it; retain the originals/new audio for explicit recovery.
            bool restored = outputStamp (target) == publishedStamps[i];
            if (restored && backups[i].existsAsFile())
            {
                juce::TemporaryFile temporary (target);
                restored = backups[i].copyFileTo (temporary.getFile()) && temporary.overwriteTargetFileWithTemporary();
            }
            else if (restored) restored = target.deleteFile();
            if (! restored) remaining.add (target);
        }
        out.published = std::move (remaining);
        if (! out.published.isEmpty())
        {
            safeToDeleteStage = false;
            return finish (juce::Result::fail ("Render publication could not be fully restored. Recovery files were kept in: " + stage.getFullPathName()));
        }
        return finish (failure);
    };
    for (int i = 0; i < staged.outputs.size(); ++i)
    {
        if (abort()) { out.cancelled = true; return rollback (juce::Result::fail ("Offline rendering cancelled.")); }
        const auto target = command.outputs[i];
        if (outputStamp (target, cancelled) != command.outputStamps.at (target.getFullPathName()))
            return rollback (juce::Result::fail ("Render outputs changed. Review the overwrite confirmation again."));
        juce::TemporaryFile temporary (target);
        if (! staged.outputs[i].copyFileTo (temporary.getFile()) || ! temporary.overwriteTargetFileWithTemporary())
            return rollback (juce::Result::fail ("Cannot publish rendered audio: " + target.getFullPathName()));
        out.published.add (target);
        // Rollback must run even after cancellation, so its fingerprint cannot
        // use the cancelled callback. No decoding/allocation occurs on the UI.
        publishedStamps.add (outputStamp (target));
    }
    if (abort()) { out.cancelled = true; return rollback (juce::Result::fail ("Offline rendering cancelled.")); }
    return finish (juce::Result::ok());
}

juce::Result requireFlStudioClosed()
{
#if JUCE_WINDOWS
    const auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return juce::Result::fail ("Cannot check whether FL Studio is running.");
    struct Close { HANDLE value; ~Close() { CloseHandle (value); } } close { snapshot };
    PROCESSENTRY32W entry {}; entry.dwSize = sizeof (entry);
    if (! Process32FirstW (snapshot, &entry)) return juce::Result::fail ("Cannot check whether FL Studio is running.");
    do
    {
        const juce::String name (entry.szExeFile);
        if (name.equalsIgnoreCase ("FL64.exe") || name.equalsIgnoreCase ("FL.exe")
            || name.equalsIgnoreCase ("FL64 (scaled).exe"))
            return juce::Result::fail ("Save your work and close FL Studio before starting offline rendering.");
    } while (Process32NextW (snapshot, &entry));
    return juce::Result::ok();
#else
    return juce::Result::fail ("FL Studio process detection is not supported on this platform.");
#endif
}
}
