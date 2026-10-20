#include "ProjectStatistics.h"
#include <algorithm>
#include <map>
#include <set>

namespace ProjectStatistics
{
juce::String pathIdentity (const juce::String& path)
{
    auto result = path.replaceCharacter ('\\', '/');
   #if JUCE_WINDOWS
    result = result.toLowerCase();
   #endif
    return result;
}
namespace
{
constexpr juce::int64 maxProjectBytes = 256 * 1024 * 1024;
bool cancelled (const Cancel& cancel) { return cancel && cancel(); }
void unknownPlugin (Inventory& out, const juce::String& name)
{
    ++out.unclassifiedPlugins;
    if (out.warnings.size() < 10) out.warnings.addIfNotAlreadyThere ("Unclassified plugin: " + name);
}
void sample (Inventory& out, juce::String path, const juce::File& project, const juce::File& resolved = {})
{
    path = path.trim().replaceCharacter ('\\', '/');
    if (path.isEmpty()) return;
    // A saved path remains an identity even when its volume/sample is unavailable.
    auto file = resolved;
    const bool foreignAbsolute = path.startsWithChar ('/') || (path.length() > 2 && path[1] == ':');
   #if JUCE_WINDOWS
    const bool foreignPlatform = path.startsWithChar ('/') && ! path.startsWith ("//");
   #else
    const bool foreignPlatform = path.length() > 2 && path[1] == ':';
   #endif
    if (file == juce::File{} && ! foreignPlatform && ! path.containsChar ('%') && (! foreignAbsolute || juce::File::isAbsolutePath (path)))
        file = juce::File::isAbsolutePath (path) ? juce::File (path) : project.getParentDirectory().getChildFile (path);
    Item item; item.path = file != juce::File{} ? file.getFullPathName() : path;
    item.identity = pathIdentity (item.path); item.name = path.fromLastOccurrenceOf ("/", false, false);
    item.available = file != juce::File{} && file.existsAsFile();
    out.items[0].push_back (std::move (item));
}
void plugin (Inventory& out, Category category, const juce::String& name, const juce::String& family)
{
    if (name.trim().isEmpty()) { unknownPlugin (out, "name unavailable"); return; }
    Item item; item.name = name.trim(); item.identity = family + ":" + item.name.toLowerCase();
    out.items[(size_t) category].push_back (std::move (item));
}
juce::String flText (juce::InputStream& input, int size, bool unicode)
{
    if (size < 0 || size > 65536) return {};
    juce::MemoryBlock bytes ((size_t) size + 2, true);
    if (input.read (bytes.getData(), size) != size) return {};
    const auto* p = (const uint8_t*) bytes.getData();
    if (! unicode) return juce::String::fromUTF8 ((const char*) p, size);
    juce::String result;
    for (int i = 0; i + 1 < size; i += 2)
    {
        const auto c = juce::ByteOrder::littleEndianShort (p + i);
        if (c == 0) break;
        if (c >= 0xd800 && c <= 0xdbff && i + 3 < size)
        {
            const auto low = juce::ByteOrder::littleEndianShort (p + i + 2);
            if (low >= 0xdc00 && low <= 0xdfff)
            { result += juce::String::charToString (0x10000 + ((c - 0xd800) << 10) + low - 0xdc00); i += 2; continue; }
        }
        result += juce::String::charToString (c);
    }
    return result;
}
Inventory readFl (const juce::File& file, const Cancel& cancel)
{
    Inventory out;
    auto fail = [&] (const char* reason) { out.items = {}; out.result = juce::Result::fail (reason); return out; };
    juce::FileInputStream source (file);
    juce::BufferedInputStream input (&source, 65536, false);
    const auto length = input.getTotalLength();
    if (! source.openedOk() || length < 22 || length > maxProjectBytes) return fail ("Unreadable or oversized FL Studio project");
    char header[22] {};
    if (input.read (header, 22) != 22 || std::memcmp (header, "FLhd", 4) != 0
        || juce::ByteOrder::littleEndianInt (header + 4) != 6 || juce::ByteOrder::littleEndianShort (header + 8) != 0
        || std::memcmp (header + 14, "FLdt", 4) != 0 || juce::ByteOrder::littleEndianInt (header + 18) != length - 22)
        return fail ("Invalid FL Studio project header");
    bool unicode = false, mixer = false, haveChannel = false; int major = 0, type = -1, events = 0;
    juce::String currentPlugin, wrapperName, samplePath, dataPath;
    juce::Array<juce::File> factories;
    for (const auto* variable : { "ProgramFiles", "ProgramFiles(x86)" })
    {
        const auto root = juce::SystemStats::getEnvironmentVariable (variable, {});
        if (root.isEmpty()) continue;
        const auto installs = juce::File (root).getChildFile ("Image-Line").findChildFiles (juce::File::findDirectories, false, "FL Studio*");
        for (int i = 0; i < juce::jmin (64, installs.size()); ++i) factories.add (installs[i].getChildFile ("Data"));
    }
    auto flush = [&]
    {
        if (! mixer && haveChannel && type != 3 && type != 5 && samplePath.isNotEmpty())
            sample (out, samplePath, file, UsedSounds::resolveSample (samplePath, file, dataPath, factories));
        if ((mixer || (haveChannel && type != 3 && type != 5)) && currentPlugin.isNotEmpty())
        {
            const bool wrapper = currentPlugin.containsIgnoreCase ("fruity wrapper");
            if (wrapper && wrapperName.isEmpty()) unknownPlugin (out, "FL Studio wrapper name unavailable");
            else plugin (out, mixer ? Category::Effects : Category::Instruments,
                         wrapper ? wrapperName : currentPlugin, wrapper ? "external" : "FL Studio");
        }
        currentPlugin.clear(); wrapperName.clear(); samplePath.clear(); type = -1;
    };
    while (input.getPosition() < length)
    {
        if (++events > 2000000 || cancelled (cancel)) return fail ("Cancelled or too many FL Studio events");
        const auto id = (uint8_t) input.readByte();
        juce::int64 n = id < 64 ? 1 : id < 128 ? 2 : id < 192 ? 4 : 0;
        if (id == 172 && major >= 25) n = 3;
        if (id >= 192)
        {
            bool ended = false;
            for (int shift = 0; shift <= 28 && input.getPosition() < length; shift += 7)
            { const auto b = (uint8_t) input.readByte(); n |= (juce::int64) (b & 127) << shift; if ((b & 128) == 0) { ended = true; break; } }
            if (! ended) return fail ("Invalid FL Studio event length");
        }
        const auto end = input.getPosition() + n;
        if (end > length) return fail ("Truncated FL Studio event");
        if (id == 199) { major = flText (input, (int) n, false).getIntValue(); unicode = major >= 12; }
        else if (id == 202) dataPath = flText (input, (int) n, unicode);
        else if (id == 236) { flush(); mixer = true; haveChannel = false; }
        else if (id == 64 && ! mixer) { flush(); haveChannel = true; }
        else if (id == 201)
        {
            if (mixer) flush();
            currentPlugin = flText (input, (int) n, unicode); wrapperName.clear();
        }
        else if (id == 21 && ! mixer) type = (uint8_t) input.readByte();
        else if (id == 196 && ! mixer) samplePath = flText (input, (int) n, unicode);
        else if (id == 213 && currentPlugin.containsIgnoreCase ("fruity wrapper") && n >= 4)
        {
            input.skipNextBytes (4);
            while (input.getPosition() + 12 <= end)
            {
                const auto field = input.readInt(); const auto fieldSize = input.readInt64();
                if (fieldSize < 0 || fieldSize > end - input.getPosition()) return fail ("Invalid FL Studio wrapper metadata");
                const auto next = input.getPosition() + fieldSize;
                if (field == 54 && fieldSize <= 65536) wrapperName = flText (input, (int) fieldSize, false);
                input.setPosition (next); // Skip opaque plugin state without allocating it.
            }
        }
        input.setPosition (end);
    }
    flush();
    if (major < 10) return fail ("FL Studio 10 or later is required");
    return out;
}

// Read one small XML tag at a time. Hex plugin buffers and note bodies are never retained.
class XmlTags
{
public:
    XmlTags (juce::InputStream& source, const Cancel& callback) : input (source), cancel (callback) {}
    bool next (juce::String& tag)
    {
        std::string text; int c = 0;
        while ((c = byte()) >= 0 && c != '<') {}
        if (c < 0) return false;
        text += '<'; char quote = 0;
        while ((c = byte()) >= 0)
        {
            text += (char) c;
            if (text.size() > 65536) { error = "Oversized Ableton XML tag"; return false; }
            if (text.rfind ("<!--", 0) == 0)
            { if (text.size() >= 7 && text.compare (text.size() - 3, 3, "-->") == 0) { tag = {}; return true; } continue; }
            if (quote != 0) { if (c == quote) quote = 0; }
            else if (c == '\'' || c == '"') quote = (char) c;
            else if (c == '>') { tag = juce::String::fromUTF8 (text.data(), (int) text.size()); return true; }
        }
        error = "Truncated Ableton XML tag"; return false;
    }
    juce::String error;
private:
    int byte()
    {
        if (offset == size)
        {
            if (cancelled (cancel)) { error = "Cancelled"; return -1; }
            size = (int) input.read (buffer, sizeof (buffer)); offset = 0;
            total += size;
            if (total > 512 * 1024 * 1024) { error = "Ableton XML exceeds the 512 MB streaming limit"; return -1; }
            if (size <= 0) return -1;
        }
        return (uint8_t) buffer[offset++];
    }
    juce::InputStream& input; const Cancel& cancel;
    char buffer[65536] {}; int offset = 0, size = 0; juce::int64 total = 0;
};
Inventory readAbleton (const juce::File& file, const Cancel& cancel)
{
    Inventory out;
    auto fail = [&] (const juce::String& reason) { out.items = {}; out.result = juce::Result::fail (reason); return out; };
    juce::FileInputStream source (file);
    if (! source.openedOk() || source.getTotalLength() > maxProjectBytes) return fail ("Unreadable or oversized Ableton project");
    const auto a = (uint8_t) source.readByte(), b = (uint8_t) source.readByte(); source.setPosition (0);
    std::unique_ptr<juce::GZIPDecompressorInputStream> gzip;
    if (a == 0x1f && b == 0x8b) gzip = std::make_unique<juce::GZIPDecompressorInputStream> (&source, false, juce::GZIPDecompressorInputStream::gzipFormat);
    XmlTags xml (gzip ? static_cast<juce::InputStream&> (*gzip) : static_cast<juce::InputStream&> (source), cancel);
    std::vector<juce::String> stack;
    juce::String tag, absolute, relative, pluginName, descriptor, pluginFamily;
    int pluginDepth = 0, sampleDepth = 0, pluginType = 0, branchType = 0, events = 0; bool ableton = false, liveSet = false;
    const juce::StringArray instruments { "UltraAnalog", "Collision", "InstrumentImpulse", "DrumGroupDevice", "InstrumentGroupDevice",
        "Operator", "OriginalSimpler", "MultiSampler", "LoungeLizard", "StringStudio", "InstrumentVector", "InstrumentMeld", "Drift", "DrumCell", "Electric", "Sampler", "Simpler" };
    const juce::StringArray racks { "DrumGroupDevice", "InstrumentGroupDevice", "AudioEffectGroupDevice", "MidiEffectGroupDevice" };
    const juce::StringArray nativeEffects { "Amp", "AutoFilter", "AutoPan", "AutoPan2", "AutoPanTremolo", "AutoShift", "BeatRepeat", "Cabinet", "ChannelEq", "Chorus", "Chorus2", "Compressor2", "Corpus", "Delay", "DynamicTube", "Echo", "Eq8", "FilterEQ3", "Erosion", "ExternalAudioEffect", "FilterDelay", "Flanger", "FrequencyShifter", "Gate", "GlueCompressor", "GrainDelay", "HybridReverb", "Limiter", "Looper", "MultibandDynamics", "Overdrive", "Pedal", "Phaser", "PhaserNew", "PingPongDelay", "Redux", "Redux2", "Resonator", "Reverb", "Roar", "Saturator", "Shifter", "SimpleDelay", "SpectralResonator", "SpectralTime", "SpectrumAnalyzer", "StereoGain", "Tuner", "Vinyl", "Vocoder", "AlignDelay", "MidiArpeggiator", "MidiChord", "MidiNoteLength", "MidiPitcher", "MidiRandom", "MidiScale", "MidiVelocity", "MidiCCControl", "MidiNoteEcho", "MidiShaper", "MidiStrum", "MidiQuantize", "MidiRecombine", "MidiTimeWarp" };
    const std::map<juce::String, juce::String> displayNames {
        { "UltraAnalog", "Analog" }, { "InstrumentImpulse", "Impulse" }, { "OriginalSimpler", "Simpler" }, { "MultiSampler", "Sampler" },
        { "LoungeLizard", "Electric" }, { "StringStudio", "Tension" }, { "InstrumentVector", "Wavetable" }, { "InstrumentMeld", "Meld" },
        { "StereoGain", "Utility" }, { "Eq8", "EQ Eight" }, { "FilterEQ3", "EQ Three" }, { "Compressor2", "Compressor" },
        { "AutoPan", "Auto Pan" }, { "AutoPan2", "Auto Pan" }, { "AutoFilter", "Auto Filter" }, { "AutoFilter2", "Auto Filter" }, { "Chorus2", "Chorus-Ensemble" }, { "PhaserNew", "Phaser-Flanger" },
        { "SpectrumAnalyzer", "Spectrum" }, { "MidiPitcher", "Pitch" }, { "MidiVelocity", "Velocity" }, { "MidiScale", "Scale" },
        { "MidiArpeggiator", "Arpeggiator" }, { "MidiChord", "Chord" }, { "MidiNoteLength", "Note Length" }, { "MidiRandom", "Random" }
    };
    auto contains = [&] (const char* name) { return std::find (stack.begin(), stack.end(), name) != stack.end(); };
    auto close = [&] (const juce::String& name)
    {
        if (sampleDepth == (int) stack.size() && name == "SampleRef")
        {
            auto candidate = relative.isNotEmpty() ? file.getParentDirectory().getChildFile (relative) : juce::File{};
            if (candidate != juce::File{} && candidate.existsAsFile()) sample (out, candidate.getFullPathName(), file);
            else sample (out, absolute.isNotEmpty() ? absolute : relative, file);
            sampleDepth = 0;
        }
        if (pluginDepth == (int) stack.size())
        {
            if (pluginType != 1 && pluginType != 2) pluginType = branchType;
            if (pluginType == 1 || pluginType == 2)
                plugin (out, pluginType == 1 ? Category::Instruments : Category::Effects, pluginName, pluginFamily);
            else unknownPlugin (out, pluginName.isNotEmpty() ? pluginName : "name/type unavailable");
            pluginDepth = 0;
        }
    };
    while (xml.next (tag))
    {
        if (++events > 2000000 || cancelled (cancel)) return fail ("Cancelled or too many Ableton XML elements");
        if (tag.isEmpty() || tag.startsWith ("<?")) continue;
        if (tag.startsWith ("<!")) return fail ("Unsupported Ableton XML declaration");
        if (tag.startsWith ("</"))
        {
            const auto name = tag.substring (2, tag.length() - 1).trim();
            if (stack.empty() || stack.back() != name) return fail ("Mismatched Ableton XML elements");
            close (name); stack.pop_back(); continue;
        }
        const bool empty = tag.endsWith ("/>");
        auto element = juce::XmlDocument::parse (empty ? tag : tag.dropLastCharacters (1) + "/>");
        if (! element) return fail ("Invalid Ableton XML attributes");
        const auto name = element->getTagName(), value = element->getStringAttribute ("Value");
        const auto parent = stack.empty() ? juce::String{} : stack.back();
        if (stack.empty()) { if (ableton || name != "Ableton") return fail ("Not an Ableton Live Set"); ableton = true; }
        if (name == "LiveSet" && parent == "Ableton") liveSet = true;
        if (stack.size() >= 256) return fail ("Ableton XML nesting exceeds the safe limit");
        stack.push_back (name);
        if (name == "SampleRef" && ! contains ("SourceContext") && ! contains ("PresetRef"))
        { sampleDepth = (int) stack.size(); absolute.clear(); relative.clear(); }
        if (sampleDepth > 0 && parent == "FileRef")
        { if (name == "Path") absolute = value; if (name == "RelativePath") relative = value; }
        if ((name == "PluginDevice" || name == "AuPluginDevice") && parent == "Devices" && pluginDepth == 0)
        { pluginDepth = (int) stack.size(); pluginName.clear(); descriptor.clear(); pluginFamily = "external"; pluginType = 0; branchType = 0; }
        if ((name == "MxDeviceInstrument" || name == "MxDeviceAudioEffect" || name == "MxDeviceMidiEffect") && parent == "Devices" && pluginDepth == 0)
        {
            pluginDepth = (int) stack.size(); pluginName.clear(); descriptor.clear(); pluginFamily = "Max for Live";
            pluginType = name == "MxDeviceInstrument" ? 1 : 2; branchType = 0;
        }
        if (pluginDepth > 0)
        {
            if (parent == "PluginDesc") descriptor = name;
            if (parent == descriptor)
            {
                if (name == "PlugName" || name == "Name") pluginName = value;
                if (name == "DeviceType" && descriptor == "Vst3PluginInfo") pluginType = value.getIntValue();
                // Live's VST2 flags are not the VST SDK's effFlags: bit zero marks instruments.
                // Real Live 12 sets: Kontakt=1287, Serum=9477; EchoBoy=1280, OTT=768.
                if (name == "Flags" && descriptor == "VstPluginInfo") pluginType = (value.getLargeIntValue() & 1) != 0 ? 1 : 2;
                if (name == "ComponentType" && descriptor == "AuPluginInfo")
                { if (value == "1635085685") pluginType = 1; else if (value == "1635085670" || value == "1635085688" || value == "1635085677") pluginType = 2; }
            }
            if (name == "BranchDeviceId")
            { if (value.contains (":instr:")) branchType = 1; else if (value.contains (":audiofx:") || value.contains (":midifx:")) branchType = 2; }
            if (pluginFamily == "Max for Live" && parent == "FileRef" && (contains ("MxPatchRef") || contains ("MxDPatchRef"))
                && (name == "Path" || name == "RelativePath") && value.endsWithIgnoreCase (".amxd"))
                pluginName = value.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false).dropLastCharacters (5);
        }
        else if (parent == "Devices" && ! racks.contains (name))
        {
            // Native device identities are stable XML types, independent of user-renamed tracks/devices.
            const auto display = displayNames.find (name);
            const auto label = display != displayNames.end() ? display->second : name;
            if (instruments.contains (name)) plugin (out, Category::Instruments, label, "Ableton Live");
            else if (nativeEffects.contains (name) || name == "AutoFilter2") plugin (out, Category::Effects, label, "Ableton Live");
            else unknownPlugin (out, name);
        }
        if (empty) { close (name); stack.pop_back(); }
    }
    if (xml.error.isNotEmpty()) return fail (xml.error);
    if (! stack.empty() || ! ableton || ! liveSet) return fail ("Incomplete Ableton Live Set");
    return out;
}
}
Inventory readProject (const juce::File& file, const Cancel& cancel)
{
    const auto stamp = file.getLastModificationTime(); const auto size = file.getSize();
    auto result = file.hasFileExtension ("flp") ? readFl (file, cancel) : readAbleton (file, cancel);
    if (file.getLastModificationTime() != stamp || file.getSize() != size)
    { result.items = {}; result.result = juce::Result::fail ("Project changed while reading; save and refresh Statistics"); }
    return result;
}
Report collect (const juce::Array<ProjectEntry>& entries, const Cancel& cancel)
{
    Report out;
    std::set<juce::String> projects;
    std::array<std::map<juce::String, Item>, 3> counts;
    for (const auto& entry : entries)
    {
        if (cancelled (cancel)) { out.error = "Cancelled"; return out; }
        if (entry.trashed || entry.pending) continue;
        if (entry.projectPath.isEmpty()) { ++out.unlinked; continue; }
        const juce::File file (entry.projectPath);
        if (! projects.insert (pathIdentity (file.getFullPathName())).second) continue;
        if (! file.hasFileExtension ("flp;als")) { ++out.excluded; if (out.details.size() < 20) out.details.add (file.getFileName() + ": format not supported"); continue; }
        auto inventory = readProject (file, cancel);
        if (inventory.result.failed())
        { ++out.failed; if (out.details.size() < 20) out.details.add (file.getFileName() + ": " + inventory.result.getErrorMessage()); continue; }
        ++out.read; out.unclassifiedPlugins += inventory.unclassifiedPlugins;
        for (const auto& warning : inventory.warnings)
            if (out.details.size() < 20) out.details.add (file.getFileName() + ": " + warning);
        for (size_t category = 0; category < counts.size(); ++category)
        {
            std::set<juce::String> seen;
            for (auto& item : inventory.items[category])
            {
                if (! seen.insert (item.identity).second) continue;
                auto& target = counts[category][item.identity];
                if (target.projects == 0) { target = std::move (item); target.projects = 1; }
                else { ++target.projects; if (item.available) { target.path = item.path; target.available = true; } }
            }
        }
    }
    for (size_t category = 0; category < counts.size(); ++category)
    {
        auto& items = out.items[category]; items.reserve (counts[category].size());
        for (auto& pair : counts[category]) items.push_back (std::move (pair.second));
        std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
        { if (a.projects != b.projects) return a.projects > b.projects; const auto cmp = a.name.compareIgnoreCase (b.name); return cmp != 0 ? cmp < 0 : a.identity < b.identity; });
        // Keep the complete ranking so hidden entries can be replaced below the visible cap.
        items.shrink_to_fit();
    }
    return out;
}
size_t Report::metadataBytes() const
{
    size_t bytes = sizeof (*this);
    const auto stringBytes = [] (const juce::String& s) { return s.getNumBytesAsUTF8() + 1 + 32; };
    for (const auto& category : items)
    {
        bytes += category.capacity() * sizeof (Item);
        for (const auto& item : category) bytes += stringBytes (item.name) + stringBytes (item.identity) + stringBytes (item.path);
    }
    for (const auto& detail : details) bytes += sizeof (juce::String) + stringBytes (detail);
    return bytes;
}
namespace
{
const char* preferenceKey (Category category)
{
    switch (category) { case Category::Samples: return "samples"; case Category::Instruments: return "instruments"; default: return "effects"; }
}
}
juce::Result readHiddenEntries (HiddenEntries& entries)
{
    entries = {};
    const auto file = LibraryStore::configFile();
    if (! file.existsAsFile()) return juce::Result::ok();
    const auto config = LibraryStore::readJson (file);
    if (config.getDynamicObject() == nullptr) return juce::Result::fail ("Cannot read saved hidden entries. Check Settings and refresh Statistics.");
    const auto hidden = config.getProperty ("statisticsHidden", {});
    if (hidden.isVoid()) return juce::Result::ok();
    if (hidden.getDynamicObject() == nullptr) return juce::Result::fail ("Invalid saved hidden entries. Statistics has not changed them.");
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto values = hidden.getProperty (preferenceKey ((Category) i), {});
        if (values.isVoid()) continue;
        const auto* array = values.getArray();
        if (! array) return juce::Result::fail ("Invalid saved hidden-entry list");
        for (const auto& value : *array)
        {
            if (! value.isString()) return juce::Result::fail ("Invalid saved hidden-entry identity");
            entries[i].insert (value.toString());
        }
    }
    return juce::Result::ok();
}
juce::Result saveHiddenEntry (Category category, const juce::String& identity, bool hide)
{
    return LibraryStore::updateJson (LibraryStore::configFile(), [category, identity, hide] (juce::DynamicObject& config)
    {
        auto hidden = config.getProperty ("statisticsHidden").clone();
        if (hidden.isVoid()) hidden = juce::var (new juce::DynamicObject());
        auto* object = hidden.getDynamicObject();
        if (! object) return juce::Result::fail ("Cannot update invalid hidden-entry preferences");
        const auto key = preferenceKey (category);
        auto values = object->getProperty (key).clone();
        if (values.isVoid()) values = juce::Array<juce::var>();
        auto* array = values.getArray();
        if (! array) return juce::Result::fail ("Cannot update invalid hidden-entry list");
        array->removeAllInstancesOf (identity);
        if (hide) array->add (identity);
        object->setProperty (key, values); config.setProperty ("statisticsHidden", hidden);
        return juce::Result::ok();
    }, true);
}
}
