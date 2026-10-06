#include "RecordingEngine.h"
#include "KeyDetector.h"
#include "BpmDetector.h"
#include "SettingsManager.h"
#include <algorithm>

namespace
{
juce::var loadMetadata (const juce::File& folder)
{
    return juce::JSON::parse (folder.getChildFile ("metadata.json").loadFileAsString());
}
bool writeMetadata (const juce::File& folder, const juce::var& metadata)
{
    return folder.getChildFile ("metadata.json").replaceWithText (juce::JSON::toString (metadata));
}
}

RecordingEngine::RecordingEngine (ProjectDetector detect)
    : juce::Thread ("RememberEncoding"), detector (std::move (detect))
{
    startThread();
}

RecordingEngine::~RecordingEngine()
{
    // A short export may end without a stopped transport callback.
    finishCapture();
    signalThreadShouldExit();
    wake.signal();
    stopThread (-1);
    juce::ScopedLock lock (projectLock);
    removePending();
}

void RecordingEngine::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    finishCapture();
    signalThreadShouldExit();
    wake.signal();
    stopThread (-1); // never resize a buffer while the worker is encoding it
    sampleRate = newSampleRate;
    auto& settings = SettingsManager::getInstance();
    recordSeconds = (float) settings.readRecordSeconds();
    oggQuality = settings.readOggQuality();
    recordMono = settings.readRecordMono();
    samplesToCapture = (int) (sampleRate * recordSeconds.load());
    for (auto& slot : slots)
    {
        slot.audio.setSize (2, samplesToCapture, false, false, true);
        slot.state.store (SlotState::Free);
        slot.count = 0;
    }
    activeSlot = -1;
    wasPlaying = false;
    previousTime = -1;
    previousBlockSeconds = offlineTime = 0;
    samplesWritten.store (0);
    capturing.store (false);
    state.store (State::Idle);
    startThread();
}

void RecordingEngine::startCapture (double bpm)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto expected = SlotState::Free;
        if (! slots[i].state.compare_exchange_strong (expected, SlotState::Capturing)) continue;
        activeSlot = (int) i;
        auto& slot = slots[i];
        slot.count = 0;
        slot.bpm = bpm;
        slot.generation = generation.load();
        slot.sequence = ++nextSequence;
        capturedBpm.store (bpm);
        samplesWritten.store (0);
        capturing.store (true);
        return;
    }
}

void RecordingEngine::finishCapture()
{
    if (activeSlot < 0) return;
    auto& slot = slots[(size_t) activeSlot];
    bool valid = slot.count >= (int) sampleRate;
    slot.state.store (valid ? SlotState::Ready : SlotState::Free);
    activeSlot = -1;
    capturing.store (false);
    if (valid) { state.store (State::Encoding); wake.signal(); }
}

void RecordingEngine::processBlock (const juce::AudioBuffer<float>& buffer,
                                    juce::AudioPlayHead* playHead, bool offline)
{
    auto position = playHead != nullptr ? playHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
    if (! position.hasValue() && ! offline) return;
    const bool playing = offline || position->getIsPlaying();
    const double time = position.hasValue()
        ? position->getTimeInSeconds().orFallback (
              (double) position->getTimeInSamples().orFallback (0) / sampleRate)
        : offlineTime;
    const double blockSeconds = (double) buffer.getNumSamples() / sampleRate;
    const bool movedBack = previousTime >= 0 && time + blockSeconds * 0.5 < previousTime;
    const bool jumpedForward = previousTime >= 0 && time > previousTime + previousBlockSeconds + blockSeconds * 2;
    // A stopped block, seek, or another loop must not be appended to the preview.
    if (activeSlot >= 0 && (! playing || movedBack || jumpedForward)) finishCapture();
    if (playing && time >= 0 && time < 0.25 && (! wasPlaying || movedBack))
        startCapture (position.hasValue() ? position->getBpm().orFallback (0) : 0);
    if (playing && activeSlot >= 0)
    {
        auto& slot = slots[(size_t) activeSlot];
        if (slot.generation != generation.load()) finishCapture();
        else
        {
            const int count = juce::jmin (buffer.getNumSamples(), samplesToCapture - slot.count);
            for (int ch = 0; ch < 2; ++ch)
            {
                if (buffer.getNumChannels() > 0)
                    slot.audio.copyFrom (ch, slot.count, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, count);
                else
                    slot.audio.clear (ch, slot.count, count);
            }
            slot.count += count;
            samplesWritten.store (slot.count);
            if (slot.count == samplesToCapture) finishCapture();
        }
    }
    wasPlaying = playing;
    previousTime = time;
    previousBlockSeconds = blockSeconds;
    offlineTime += blockSeconds;
}

juce::File RecordingEngine::entryFolder() const
{
    return entry.confirmed ? outputFolder.getChildFile (entry.id)
                           : outputFolder.getChildFile (".pending").getChildFile (entry.id);
}

void RecordingEngine::removePending()
{
    if (outputFolder == juce::File{}) return;
    auto root = outputFolder.getChildFile (".pending");
    auto folder = root.getChildFile (entry.id);
    if (folder.isAChildOf (root)) folder.deleteRecursively();
}

bool RecordingEngine::promotePending (const juce::File& oldFolder)
{
    auto target = entryFolder();
    if (target == oldFolder || ! oldFolder.isDirectory()) return true;
    if (! oldFolder.isAChildOf (outputFolder.getChildFile (".pending"))) return false;
    if (target.exists())
    {
        // Pending audio can arrive before the host identifies an existing file.
        // Merge its prefix into that entry instead of creating a duplicate.
        auto source = oldFolder.getChildFile ("preview.wav");
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
        if (! reader || samplesToCapture <= 0) return false;
        Slot pending;
        pending.count = juce::jmin (samplesToCapture.load(), (int) std::llround ((double) reader->lengthInSamples * sampleRate / reader->sampleRate));
        pending.audio.setSize (2, pending.count);
        if (reader->sampleRate == sampleRate)
        {
            if (! reader->read (&pending.audio, 0, pending.count, 0, true, true)) return false;
        }
        else
        {
            juce::AudioBuffer<float> original (2, (int) reader->lengthInSamples);
            if (! reader->read (&original, 0, original.getNumSamples(), 0, true, true)) return false;
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::LagrangeInterpolator resampler;
                resampler.process (reader->sampleRate / sampleRate, original.getReadPointer (ch),
                                   pending.audio.getWritePointer (ch), pending.count, original.getNumSamples(), 0);
            }
        }
        pending.bpm = (double) loadMetadata (oldFolder).getProperty ("bpm", 0.0);
        encodeAndSave (pending);
        if (state.load() == State::Error) return false;
        reader.reset();
        return oldFolder.deleteRecursively();
    }
    return oldFolder.moveFileTo (target);
}

void RecordingEngine::adoptExistingEntry (const juce::String& path)
{
    if (! outputFolder.isDirectory()) return;
    juce::File match;
    for (auto& folder : outputFolder.findChildFiles (juce::File::findDirectories, false))
    {
        auto metadata = loadMetadata (folder);
        auto existingPath = metadata.getProperty ("projectPath", "").toString();
        if (existingPath.isNotEmpty() && juce::File (existingPath) == juce::File (path))
        {
            if (match != juce::File{}) return;
            match = folder;
        }
    }
    if (match == juce::File{}) return;
    auto metadata = loadMetadata (match);
    auto id = metadata.getProperty ("projectId", "").toString();
    id = id.isNotEmpty() && ! juce::Uuid (id).isNull() ? juce::Uuid (id).toString() : entry.id;
    auto target = outputFolder.getChildFile (id);
    if (match != target && ! match.moveFileTo (target)) return;
    entry.id = id;
}

void RecordingEngine::refreshHostProject()
{
    auto host = detector();
    if (! host.available) return;
    juce::ScopedLock lock (projectLock);
    daw = host.daw;
    auto now = juce::Time::getMillisecondCounter();
    if (host.name != lastHostName || now - lastResolve >= 2000 || restored)
    {
        lastResolve = now;
        lastHostName = host.name;
        if (host.path.isEmpty() && host.name.isNotEmpty())
        {
            if (entry.path.isNotEmpty() && juce::File (entry.path).existsAsFile()
                && juce::File (entry.path).getFileNameWithoutExtension() == host.name)
                host.path = entry.path;
            else
            {
                auto roots = ProjectIdentity::searchRoots();
                if (entry.path.isNotEmpty()) roots.addIfNotAlreadyThere (juce::File (entry.path).getParentDirectory());
                host.path = ProjectIdentity::resolve (host.name, daw, roots);
            }
        }
    }
    else if (host.path.isEmpty() && entry.confirmed && entry.name == host.name
             && juce::File (entry.path).existsAsFile())
        host.path = entry.path;
    if (host.path.isEmpty())
    {
        if (restored || (entry.confirmed && host.name != entry.name))
        {
            removePending();
            entry = Entry{};
            generation.fetch_add (1);
            manualName = false;
        }
        restored = false;
        if (! manualName || host.name.isNotEmpty())
        {
            entry.name = host.name;
            if (host.name.isNotEmpty()) manualName = false;
            updateMetadata();
        }
        return;
    }
    juce::File file (host.path);
    if (! file.existsAsFile()) return;
    const auto previousFolder = entryFolder();
    bool changed = entry.path.isNotEmpty() && juce::File (entry.path) != file;
    bool fork = changed && juce::File (entry.path).existsAsFile();
    if (fork)
    {
        entry = Entry{};
        generation.fetch_add (1);
        adoptExistingEntry (host.path);
        auto target = outputFolder.getChildFile (entry.id);
        if (! target.exists() && previousFolder.getChildFile ("preview.ogg").existsAsFile())
            previousFolder.copyDirectoryTo (target);
    }
    else if (! entry.confirmed)
    {
        adoptExistingEntry (host.path);
    }
    entry.path = file.getFullPathName();
    entry.name = file.getFileNameWithoutExtension();
    entry.confirmed = true;
    manualName = false;
    restored = false;
    auto stamp = file.getLastModificationTime().toMilliseconds();
    if (changed || entry.hash.isEmpty() || stamp != entry.fileStamp)
    {
        entry.fileId = ProjectIdentity::fileId (file);
        entry.hash = ProjectIdentity::fingerprint (file);
        entry.fileStamp = stamp;
    }
    if (! fork && ! promotePending (previousFolder)) state.store (State::Error);
    updateMetadata();
}

void RecordingEngine::updateMetadata()
{
    auto folder = entryFolder();
    auto metadata = loadMetadata (folder);
    if (! metadata.isObject()) return;
    auto* obj = metadata.getDynamicObject();
    bool changed = obj->getProperty ("projectName").toString() != entry.name
        || obj->getProperty ("projectPath").toString() != entry.path
        || obj->getProperty ("projectId").toString() != entry.id
        || obj->getProperty ("projectFileId").toString() != entry.fileId
        || obj->getProperty ("projectFingerprint").toString() != entry.hash
        || (bool) obj->getProperty ("pending") == entry.confirmed;
    if (! changed) return;
    obj->setProperty ("projectId", entry.id);
    obj->setProperty ("projectName", entry.name);
    obj->setProperty ("projectPath", entry.path);
    obj->setProperty ("projectFileId", entry.fileId);
    obj->setProperty ("projectFingerprint", entry.hash);
    obj->setProperty ("pending", ! entry.confirmed);
    if (! writeMetadata (folder, metadata)) state.store (State::Error);
    else if (entry.confirmed && state.load() == State::Pending) state.store (State::Done);
}

void RecordingEngine::run()
{
    for (;;)
    {
        refreshHostProject();
        processReadySlots();
        if (threadShouldExit()) break;
        wake.wait (250);
    }
}

void RecordingEngine::processReadySlots()
{
    for (;;)
    {
        Slot* next = nullptr;
        for (auto& slot : slots)
            if (slot.state.load() == SlotState::Ready && (next == nullptr || slot.sequence < next->sequence)) next = &slot;
        if (next == nullptr) return;
        next->state.store (SlotState::Encoding);
        {
            juce::ScopedLock lock (projectLock);
            if (next->generation == generation.load()) encodeAndSave (*next);
            else state.store (State::Idle);
        }
        next->state.store (SlotState::Free);
    }
}

void RecordingEngine::encodeAndSave (Slot& slot)
{
    if (! outputFolder.isDirectory()) { state.store (State::Error); return; }
    auto folder = entryFolder();
    if (folder.createDirectory().failed()) { state.store (State::Error); return; }
    auto ogg = folder.getChildFile ("preview.ogg");
    auto pcm = folder.getChildFile ("preview.wav");
    juce::AudioBuffer<float> merged (2, samplesToCapture);
    merged.clear();
    int length = slot.count;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (pcm.existsAsFile() ? pcm : ogg)))
    {
        int oldLength = juce::jmin (samplesToCapture.load(), (int) std::llround ((double) reader->lengthInSamples * sampleRate / reader->sampleRate));
        if (reader->sampleRate == sampleRate)
        {
            if (! reader->read (&merged, 0, oldLength, 0, true, true)) { state.store (State::Error); return; }
        }
        else
        {
            juce::AudioBuffer<float> original (2, (int) reader->lengthInSamples);
            if (! reader->read (&original, 0, original.getNumSamples(), 0, true, true)) { state.store (State::Error); return; }
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.process (reader->sampleRate / sampleRate, original.getReadPointer (ch),
                                      merged.getWritePointer (ch), oldLength, original.getNumSamples(), 0);
            }
        }
        length = juce::jmax (length, oldLength);
    }
    else if (pcm.existsAsFile() || ogg.existsAsFile())
    {
        state.store (State::Error); return;
    }
    for (int ch = 0; ch < 2; ++ch) merged.copyFrom (ch, 0, slot.audio, ch, 0, slot.count);
    // Keep a lossless source so untouched tails don't degrade on each re-record.
    juce::TemporaryFile wavTemporary (pcm), oggTemporary (ogg);
    auto writeAudio = [&] (const juce::File& file, juce::AudioFormat& format, int channels, int bits, int quality)
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
            .withSampleRate (sampleRate).withNumChannels (channels).withBitsPerSample (bits).withQualityOptionIndex (quality));
        if (! writer) return false;
        if (channels == 1)
        {
            juce::AudioBuffer<float> mono (1, length);
            for (int i = 0; i < length; ++i) mono.setSample (0, i, 0.5f * (merged.getSample (0, i) + merged.getSample (1, i)));
            return writer->writeFromAudioSampleBuffer (mono, 0, length);
        }
        return writer->writeFromAudioSampleBuffer (merged, 0, length);
    };
    juce::WavAudioFormat wavFormat;
    juce::OggVorbisAudioFormat oggFormat;
    if (! writeAudio (wavTemporary.getFile(), wavFormat, 2, 24, 0)
        || ! writeAudio (oggTemporary.getFile(), oggFormat, recordMono ? 1 : 2, 16, oggQuality))
    {
        state.store (State::Error); return;
    }
    if (! oggTemporary.overwriteTargetFileWithTemporary() || ! wavTemporary.overwriteTargetFileWithTemporary())
    {
        state.store (State::Error); return;
    }
    juce::AudioBuffer<float> analysis (merged.getArrayOfWritePointers(), 2, length);
    auto key = KeyDetector::detectKey (analysis, sampleRate);
    auto bpm = slot.bpm > 0 ? slot.bpm : BpmDetector::detectBpm (analysis, sampleRate);
    auto metadata = loadMetadata (folder);
    if (! metadata.isObject()) metadata = juce::var (new juce::DynamicObject());
    auto* obj = metadata.getDynamicObject();
    obj->setProperty ("projectId", entry.id);
    obj->setProperty ("projectName", entry.name);
    obj->setProperty ("projectPath", entry.path);
    obj->setProperty ("projectFileId", entry.fileId);
    obj->setProperty ("projectFingerprint", entry.hash);
    obj->setProperty ("pending", ! entry.confirmed);
    obj->setProperty ("daw", daw);
    obj->setProperty ("recordedAt", juce::Time::getCurrentTime().toISO8601 (true));
    obj->setProperty ("durationSeconds", (double) length / sampleRate);
    obj->setProperty ("bpm", bpm);
    obj->setProperty ("key", key);
    if (! writeMetadata (folder, metadata)) { state.store (State::Error); return; }
    {
        juce::ScopedLock lock (resultsLock);
        detectedKey = key;
        lastSavedTime = juce::Time::getCurrentTime().toString (true, true, false, true);
    }
    capturedBpm.store (bpm);
    state.store (entry.confirmed ? State::Done : State::Pending);
    juce::Logger::writeToLog ("Saved preview id=" + entry.id + " name=\"" + entry.name
        + "\" path=\"" + entry.path + "\" seconds=" + juce::String ((double) length / sampleRate, 2));
}

void RecordingEngine::setOutputFolder (const juce::File& folder)
{
    juce::ScopedLock lock (projectLock);
    bool changed = outputFolder != folder;
    outputFolder = folder;
    if (changed && entry.confirmed) adoptExistingEntry (entry.path);
    wake.signal();
}
void RecordingEngine::setProjectName (const juce::String& name)
{
    juce::ScopedLock lock (projectLock);
    if (entry.confirmed) return;
    entry.name = name.trim();
    manualName = entry.name.isNotEmpty();
    updateMetadata();
}
juce::String RecordingEngine::getProjectName() const
{
    juce::ScopedLock lock (projectLock);
    return entry.name;
}
bool RecordingEngine::isProjectConfirmed() const
{
    juce::ScopedLock lock (projectLock);
    return entry.confirmed;
}
void RecordingEngine::saveState (juce::XmlElement& xml) const
{
    juce::ScopedLock lock (projectLock);
    xml.setAttribute ("projectId", entry.id);
    xml.setAttribute ("projectName", entry.name);
    xml.setAttribute ("projectPath", entry.path);
    xml.setAttribute ("projectFileId", entry.fileId);
    xml.setAttribute ("projectFingerprint", entry.hash);
}
void RecordingEngine::restoreState (const juce::XmlElement& xml)
{
    juce::ScopedLock lock (projectLock);
    removePending();
    entry = Entry{};
    auto id = xml.getStringAttribute ("projectId");
    if (id.isNotEmpty() && ! juce::Uuid (id).isNull()) entry.id = juce::Uuid (id).toString();
    entry.path = xml.getStringAttribute ("projectPath");
    if (! juce::File::isAbsolutePath (entry.path)) entry.path.clear();
    entry.fileId = xml.getStringAttribute ("projectFileId");
    entry.hash = xml.getStringAttribute ("projectFingerprint");
    // A legacy projectName can come from a preset/template; it isn't evidence.
    // Do not publish into a restored entry until the current DAW confirms it.
    entry.confirmed = false;
    restored = true;
    manualName = false;
    generation.fetch_add (1);
    wake.signal();
}
RecordingEngine::State RecordingEngine::getState() const noexcept
{
    if (capturing.load()) return State::Recording;
    for (auto& slot : slots)
    {
        auto value = slot.state.load();
        if (value == SlotState::Ready || value == SlotState::Encoding) return State::Encoding;
    }
    return state.load();
}
float RecordingEngine::getProgress() const noexcept
{
    auto total = samplesToCapture.load();
    return total > 0 ? (float) samplesWritten.load() / (float) total : 0;
}
juce::String RecordingEngine::getDetectedKey() const
{
    juce::ScopedLock lock (resultsLock);
    return detectedKey;
}
juce::String RecordingEngine::getLastSavedTime() const
{
    juce::ScopedLock lock (resultsLock);
    return lastSavedTime;
}
