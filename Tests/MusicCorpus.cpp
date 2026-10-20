#include "../Shared/PreviewAudio.h"
#include "MusicCorpus.h"
#include "MusicFeatures.h"
#include "../Plugin/Source/PreviewAnalysis.h"
#include "../Shared/LibraryStore.h"
#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
void check (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
}
juce::var object() { return juce::var (new juce::DynamicObject()); }
void put (juce::var& target, const char* field, const juce::var& value)
{
    target.getDynamicObject()->setProperty (field, value);
}
bool numeric (const juce::var& value) { return value.isInt() || value.isInt64() || value.isDouble(); }
bool digest (const juce::String& value)
{
    return value.length() == 64 && value.containsOnly ("0123456789abcdefABCDEF");
}
juce::String fileDigest (const juce::File& file)
{
    auto source = std::make_unique<juce::FileInputStream> (file);
    check (source->openedOk(), "Cannot read corpus input: " + file.getFullPathName());
    auto* native = source.get();
    juce::BufferedInputStream buffered (source.release(), 65536, true);
    const auto hash = juce::SHA256 (buffered).toHexString();
    check (native->getStatus().wasOk() && buffered.getPosition() == native->getTotalLength(),
           "Incomplete corpus input: " + file.getFullPathName());
    return hash;
}
struct Key
{
    int root = -1;
    bool minor = false;
    juce::String name { "Unknown" };
};
Key key (juce::String value)
{
    if (value == "Unknown") return {};
    const auto fields = juce::StringArray::fromTokens (value, " ", "");
    check (fields.size() == 2 && (fields[1] == "major" || fields[1] == "minor"), "Invalid reference key: " + value);
    const auto pitch = fields[0];
    const juce::StringArray notes { "C", "D", "E", "F", "G", "A", "B" };
    constexpr int roots[] { 0, 2, 4, 5, 7, 9, 11 };
    const auto index = notes.indexOf (pitch.substring (0, 1));
    check (index >= 0 && (pitch.length() == 1 || (pitch.length() == 2 && (pitch[1] == '#' || pitch[1] == 'b'))),
           "Invalid reference pitch: " + value);
    Key result;
    result.root = (roots[index] + (pitch.endsWithChar ('#') ? 1 : pitch.endsWithChar ('b') ? -1 : 0) + 12) % 12;
    result.minor = fields[1] == "minor";
    const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    result.name = names[result.root] + (result.minor ? " minor" : " major");
    return result;
}
struct Entry
{
    juce::String id, hash;
    juce::File audio;
    bool tempoLabelled = false, keyLabelled = false;
    double bpm = 0;
    Key expectedKey;
};
double fraction (int numerator, int denominator) { return denominator > 0 ? (double) numerator / denominator : 0; }
struct Counts
{
    int known = 0, unknown = 0, covered = 0, exact = 0, halfDouble = 0, unknownCorrect = 0;
    juce::var toJson (bool tempo) const
    {
        auto result = object();
        put (result, "knownReferences", known); put (result, "unknownReferences", unknown);
        put (result, "knownEstimatesForKnownReferences", covered); put (result, "correctKnown", exact);
        put (result, "coverage", known > 0 ? juce::var (fraction (covered, known)) : juce::var{});
        put (result, "accuracy", known > 0 ? juce::var (fraction (exact, known)) : juce::var{});
        put (result, "unknownRecall", unknown > 0 ? juce::var (fraction (unknownCorrect, unknown)) : juce::var{});
        if (tempo) put (result, "halfDoubleAccuracy", known > 0 ? juce::var (fraction (halfDouble, known)) : juce::var{});
        return result;
    }
};
bool tempoMatches (double estimated, double expected)
{
    return estimated > 0 && std::abs (estimated - expected) <= expected * 0.04;
}
}

void evaluateMusicCorpus (const juce::File& manifest, const juce::File& report, bool features)
{
    check (manifest != report, "Corpus report cannot replace its manifest");
    juce::FileInputStream input (manifest);
    check (input.openedOk() && input.getTotalLength() > 0 && input.getTotalLength() <= 8 * 1024 * 1024,
           "Cannot read a non-empty corpus manifest within 8 MiB");
    juce::MemoryBlock bytes;
    input.readIntoMemoryBlock (bytes, 8 * 1024 * 1024 + 1);
    check (input.getStatus().wasOk() && (juce::int64) bytes.getSize() == input.getTotalLength(), "Incomplete corpus manifest");
    const auto definition = juce::JSON::parse (juce::String::createStringFromData (bytes.getData(), (int) bytes.getSize()));
    check (definition.getDynamicObject() != nullptr && definition.getProperty ("schemaVersion", 0).isInt()
           && (int) definition.getProperty ("schemaVersion", 0) == 1, "Unsupported corpus manifest schema");
    const auto listed = definition.getProperty ("entries", juce::var{});
    check (listed.isArray() && listed.size() > 0 && listed.size() <= 10000, "Corpus requires 1-10000 entries");
    std::vector<Entry> entries;
    std::set<juce::String> ids;
    Counts tempos, keys;
    for (const auto& item : *listed.getArray())
    {
        check (item.getDynamicObject() != nullptr, "Corpus entry must be an object");
        Entry entry;
        entry.id = item.getProperty ("id", "").toString();
        const auto path = item.getProperty ("file", "").toString();
        entry.hash = item.getProperty ("sha256", "").toString();
        check (entry.id.isNotEmpty() && ids.insert (entry.id).second && path.isNotEmpty() && digest (entry.hash),
               "Corpus entry needs a unique ID, audio path and SHA256");
        entry.audio = manifest.getParentDirectory().getChildFile (path);
        check (entry.audio != report, "Corpus report cannot replace input audio");
        const auto referenceBpm = item.getProperty ("referenceBpm", juce::var{});
        const auto referenceKey = item.getProperty ("referenceKey", juce::var{});
        entry.tempoLabelled = ! referenceBpm.isVoid(); entry.keyLabelled = ! referenceKey.isVoid();
        check (entry.tempoLabelled || entry.keyLabelled, "Corpus entry has no reference annotation");
        if (entry.tempoLabelled)
        {
            check (numeric (referenceBpm) && std::isfinite ((double) referenceBpm)
                   && (double) referenceBpm >= 0 && (double) referenceBpm <= 400, "Invalid reference BPM");
            entry.bpm = (double) referenceBpm;
            (entry.bpm > 0 ? tempos.known : tempos.unknown)++;
        }
        if (entry.keyLabelled)
        {
            check (referenceKey.isString(), "Invalid reference key type");
            entry.expectedKey = key (referenceKey.toString());
            (entry.expectedKey.root >= 0 ? keys.known : keys.unknown)++;
        }
        entries.push_back (std::move (entry));
    }
    juce::AudioFormatManager formats; PreviewAudio::registerFormats (formats);
    juce::Array<juce::var> observations;
    std::vector<double> durations;
    int errors = 0;
    const auto started = juce::Time::getCurrentTime().toISO8601 (true);
    for (const auto& entry : entries)
    {
        auto observation = object(); put (observation, "id", entry.id);
        put (observation, "file", entry.audio.getFullPathName()); put (observation, "sha256", entry.hash.toLowerCase());
        if (entry.tempoLabelled) put (observation, "referenceBpm", entry.bpm);
        if (entry.keyLabelled) put (observation, "referenceKey", entry.expectedKey.name);
        try
        {
            const auto hashing = juce::Time::getMillisecondCounterHiRes();
            check (fileDigest (entry.audio).equalsIgnoreCase (entry.hash), "Audio SHA256 differs from the corpus manifest");
            put (observation, "hashMs", juce::Time::getMillisecondCounterHiRes() - hashing);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (entry.audio));
            check (reader != nullptr && reader->numChannels >= 1 && reader->numChannels <= 2
                   && std::isfinite (reader->sampleRate) && reader->sampleRate > 0 && reader->lengthInSamples > 0
                   && reader->lengthInSamples <= std::numeric_limits<int>::max()
                   && reader->lengthInSamples / reader->sampleRate <= 180, "Unreadable, unsupported or over-180-second corpus audio");
            PreviewAnalysis::Source source;
            source.pcm = entry.audio; source.revision = entry.id;
            source.sampleRate = reader->sampleRate; source.samples = (int) reader->lengthInSamples;
            source.channels = (int) reader->numChannels; source.changedPrefix = source.samples;
            put (observation, "sampleRate", source.sampleRate); put (observation, "channels", source.channels);
            put (observation, "durationSeconds", source.samples / source.sampleRate);
            reader.reset();
            PreviewAnalysis analysis (0); // Evaluate a complete first capture with no feature reuse or host BPM.
            const auto begin = juce::Time::getMillisecondCounterHiRes();
            PreviewAnalysis::Result result;
            juce::AudioBuffer<float> canonical;
            KeyDetector::Features keyFeatures;
            BpmDetector::Features tempoFeatures;
            if (features)
            {
                canonical = AnalysisAudio::fromFile (source.pcm, source.channels);
                check (canonical.getNumSamples() == AnalysisAudio::convertedLength (source.samples, source.sampleRate, AnalysisAudio::rate),
                       "Calibration PCM normalization did not complete");
                keyFeatures = KeyDetector::extract (canonical, canonical.getNumSamples());
                tempoFeatures = BpmDetector::extract (canonical, canonical.getNumSamples());
                result.valid = keyFeatures.valid && tempoFeatures.valid;
                result.key = KeyDetector::estimate (keyFeatures); result.bpm = BpmDetector::estimate (tempoFeatures);
            }
            else result = analysis.analyse (source, std::make_shared<PreviewAnalysis::Publication>());
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - begin;
            check (result.valid, result.error.isNotEmpty() ? result.error : "Corpus analysis did not complete");
            durations.push_back (elapsed);
            put (observation, "analysisMs", elapsed); put (observation, "bpm", result.bpm.bpm);
            put (observation, "bpmConfidence", result.bpm.confidence); put (observation, "alternativeBpm", result.bpm.alternative);
            put (observation, "possibleTempoVariation", result.bpm.possibleVariation);
            put (observation, "startBpm", result.bpm.startBpm); put (observation, "endBpm", result.bpm.endBpm);
            put (observation, "key", result.key.key); put (observation, "keyConfidence", result.key.confidence);
            put (observation, "keyCorrelation", result.key.correlation); put (observation, "keyMargin", result.key.margin);
            if (features)
            {
                const auto traceStart = juce::Time::getMillisecondCounterHiRes();
                const auto trace = exportMusicFeatures (canonical, keyFeatures, tempoFeatures, report);
                put (observation, "featuresFile", trace.getFullPathName()); put (observation, "featuresSha256", fileDigest (trace));
                put (observation, "featureExportMs", juce::Time::getMillisecondCounterHiRes() - traceStart);
            }
            if (entry.tempoLabelled)
            {
                const auto exact = tempoMatches (result.bpm.bpm, entry.bpm);
                const auto alternative = exact || tempoMatches (result.bpm.bpm, entry.bpm * 0.5)
                                               || tempoMatches (result.bpm.bpm, entry.bpm * 2);
                put (observation, "tempoMatch", entry.bpm > 0 ? exact : result.bpm.bpm == 0);
                if (entry.bpm > 0)
                { tempos.covered += result.bpm.bpm > 0; tempos.exact += exact; tempos.halfDouble += alternative; }
                else tempos.unknownCorrect += result.bpm.bpm == 0;
            }
            if (entry.keyLabelled)
            {
                const auto estimated = key (result.key.key);
                const auto exact = estimated.root == entry.expectedKey.root
                    && (estimated.root < 0 || estimated.minor == entry.expectedKey.minor);
                put (observation, "keyMatch", exact);
                if (entry.expectedKey.root >= 0) { keys.covered += estimated.root >= 0; keys.exact += exact; }
                else keys.unknownCorrect += exact;
            }
        }
        catch (const std::exception& error) { ++errors; put (observation, "error", juce::String (error.what())); }
        observations.add (observation);
        if (observations.size() % 25 == 0) std::cout << "CORPUS evaluated=" << observations.size() << "/" << entries.size() << " errors=" << errors << std::endl;
    }
    auto output = object();
    put (output, "schemaVersion", 1); put (output, "analysisVersion", PreviewAnalysis::version);
    put (output, "manifestSha256", juce::SHA256 (bytes).toHexString()); put (output, "manifest", manifest.getFullPathName());
    put (output, "executableSha256", fileDigest (juce::File::getSpecialLocation (juce::File::currentExecutableFile)));
    put (output, "dataset", definition.getProperty ("dataset", "")); put (output, "source", definition.getProperty ("source", ""));
    put (output, "startedAt", started); put (output, "completedAt", juce::Time::getCurrentTime().toISO8601 (true));
    put (output, "complete", errors == 0); put (output, "errors", errors); put (output, "entries", observations);
    put (output, "tempoToleranceRelative", 0.04); put (output, "tempo", tempos.toJson (true)); put (output, "key", keys.toJson (false));
    std::sort (durations.begin(), durations.end());
    if (! durations.empty())
    {
        auto latency = object(); double total = 0; for (auto duration : durations) total += duration;
        const auto middle = durations.size() / 2;
        const auto median = durations.size() % 2 != 0 ? durations[middle] : (durations[middle - 1] + durations[middle]) * 0.5;
        put (latency, "meanMs", total / durations.size()); put (latency, "medianMs", median);
        put (latency, "p95Ms", durations[(size_t) std::ceil (0.95 * durations.size()) - 1]); put (latency, "maxMs", durations.back());
        put (output, "analysisLatency", latency);
    }
    const auto saved = LibraryStore::writeJsonAtomically (report, output);
    check (saved.wasOk(), saved.getErrorMessage());
    std::cout << "CORPUS report=" << report.getFullPathName() << " evaluated=" << entries.size() << " errors=" << errors
              << " tempoAccuracy=" << fraction (tempos.exact, tempos.known) << " keyAccuracy=" << fraction (keys.exact, keys.known) << std::endl;
    check (errors == 0, "Corpus evaluation is incomplete; inspect the saved report");
}

void testMusicCorpusEvaluation (const juce::File& root)
{
    const auto folder = root.getChildFile ("music-corpus-contract"); check (folder.createDirectory().wasOk(), "Cannot create corpus contract fixture");
    const auto silent = folder.getChildFile ("silent.wav"), invalid = folder.getChildFile ("invalid.wav");
    juce::AudioBuffer<float> silence (2, 22050 * 2); silence.clear();
    juce::WavAudioFormat wav;
    auto stream = silent.createOutputStream(); check (stream != nullptr, "Cannot create corpus WAV");
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 22050, 2, 32, {}, 0));
    check (writer != nullptr, "Cannot create corpus WAV writer"); stream.release();
    check (writer->writeFromAudioSampleBuffer (silence, 0, silence.getNumSamples()), "Cannot write corpus silence"); writer.reset();
    check (invalid.replaceWithText ("not audio"), "Cannot create corrupt corpus input");
    auto makeEntry = [&] (const char* id, const juce::File& audio, double bpm, const char* label)
    {
        auto value = object(); put (value, "id", id); put (value, "file", audio.getFileName());
        put (value, "sha256", fileDigest (audio)); put (value, "referenceBpm", bpm); put (value, "referenceKey", label); return value;
    };
    const auto manifest = folder.getChildFile ("manifest.json"), report = folder.getChildFile ("report.json");
    auto definition = object(); put (definition, "schemaVersion", 1);
    juce::Array<juce::var> rows { makeEntry ("unknown", silent, 0, "Unknown"), makeEntry ("wrong", silent, 120, "Eb minor"),
                                makeEntry ("corrupt", invalid, 90, "G minor") };
    auto unlabelledTempo = makeEntry ("key-only", silent, 0, "Unknown");
    unlabelledTempo.getDynamicObject()->removeProperty ("referenceBpm"); rows.add (unlabelledTempo);
    put (definition, "entries", rows); check (LibraryStore::writeJsonAtomically (manifest, definition).wasOk(), "Cannot save corpus fixture");
    bool rejected = false;
    try { evaluateMusicCorpus (manifest, report); } catch (const std::exception&) { rejected = true; }
    const auto failed = LibraryStore::readJson (report);
    check (rejected && ! (bool) failed.getProperty ("complete", true) && (int) failed.getProperty ("errors", 0) == 1,
           "Corrupt corpus audio was silently omitted or accepted");
    for (const auto* metric : { "tempo", "key" })
    {
        const auto counts = failed.getProperty (metric, juce::var{});
        check ((int) counts.getProperty ("knownReferences", 0) == 2 && (double) counts.getProperty ("accuracy", -1) == 0
               && (double) counts.getProperty ("coverage", -1) == 0 && (double) counts.getProperty ("unknownRecall", -1) == 1,
               "Corpus metrics hid failed inputs/abstentions or confused unlabelled and unknown");
        check ((int) counts.getProperty ("unknownReferences", 0) == (juce::String (metric) == "tempo" ? 1 : 2),
               "Unlabelled tempo was counted as a reference to unknown tempo");
    }
    check (failed.getProperty ("entries", juce::var{})[1].getProperty ("referenceKey", "").toString() == "D# minor", "Enharmonic reference was not normalised");
    rows = juce::Array<juce::var> { rows[0] }; put (definition, "entries", rows);
    check (LibraryStore::writeJsonAtomically (manifest, definition).wasOk(), "Cannot save valid corpus fixture");
    evaluateMusicCorpus (manifest, report);
    const auto complete = LibraryStore::readJson (report);
    check ((bool) complete.getProperty ("complete", false) && complete.getProperty ("key", juce::var{}).getProperty ("accuracy", 0).isVoid(),
           "Unknown-only corpus invented labelled accuracy");
    auto changed = rows[0].clone(); put (changed, "sha256", juce::String::repeatedString ("0", 64)); rows.set (0, changed);
    put (definition, "entries", rows); check (LibraryStore::writeJsonAtomically (manifest, definition).wasOk(), "Cannot save checksum fixture");
    rejected = false; try { evaluateMusicCorpus (manifest, report); } catch (const std::exception&) { rejected = true; }
    check (rejected && (int) LibraryStore::readJson (report).getProperty ("errors", 0) == 1, "Corpus ignored a stale audio checksum");
    put (definition, "entries", juce::Array<juce::var> { rows[0], rows[0] });
    check (LibraryStore::writeJsonAtomically (manifest, definition).wasOk(), "Cannot save duplicate-ID fixture");
    rejected = false; try { evaluateMusicCorpus (manifest, report); } catch (const std::exception&) { rejected = true; }
    check (rejected, "Corpus accepted duplicate identities");
    rejected = false; try { evaluateMusicCorpus (manifest, silent); } catch (const std::exception&) { rejected = true; }
    check (rejected && fileDigest (silent) == complete.getProperty ("entries", juce::var{})[0].getProperty ("sha256", "").toString(),
           "Invalid corpus/report collision changed an input");
    std::cout << "PASS: corpus evaluator retains errors/abstentions in denominators, separates unlabelled/unknown, normalises enharmonic keys, checks SHA256 and protects input paths\n";
}
