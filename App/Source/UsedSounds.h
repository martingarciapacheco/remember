#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>

// Saved-project readers and all file/render work run on workers, never paint/audio callbacks.
namespace UsedSounds
{
enum class Kind { Sample, Midi, Generator };
enum class Voice { Piano = 1, Saw, Square, Sine };
struct Note
{
    double beat = 0, length = 0;
    int key = 60, velocity = 100, channel = 1;
};
struct Sound
{
    Kind kind = Kind::Sample;
    juce::String name, channelName, instrument, patternName, sourcePath, exportError;
    juce::String presetName; // Explicit metadata from the saved state; empty means unavailable.
    std::shared_ptr<const juce::MemoryBlock> presetData; // FL Studio .fst; generators only.
    juce::File sample;
    int channelId = -1, patternId = 0, rootNote = 60;
    bool inPlaylist = false;
    std::vector<Note> notes;
};
struct Inventory
{
    juce::Result result = juce::Result::ok();
    juce::String projectName, version;
    double bpm = 0;
    int ppq = 96;
    bool playlistKnown = false;
    std::vector<Sound> sounds;
    juce::StringArray warnings;
};
using Cancel = std::function<bool()>;
Inventory readProject (const juce::File&, const Cancel& = {});
// Explicit root candidates allow resolution without an unbounded disk search.
juce::File resolveSample (const juce::String&, const juce::File& project, const juce::String& dataPath,
                          const juce::Array<juce::File>& factoryRoots = {});
Voice voiceFromId (int);
juce::String voiceName (Voice);
juce::String exportName (const juce::String& project, const Sound&, double bpm, const juce::String& key);
juce::Result writeMidi (const Sound&, double bpm, const juce::File& target);
juce::Result writePreset (const Sound&, const juce::File& target);
// Streaming synthesis on the playback worker; borrowed notes and bounded buffers.
std::unique_ptr<juce::AudioFormatReader> createPreviewReader (const Sound&,
    std::shared_ptr<const std::vector<Note>>, double bpm, Voice);
}
