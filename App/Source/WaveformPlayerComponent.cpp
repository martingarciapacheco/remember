#include "WaveformPlayerComponent.h"
#include "ProjectListComponent.h"   // for Pal::
#include "../../Shared/LibraryStore.h"

namespace
{
std::atomic<int> activeLoads { 0 };
struct LoadTicket
{
    LoadTicket() { activeLoads.fetch_add (1); }
    ~LoadTicket() { source.reset(); activeLoads.fetch_sub (1); }
    std::unique_ptr<ReadAheadSource> source;
    std::shared_ptr<WaveformEnvelope> envelope;
};
}
int WaveformPlayerComponent::pendingLoads() { return activeLoads.load(); }

WaveformPlayerComponent::WaveformPlayerComponent (juce::AudioDeviceManager* externalDevice, bool persistDeviceState)
    : outputManager (externalDevice ? *externalDevice : deviceManager), persistAudioState (persistDeviceState)
{
    auto safePlayer = juce::Component::SafePointer<WaveformPlayerComponent> (this);
    outputManager.addChangeListener (this);
    sourcePlayer.onError = [safePlayer] (juce::String error)
    { juce::MessageManager::callAsync ([safePlayer, error] { if (safePlayer != nullptr) safePlayer->handleDeviceFailure (error); }); };
    deviceSettingsLoaded = ! persistAudioState;
    if (persistAudioState)
    {
        auto state = std::make_shared<juce::String>();
        auto ticket = std::make_shared<LoadTicket>();
        auto preferences = std::make_shared<juce::var>();
        ProjectScanner::queryAsync ([state, ticket, preferences]
        {
            *preferences = LibraryStore::readJson (LibraryStore::configFile());
            *state = preferences->getProperty ("audioDeviceState", "").toString();
            return juce::Result::ok();
        }, [safePlayer, state, ticket, preferences] (juce::Result)
        {
            if (safePlayer == nullptr) return;
            safePlayer->deviceSettingsLoaded = true;
            if (! safePlayer->audioDeviceOpen) safePlayer->savedAudioState = *state;
            if (! safePlayer->preferencesTouched)
            {
                safePlayer->applyingPreferences = true;
                safePlayer->volumeSlider.setValue (juce::jlimit (0.0, 1.0, (double) preferences->getProperty ("playbackVolume", 1.0)), juce::dontSendNotification);
                safePlayer->muteBtn.setToggleState ((bool) preferences->getProperty ("playbackMuted", false), juce::dontSendNotification);
                safePlayer->normalize = (bool) preferences->getProperty ("normalizePlayback", false);
                safePlayer->normalizeBtn.setToggleState (safePlayer->normalize, juce::dontSendNotification);
                safePlayer->autoplay = (bool) preferences->getProperty ("autoplaySelection", true);
                safePlayer->queue.setShuffle ((bool) preferences->getProperty ("shufflePlayback", false));
                safePlayer->shuffleBtn.setToggleState (safePlayer->queue.shuffle, juce::dontSendNotification);
                safePlayer->queue.repeat = (PlaybackQueue::Repeat) juce::jlimit (0, 2, (int) preferences->getProperty ("repeatPlayback", 0));
                safePlayer->repeatBtn.setToggleState (safePlayer->queue.repeat != PlaybackQueue::Off, juce::dontSendNotification);
                safePlayer->repeatBtn.getProperties().set ("rememberRepeatOne", safePlayer->queue.repeat == PlaybackQueue::One);
                safePlayer->applyOutputGain(); safePlayer->applyingPreferences = false;
            }
        });
    }
    playBtn.setEnabled (false);
    RememberUI::iconButton (previousBtn, "previous", "Previous preview; restart after 3 seconds");
    RememberUI::iconButton (nextBtn, "next", "Next preview");
    RememberUI::iconButton (shuffleBtn, "shuffle", "Shuffle previews");
    RememberUI::iconButton (repeatBtn, "repeat", "Repeat: off");
    RememberUI::iconButton (muteBtn, "volume", "Mute volume");
    RememberUI::iconButton (normalizeBtn, "normalize", "Normalize preview loudness to -14 LUFS");
    for (auto* button : { &previousBtn, &nextBtn, &shuffleBtn, &repeatBtn, &muteBtn, &normalizeBtn, &playbackOptionsBtn })
    {
        button->setWantsKeyboardFocus (false);
        RememberUI::bindColour (*button, juce::TextButton::buttonColourId, Pal::Elevated);
        RememberUI::bindColour (*button, juce::TextButton::textColourOffId, Pal::Text);
        addAndMakeVisible (*button);
    }
    shuffleBtn.setClickingTogglesState (true); muteBtn.setClickingTogglesState (true); normalizeBtn.setClickingTogglesState (true);
    previousBtn.onClick = [this] { previous(); }; nextBtn.onClick = [this] { next(); };
    shuffleBtn.onClick = [this] { queue.setShuffle (shuffleBtn.getToggleState()); savePlaybackPreferences(); };
    repeatBtn.onClick = [this]
    {
        queue.repeat = (PlaybackQueue::Repeat) (((int) queue.repeat + 1) % 3);
        repeatBtn.setToggleState (queue.repeat != PlaybackQueue::Off, juce::dontSendNotification);
        repeatBtn.getProperties().set ("rememberRepeatOne", queue.repeat == PlaybackQueue::One);
        RememberUI::iconButton (repeatBtn, "repeat", queue.repeat == PlaybackQueue::Off ? "Repeat: off" : queue.repeat == PlaybackQueue::All ? "Repeat: all" : "Repeat: one");
        repeatBtn.repaint(); savePlaybackPreferences();
    };
    muteBtn.onClick = [this] { setMuted (getVolume() == 0 ? false : muteBtn.getToggleState()); };
    normalizeBtn.onClick = [this] { setNormalized (normalizeBtn.getToggleState()); };
    playbackOptionsBtn.setTooltip ("Playback preferences"); playbackOptionsBtn.onClick = [this] { showPlaybackOptions(); };
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal); volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange (0, 1, 0.01); volumeSlider.setValue (1, juce::dontSendNotification); volumeSlider.setTitle ("Volume");
    volumeSlider.setTooltip ("Playback volume");
    RememberUI::bindColour (volumeSlider, juce::Slider::thumbColourId, Pal::Text);
    RememberUI::bindColour (volumeSlider, juce::Slider::trackColourId, Pal::Accent);
    RememberUI::bindColour (volumeSlider, juce::Slider::backgroundColourId, Pal::Border);
    volumeSlider.onValueChange = [this]
    {
        preferencesTouched = true;
        if (volumeSlider.getValue() > 0)
        {
            volumeSlider.getProperties().set ("rememberAudibleVolume", volumeSlider.getValue());
            muteBtn.setToggleState (false, juce::dontSendNotification);
        }
        applyOutputGain();
        if (! volumeSlider.isMouseButtonDown()) savePlaybackPreferences();
    };
    volumeSlider.onDragEnd = [this] { savePlaybackPreferences(); };
    addAndMakeVisible (volumeSlider);

    auto btnSetup = [&] (juce::TextButton& b, uint32_t bg, uint32_t fg)
    {
        RememberUI::bindColour (b, juce::TextButton::buttonColourId, bg);
        RememberUI::bindColour (b, juce::TextButton::textColourOffId, fg);
    };

    RememberUI::iconButton (playBtn, "play", "Play or pause preview (Space)");
    playBtn.getProperties().set ("rememberTransportPrimary", true);
    btnSetup (playBtn, Pal::Action,   Pal::OnAction);
    btnSetup (openBtn,   Pal::Elevated, Pal::Text);
    btnSetup (assignBtn, Pal::Elevated, Pal::Subtext);
    btnSetup (audioBtn, Pal::Elevated, Pal::Subtext);
    audioBtn.onClick = [this] { showAudioSettings(); };

    playBtn.onClick  = [this] { togglePlay(); };

    openBtn.onClick  = [safePlayer]
    {
        if (safePlayer == nullptr) return;
        ProjectScanner::openProjectAsync (safePlayer->currentEntry, false,
            [safePlayer] (juce::Result result)
            {
                if (safePlayer != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "Cannot open project", result.getErrorMessage());
            });
    };

    assignBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select project file",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.als;*.flp;*.ptx;*.song;*.npr;*.rpp;*.cpr;*.bwproject");

        auto safe = juce::Component::SafePointer<WaveformPlayerComponent> (this);
        auto selected = currentEntry;
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, selected] (const juce::FileChooser& fc) mutable
            {
                if (safe == nullptr || fc.getResults().isEmpty()) return;
                selected.projectPath = fc.getResult().getFullPathName();
                auto association = std::make_shared<ProjectEntry> (selected);
                ProjectScanner::editAsync ([association]
                {
                    auto result = association->save ({ "projectPath" });
                    if (result.failed()) return result;
                    const auto latest = LibraryStore::readJson (association->metadataFile);
                    if (latest.getDynamicObject() == nullptr || latest.getProperty ("projectId", "").toString() != association->projectId)
                        return juce::Result::fail ("The entry changed; refresh the library");
                    association->projectPath = latest.getProperty ("projectPath", "").toString();
                    association->projectFileId = latest.getProperty ("projectFileId", "").toString();
                    association->projectFingerprint = latest.getProperty ("projectFingerprint", "").toString();
                    association->name = latest.getProperty ("projectName", association->name).toString();
                    association->projectAvailable = association->projectPath.isNotEmpty()
                        && juce::File (association->projectPath).existsAsFile()
                        && (association->projectFileId.isEmpty()
                            || ProjectIdentity::fileId (juce::File (association->projectPath)) == association->projectFileId);
                    return result;
                }, [safe, association] (juce::Result result)
                {
                    if (safe == nullptr) return;
                    if (result.failed())
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                            "Cannot assign project", result.getErrorMessage());
                        return;
                    }
                    if (safe->currentEntry.metadataFile == association->metadataFile && safe->currentEntry.projectId == association->projectId)
                    {
                        safe->currentEntry.projectPath = association->projectPath;
                        safe->currentEntry.projectFileId = association->projectFileId;
                        safe->currentEntry.projectFingerprint = association->projectFingerprint;
                        safe->currentEntry.projectAvailable = association->projectAvailable;
                        safe->currentEntry.projectProblem = association->projectAvailable ? juce::String{} : "Selected project could not be verified";
                        safe->currentEntry.name = association->name;
                        safe->nameLabel.setText (association->name, juce::dontSendNotification);
                        safe->updatePathLabel();
                        safe->openBtn.setEnabled (association->projectAvailable);
                    }
                });
            });
    };

    nameLabel.setFont (RememberUI::font (14.0f, true));
    RememberUI::bindColour (nameLabel, juce::Label::textColourId, Pal::Text);

    pathLabel.setFont (RememberUI::font (12.0f));
    RememberUI::bindColour (pathLabel, juce::Label::textColourId, Pal::Subtext);

    timeLabel.setFont (RememberUI::font (12.0f));
    RememberUI::bindColour (timeLabel, juce::Label::textColourId, Pal::Subtext);
    timeLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (playBtn);
    addAndMakeVisible (openBtn);
    addAndMakeVisible (assignBtn);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (pathLabel);
    addAndMakeVisible (timeLabel);
    addAndMakeVisible (audioBtn);

    // Timer starts only when playback begins (saves CPU while idle)
}

WaveformPlayerComponent::~WaveformPlayerComponent()
{
    stopTimer();
    loadGeneration->fetch_add (1);
    outputManager.removeChangeListener (this);
    if (audioDialog != nullptr)
    { audioDialog->clearContentComponent(); audioDialog->exitModalState (0); }
    detachSource();
    sourcePlayer.setSource (nullptr);
    outputManager.removeAudioCallback (&sourcePlayer);
    outputManager.closeAudioDevice();
}

// ── Public API ────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::releaseEntry (const juce::File& metadataFile)
{
    if (currentEntry.metadataFile != metadataFile) return;
    loadGeneration->fetch_add (1);
    stop(); detachSource();
    currentEntry = {}; waveform.reset();
    isLoaded = false; isLoading = false; playWhenLoaded = false;
    playbackError.clear(); previewError.clear(); playbackErrorFromDevice = false;
    playBtn.setEnabled (false); openBtn.setEnabled (false); assignBtn.setEnabled (false);
    nameLabel.setText ("", juce::dontSendNotification);
    pathLabel.setText ("", juce::dontSendNotification);
    timeLabel.setText ("", juce::dontSendNotification);
    repaint();
}

void WaveformPlayerComponent::loadEntry (const ProjectEntry& entry)
{
    stop();
    detachSource();
    const auto request = loadGeneration->fetch_add (1) + 1;
    currentEntry = entry;
    queue.select (entry);
    if (onEntryLoaded) onEntryLoaded (entry);
    isLoaded     = false;
    isLoading = true;
    playWhenLoaded = false;
    playbackError.clear();
    previewError.clear();
    playbackErrorFromDevice = false;
    reportedUnderruns = 0;
    pathLabel.setTooltip ({});
    waveform.reset();
    playBtn.setEnabled (false);
    repaint();

    nameLabel.setText (entry.name, juce::dontSendNotification);
    updatePathLabel();
    timeLabel.setText ("", juce::dontSendNotification);

    openBtn.setEnabled (entry.projectAvailable);
    assignBtn.setEnabled (! entry.pending);

    if (entry.storageError.isNotEmpty())
    {
        isLoading = false; playbackError = previewError = entry.storageError;
        pathLabel.setText (playbackError, juce::dontSendNotification);
        return;
    }

    // One loader for the process. Obsolete requests never touch a component;
    // only the message-thread completion dereferences its SafePointer.
    static juce::ThreadPool loaders (1);
    auto generation = loadGeneration;
    auto safe = juce::Component::SafePointer<WaveformPlayerComponent> (this);
    auto ticket = std::make_shared<LoadTicket>();
    loaders.addJob ([safe, generation, request, entry, ticket]
    {
        if (generation->load() != request) return;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (entry.previewOgg));
        if (generation->load() != request) return;
        if (reader) ticket->source = std::make_unique<ReadAheadSource> (std::move (reader));
        if (generation->load() != request) return;
        juce::MessageManager::callAsync ([safe, generation, request, ticket, entry]
        {
            if (safe == nullptr || generation->load() != request) return;
            safe->isLoading = false;
            if (! ticket->source || ! ticket->source->valid())
            {
                safe->playbackError = "Cannot read preview: " + entry.previewOgg.getFullPathName()
                    + (ticket->source ? " (" + ticket->source->error() + ")" : juce::String{});
                safe->playbackErrorFromDevice = false;
                safe->previewError = safe->playbackError;
                safe->pathLabel.setText (safe->playbackError, juce::dontSendNotification);
                safe->repaint();
                return;
            }
            safe->readerSource = std::move (ticket->source);
            safe->transport.setSource (safe->readerSource.get(), 0, nullptr, safe->readerSource->sampleRate());
            safe->isLoaded = true;
            safe->playBtn.setEnabled (true);
            safe->playBtn.setButtonText (">");
            safe->updateTimeLabel();
            if (safe->playWhenLoaded) safe->play();
            safe->repaint();
        });
        ticket->envelope = std::make_shared<WaveformEnvelope>();
        ProjectScanner::queryAsync ([ticket, entry, generation, request]
        { return ticket->envelope->read (entry.previewOgg, [generation, request] { return generation->load() != request; }); },
            [safe, generation, request, ticket] (juce::Result result)
            {
                if (safe == nullptr || generation->load() != request) return;
                if (result.wasOk())
                {
                    safe->waveform = ticket->envelope; safe->applyOutputGain();
                    if (safe->playWhenLoaded) safe->play();
                }
                else safe->pathLabel.setTooltip (result.getErrorMessage());
                safe->repaint();
            });
    });
}

void WaveformPlayerComponent::detachSource()
{
    transport.setSource (nullptr);
    readerSource.reset();
    waitingForBuffer = false;
}

bool WaveformPlayerComponent::ensureAudioDevice()
{
    if (audioDeviceOpen) return true;
    if (! deviceSettingsLoaded) return false;
    auto xml = juce::parseXML (savedAudioState);
    if (savedAudioState.isNotEmpty() && ! xml)
    { playbackErrorFromDevice = true; playbackError = "Saved audio output is invalid. Use Audio to choose an output."; pathLabel.setText (playbackError, juce::dontSendNotification); return false; }
    const auto outputError = outputManager.initialise (0, 2, xml.get(), false);
    if (outputError.isNotEmpty())
    {
        playbackErrorFromDevice = true;
        playbackError = outputError;
        pathLabel.setText ("Audio output unavailable: " + playbackError, juce::dontSendNotification);
        return false;
    }
    sourcePlayer.setSource (&transport);
    outputManager.addAudioCallback (&sourcePlayer);
    audioDeviceOpen = true;
    clearDeviceError();
    return true;
}

void WaveformPlayerComponent::releaseAudioDevice()
{
    if (! audioDeviceOpen) return;
    audioDeviceOpen = false;
    sourcePlayer.setSource (nullptr);
    outputManager.removeAudioCallback (&sourcePlayer);
    outputManager.closeAudioDevice();
}
void WaveformPlayerComponent::armIdleRelease()
{
    idleSince = juce::Time::getMillisecondCounter();
    if (audioDeviceOpen) startTimer (250); else stopTimer();
}
void WaveformPlayerComponent::setUiActive (bool active)
{
    uiActive = active;
    if (transport.isPlaying() || waitingForBuffer) startTimer (active ? 42 : 250);
    else if (audioDeviceOpen) startTimer (250);
    if (active) { updateTimeLabel(); repaint(); }
}
void WaveformPlayerComponent::handleDeviceFailure (const juce::String& reason)
{
    if (! audioDeviceOpen) return;
    stop(); releaseAudioDevice(); stopTimer();
    playbackError = "Audio output stopped: " + reason + ". Use Audio to choose an output, then Play to retry.";
    playbackErrorFromDevice = true;
    pathLabel.setText (playbackError, juce::dontSendNotification);
    juce::Logger::writeToLog (playbackError);
}
void WaveformPlayerComponent::clearDeviceError()
{
    if (! playbackErrorFromDevice) return;
    playbackErrorFromDevice = false; playbackError = previewError;
    if (previewError.isNotEmpty()) pathLabel.setText (previewError, juce::dontSendNotification);
    else updatePathLabel();
}
void WaveformPlayerComponent::showAudioSettings()
{
    if (audioDialog != nullptr) { audioDialog->toFront (true); return; }
    if (! deviceSettingsLoaded) { openSettingsWhenReady = true; startTimer (100); return; }
    if (savedAudioState.isNotEmpty() && ! juce::parseXML (savedAudioState)) savedAudioState.clear();
    ensureAudioDevice();
    juce::DialogWindow::LaunchOptions options;
    auto* selector = new juce::AudioDeviceSelectorComponent (outputManager, 0, 0, 2, 2, false, false, true, false);
    // The player owns this dialog and clears it before its style reference dies.
    selector->setLookAndFeel (&getLookAndFeel());
    selector->setSize (500, 340);
    options.content.setOwned (selector);
    options.dialogTitle = "Audio output";
    options.dialogBackgroundColour = RememberUI::colour (Pal::Surface);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.escapeKeyTriggersCloseButton = true;
    audioDialog = options.launchAsync();
    armIdleRelease();
}

void WaveformPlayerComponent::stop()
{
    if (readerSource) readerSource->setActive (false);
    // Rebinding stops immediately without AudioTransportSource's one-second
    // polling wait if a device has disappeared. ReleaseResources does no I/O.
    transport.setSource (nullptr);
    if (readerSource) transport.setSource (readerSource.get(), 0, nullptr, readerSource->sampleRate());
    transport.setPosition (0.0);
    playBtn.setButtonText (">");
    armIdleRelease();
    playWhenLoaded = false;
    waitingForBuffer = false;
}

// ── Playback ─────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::togglePlay()
{
    if (! isLoaded) return;
    if (normalize && ! waveform && ! transport.isPlaying())
    { playWhenLoaded = ! playWhenLoaded; playBtn.setButtonText (playWhenLoaded ? "..." : ">"); return; }
    if (waitingForBuffer) { waitingForBuffer = false; playBtn.setButtonText (">"); armIdleRelease(); return; }
    if (transport.isPlaying())
    {
        readerSource->setActive (false);
        const auto position = this->position();
        transport.setSource (nullptr);
        transport.setSource (readerSource.get(), 0, nullptr, readerSource->sampleRate());
        transport.setPosition (position);
        playBtn.setButtonText (">");
        armIdleRelease();
    }
    else
    {
        readerSource->requestPrefill();
        if (position() >= duration() - 0.05) readerSource->setNextReadPosition (0);
        if (! deviceSettingsLoaded || ! readerSource->ready ((int) (readerSource->sampleRate() * 0.25)))
        { waitingForBuffer = true; playBtn.setButtonText ("..."); startTimer (uiActive ? 42 : 250); return; }
        if (! ensureAudioDevice()) { playBtn.setButtonText (">"); if (audioDialog == nullptr) stopTimer(); return; }
        transport.start();
        readerSource->setActive (true);
        playBtn.setButtonText ("||");
        startTimer (uiActive ? 42 : 250);
    }
}

void WaveformPlayerComponent::play()
{
    if (isLoading) { playWhenLoaded = true; return; }
    if (normalize && ! waveform && isLoaded) { playWhenLoaded = true; return; }
    if (! isLoaded || transport.isPlaying() || waitingForBuffer) return;
    togglePlay();
}

void WaveformPlayerComponent::seekTo (double norm)
{
    if (isLoaded)
    {
        const auto target = juce::jlimit (0.0, duration(), norm * duration());
        if (audioDeviceOpen) transport.setPosition (target);
        else readerSource->setNextReadPosition ((juce::int64) (target * readerSource->sampleRate()));
        readerSource->requestPrefill();
    }
}
double WaveformPlayerComponent::duration() const { return readerSource ? readerSource->getTotalLength() / readerSource->sampleRate() : 0; }
double WaveformPlayerComponent::position() const { return readerSource ? readerSource->getNextReadPosition() / readerSource->sampleRate() : 0; }

double WaveformPlayerComponent::getPositionNormalized() const
{
    double len = duration();
    return len > 0.0 ? position() / len : 0.0;
}

void WaveformPlayerComponent::updateTimeLabel()
{
    if (! isLoaded) return;
    auto toStr = [] (double s)
    {
        int m = (int) s / 60, sec = (int) s % 60;
        return juce::String::formatted ("%d:%02d", m, sec);
    };
    timeLabel.setText (toStr (position()) + " / " + toStr (duration()),
                       juce::dontSendNotification);
}

void WaveformPlayerComponent::updatePathLabel()
{
    pathLabel.setText (currentEntry.pending ? "Unsaved project - save in the DAW to keep this audio"
                       : currentEntry.projectProblem.isNotEmpty() ? currentEntry.projectProblem
                       : currentEntry.projectPath.isEmpty()
                           ? "(no project file - press Assign path)" : currentEntry.projectPath,
                       juce::dontSendNotification);
    pathLabel.setTooltip (currentEntry.projectProblem + "\n" + currentEntry.projectPath);
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::changeListenerCallback (juce::ChangeBroadcaster* broadcaster)
{
    if (broadcaster != &outputManager) return;
    auto* device = outputManager.getCurrentAudioDevice();
    if (! audioDeviceOpen && audioDialog != nullptr && device && device->isOpen())
    {
        sourcePlayer.setSource (&transport); outputManager.addAudioCallback (&sourcePlayer);
        audioDeviceOpen = true; armIdleRelease();
    }
    if (! audioDeviceOpen) return;
    if (! device || ! device->isOpen())
    {
        const auto reason = device ? device->getLastError() : juce::String{};
        handleDeviceFailure (reason.isNotEmpty() ? reason : "Output device disconnected"); return;
    }
    clearDeviceError();
    if (auto xml = outputManager.createStateXml())
    {
        savedAudioState = xml->toString();
        if (persistAudioState && savedAudioState != lastSavedAudioState)
        {
            lastSavedAudioState = savedAudioState;
            const auto state = savedAudioState;
            auto safe = juce::Component::SafePointer<WaveformPlayerComponent> (this);
            ProjectScanner::editAsync ([state]
            {
                juce::var patch (new juce::DynamicObject()); patch.getDynamicObject()->setProperty ("audioDeviceState", state);
                return LibraryStore::patchJson (LibraryStore::configFile(), patch, true);
            }, [safe] (juce::Result result)
            { if (safe != nullptr && result.failed()) { safe->playbackErrorFromDevice = false; safe->playbackError = "Cannot save audio output: " + result.getErrorMessage(); safe->pathLabel.setText (safe->playbackError, juce::dontSendNotification); } });
        }
    }
}

void WaveformPlayerComponent::timerCallback()
{
    if (openSettingsWhenReady && deviceSettingsLoaded) { openSettingsWhenReady = false; showAudioSettings(); }
    if (audioDeviceOpen)
    {
        auto* device = outputManager.getCurrentAudioDevice();
        if (! device || ! device->isOpen() || ! device->isPlaying())
        { handleDeviceFailure (device ? device->getLastError() : "Output device disconnected"); return; }
    }
    if (readerSource && readerSource->readFailed())
    {
        stop(); releaseAudioDevice(); stopTimer(); isLoaded = false; playBtn.setEnabled (false);
        playbackError = "Preview read failed: " + currentEntry.previewOgg.getFullPathName() + " (" + readerSource->error() + ")";
        playbackErrorFromDevice = false;
        previewError = playbackError;
        pathLabel.setText (playbackError, juce::dontSendNotification); return;
    }
    if (waitingForBuffer)
    {
        if (deviceSettingsLoaded && readerSource && readerSource->ready ((int) (readerSource->sampleRate() * 0.25)))
        { waitingForBuffer = false; togglePlay(); }
        return;
    }
    if (transport.isPlaying())
    {
        if (uiActive) { updateTimeLabel(); repaint(); }
        const auto underruns = getUnderruns();
        if (underruns != reportedUnderruns)
        {
            reportedUnderruns = underruns;
            juce::Logger::writeToLog ("Playback underruns=" + juce::String ((juce::int64) underruns)
                + " cacheBytes=" + juce::String ((juce::int64) readerSource->bufferBytes()));
            pathLabel.setTooltip ("Storage could not supply audio in time. Underruns: " + juce::String ((juce::int64) underruns));
        }
    }

    if (isLoaded && transport.hasStreamFinished())
    {
        if (auto following = queue.next (true)) { loadEntry (*following); play(); return; }
        readerSource->setActive (false);
        transport.setPosition (0.0);
        playBtn.setButtonText (">");
        armIdleRelease();
    }
    else if (! transport.isPlaying() && audioDeviceOpen && audioDialog == nullptr
             && juce::Time::getMillisecondCounter() - idleSince >= 2000)
    { releaseAudioDevice(); stopTimer(); }
}

void WaveformPlayerComponent::mouseDown (const juce::MouseEvent& e)
{
    if (waveArea.contains (e.getPosition()))
    {
        seekTo ((double) (e.x - waveArea.getX()) / (double) waveArea.getWidth());
        updateTimeLabel();
        repaint();
    }
}

void WaveformPlayerComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Only scrub if the drag started inside the waveform area — and clamp the
    // x position so dragging outside the bounds pins the playhead to the edge.
    if (! waveArea.contains (e.getMouseDownPosition())) return;

    int x = juce::jlimit (waveArea.getX(), waveArea.getRight(), e.x);
    double norm = (double) (x - waveArea.getX()) / (double) waveArea.getWidth();
    seekTo (norm);
    updateTimeLabel();
    repaint();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));
    RememberUI::panel (g, getLocalBounds().toFloat().reduced (12, 6));
    if (! isLoaded)
    {
        g.setColour (RememberUI::colour (Pal::Subtext));
        g.setFont (RememberUI::font (13));
        g.drawText (isLoading ? "Loading preview..." : "Select a recording to listen to its preview",
                    waveArea, juce::Justification::centred);
        return;
    }

    // Waveform background
    g.setColour (RememberUI::colour (Pal::BgDeep));
    g.fillRoundedRectangle (waveArea.toFloat(), 8.0f);

    if (waveform && ! waveform->channels[0].empty())
    {
        // Waveform fill
        g.setColour (RememberUI::colour (Pal::Accent).withAlpha (0.7f));
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& peaks = waveform->channels[(size_t) ch];
            const auto centre = waveArea.getY() + waveArea.getHeight() * (0.25f + 0.5f * ch);
            const auto scale = waveArea.getHeight() * 0.225f;
            for (int x = 0; x < waveArea.getWidth(); ++x)
            {
                const auto first = (size_t) x * peaks.size() / waveArea.getWidth();
                const auto last = juce::jmin (peaks.size(), juce::jmax (first + 1, (size_t) (x + 1) * peaks.size() / waveArea.getWidth()));
                float low = 0, high = 0;
                for (auto i = first; i < last; ++i) { low = juce::jmin (low, peaks[i].low); high = juce::jmax (high, peaks[i].high); }
                g.drawVerticalLine (waveArea.getX() + x, centre - juce::jmin (1.0f, high) * scale,
                                   centre - juce::jmax (-1.0f, low) * scale);
            }
        }

    }
    // The cursor is usable while the background waveform is still loading.
    float posX = waveArea.getX() + (float) getPositionNormalized() * waveArea.getWidth();
    g.setColour (RememberUI::colour (Pal::Text).withAlpha (0.8f));
    g.drawLine (posX, (float) waveArea.getY(), posX, (float) waveArea.getBottom(), 1.5f);
}

// ── Layout ─────────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::resized()
{
    auto area = getLocalBounds().reduced (26, 16);

    auto topRow = area.removeFromTop (34);
    shuffleBtn.setBounds (topRow.removeFromLeft (32)); previousBtn.setBounds (topRow.removeFromLeft (32));
    playBtn.setBounds (topRow.removeFromLeft (36)); nextBtn.setBounds (topRow.removeFromLeft (32)); repeatBtn.setBounds (topRow.removeFromLeft (32));
    audioBtn.setBounds (topRow.removeFromRight (56).reduced (2, 2));
    playbackOptionsBtn.setBounds (topRow.removeFromRight (30)); normalizeBtn.setBounds (topRow.removeFromRight (30));
    volumeSlider.setBounds (topRow.removeFromRight (getWidth() >= 800 ? 100 : 72)); muteBtn.setBounds (topRow.removeFromRight (30));
    timeLabel.setBounds (topRow.removeFromRight (80)); nameLabel.setBounds (topRow.reduced (6, 0));
    area.removeFromTop (6);

    // Bottom row: open + assign + path
    auto btmRow = area.removeFromBottom (32);
    openBtn.setBounds   (btmRow.removeFromRight (110).reduced (2, 0));
    btmRow.removeFromRight (6);
    assignBtn.setBounds (btmRow.removeFromRight (130).reduced (2, 0));
    pathLabel.setBounds (btmRow);

    area.removeFromBottom (6);

    // Waveform occupies the middle
    waveArea = area;
}

void WaveformPlayerComponent::next (bool automatic)
{
    const bool resume = automatic || isPlaying() || waitingForBuffer || playWhenLoaded;
    if (auto entry = queue.next (automatic)) { loadEntry (*entry); if (resume) play(); }
}
void WaveformPlayerComponent::previous()
{
    const bool resume = isPlaying() || waitingForBuffer || playWhenLoaded;
    if (position() > 3.0) { seekTo (0); updateTimeLabel(); repaint(); return; }
    if (auto entry = queue.previous()) { loadEntry (*entry); if (resume) play(); }
}
void WaveformPlayerComponent::setVolume (double value)
{
    volumeSlider.setValue (juce::jlimit (0.0, 1.0, value), juce::dontSendNotification);
    if (value > 0) { volumeSlider.getProperties().set ("rememberAudibleVolume", getVolume()); muteBtn.setToggleState (false, juce::dontSendNotification); }
    applyOutputGain(); savePlaybackPreferences();
}
void WaveformPlayerComponent::setMuted (bool value)
{
    muteBtn.setToggleState (value, juce::dontSendNotification);
    if (! value && getVolume() == 0)
        volumeSlider.setValue ((double) volumeSlider.getProperties().getWithDefault ("rememberAudibleVolume", 1.0), juce::dontSendNotification);
    RememberUI::iconButton (muteBtn, value ? "mute" : "volume", value ? "Unmute volume" : "Mute volume");
    applyOutputGain(); savePlaybackPreferences();
}
void WaveformPlayerComponent::setNormalized (bool value)
{
    normalize = value; normalizeBtn.setToggleState (value, juce::dontSendNotification);
    applyOutputGain(); savePlaybackPreferences();
    if (! value && playWhenLoaded && isLoaded) play();
}
void WaveformPlayerComponent::applyOutputGain()
{
    const auto volume = volumeSlider.getValue();
    if (volume > 0) volumeSlider.getProperties().set ("rememberAudibleVolume", volume);
    RememberUI::iconButton (muteBtn, isMuted() || volume == 0 ? "mute" : "volume", isMuted() || volume == 0 ? "Unmute volume" : "Mute volume");
    const auto gain = normalize && waveform ? waveform->normalizationGain : 1.0f;
    transport.setGain (isMuted() ? 0.0f : (float) (volume * volume) * gain);
    normalizeBtn.setTooltip (normalize && ! waveform ? "Measuring preview loudness..." : "Normalize preview loudness to -14 LUFS (1 dB decoded peak headroom)");
}
void WaveformPlayerComponent::savePlaybackPreferences()
{
    if (applyingPreferences || ! persistAudioState) return;
    preferencesTouched = true;
    juce::var patch (new juce::DynamicObject()); auto& o = *patch.getDynamicObject();
    o.setProperty ("playbackVolume", getVolume()); o.setProperty ("playbackMuted", isMuted()); o.setProperty ("normalizePlayback", normalize);
    o.setProperty ("shufflePlayback", queue.shuffle); o.setProperty ("repeatPlayback", (int) queue.repeat); o.setProperty ("autoplaySelection", autoplay);
    auto safe = juce::Component::SafePointer<WaveformPlayerComponent> (this);
    ProjectScanner::editAsync ([patch] { return LibraryStore::patchJson (LibraryStore::configFile(), patch, true); }, [safe] (juce::Result result)
    { if (safe != nullptr && result.failed()) { safe->pathLabel.setText ("Cannot save playback preferences", juce::dontSendNotification); safe->pathLabel.setTooltip (result.getErrorMessage()); } });
}
void WaveformPlayerComponent::showPlaybackOptions()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Play when selecting a recording", true, autoplay);
    menu.addItem (2, "Normalize preview volume", true, normalize);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&playbackOptionsBtn), [safe = juce::Component::SafePointer<WaveformPlayerComponent> (this)] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) { safe->autoplay = ! safe->autoplay; safe->savePlaybackPreferences(); }
        if (r == 2) safe->setNormalized (! safe->normalize);
    });
}
