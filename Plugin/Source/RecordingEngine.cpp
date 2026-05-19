#include "RecordingEngine.h"
#include "KeyDetector.h"
#include "BpmDetector.h"
#include "SettingsManager.h"

#if JUCE_WINDOWS
 #include <windows.h>

// Walks all visible top-level windows and returns the title of the first one
// whose text contains `keyword`.
static juce::String findWindowTitle (const char* keyword)
{
    struct Finder {
        const char* kw;
        char        title[1024] {};
        bool        found { false };

        static BOOL CALLBACK proc (HWND hwnd, LPARAM lp)
        {
            if (! IsWindowVisible (hwnd)) return TRUE;
            auto* self = reinterpret_cast<Finder*> (lp);
            char buf[1024] {};
            GetWindowTextA (hwnd, buf, sizeof (buf));
            if (strstr (buf, self->kw))
            {
                strcpy_s (self->title, buf);
                self->found = true;
                return FALSE;
            }
            return TRUE;
        }
    } f;
    f.kw = keyword;
    EnumWindows (Finder::proc, reinterpret_cast<LPARAM> (&f));
    return f.found ? juce::String::fromUTF8 (f.title) : juce::String{};
}
#endif

// ---------------------------------------------------------------------------

RecordingEngine::RecordingEngine() : juce::Thread ("RememberEncoding")
{
    startThread();
}

RecordingEngine::~RecordingEngine()
{
    signalThreadShouldExit();
    encodeTrigger.signal();
    stopThread (3000);
}

void RecordingEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    currentSampleRate = sampleRate;

    // Snapshot current settings — never touched again on the audio thread.
    auto& s = SettingsManager::getInstance();
    currentRecordSeconds = (float) s.readRecordSeconds();
    currentOggQuality    = s.readOggQuality();
    currentRecordMono    = s.readRecordMono();

    samplesToCapture = (int) (sampleRate * currentRecordSeconds);
    // captureBuffer is ALWAYS stereo so key/BPM detection still sees the full
    // signal; mono downmix is applied later, at encode time.
    if (captureBuffer.getNumSamples() != samplesToCapture
        || captureBuffer.getNumChannels() != 2)
        captureBuffer.setSize (2, samplesToCapture, false, false, true);
    samplesWritten.store (0);
    state.store (State::Idle);
}

// ── Audio thread ─────────────────────────────────────────────────────────────

void RecordingEngine::processBlock (const juce::AudioBuffer<float>& buffer,
                                    juce::AudioPlayHead*            playHead)
{
    if (playHead == nullptr) return;

    auto pos = playHead->getPosition();
    if (! pos.hasValue()) return;

    bool   isPlaying     = pos->getIsPlaying();
    double timeInSeconds = pos->getTimeInSeconds().orFallback (-1.0);
    bool   isNearStart   = isPlaying && timeInSeconds >= 0.0 && timeInSeconds < kTriggerThreshold;

    bool wasNear = wasPlayingNearStart.load();

    if (isNearStart && ! wasNear)
    {
        auto cur = state.load();
        if (cur == State::Idle || cur == State::Done || cur == State::Error)
            startCapture (pos->getBpm().orFallback (0.0));
    }

    wasPlayingNearStart.store (isNearStart);

    if (state.load() == State::Recording)
    {
        int written   = samplesWritten.load();
        int remaining = samplesToCapture - written;
        int toCopy    = juce::jmin (buffer.getNumSamples(), remaining);

        for (int ch = 0; ch < juce::jmin (buffer.getNumChannels(), captureBuffer.getNumChannels()); ++ch)
            captureBuffer.copyFrom (ch, written, buffer, ch, 0, toCopy);

        int newWritten = written + toCopy;
        samplesWritten.store (newWritten);

        if (newWritten >= samplesToCapture)
        {
            state.store (State::Encoding);
            encodeTrigger.signal();
        }
    }

    // Early-stop on transport stop while recording:
    //  - >= 3s captured → save what we have
    //  - < 3s          → discard and reset to Idle so the next play-from-start
    //                    can trigger a fresh capture (otherwise the engine got
    //                    stuck in Recording and ignored future triggers)
    bool prevPlaying = wasPlaying.load();
    wasPlaying.store (isPlaying);
    if (! isPlaying && prevPlaying && state.load() == State::Recording)
    {
        int written = samplesWritten.load();
        if (written >= (int) (currentSampleRate * 3.0))
        {
            state.store (State::Encoding);
            encodeTrigger.signal();
        }
        else
        {
            samplesWritten.store (0);
            state.store (State::Idle);
        }
    }
}

void RecordingEngine::startCapture (double bpm)
{
    // Audio thread — must not block. Path/window detection deferred to encode thread.
    samplesWritten.store (0);
    capturedBpm.store (bpm);
    state.store (State::Recording);
    // Cheap: writeToLog uses a CriticalSection (not blocking enough to matter
    // for a capture-start event, which happens at most every minute).
    juce::Logger::writeToLog ("Capture started (host bpm=" + juce::String (bpm, 1) + ")");
}

// ── Windows project-path detection ───────────────────────────────────────────

// Tries to extract a full file path ending in `ext` (e.g. ".rpp") from a window title
// that may also contain extra text. Returns empty if no existing file path could be found.
static juce::String extractPathFromTitle (const juce::String& title, const juce::String& ext)
{
    auto idx = title.indexOfIgnoreCase (0, ext);
    if (idx < 0) return {};
    int end = idx + ext.length();

    // Look for a Windows drive-letter pattern "X:\" before the extension
    auto prefix   = title.substring (0, end);
    auto colonPos = prefix.lastIndexOfChar (':');
    if (colonPos >= 1 && colonPos < prefix.length() - 1
        && (prefix[colonPos + 1] == '\\' || prefix[colonPos + 1] == '/'))
    {
        auto candidate = prefix.substring (colonPos - 1).trim();
        if (juce::File (candidate).existsAsFile())
            return candidate;
    }
    return {};
}

juce::String RecordingEngine::detectProjectFilePath() const
{
#if JUCE_WINDOWS
    juce::PluginHostType host;

    if (host.isFruityLoops())
    {
        // FL Studio window title: "FL Studio XX - C:\path\to\Project.flp"
        auto title = findWindowTitle ("FL Studio");
        auto dash  = title.lastIndexOf (" - ");
        if (dash >= 0)
        {
            auto path = title.substring (dash + 3).trim();
            if (juce::File (path).existsAsFile())
                return path;
        }
        // Fall back to scanning the title for ".flp"
        return extractPathFromTitle (title, ".flp");
    }
    else if (host.isAbletonLive())
    {
        // Ableton title: "SetName - Ableton Live X.X.X Edition"
        auto title = findWindowTitle ("Ableton Live");
        auto dash  = title.indexOf (" - Ableton Live");
        if (dash > 0)
        {
            auto setName = title.substring (0, dash).trim();
            if (setName.isEmpty()) return {};

            juce::Array<juce::File> searchRoots {
                juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ableton"),
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ableton Projects"),
                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
            };

            for (auto& root : searchRoots)
            {
                if (! root.isDirectory()) continue;
                auto found = root.findChildFiles (juce::File::findFiles, true, setName + ".als");
                if (! found.isEmpty())
                    return found[0].getFullPathName();
            }
        }
    }
    else if (host.isReaper())
    {
        // Reaper title usually contains the full project path ending in .rpp
        auto title = findWindowTitle ("REAPER");
        if (auto path = extractPathFromTitle (title, ".rpp"); path.isNotEmpty())
            return path;
    }
    else if (host.isCubase() || host.isNuendo())
    {
        // Cubase/Nuendo titles rarely include the full path — return empty and
        // let the browser app resolve by name.
        auto title = findWindowTitle (host.isCubase() ? "Cubase" : "Nuendo");
        if (auto path = extractPathFromTitle (title, host.isCubase() ? ".cpr" : ".npr"); path.isNotEmpty())
            return path;
    }
    else if (host.isStudioOne())
    {
        auto title = findWindowTitle ("Studio One");
        if (auto path = extractPathFromTitle (title, ".song"); path.isNotEmpty())
            return path;
    }
    else if (host.isProTools())
    {
        auto title = findWindowTitle ("Pro Tools");
        if (auto path = extractPathFromTitle (title, ".ptx"); path.isNotEmpty())
            return path;
    }
    else if (host.isBitwigStudio())
    {
        auto title = findWindowTitle ("Bitwig");
        if (auto path = extractPathFromTitle (title, ".bwproject"); path.isNotEmpty())
            return path;
    }
#endif
    return {};
}

// ── Background encoding thread ────────────────────────────────────────────────

void RecordingEngine::run()
{
    while (! threadShouldExit())
    {
        encodeTrigger.wait (500);
        encodeTrigger.reset();
        if (threadShouldExit()) break;
        if (state.load() == State::Encoding)
            encodeAndSave();
    }
}

void RecordingEngine::encodeAndSave()
{
    // Detect host/path here (background thread) — never on audio thread
    juce::PluginHostType host;
    detectedDaw             = juce::String::fromUTF8 (host.getHostDescription());
    detectedProjectFilePath = detectProjectFilePath();

    auto folder = outputFolder;
    auto name   = projectName.isEmpty() ? "Unnamed Project" : projectName;

    if (! folder.isDirectory())
    {
        juce::Logger::writeToLog ("encodeAndSave: previews folder not configured or missing: "
                                  + folder.getFullPathName());
        state.store (State::Error);
        return;
    }

    auto projectFolder = folder.getChildFile (sanitizeFolderName (name));
    projectFolder.createDirectory();

    // ── OGG encode ───────────────────────────────────────────────────────────
    auto oggFile = projectFolder.getChildFile ("preview.ogg");
    oggFile.deleteFile();

    juce::OggVorbisAudioFormat format;
    auto fileStream = std::make_unique<juce::FileOutputStream> (oggFile);
    if (fileStream->failedToOpen()) { state.store (State::Error); return; }

    std::unique_ptr<juce::OutputStream> outStream = std::move (fileStream);

    const int writeChannels = currentRecordMono ? 1 : 2;
    auto writerOptions = juce::AudioFormatWriterOptions{}
                             .withSampleRate (currentSampleRate)
                             .withNumChannels (writeChannels)
                             .withBitsPerSample (16)
                             .withQualityOptionIndex (currentOggQuality);

    auto writer = format.createWriterFor (outStream, writerOptions);
    if (writer == nullptr)
    {
        juce::Logger::writeToLog ("encodeAndSave: OggVorbisAudioFormat::createWriterFor returned null"
                                  " (sr=" + juce::String (currentSampleRate, 0)
                                  + ", q=" + juce::String (currentOggQuality) + ")");
        state.store (State::Error);
        return;
    }

    int samplesRecorded = juce::jmin (samplesWritten.load(), samplesToCapture);

    if (currentRecordMono)
    {
        // Downmix L+R → mono on the encode thread. Cheap (< 1 ms for 60 s of 44.1k).
        juce::AudioBuffer<float> monoBuffer (1, samplesRecorded);
        auto* dst = monoBuffer.getWritePointer (0);
        auto* l   = captureBuffer.getReadPointer (0);
        auto* r   = captureBuffer.getReadPointer (1);
        for (int i = 0; i < samplesRecorded; ++i)
            dst[i] = (l[i] + r[i]) * 0.5f;
        writer->writeFromAudioSampleBuffer (monoBuffer, 0, samplesRecorded);
    }
    else
    {
        writer->writeFromAudioSampleBuffer (captureBuffer, 0, samplesRecorded);
    }
    writer.reset();

    // Build a "view" buffer containing only the samples actually recorded —
    // analysis on the full pre-allocated buffer would include trailing silence.
    juce::AudioBuffer<float> analysisView (captureBuffer.getArrayOfWritePointers(),
                                           captureBuffer.getNumChannels(),
                                           samplesRecorded);

    // ── Key detection ────────────────────────────────────────────────────────
    juce::String key = KeyDetector::detectKey (analysisView, currentSampleRate);

    // ── BPM fallback: if the DAW didn't report a tempo, analyze the audio ───
    if (capturedBpm.load() <= 0.0)
    {
        double estimated = BpmDetector::detectBpm (analysisView, currentSampleRate);
        if (estimated > 0.0)
            capturedBpm.store (estimated);
    }

    // ── Metadata ─────────────────────────────────────────────────────────────
    saveMetadata (projectFolder, key);

    {
        juce::ScopedLock sl (resultsLock);
        detectedKey   = key;
        lastSavedTime = juce::Time::getCurrentTime().toString (true, true, false, true);
    }

    state.store (State::Done);

    juce::Logger::writeToLog ("Saved preview: \"" + projectFolder.getFileName()
        + "\"  key=" + key
        + "  bpm=" + juce::String (capturedBpm.load(), 1)
        + "  samples=" + juce::String (samplesRecorded)
        + (currentRecordMono ? "  (mono)" : "  (stereo)"));
}

void RecordingEngine::saveMetadata (const juce::File& projectFolder, const juce::String& key)
{
    auto metaFile = projectFolder.getChildFile ("metadata.json");

    auto existing = metaFile.existsAsFile()
                        ? juce::JSON::parse (metaFile.loadFileAsString())
                        : juce::var{};
    const bool hasExisting = existing.isObject();

    // ── Fields owned by the user via the browser app — preserve when present ─
    // projectName: a user rename (Ctrl+R in the app) must survive re-recording.
    juce::String finalName = projectName.isEmpty() ? "Unnamed Project" : projectName;
    if (hasExisting)
    {
        auto existingName = existing.getProperty ("projectName", "").toString();
        if (existingName.isNotEmpty()) finalName = existingName;
    }

    // projectPath: prefer freshly detected, fall back to existing
    auto existingPath = hasExisting ? existing.getProperty ("projectPath", "").toString()
                                    : juce::String{};
    auto finalPath = detectedProjectFilePath.isNotEmpty() ? detectedProjectFilePath : existingPath;

    // daw: prefer freshly detected, fall back to existing
    auto existingDaw = hasExisting ? existing.getProperty ("daw", "").toString() : juce::String{};
    auto finalDaw    = detectedDaw.isNotEmpty() ? detectedDaw : existingDaw;

    // favorite: preserve user's flag across re-records
    bool finalFav = hasExisting ? (bool) existing.getProperty ("favorite", false) : false;

    // tags: preserve user's tag list across re-records
    juce::Array<juce::var> finalTags;
    if (hasExisting)
        if (auto* arr = existing.getProperty ("tags", juce::var()).getArray())
            for (auto& t : *arr) finalTags.add (t);

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty ("projectName",     finalName);
    obj->setProperty ("projectPath",     finalPath);
    obj->setProperty ("daw",             finalDaw);
    obj->setProperty ("recordedAt",      juce::Time::getCurrentTime().toISO8601 (true));
    obj->setProperty ("durationSeconds", (int) currentRecordSeconds);
    obj->setProperty ("bpm",             capturedBpm.load());
    obj->setProperty ("key",             key);
    obj->setProperty ("favorite",        finalFav);
    obj->setProperty ("tags",            finalTags);

    metaFile.replaceWithText (juce::JSON::toString (juce::var (obj.get())));
}

// ── Accessors ────────────────────────────────────────────────────────────────

float RecordingEngine::getProgress() const noexcept
{
    if (samplesToCapture == 0) return 0.0f;
    return (float) samplesWritten.load() / (float) samplesToCapture;
}

juce::String RecordingEngine::getDetectedKey() const
{
    juce::ScopedLock sl (resultsLock);
    return detectedKey;
}

juce::String RecordingEngine::getLastSavedTime() const
{
    juce::ScopedLock sl (resultsLock);
    return lastSavedTime;
}

void RecordingEngine::setOutputFolder (const juce::File& folder) { outputFolder = folder; }
void RecordingEngine::setProjectName  (const juce::String& name) { projectName  = name;   }

juce::String RecordingEngine::readPersistedProjectName() const
{
    if (projectName.isEmpty() || ! outputFolder.isDirectory()) return {};
    auto metaFile = outputFolder.getChildFile (sanitizeFolderName (projectName))
                                .getChildFile ("metadata.json");
    if (! metaFile.existsAsFile()) return {};
    auto json = juce::JSON::parse (metaFile.loadFileAsString());
    if (! json.isObject()) return {};
    return json.getProperty ("projectName", "").toString();
}

void RecordingEngine::writePersistedProjectName (const juce::String& name) const
{
    if (projectName.isEmpty() || ! outputFolder.isDirectory()) return;
    auto metaFile = outputFolder.getChildFile (sanitizeFolderName (projectName))
                                .getChildFile ("metadata.json");
    if (! metaFile.existsAsFile()) return;
    auto json = juce::JSON::parse (metaFile.loadFileAsString());
    if (! json.isObject()) return;
    json.getDynamicObject()->setProperty ("projectName", name);
    metaFile.replaceWithText (juce::JSON::toString (json));
}

juce::String RecordingEngine::sanitizeFolderName (const juce::String& name)
{
    juce::String safe = name;
    for (const char* c = "<>:\"/\\|?*"; *c; ++c)
        safe = safe.replaceCharacter ((juce::juce_wchar) *c, '_');
    return safe.trim().isEmpty() ? "Unnamed" : safe.trim();
}
