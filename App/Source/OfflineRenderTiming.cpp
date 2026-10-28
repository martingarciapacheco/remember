#include "OfflineRenderTiming.h"
#include "PreviewSelection.h"
#include "../../Shared/ProjectIdentity.h"
#include <cmath>
#include <tuple>

namespace OfflineRendering
{
namespace
{
juce::String historyKey (const juce::File& file)
{
    auto path = file.getFullPathName();
#if JUCE_WINDOWS
    path = path.toLowerCase();
#endif
    return juce::SHA256 (path.toRawUTF8(), path.getNumBytesAsUTF8()).toHexString();
}
}
ProjectTiming readProjectTiming (const juce::File& project, const Cancel& cancel)
{
    ProjectTiming out;
    auto fail = [&] (const char* reason) { out.result = juce::Result::fail (reason); out.seconds = 0; return out; };
    juce::FileInputStream source (project);
    juce::BufferedInputStream in (source, 64 * 1024);
    const auto size = in.getTotalLength();
    if (! source.openedOk() || size < 22 || size > 256 * 1024 * 1024) return fail ("Cannot read FL Studio project duration.");
    if (in.readInt() != 0x64684c46 || in.readInt() != 6) return fail ("Invalid FL Studio project header.");
    in.readShort(); in.readShort(); const auto ppq = (unsigned short) in.readShort();
    if (ppq == 0 || in.readInt() != 0x74644c46) return fail ("Invalid FL Studio project timing header.");
    const auto dataBytes = (juce::uint32) in.readInt();
    if ((juce::int64) dataBytes != size - 22) return fail ("Truncated FL Studio project data.");
    std::map<int, juce::uint64> ends;
    int version = 0, arrangement = 0, current = 0, numerator = 4, denominator = 4;
    bool haveArrangement = false;
    std::map<int, std::pair<juce::uint32, juce::uint32>> selections;
    int events = 0;
    while (in.getPosition() < size)
    {
        if (cancel && cancel()) return fail ("Offline rendering scan cancelled.");
        if (++events > 2000000) return fail ("Too many FL Studio timing events.");
        const auto id = (unsigned char) in.readByte();
        juce::uint64 count = id < 64 ? 1 : id < 128 ? 2 : id < 192 ? 4 : 0;
        if (id == 172 && version >= 25) count = 3;
        if (id >= 192)
        {
            bool end = false;
            for (int shift = 0; shift <= 28 && in.getPosition() < size; shift += 7)
            {
                const auto byte = (unsigned char) in.readByte(); count |= (juce::uint64) (byte & 127) << shift;
                if ((byte & 128) == 0) { end = true; break; }
            }
            if (! end) return fail ("Invalid FL Studio event length.");
        }
        if (count > (juce::uint64) (size - in.getPosition())) return fail ("Truncated FL Studio event.");
        const auto next = in.getPosition() + (juce::int64) count;
        if (id == 199 && count <= 256)
        {
            char value[257] {}; in.read (value, (int) count); version = juce::String::fromUTF8 (value, (int) count).getIntValue();
        }
        else if (id == 156 && count == 4) out.bpm = (juce::uint32) in.readInt() / 1000.0;
        else if (id == 66 && count == 2 && out.bpm == 0) out.bpm = (unsigned short) in.readShort();
        else if (id == 17) numerator = (unsigned char) in.readByte();
        else if (id == 18) denominator = (unsigned char) in.readByte();
        else if (id == 99) { arrangement = (unsigned short) in.readShort(); haveArrangement = true; }
        else if (id == 100) current = (unsigned short) in.readShort();
        else if (id == 217 && count >= 8)
        { const auto start = (juce::uint32) in.readInt(); selections[arrangement] = { start, (juce::uint32) in.readInt() }; }
        else if (id == 148 && haveArrangement)
        {
            const auto marker = (juce::uint32) in.readInt();
            const auto tick = marker >= 0x08000000u ? marker - 0x08000000u : marker;
            ends[arrangement] = juce::jmax (ends[arrangement], (juce::uint64) tick);
        }
        else if (id == 233)
        {
            if (version < 20 || version > 26) return fail ("Unsupported FL Studio playlist timing layout.");
            const int stride = version >= 26 ? 88 : version >= 25 ? 80 : version >= 21 ? 60 : 32;
            if (count % stride != 0) return fail ("Unsupported FL Studio playlist timing layout.");
            for (juce::uint64 offset = 0; offset < count; offset += stride)
            {
                if ((offset / stride) % 256 == 0 && cancel && cancel()) return fail ("Offline rendering scan cancelled.");
                const auto start = (juce::uint32) in.readInt();
                if ((unsigned short) in.readShort() != 20480) return fail ("Unsupported FL Studio playlist timing layout.");
                in.readShort(); const auto length = (juce::uint32) in.readInt();
                ends[arrangement] = juce::jmax (ends[arrangement], (juce::uint64) start + length);
                in.setPosition (next - (juce::int64) count + (juce::int64) offset + stride);
            }
        }
        if (! in.setPosition (next)) return fail ("Cannot read FL Studio project duration.");
    }
    if (source.getStatus().failed() || (cancel && cancel())) return fail ("Cannot read FL Studio project duration.");
    if (out.bpm < 10 || out.bpm > 999 || numerator < 1 || denominator < 1) return fail ("Saved project tempo or meter is unavailable.");
    const auto bar = (double) ppq * numerator * 4.0 / denominator;
    double ticks = juce::jmax (bar, std::ceil ((double) ends[current] / bar) * bar);
    const auto [selectionStart, selectionEnd] = selections[current];
    if (selectionEnd > selectionStart && selectionEnd < 0x7fffffffu)
    { ticks = selectionEnd - selectionStart; out.selection = true; }
    out.seconds = ticks / ppq * 60.0 / out.bpm;
    if (! std::isfinite (out.seconds) || out.seconds > 7 * 86400) return fail ("Project duration exceeds the estimation limit.");
    return out;
}

Estimate estimateBatch (const BatchPlan& plan, const FlStudioSettings& settings, const juce::var& config, const Cancel& cancel)
{
    Estimate out;
    const auto history = config.getProperty ("offlineRenderHistory", juce::var{});
    const int rate = (int) settings.values["SampleRate"];
    const int channelMode = settings.values.contains ("ChannelsBox") ? (int) settings.values["ChannelsBox"] : -1;
    const int channels = channelMode == 0 ? 2 : channelMode > 0 && channelMode <= 3 ? 1 : 0;
    const int wavBits[] { 16, 24, 32 };
    for (const auto& job : plan.jobs)
    {
        if (cancel && cancel()) break;
        const auto timing = readProjectTiming (job.project, cancel);
        if (timing.result.failed()) { ++out.unknownProjects; out.unknownSize = true; continue; }
        out.audioSeconds += timing.seconds;
        const auto previous = history.getProperty (historyKey (job.project), juce::var{});
        const auto actual = (double) previous.getProperty ("seconds", 0.0);
        const auto duration = (double) previous.getProperty ("audioSeconds", 0.0);
        if (actual > 0 && duration > 0 && previous.getProperty ("projectFileId", "").toString() == ProjectIdentity::fileId (job.project))
        { out.timeSeconds += actual * timing.seconds / duration; ++out.historicalProjects; }
        else out.timeSeconds += timing.seconds;
        for (const auto& format : job.formats)
        {
            double bytes = 0;
            if (format == "wav" || format == "flac")
            {
                const auto key = format == "wav" ? "WFBox" : "FlacBitDepthBox";
                if (rate < 8000 || rate > 768000 || channels == 0 || ! settings.values.contains (key)) { out.unknownSize = true; continue; }
                const int index = (int) settings.values[key];
                if (index < 0 || index > (format == "wav" ? 2 : 1)) { out.unknownSize = true; continue; }
                bytes = timing.seconds * rate * channels * wavBits[index] / 8.0 + 4096;
                // Uncompressed upper reference; actual FLAC depends on content.
                if (format == "flac") bytes *= 1.01;
            }
            else
            {
                const auto key = format == "ogg" ? "OggBitrateSlider" : "MP3BitrateSlider";
                if (format == "m4a" || ! settings.values.contains (key)) { out.unknownSize = true; continue; }
                const int index = (int) settings.values[key];
                if (index != 13) { out.unknownSize = true; continue; }
                bytes = timing.seconds * 320 * 1000.0 / 8 + 4096;
            }
            out.bytes += (juce::int64) std::ceil (bytes);
        }
    }
    return out;
}

juce::File bestPreview (const juce::Array<juce::File>& files)
{
    juce::File best; std::tuple<int, int, double, double> score { -1, 0, 0, 0 };
    for (const auto& file : files)
    {
        auto reader = PreviewAudio::open (file); if (! reader || reader->lengthInSamples <= 0) continue;
        const bool lossless = file.hasFileExtension ("wav;flac;aif;aiff");
        const auto candidate = std::make_tuple (lossless ? 1 : 0, lossless ? (int) reader->bitsPerSample : 0,
            reader->sampleRate, (double) file.getSize() * reader->sampleRate / reader->lengthInSamples);
        if (candidate > score) { score = candidate; best = file; }
    }
    return best;
}

juce::Result recordCompletedRender (const Command& command, const RenderResult& rendered, double audioSeconds, bool use, const Cancel& cancel)
{
    if (rendered.result.failed() || rendered.published.isEmpty()) return rendered.result;
    if (cancel && cancel()) return juce::Result::fail ("Offline rendering cancelled.");
    if (command.projectFileId.isEmpty() || ProjectIdentity::fileId (command.project) != command.projectFileId)
        return juce::Result::fail ("The associated project changed; refresh the library.");
    const auto selected = bestPreview (rendered.published);
    juce::var record (new juce::DynamicObject()); auto& value = *record.getDynamicObject();
    value.setProperty ("projectPath", command.project.getFullPathName());
    value.setProperty ("projectFileId", command.projectFileId);
    value.setProperty ("seconds", rendered.seconds); value.setProperty ("audioSeconds", audioSeconds);
    value.setProperty ("renderedAt", juce::Time::getCurrentTime().toISO8601 (true));
    value.setProperty ("previewPath", selected.getFullPathName()); value.setProperty ("useAsPreview", use);
    juce::Array<juce::var> outputs; for (const auto& file : rendered.published) outputs.add (file.getFullPathName());
    value.setProperty ("outputs", outputs);
    const auto saved = LibraryStore::updateJson (LibraryStore::configFile(), [&] (juce::DynamicObject& config)
    {
        if (cancel && cancel()) return juce::Result::fail ("Offline rendering cancelled.");
        auto history = config.getProperty ("offlineRenderHistory").clone();
        if (! history.isObject()) history = juce::var (new juce::DynamicObject());
        history.getDynamicObject()->setProperty (historyKey (command.project), record);
        config.setProperty ("offlineRenderHistory", history); return juce::Result::ok();
    }, true);
    if (saved.failed() || ! use || selected == juce::File{}) return saved;
    for (const auto& entry : ProjectScanner::scan())
    {
        if (cancel && cancel()) return juce::Result::fail ("Offline rendering cancelled.");
        if (! entry.pending && juce::File::isAbsolutePath (entry.projectPath) && juce::File (entry.projectPath) == command.project)
        {
            const auto result = PreviewSelection::select (entry, selected, false);
            if (result.failed() && result.getErrorMessage() != "Manual preview selection is protected.") return result;
        }
    }
    return saved;
}
}
