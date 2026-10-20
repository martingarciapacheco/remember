#include "UsedSounds.h"
#include "UsedSoundPresets.h"
#include <map>
#include <set>
#include <algorithm>

namespace UsedSounds
{
namespace
{
uint16_t u16 (const uint8_t* p) { return juce::ByteOrder::littleEndianShort (p); }
uint32_t u32 (const uint8_t* p) { return juce::ByteOrder::littleEndianInt (p); }
juce::String text (const uint8_t* p, size_t n, bool unicode)
{
    if (n > 65536) return {};
    if (! unicode) return juce::String::fromUTF8 ((const char*) p, (int) n);
    juce::String result;
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        const auto c = u16 (p + i);
        if (c == 0) break;
        // Decode surrogate pairs without depending on native wchar_t width.
        if (c >= 0xd800 && c <= 0xdbff && i + 3 < n)
        {
            const auto next = u16 (p + i + 2);
            if (next >= 0xdc00 && next <= 0xdfff)
            { result += juce::String::charToString (0x10000 + ((c - 0xd800) << 10) + next - 0xdc00); i += 2; continue; }
        }
        result += juce::String::charToString (c);
    }
    return result;
}
struct Channel
{
    juce::String name, plugin, vstName, samplePath;
    const uint8_t* state = nullptr; // Borrowed from readProject's file buffer only.
    size_t stateSize = 0;
    std::vector<std::pair<const uint8_t*, size_t>> presetEvents;
    bool hasPluginState = false;
    int type = -1, root = 60;
};
juce::Array<juce::File> factoryDirectories()
{
    juce::Array<juce::File> roots;
    for (const auto& variable : { "ProgramFiles", "ProgramFiles(x86)" })
    {
        const auto path = juce::SystemStats::getEnvironmentVariable (variable, {});
        if (path.isEmpty()) continue;
        juce::Array<juce::File> installs;
        juce::File (path).getChildFile ("Image-Line").findChildFiles (installs, juce::File::findDirectories, false, "FL Studio*");
        for (int i = 0; i < juce::jmin (64, installs.size()); ++i) roots.add (installs[i].getChildFile ("Data"));
    }
    return roots;
}
juce::String safePart (juce::String s)
{
    s = s.replaceCharacters ("<>:\"/\\|?*", "_________");
    for (int c = 0; c < 32; ++c) s = s.removeCharacters (juce::String::charToString ((juce::juce_wchar) c));
    s = s.trim().trimCharactersAtEnd (". ");
    return s.isEmpty() ? "Unknown" : s.substring (0, 24);
}
}

Voice voiceFromId (int id) { return (Voice) juce::jlimit (1, 4, id); }
juce::String voiceName (Voice v)
{
    switch (v) { case Voice::Saw: return "Saw"; case Voice::Square: return "Square"; case Voice::Sine: return "Sine"; default: return "Simple piano"; }
}

juce::File resolveSample (const juce::String& raw, const juce::File& project, const juce::String& dataPath,
                          const juce::Array<juce::File>& factoryRoots)
{
    auto path = raw.trim().replaceCharacter ('\\', '/');
    if (path.isEmpty()) return {};
    const auto parent = project.getParentDirectory();
    juce::Array<juce::File> candidates;
    if (path.startsWithIgnoreCase ("%FLStudioFactoryData%"))
    {
        const auto tail = path.substring (21).trimCharactersAtStart ("/");
        for (const auto& root : factoryRoots) candidates.add (root.getChildFile (tail));
    }
    else if (path.startsWithIgnoreCase ("%FLStudioUserData%"))
        candidates.add (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Image-Line/FL Studio").getChildFile (path.substring (18).trimCharactersAtStart ("/")));
    else if (! path.containsChar ('%'))
    {
        if (juce::File::isAbsolutePath (path)) candidates.add (juce::File (path));
        else candidates.add (parent.getChildFile (path));
        if (dataPath.isNotEmpty() && ! dataPath.containsChar ('%'))
        {
            const auto data = juce::File::isAbsolutePath (dataPath) ? juce::File (dataPath) : parent.getChildFile (dataPath);
            candidates.add (data.getChildFile (juce::File::isAbsolutePath (path) ? juce::File (path).getFileName() : path));
        }
    }
    // Collected projects often keep samples beside the FLP. Never guess among recursive name matches.
    const auto basename = path.fromLastOccurrenceOf ("/", false, false);
    if (basename.isNotEmpty())
    {
        candidates.add (parent.getChildFile (basename));
        candidates.add (parent.getChildFile ("Samples").getChildFile (basename));
    }
    for (const auto& candidate : candidates) if (candidate.existsAsFile()) return candidate;
    return {};
}

Inventory readProject (const juce::File& project, const Cancel& cancelled)
{
    Inventory out; out.projectName = project.getFileNameWithoutExtension();
    auto fail = [&] (const juce::String& reason) { out.sounds.clear(); out.result = juce::Result::fail (reason); return out; };
    if (! project.hasFileExtension ("flp")) return fail ("Used sounds currently supports FL Studio (.flp). Ableton support is planned.");
    juce::FileInputStream stream (project);
    if (! stream.openedOk()) return fail ("Cannot read the saved project. Check its location and access.");
    const auto size = stream.getTotalLength();
    if (size < 22 || size > 256 * 1024 * 1024) return fail ("Invalid or oversized FL Studio project (limit: 256 MB).");
    const auto stamp = project.getLastModificationTime();
    juce::MemoryBlock block ((size_t) size);
    auto* bytes = (uint8_t*) block.getData();
    for (juce::int64 pos = 0; pos < size;)
    {
        if (cancelled && cancelled()) return fail ("Cancelled");
        const int n = (int) juce::jmin<juce::int64> (65536, size - pos);
        if (stream.read (bytes + pos, n) != n) return fail ("Project changed while reading. Save it and refresh Used sounds.");
        pos += n;
    }
    if (project.getSize() != size || project.getLastModificationTime() != stamp)
        return fail ("Project changed while reading. Save it and refresh Used sounds.");
    if (std::memcmp (bytes, "FLhd", 4) != 0 || u32 (bytes + 4) != 6 || u16 (bytes + 8) != 0)
        return fail ("This file is not a supported FL Studio project.");
    out.ppq = u16 (bytes + 12);
    if (out.ppq < 1 || out.ppq > 9600) return fail ("Unsupported FL Studio timebase.");
    if (std::memcmp (bytes + 14, "FLdt", 4) != 0 || u32 (bytes + 18) != (uint64_t) size - 22)
        return fail ("Incomplete FL Studio project data. Save the project and try again.");
    std::map<int, Channel> channels;
    std::map<int, juce::String> patternNames;
    std::map<std::pair<int, int>, std::vector<Note>> patterns;
    std::set<int> usedPatterns, usedChannels;
    int channel = -1, pattern = 0; bool unicode = false, mixer = false, extendedNotes = false, playlistUnsupported = false;
    juce::String dataPath;
    size_t pos = 22, count = 0, noteCount = 0;
    const uint8_t* versionEvent = nullptr; size_t versionEventSize = 0, presetBytes = 0;
    while (pos < (size_t) size)
    {
        if ((++count & 255) == 0 && cancelled && cancelled()) return fail ("Cancelled");
        if (count > 2000000) return fail ("Too many project events.");
        const auto eventStart = pos;
        const int id = bytes[pos++];
        uint64_t length = id < 64 ? 1 : id < 128 ? 2 : id < 192 ? 4 : 0;
        // FL 25+ added a three-byte project flag in the DWORD opcode range.
        // Reading four bytes consumes the following C0 opcode and desynchronises
        // every later event (tempo, notes, channels and playlists).
        if (id == 172 && out.version.getIntValue() >= 25) length = 3;
        if (id >= 192)
        {
            bool ended = false;
            for (int shift = 0; shift <= 28 && pos < (size_t) size; shift += 7)
            {
                const auto b = bytes[pos++]; length |= (uint64_t) (b & 127) << shift;
                if ((b & 128) == 0) { ended = true; break; }
            }
            if (! ended) return fail ("Invalid FL Studio event length.");
        }
        if (length > (size_t) size - pos) return fail ("Truncated FL Studio event.");
        const auto* p = bytes + pos; const auto n = (size_t) length; pos += n;
        if (id == 199)
        {
            out.version = text (p, n, false);
            versionEvent = bytes + eventStart; versionEventSize = pos - eventStart;
            unicode = out.version.getIntValue() >= 12;
            if (out.version.getIntValue() < 10) return fail ("This reader requires FL Studio 10 or newer.");
        }
        else if (id == 156) out.bpm = u32 (p) / 1000.0;
        else if (id == 66 && out.bpm == 0) out.bpm = u16 (p);
        else if (id == 202) dataPath = text (p, n, unicode);
        else if (id == 236) { mixer = true; channel = -1; } // insert flags start mixer state
        else if (id == 64 && ! mixer) { channel = u16 (p); channels[channel]; }
        else if (id == 65) pattern = u16 (p);
        else if (id == 193) patternNames[pattern] = text (p, n, unicode);
        else if (id == 224)
        {
            if (n % 24 != 0) return fail ("Unsupported FL Studio note layout.");
            for (size_t i = 0; i < n; i += 24)
            {
                if (++noteCount > 500000) return fail ("Too many notes in this project (limit: 500,000).");
                const auto* note = p + i;
                const int key = u16 (note + 12), owner = u16 (note + 6);
                if (key > 127) { extendedNotes = true; continue; }
                if ((u16 (note + 4) & 8) != 0 || note[16] != 120) extendedNotes = true;
                const double duration = u32 (note + 8) == 0 ? out.ppq / 4.0 : (double) u32 (note + 8);
                patterns[{ pattern, owner }].push_back ({ u32 (note) / (double) out.ppq,
                    duration / out.ppq, key, juce::jlimit (1, 127, (int) std::lround (note[21] * 127.0 / 128.0)), (note[19] & 15) + 1 });
            }
        }
        else if (id == 233)
        {
            // FL 26 uses 88 bytes, FL 25 uses 80, FL 21-24 uses 60, older 32.
            // Divisibility alone is ambiguous (1760 fits both 80 and 88).
            const int preferred = out.version.getIntValue() >= 26 ? 88 : out.version.getIntValue() >= 25 ? 80 : out.version.getIntValue() >= 21 ? 60 : 32;
            int stride = 0;
            for (int candidate : { preferred, 88, 80, 60, 32 })
            {
                if (n % (size_t) candidate != 0) continue;
                bool valid = true;
                for (size_t offset = 0; offset < n; offset += (size_t) candidate)
                    if (u16 (p + offset + 4) != 20480) { valid = false; break; }
                if (valid) { stride = candidate; break; }
            }
            if (stride == 0) { playlistUnsupported = true; continue; }
            out.playlistKnown = true;
            for (size_t i = 0; i < n; i += (size_t) stride)
            {
                const auto base = u16 (p + i + 4), item = u16 (p + i + 6);
                if (item > base) usedPatterns.insert (item - base); else usedChannels.insert (item);
            }
        }
        else if (channel >= 0 && ! mixer)
        {
            auto& ch = channels[channel];
            if (id == 201) { ch.presetEvents.clear(); ch.hasPluginState = false; }
            if (id == 201 || id == 212 || id == 213 || id == 203 || id == 128 || id == 155)
                ch.presetEvents.emplace_back (bytes + eventStart, pos - eventStart);
            if (id == 213) ch.hasPluginState = n > 0;
            if (id == 21) ch.type = p[0];
            else if (id == 192 || id == 203) ch.name = text (p, n, unicode);
            else if (id == 196) ch.samplePath = text (p, n, unicode);
            else if (id == 201) { ch.plugin = text (p, n, unicode); ch.vstName.clear(); ch.state = nullptr; ch.stateSize = 0; }
            else if (id == 135) ch.root = juce::jlimit (0, 127, (int) u32 (p));
            else if (id == 213)
            {
                ch.state = nullptr; ch.stateSize = 0;
                if (! ch.plugin.containsIgnoreCase ("fruity wrapper"))
                { ch.state = p; ch.stateSize = n; continue; }
                if (n < 4) continue;
                // Read only the wrapper's named metadata fields; never load plugin state/code.
                size_t offset = 4;
                while (offset + 12 <= n)
                {
                    const auto field = u32 (p + offset);
                    const auto fieldSize = juce::ByteOrder::littleEndianInt64 (p + offset + 4);
                    offset += 12;
                    if (fieldSize > n - offset) { ch.state = nullptr; ch.stateSize = 0; break; }
                    if (field == 54) ch.vstName = text (p + offset, (size_t) fieldSize, false);
                    if (field == 53) { ch.state = p + offset; ch.stateSize = (size_t) fieldSize; }
                    offset += (size_t) fieldSize;
                }
                if (offset != n) { ch.state = nullptr; ch.stateSize = 0; }
            }
        }
    }
    const auto factories = factoryDirectories();
    for (const auto& pair : patterns)
    {
        const auto patternId = pair.first.first, channelId = pair.first.second;
        const auto found = channels.find (channelId);
        if (found == channels.end()) return fail ("A pattern refers to an unknown channel. Unsupported project layout.");
        const auto& ch = found->second;
        if (ch.type == 5) continue;
        Sound sound; sound.kind = Kind::Midi; sound.channelId = channelId; sound.patternId = patternId;
        sound.channelName = ch.name.isNotEmpty() ? ch.name : "Channel " + juce::String (channelId + 1);
        sound.patternName = patternNames[patternId].isNotEmpty() ? patternNames[patternId] : "Pattern " + juce::String (patternId);
        sound.name = sound.channelName + " / " + sound.patternName;
        sound.instrument = ch.vstName.isNotEmpty() ? ch.vstName : ch.plugin;
        sound.sourcePath = ch.samplePath; sound.rootNote = ch.root;
        sound.sample = resolveSample (ch.samplePath, project, dataPath, factories);
        sound.notes = pair.second; sound.inPlaylist = usedPatterns.count (patternId) > 0;
        if (sound.inPlaylist) usedChannels.insert (channelId);
        out.sounds.push_back (std::move (sound));
    }
    for (const auto& pair : channels)
    {
        if (cancelled && cancelled()) return fail ("Cancelled");
        const auto& ch = pair.second;
        if (ch.type == 5 || ch.type == 3) continue;
        Sound sound; sound.channelId = pair.first;
        sound.channelName = ch.name.isNotEmpty() ? ch.name : "Channel " + juce::String (pair.first + 1);
        sound.inPlaylist = usedChannels.count (pair.first) > 0;
        if (ch.samplePath.isNotEmpty())
        {
            sound.kind = Kind::Sample; sound.sourcePath = ch.samplePath;
            sound.name = ch.samplePath.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
            sound.sample = resolveSample (ch.samplePath, project, dataPath, factories);
            out.sounds.push_back (sound);
        }
        if (ch.plugin.isNotEmpty())
        {
            sound.kind = Kind::Generator; sound.instrument = ch.vstName.isNotEmpty() ? ch.vstName : ch.plugin;
            sound.name = sound.instrument; sound.sample = {}; sound.sourcePath.clear();
            sound.presetName = Presets::read (sound.instrument, ch.state, ch.stateSize, ch.plugin.containsIgnoreCase ("fruity wrapper"));
            size_t dataSize = versionEventSize;
            for (const auto& event : ch.presetEvents) dataSize += event.second;
            if (ch.hasPluginState && versionEvent != nullptr && dataSize <= 64 * 1024 * 1024
                && presetBytes + dataSize <= 128 * 1024 * 1024)
            {
                // Plugin preset (0x30), as used by FL Studio's Save preset as.
                // Preserve encoded plugin/wrapper events byte for byte, including
                // opaque state. Never include another channel, MIDI or mixer data.
                auto preset = std::make_shared<juce::MemoryBlock>();
                juce::MemoryOutputStream output (*preset, false);
                output.write ("FLhd", 4); output.writeInt (6); output.writeShort (0x30);
                output.writeShort (1); output.writeShort ((short) out.ppq);
                output.write ("FLdt", 4); output.writeInt ((int) dataSize);
                output.write (versionEvent, versionEventSize);
                for (const auto& event : ch.presetEvents) output.write (event.first, event.second);
                output.flush(); sound.presetData = std::move (preset); presetBytes += dataSize;
            }
            else sound.exportError = ch.hasPluginState ? "Preset export exceeds the safe size limit" : "Plugin state is not saved in this project";
            out.sounds.push_back (std::move (sound));
        }
    }
    if (playlistUnsupported) out.playlistKnown = false;
    if (! out.playlistKnown) out.warnings.add ("Playlist usage unavailable. Showing sounds loaded in the saved project.");
    if (extendedNotes) out.warnings.add ("FL slide/fine pitch and notes above MIDI 127 cannot be represented exactly in standard MIDI.");
    out.warnings.add ("Basic audition: first 30 seconds; no mixer effects, automation or complex sampler mapping.");
    return out;
}

juce::String exportName (const juce::String& project, const Sound& sound, double bpm, const juce::String& key)
{
    auto name = safePart (project) + "_" + safePart (sound.channelName.isNotEmpty() ? sound.channelName : sound.name);
    if (sound.kind == Kind::Generator)
        return name + "_" + safePart (sound.presetName.isNotEmpty() ? sound.presetName : sound.instrument)
            + "_ch" + juce::String (sound.channelId + 1) + ".fst";
    if (sound.kind == Kind::Midi) name += "_" + safePart (sound.patternName) + "_ch" + juce::String (sound.channelId + 1) + "_p" + juce::String (sound.patternId);
    name += "_" + (bpm > 0 ? juce::String (bpm, bpm == std::floor (bpm) ? 0 : 2) : "Unknown") + "BPM_" + safePart (key);
    return name + (sound.kind == Kind::Midi ? ".mid" : sound.sample.getFileExtension());
}

juce::Result writePreset (const Sound& sound, const juce::File& target)
{
    if (sound.kind != Kind::Generator || ! sound.presetData || sound.presetData->getSize() == 0)
        return juce::Result::fail (sound.exportError.isNotEmpty() ? sound.exportError : "Saved plugin state is unavailable");
    juce::FileOutputStream stream (target);
    if (! stream.openedOk() || ! stream.setPosition (0) || stream.truncate().failed()
        || ! stream.write (sound.presetData->getData(), sound.presetData->getSize()))
        return juce::Result::fail ("Cannot write preset export");
    stream.flush(); return stream.getStatus();
}

juce::Result writeMidi (const Sound& sound, double bpm, const juce::File& target)
{
    if (sound.kind != Kind::Midi || sound.notes.empty() || ! std::isfinite (bpm) || bpm < 10 || bpm > 1000)
        return juce::Result::fail ("MIDI notes and a valid project tempo are required.");
    juce::MidiMessageSequence notes, tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)));
    tempo.addEvent (juce::MidiMessage::textMetaEvent (3, sound.channelName + " / " + sound.patternName));
    for (const auto& note : sound.notes)
    {
        if (! std::isfinite (note.beat) || ! std::isfinite (note.length) || note.beat < 0 || note.length <= 0
            || note.key < 0 || note.key > 127 || note.beat + note.length > 10000000)
            return juce::Result::fail ("Invalid MIDI note data.");
        auto on = juce::MidiMessage::noteOn (note.channel, note.key, (juce::uint8) note.velocity);
        auto off = juce::MidiMessage::noteOff (note.channel, note.key);
        on.setTimeStamp (note.beat * 960); off.setTimeStamp ((note.beat + note.length) * 960);
        notes.addEvent (on); notes.addEvent (off);
    }
    notes.sort(); notes.updateMatchedPairs();
    juce::MidiFile midi; midi.setTicksPerQuarterNote (960); midi.addTrack (tempo); midi.addTrack (notes);
    juce::FileOutputStream stream (target);
    if (! stream.openedOk() || ! stream.setPosition (0) || stream.truncate().failed() || ! midi.writeTo (stream))
        return juce::Result::fail ("Cannot write MIDI export.");
    stream.flush(); return stream.getStatus();
}


namespace
{
// AudioFormatReader is consumed exclusively by ReadAheadSource's worker. The
// audio callback only copies its small ring of ready blocks; no synthesis/I/O.
class PreviewReader final : public juce::AudioFormatReader
{
public:
    PreviewReader (const Sound& sound, std::shared_ptr<const std::vector<Note>> events, double bpm, Voice instrument)
        : AudioFormatReader (nullptr, "Live MIDI"), notes (std::move (events)), voice (instrument), root (sound.rootNote)
    {
        sampleRate = 44100; bitsPerSample = 32; numChannels = 2; usesFloatingPointData = true;
        if (! notes || notes->empty() || ! std::isfinite (bpm) || bpm < 10 || bpm > 1000) return;
        beatFrames = 60.0 / bpm * sampleRate;
        double end = 0;
        for (size_t i = 0; i < notes->size(); ++i)
        {
            const auto& n = (*notes)[i];
            if (! std::isfinite (n.beat) || ! std::isfinite (n.length) || n.beat < 0 || n.length <= 0 || n.key < 0 || n.key > 127) return;
            order.push_back (i); end = juce::jmax (end, (n.beat + n.length) * beatFrames);
        }
        std::stable_sort (order.begin(), order.end(), [this] (size_t a, size_t b) { return (*notes)[a].beat < (*notes)[b].beat; });
        if (sound.sourcePath.isNotEmpty())
        {
            juce::AudioFormatManager formats; formats.registerBasicFormats(); sample.reset (formats.createReaderFor (sound.sample));
            if (! sample || sample->sampleRate <= 0 || sample->lengthInSamples <= 0) return;
            // One 1025-frame interpolation window per voice, independent of file
            // size or pattern duration (about 513 KiB maximum for 64 voices).
            for (auto& v : voices) v.cache.setSize (2, 1025);
        }
        lengthInSamples = (juce::int64) juce::jmin (30.0 * sampleRate, end + 0.12 * sampleRate);
    }
    bool readSamples (int* const* output, int channels, int offset, juce::int64 start, int count) override
    {
        if (start != position)
        {
            next = 0;
            for (auto& v : voices) { v.active = false; v.page = -1; }
        }
        for (int i = 0; i < count; ++i)
        {
            const auto t = start + i;
            for (auto& v : voices) if (v.active && t >= v.end) v.active = false;
            while (next < order.size() && (*notes)[order[next]].beat * beatFrames <= (double) t)
            {
                const auto& n = (*notes)[order[next++]];
                const double end = (n.beat + n.length) * beatFrames + 0.02 * sampleRate;
                if (end <= t) continue;
                for (auto& v : voices) if (! v.active)
                {
                    v.active = true; v.note = &n; v.start = n.beat * beatFrames; v.end = end; v.page = -1;
                    v.frequency = juce::MidiMessage::getMidiNoteInHertz (n.key);
                    v.ratio = sample ? sample->sampleRate / sampleRate * std::pow (2.0, (n.key - root) / 12.0) : 0;
                    break;
                }
            }
            double mixed[2] {};
            for (auto& v : voices) if (v.active)
            {
                const auto seconds = (t - v.start) / sampleRate;
                const auto env = juce::jmin (1.0, seconds / 0.003) * juce::jlimit (0.0, 1.0, (v.end - t) / (sampleRate * 0.02));
                const auto gain = env * juce::jlimit (0, 127, v.note->velocity) / 127.0 * 0.30;
                if (sample)
                {
                    const auto source = (t - v.start) * v.ratio;
                    if (source >= sample->lengthInSamples - 1) continue;
                    const auto frame = (juce::int64) source;
                    const auto page = frame / 1024 * 1024;
                    if (v.page != page)
                    {
                        if (! sample->read (&v.cache, 0, 1025, page, true, true)) return false;
                        v.page = page;
                    }
                    const int a = (int) (frame - page); const auto fraction = source - frame;
                    for (int ch = 0; ch < 2; ++ch)
                        mixed[ch] += gain * (v.cache.getSample (ch, a) * (1 - fraction) + v.cache.getSample (ch, a + 1) * fraction);
                }
                else
                {
                    const auto phase = seconds * v.frequency * juce::MathConstants<double>::twoPi;
                    double value = std::sin (phase);
                    if (voice == Voice::Piano)
                        value = (value + 0.35 * std::sin (phase * 2) * std::exp (-seconds * 3)
                            + 0.15 * std::sin (phase * 3) * std::exp (-seconds * 5)) * std::exp (-seconds * 1.8);
                    else if (voice == Voice::Saw || voice == Voice::Square)
                    {
                        value = 0;
                        const int harmonics = juce::jmin (24, (int) (sampleRate * 0.45 / v.frequency));
                        // Harmonic recurrence avoids up to 24 transcendental
                        // calls per voice/sample while retaining band limiting.
                        const auto twiceCos = 2 * std::cos (phase);
                        double previous = 0, harmonic = std::sin (phase);
                        for (int h = 1; h <= harmonics; ++h)
                        {
                            if (voice != Voice::Square || (h & 1) != 0) value += harmonic / h;
                            const auto following = twiceCos * harmonic - previous;
                            previous = harmonic; harmonic = following;
                        }
                        value *= voice == Voice::Square ? 1.0 : 0.65;
                    }
                    mixed[0] += gain * value; mixed[1] += gain * value;
                }
            }
            for (int ch = 0; ch < channels; ++ch)
                if (output[ch]) reinterpret_cast<float*> (output[ch])[offset + i] = (float) std::tanh (mixed[juce::jmin (1, ch)]);
        }
        position = start + count;
        return true;
    }
private:
    struct Active
    {
        bool active = false;
        const Note* note = nullptr;
        double start = 0, end = 0, frequency = 0, ratio = 0;
        juce::int64 page = -1;
        juce::AudioBuffer<float> cache;
    };
    std::array<Active, 64> voices;
    std::shared_ptr<const std::vector<Note>> notes;
    std::vector<size_t> order;
    std::unique_ptr<juce::AudioFormatReader> sample;
    Voice voice;
    int root;
    size_t next = 0;
    double beatFrames = 0;
    juce::int64 position = -1;
};
}
std::unique_ptr<juce::AudioFormatReader> createPreviewReader (const Sound& sound,
    std::shared_ptr<const std::vector<Note>> notes, double bpm, Voice voice)
{
    auto reader = std::make_unique<PreviewReader> (sound, std::move (notes), bpm, voice);
    if (reader->lengthInSamples <= 0) return {};
    return reader;
}

}
