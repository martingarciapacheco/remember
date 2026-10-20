#include "../../Shared/PreviewAudio.h"
#include "WaveformPlayerComponent.h"
#include "ProjectListComponent.h"   // for Pal::
#include "../../Shared/LibraryStore.h"
#include "PreviewExport.h"

namespace
{
std::atomic<int> activeLoads { 0 };
struct WaveformPreferences : juce::ChangeBroadcaster
{
    bool colorful = false, separate = false, requested = false, touched = false;
};
WaveformPreferences& waveformPreferences() { static WaveformPreferences value; return value; }
void saveWaveformPreferences()
{
    auto& preferences = waveformPreferences(); preferences.touched = true;
    preferences.sendSynchronousChangeMessage();
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty ("colorfulWaveforms", preferences.colorful);
    patch.getDynamicObject()->setProperty ("separateWaveformChannels", preferences.separate);
    ProjectScanner::editAsync ([patch] { return LibraryStore::patchJson (LibraryStore::configFile(), patch, true); },
        [] (juce::Result result)
        {
            if (result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                "Cannot save waveform preferences", result.getErrorMessage());
        });
}
struct LoadTicket
{
    LoadTicket() { activeLoads.fetch_add (1); }
    ~LoadTicket() { source.reset(); activeLoads.fetch_sub (1); }
    std::unique_ptr<ReadAheadSource> source;
    std::shared_ptr<WaveformEnvelope> envelope;
    int low = 127, high = 0;
};
}
int WaveformPlayerComponent::pendingLoads() { return activeLoads.load(); }

WaveformPlayerComponent::WaveformPlayerComponent (juce::AudioDeviceManager* externalDevice, bool persistDeviceState)
    : outputManager (externalDevice ? *externalDevice : deviceManager), persistAudioState (persistDeviceState)
{
    auto safePlayer = juce::Component::SafePointer<WaveformPlayerComponent> (this);
    setTooltip (RememberUI::shortcutTooltip ("Seek preview", "Alt+Left / Alt+Right", "Move backward or forward by 5 seconds."));
    outputManager.addChangeListener (this);
    auto& visual = waveformPreferences();
    visual.addChangeListener (this);
    if (! visual.requested)
    {
        visual.requested = true;
        auto saved = std::make_shared<juce::var>();
        ProjectScanner::queryAsync ([saved] { *saved = LibraryStore::readJson (LibraryStore::configFile()); return juce::Result::ok(); },
            [saved] (juce::Result result)
            {
                auto& state = waveformPreferences();
                if (result.failed() || state.touched) return;
                state.colorful = (bool) saved->getProperty ("colorfulWaveforms", false);
                state.separate = (bool) saved->getProperty ("separateWaveformChannels", false);
                state.sendSynchronousChangeMessage();
            });
    }
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
                safePlayer->queue.repeat = (PlaybackQueue::Repeat) juce::jlimit (0, 3, (int) preferences->getProperty ("repeatPlayback", 0));
                safePlayer->metronome.setVolume ((float) juce::jlimit (0.0, 1.0, (double) preferences->getProperty ("metronomeVolume", 0.5)));
                safePlayer->updateRepeatButton();
                safePlayer->applyOutputGain(); safePlayer->applyingPreferences = false;
            }
        });
    }
    playBtn.setEnabled (false);
    RememberUI::iconButton (previousBtn, "previous", "Previous preview; restart after 3 seconds");
    RememberUI::iconButton (nextBtn, "next", "Next preview");
    previousBtn.setTooltip (RememberUI::shortcutTooltip ("Previous preview", "Ctrl+Left", "Restart the current preview after 3 seconds; otherwise play the previous preview."));
    nextBtn.setTooltip (RememberUI::shortcutTooltip ("Next preview", "Ctrl+Right"));
    RememberUI::iconButton (shuffleBtn, "shuffle", "Shuffle previews");
    shuffleBtn.setTooltip (RememberUI::shortcutTooltip (shuffleBtn.getTitle(), "S"));
    RememberUI::iconButton (repeatBtn, "repeat", "Repeat: off");
    RememberUI::iconButton (metronomeBtn, "metronome", "Metronome");
    metronomeBtn.setTooltip ("Metronome follows the project BPM. Right-click to adjust its volume.");
    metronomeBtn.setClickingTogglesState (true); metronomeBtn.setEnabled (false);
    metronomeBtn.onClick = [this] { setMetronomeEnabled (metronomeBtn.getToggleState()); };
    metronomeBtn.onContextMenu = [this] { showMetronomeVolume(); };
    RememberUI::iconButton (muteBtn, "volume", "Mute volume");
    muteBtn.setTooltip (RememberUI::shortcutTooltip (muteBtn.getTitle(), "M"));
    RememberUI::iconButton (normalizeBtn, "normalize", "Normalize preview loudness to -14 LUFS");
    for (auto* button : std::initializer_list<juce::TextButton*> { &previousBtn, &nextBtn, &shuffleBtn, static_cast<juce::TextButton*> (&repeatBtn), static_cast<juce::TextButton*> (&metronomeBtn), &muteBtn, &normalizeBtn, &playbackOptionsBtn })
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
        queue.repeat = (PlaybackQueue::Repeat) (((int) queue.repeat + 1) % 4);
        updateRepeatButton(); savePlaybackPreferences();
    };
    repeatBtn.onContextMenu = [this] { showRepeatOptions(); };
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

    RememberUI::iconButton (playBtn, "play", "Play or pause preview");
    playBtn.setTooltip (RememberUI::shortcutTooltip (playBtn.getTitle(), "Space"));
    playBtn.getProperties().set ("rememberTransportPrimary", true);
    btnSetup (playBtn, Pal::Action,   Pal::OnAction);
    btnSetup (openBtn,   Pal::Elevated, Pal::Text);
    setOpenProjectAvailable (false);
    btnSetup (assignBtn, Pal::Elevated, Pal::Subtext);
    assignBtn.setEnabled (false);
    btnSetup (dragBtn, Pal::Elevated, Pal::Text);
    dragBtn.setTitle ("Drag preview audio");
    dragBtn.setDescription ("Drag a copy of the complete preview as a WAV file to a DAW or folder.");
    dragBtn.setTooltip ("Select a recording to prepare its WAV copy");
    dragBtn.setEnabled (false);
    dragBtn.begin = [this]
    {
        if (exportedPreview == juce::File{}) return;
        if (! startPreviewDrag ({ exportedPreview.getFullPathName() }, false, &dragBtn))
            dragBtn.setTooltip ("Cannot start drag. Release the mouse and try again.");
    };
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
        if (! assignBtn.isEnabled()) return;
        const auto target = projectAssignmentTarget();
        if (! target)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                "Assign project path", "Select one available recording in the library first.");
            return;
        }
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select project file",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.als;*.flp;*.ptx;*.song;*.npr;*.rpp;*.cpr;*.bwproject");

        auto safe = juce::Component::SafePointer<WaveformPlayerComponent> (this);
        auto selected = *target;
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
                        safe->setOpenProjectAvailable (association->projectAvailable);
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
    addAndMakeVisible (dragBtn);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (pathLabel);
    addAndMakeVisible (timeLabel);
    addAndMakeVisible (audioBtn);

    // Timer starts only when playback begins (saves CPU while idle)
}

WaveformPlayerComponent::~WaveformPlayerComponent()
{
    stopTimer();
    cursorVBlank = {};
    if (metronomePopup != nullptr) metronomePopup->dismiss();
    loadGeneration->fetch_add (1);
    outputManager.removeChangeListener (this);
    waveformPreferences().removeChangeListener (this);
    if (audioDialog != nullptr)
    { audioDialog->clearContentComponent(); audioDialog->exitModalState (0); }
    detachSource();
    sourcePlayer.setSource (nullptr);
    outputManager.removeAudioCallback (&sourcePlayer);
    outputManager.closeAudioDevice();
}

// ── Public API ────────────────────────────────────────────────────────────────

std::optional<ProjectEntry> WaveformPlayerComponent::projectAssignmentTarget() const
{
    const auto selection = assignmentSelection ? assignmentSelection() : juce::Array<ProjectEntry> { currentEntry };
    if (selection.size() != 1) return std::nullopt;
    const auto& entry = selection.getReference (0);
    if (entry.metadataFile == juce::File{} || entry.pending || entry.trashed || entry.storageError.isNotEmpty())
        return std::nullopt;
    return entry;
}

void WaveformPlayerComponent::suspendOutput()
{
    if (transport.isPlaying()) togglePlay();
    playWhenLoaded = false; waitingForBuffer = false;
    if (readerSource) readerSource->setActive (false);
    releaseAudioDevice(); updatePlaybackUpdates();
}

void WaveformPlayerComponent::clearPreview()
{
    const auto file = currentEntry.metadataFile;
    releaseEntry (file);
}

void WaveformPlayerComponent::releaseEntry (const juce::File& metadataFile)
{
    if (currentEntry.metadataFile != metadataFile) return;
    loadGeneration->fetch_add (1);
    stop(); detachSource();
    currentEntry = {}; waveform.reset(); exportedPreview = {}; dragBtn.setEnabled (false);
    dragBtn.setTooltip ("Select a recording to prepare its WAV copy");
    metronome.setTempo (0); metronomeBtn.setEnabled (false);
    isLoaded = false; isLoading = false; playWhenLoaded = false;
    playbackError.clear(); previewError.clear(); playbackErrorFromDevice = false;
    playBtn.setEnabled (false); setOpenProjectAvailable (false); assignBtn.setEnabled (false);
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
    exportedPreview = {}; dragBtn.setEnabled (false);
    dragBtn.setTooltip (entry.pending ? "Recording is not ready for export" : entry.auditionReader
        ? "Use the MIDI row's Drag action" : "Preparing complete preview WAV...");
    queue.select (entry);
    const auto tempo = entry.bpmOverride > 0 ? entry.bpmOverride : entry.bpm;
    metronome.setTempo (tempo);
    metronomeBtn.setEnabled (std::isfinite (tempo) && tempo >= 10 && tempo <= 1000);
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
    normalizeBtn.setEnabled (! entry.auditionReader);
    applyOutputGain();
    playBtn.setEnabled (false);
    repaint();

    nameLabel.setText (entry.name, juce::dontSendNotification);
    updatePathLabel();
    timeLabel.setText ("", juce::dontSendNotification);

    setOpenProjectAvailable (entry.projectAvailable);
    assignBtn.setEnabled (! entry.pending);

    if (entry.storageError.isNotEmpty())
    {
        isLoading = false; playbackError = previewError = entry.storageError;
        dragBtn.setTooltip ("Cannot prepare WAV: " + entry.storageError);
        pathLabel.setText (playbackError, juce::dontSendNotification);
        return;
    }

    if (! compactMode && ! entry.pending && ! entry.auditionReader)
    {
        const auto generation = loadGeneration;
        auto exported = std::make_shared<juce::File>();
        ProjectScanner::queryAsync ([entry, generation, request, exported]
        {
            if (generation->load() != request) return juce::Result::ok();
            return PreviewExport::prepare (entry, ProjectScanner::getPreviewsFolder(), *exported,
                [generation, request] { return generation->load() != request; });
        }, [safe = juce::Component::SafePointer<WaveformPlayerComponent> (this), generation, request, exported] (juce::Result result)
        {
            if (! safe || generation->load() != request) return;
            safe->exportedPreview = *exported;
            safe->dragBtn.setEnabled (result.wasOk() && *exported != juce::File{});
            safe->dragBtn.setTooltip (result.wasOk() ? "Drag the complete preview as WAV to a DAW or folder. Copies remain in Exports."
                                                    : "Cannot prepare WAV: " + result.getErrorMessage());
        });
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
        if (entry.auditionNotes) for (const auto& note : *entry.auditionNotes)
        { ticket->low = juce::jmin (ticket->low, note.key); ticket->high = juce::jmax (ticket->high, note.key); }
        juce::AudioFormatManager formats;
        PreviewAudio::registerFormats (formats);
        std::unique_ptr<juce::AudioFormatReader> reader (entry.auditionReader ? entry.auditionReader() : std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (entry.previewOgg)));
        if (generation->load() != request) return;
        if (reader) ticket->source = std::make_unique<ReadAheadSource> (std::move (reader), entry.auditionReader ? 8192 : 0);
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
            safe->midiLow = ticket->low; safe->midiHigh = ticket->high;
            safe->readerSource = std::move (ticket->source);
            safe->transport.setSource (safe->readerSource.get(), 0, nullptr, safe->readerSource->sampleRate());
            safe->isLoaded = true;
            safe->playBtn.setEnabled (true);
            safe->playBtn.setButtonText (">");
            safe->updateTimeLabel();
            if (safe->playWhenLoaded) safe->play();
            safe->repaint();
        });
        if (entry.auditionReader) return; // No audio file or precomputed waveform for MIDI.
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
                else safe->pathLabel.setDescription (result.getErrorMessage());
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
    sourcePlayer.setSource (&metronome);
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
    updatePlaybackUpdates();
}
void WaveformPlayerComponent::setUiActive (bool active)
{
    uiActive = active;
    updatePlaybackUpdates();
    if (active) { updateTimeLabel(); repaint(); }
}
void WaveformPlayerComponent::visibilityChanged() { updatePlaybackUpdates(); }
void WaveformPlayerComponent::parentHierarchyChanged() { updatePlaybackUpdates(); }

void WaveformPlayerComponent::updatePlaybackUpdates()
{
    const bool visible = uiActive && isShowing();
    const bool animate = visible && isLoaded && transport.isPlaying();
    if (animate && getPeer() != nullptr)
    {
        if (cursorVBlank.isEmpty())
        {
            lastVBlankTime = juce::Time::getMillisecondCounterHiRes();
            cursorPacer.reset();
            cursorVBlank = juce::VBlankAttachment (this, [this] (double timestamp)
            {
                lastVBlankTime = juce::Time::getMillisecondCounterHiRes();
                if (cursorPacer.advance (timestamp)) drawPlaybackFrame();
            });
        }
    }
    else
    {
        cursorVBlank = {};
        cursorPacer.reset();
    }

    // Device checks, prefill, queue completion and idle release have their own
    // low-rate polling. Audio dialogs cannot change the display's frame rate.
    // Fall back to 60 Hz if the peer has no vblank events (e.g. remote desktop).
    const bool fallback = animate && (cursorVBlank.isEmpty()
        || juce::Time::getMillisecondCounterHiRes() - lastVBlankTime > 100.0);
    const int interval = fallback ? 16
        : transport.isPlaying() || waitingForBuffer ? (visible ? 50 : 250)
        : openSettingsWhenReady ? 100 : audioDeviceOpen ? 250 : 0;
    if (interval == 0) stopTimer();
    else if (getTimerInterval() != interval) startTimer (interval);
}

void WaveformPlayerComponent::drawPlaybackFrame()
{
    if (! uiActive || ! isShowing() || ! isLoaded || ! transport.isPlaying()) return;
    if (auto seconds = readerSource->presentationPosition (juce::Time::getMillisecondCounterHiRes() * 0.001))
        repaintCursor (*seconds);
}

void WaveformPlayerComponent::repaintCursor (double seconds)
{
    const auto length = duration();
    seconds = juce::jlimit (0.0, length, seconds);
    if (cursorSeconds == seconds) return;
    const auto strip = [&] (double position)
    {
        const auto x = waveArea.getX() + (length > 0 ? position / length : 0) * waveArea.getWidth();
        return juce::Rectangle<int> ((int) std::floor (x) - 2, waveArea.getY(), 6, waveArea.getHeight());
    };
    const auto previous = strip (cursorSeconds);
    cursorSeconds = seconds;
    repaint (previous);
    repaint (strip (cursorSeconds));
}

void WaveformPlayerComponent::resetCursor()
{
    repaintCursor (position());
    updateTimeLabel();
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
    if (! deviceSettingsLoaded) { openSettingsWhenReady = true; updatePlaybackUpdates(); return; }
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
    playWhenLoaded = false;
    waitingForBuffer = false;
    resetCursor();
    armIdleRelease();
}

// ── Playback ─────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::togglePlay()
{
    if (! isLoaded) return;
    if (normalize && ! currentEntry.auditionReader && ! waveform && ! transport.isPlaying())
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
        resetCursor();
        armIdleRelease();
    }
    else
    {
        readerSource->requestPrefill();
        if (position() >= duration() - 0.05) { readerSource->setNextReadPosition (0); resetCursor(); }
        if (! deviceSettingsLoaded || ! readerSource->ready ((int) (readerSource->sampleRate() * (currentEntry.auditionReader ? 0.05 : 0.25))))
        { waitingForBuffer = true; playBtn.setButtonText ("..."); updatePlaybackUpdates(); return; }
        if (! ensureAudioDevice()) { playBtn.setButtonText (">"); updatePlaybackUpdates(); return; }
        resetCursor();
        transport.start();
        readerSource->setActive (true);
        playBtn.setButtonText ("||");
        updatePlaybackUpdates();
    }
}

void WaveformPlayerComponent::play()
{
    if (isLoading) { playWhenLoaded = true; return; }
    if (normalize && ! currentEntry.auditionReader && ! waveform && isLoaded) { playWhenLoaded = true; return; }
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
        resetCursor();
    }
}
double WaveformPlayerComponent::duration() const { return readerSource ? readerSource->getTotalLength() / readerSource->sampleRate() : 0; }
double WaveformPlayerComponent::position() const { return readerSource ? readerSource->getNextReadPosition() / readerSource->sampleRate() : 0; }

double WaveformPlayerComponent::getPositionNormalized() const
{
    double len = duration();
    return len > 0.0 ? (transport.isPlaying() ? cursorSeconds : position()) / len : 0.0;
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
    pathLabel.setDescription (currentEntry.projectProblem + "\n" + currentEntry.projectPath);
}

void WaveformPlayerComponent::setOpenProjectAvailable (bool available)
{
    openBtn.setEnabled (available);
    openBtn.setTooltip (available ? RememberUI::shortcutTooltip ("Open selected project in DAW", "Ctrl+O") : juce::String{});
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::changeListenerCallback (juce::ChangeBroadcaster* broadcaster)
{
    if (broadcaster == &waveformPreferences()) { repaint (waveArea); return; }
    if (broadcaster != &outputManager) return;
    auto* device = outputManager.getCurrentAudioDevice();
    if (! audioDeviceOpen && audioDialog != nullptr && device && device->isOpen())
    {
        sourcePlayer.setSource (&metronome); outputManager.addAudioCallback (&sourcePlayer);
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
        if (deviceSettingsLoaded && readerSource && readerSource->ready ((int) (readerSource->sampleRate() * (currentEntry.auditionReader ? 0.05 : 0.25))))
        { waitingForBuffer = false; togglePlay(); }
        return;
    }
    if (transport.isPlaying())
    {
        updatePlaybackUpdates();
        if (uiActive && isShowing())
        {
            updateTimeLabel();
            if (cursorVBlank.isEmpty() || juce::Time::getMillisecondCounterHiRes() - lastVBlankTime > 100.0)
                drawPlaybackFrame();
        }
        const auto underruns = getUnderruns();
        if (underruns != reportedUnderruns)
        {
            reportedUnderruns = underruns;
            juce::Logger::writeToLog ("Playback underruns=" + juce::String ((juce::int64) underruns)
                + " cacheBytes=" + juce::String ((juce::int64) readerSource->bufferBytes()));
            pathLabel.setDescription ("Storage could not supply audio in time. Underruns: " + juce::String ((juce::int64) underruns));
        }
    }

    if (isLoaded && transport.hasStreamFinished())
    {
        if (auto following = queue.next (true)) { loadEntry (*following); play(); return; }
        readerSource->setActive (false);
        transport.setPosition (0.0);
        playBtn.setButtonText (">");
        resetCursor();
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
        if (e.mods.isPopupMenu()) { showWaveformOptions(); return; }
        if (! e.mods.isLeftButtonDown()) return;
        seekTo ((double) (e.x - waveArea.getX()) / (double) waveArea.getWidth());
    }
}

void WaveformPlayerComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Only scrub if the drag started inside the waveform area — and clamp the
    // x position so dragging outside the bounds pins the playhead to the edge.
    if (! e.mods.isLeftButtonDown() || e.mods.isPopupMenu() || ! waveArea.contains (e.getMouseDownPosition())) return;

    int x = juce::jlimit (waveArea.getX(), waveArea.getRight(), e.x);
    double norm = (double) (x - waveArea.getX()) / (double) waveArea.getWidth();
    seekTo (norm);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));
    RememberUI::panel (g, surfaceBounds().toFloat(),
                       RememberUI::surfaceRadius, false);
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
    g.fillRoundedRectangle (waveArea.toFloat(), RememberUI::controlRadius);

    if (waveform && ! waveform->channels[0].empty() && ! waveArea.isEmpty())
    {
        const bool separate = hasSeparateWaveformChannels() && waveform->channelCount > 1;
        for (int ch = 0; ch < waveform->channelCount; ++ch)
        {
            const auto& peaks = waveform->channels[(size_t) ch];
            auto channelArea = waveArea;
            if (separate)
                channelArea = { waveArea.getX(), waveArea.getY() + ch * waveArea.getHeight() / 2,
                                waveArea.getWidth(), waveArea.getHeight() / 2 };
            const auto centre = (float) channelArea.getCentreY();
            const auto scale = channelArea.getHeight() * 0.45f;
            if (separate)
            {
                g.setColour (RememberUI::colour (Pal::Border));
                g.drawHorizontalLine ((int) centre, (float) channelArea.getX(), (float) channelArea.getRight());
            }
            g.setColour (RememberUI::colour (ch == 0 ? Pal::WaveLeft : Pal::WaveRight));
            const auto clip = g.getClipBounds().getIntersection (waveArea);
            for (int x = clip.getX() - waveArea.getX(); x < clip.getRight() - waveArea.getX(); ++x)
            {
                const auto first = (size_t) x * peaks.size() / waveArea.getWidth();
                const auto last = juce::jmin (peaks.size(), juce::jmax (first + 1, (size_t) (x + 1) * peaks.size() / waveArea.getWidth()));
                float low = 0, high = 0, power = 0, moment = 0;
                for (auto i = first; i < last; ++i)
                {
                    low = juce::jmin (low, peaks[i].low); high = juce::jmax (high, peaks[i].high);
                    power += peaks[i].spectralPower; moment += peaks[i].frequencyMoment;
                }
                g.setColour (hasColorfulWaveforms() && power > 0 ? RememberUI::waveformFrequencyColour (moment / power)
                    : RememberUI::colour (ch == 0 ? Pal::WaveLeft : Pal::WaveRight));
                g.drawVerticalLine (waveArea.getX() + x, centre - juce::jmin (1.0f, high) * scale,
                                   centre - juce::jmax (-1.0f, low) * scale);
            }
            if (separate)
            {
                g.setColour (RememberUI::colour (Pal::Subtext)); g.setFont (RememberUI::font (11));
                g.drawText (ch == 0 ? "L" : "R", channelArea.reduced (4, 0).removeFromTop (14), juce::Justification::centredLeft);
            }
        }

    }
    if (currentEntry.auditionNotes && ! waveArea.isEmpty() && duration() > 0)
    {
        juce::Graphics::ScopedSaveState clip (g); g.reduceClipRegion (waveArea);
        const auto tempo = currentEntry.bpmOverride > 0 ? currentEntry.bpmOverride : currentEntry.bpm;
        const auto beats = duration() * tempo / 60.0;
        g.setColour (RememberUI::colour (Pal::Border));
        const int beatStride = juce::jmax (1, (int) std::ceil (beats / 128));
        for (int beat = 0; beat < beats; beat += beatStride)
            g.drawVerticalLine (waveArea.getX() + (int) (beat / beats * waveArea.getWidth()), (float) waveArea.getY(), (float) waveArea.getBottom());
        const auto bounds = waveArea.toFloat().reduced (4);
        const int range = juce::jmax (12, midiHigh - midiLow + 3);
        const auto& notes = *currentEntry.auditionNotes;
        const auto stride = juce::jmax<size_t> (1, (notes.size() + 2047) / 2048);
        g.setColour (RememberUI::colour (Pal::Accent));
        for (size_t i = 0; i < notes.size(); i += stride)
        {
            const auto& note = notes[i];
            g.fillRoundedRectangle (bounds.getX() + (float) (note.beat / beats) * bounds.getWidth(),
                bounds.getBottom() - (float) (note.key - midiLow + 2) / range * bounds.getHeight(),
                juce::jmax (2.0f, (float) (note.length / beats) * bounds.getWidth()), juce::jmax (2.0f, bounds.getHeight() / range), 1.0f);
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
    auto area = getLocalBounds().reduced (compactMode ? 20 : 26, 16);

    auto topRow = area.removeFromTop (34);
    shuffleBtn.setBounds (topRow.removeFromLeft (32)); previousBtn.setBounds (topRow.removeFromLeft (32));
    playBtn.setBounds (topRow.removeFromLeft (36)); nextBtn.setBounds (topRow.removeFromLeft (32)); repeatBtn.setBounds (topRow.removeFromLeft (32)); metronomeBtn.setBounds (topRow.removeFromLeft (32));
    audioBtn.setBounds (topRow.removeFromRight (56).reduced (2, 2));
    playbackOptionsBtn.setBounds (topRow.removeFromRight (30)); normalizeBtn.setBounds (topRow.removeFromRight (30));
    volumeSlider.setBounds (topRow.removeFromRight (getWidth() >= 800 ? 100 : 72)); muteBtn.setBounds (topRow.removeFromRight (30));
    timeLabel.setBounds (topRow.removeFromRight (80)); nameLabel.setBounds (topRow.reduced (6, 0));
    area.removeFromTop (6);

    // Bottom row: open + assign + path
    auto btmRow = compactMode ? juce::Rectangle<int>{} : area.removeFromBottom (32);
    openBtn.setBounds   (btmRow.removeFromRight (110).reduced (2, 0));
    btmRow.removeFromRight (6);
    assignBtn.setBounds (btmRow.removeFromRight (130).reduced (2, 0));
    btmRow.removeFromRight (6);
    dragBtn.setBounds (btmRow.removeFromRight (64).reduced (2, 0));
    pathLabel.setBounds (btmRow);

    if (! compactMode) area.removeFromBottom (6);

    // Waveform occupies the middle
    waveArea = area;
}

void WaveformPlayerComponent::updateRepeatButton()
{
    const char* names[] = { "off", "all", "one", "single" };
    const char* descriptions[] = { "Play the queue once", "Repeat the queue", "Repeat the selected preview",
                                   "Play the selected preview once, then stop" };
    const auto mode = (int) queue.repeat;
    repeatBtn.setToggleState (queue.repeat != PlaybackQueue::Off, juce::dontSendNotification);
    repeatBtn.getProperties().set ("rememberRepeatOne", queue.repeat == PlaybackQueue::One);
    RememberUI::iconButton (repeatBtn, queue.repeat == PlaybackQueue::Single ? "repeat-single" : "repeat",
                            "Repeat: " + juce::String (names[mode]));
    repeatBtn.setDescription (descriptions[mode]);
    repeatBtn.setTooltip (RememberUI::shortcutTooltip (repeatBtn.getTitle(), "L", juce::String (descriptions[mode]) + ". Left-click cycles modes; right-click chooses a mode."));
    repeatBtn.repaint();
}

void WaveformPlayerComponent::setCompactMode (bool value)
{
    if (metronomePopup != nullptr) metronomePopup->dismiss();
    compactMode = value;
    for (auto* component : std::initializer_list<juce::Component*> { &openBtn, &assignBtn, &dragBtn, &pathLabel }) component->setVisible (! value);
    resized(); repaint();
}
void WaveformPlayerComponent::setMetronomeEnabled (bool value)
{
    metronome.setEnabled (value); metronomeBtn.setToggleState (value, juce::dontSendNotification);
}
void WaveformPlayerComponent::setMetronomeVolume (double value, bool save)
{
    metronome.setVolume ((float) value);
    preferencesTouched = true;
    if (save) savePlaybackPreferences();
}
namespace
{
class MetronomeVolume final : public juce::Component
{
public:
    MetronomeVolume (double value, std::function<void(double, bool)> update)
    {
        label.setText ("Metronome volume", juce::dontSendNotification);
        label.setFont (RememberUI::font (13, true));
        RememberUI::bindColour (label, juce::Label::textColourId, Pal::Text); addAndMakeVisible (label);
        slider.setSliderStyle (juce::Slider::LinearHorizontal); slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 24);
        slider.setRange (0, 100, 1); slider.setValue (value * 100, juce::dontSendNotification); slider.setTextValueSuffix ("%");
        slider.setTitle ("Metronome volume");
        RememberUI::bindColour (slider, juce::Slider::textBoxTextColourId, Pal::Text);
        RememberUI::bindColour (slider, juce::Slider::textBoxBackgroundColourId, Pal::Content);
        RememberUI::bindColour (slider, juce::Slider::textBoxOutlineColourId, Pal::Border);
        RememberUI::bindColour (slider, juce::Slider::thumbColourId, Pal::Text);
        RememberUI::bindColour (slider, juce::Slider::trackColourId, Pal::Accent);
        RememberUI::bindColour (slider, juce::Slider::backgroundColourId, Pal::Border);
        slider.onValueChange = [this, update] { update (slider.getValue() / 100, ! slider.isMouseButtonDown()); };
        slider.onDragEnd = [this, update] { update (slider.getValue() / 100, true); };
        addAndMakeVisible (slider); setSize (240, 80);
    }
    void resized() override { auto area = getLocalBounds().reduced (12); label.setBounds (area.removeFromTop (22)); slider.setBounds (area); }
private:
    RememberUI::Style style { *this };
    juce::Label label;
    juce::Slider slider;
};
}
void WaveformPlayerComponent::showMetronomeVolume()
{
    if (metronomePopup != nullptr) { metronomePopup->dismiss(); return; }
    auto content = std::make_unique<MetronomeVolume> (getMetronomeVolume(), [safe = juce::Component::SafePointer<WaveformPlayerComponent> (this)] (double value, bool save)
    { if (safe != nullptr) safe->setMetronomeVolume (value, save); });
    // JUCE's async holder destroys the callout before its owned content, whose
    // Style retains this shared look-and-feel throughout the popup lifetime.
    auto* look = &content->getLookAndFeel();
    metronomePopup = &juce::CallOutBox::launchAsynchronously (std::move (content), metronomeBtn.getScreenBounds(), nullptr);
    metronomePopup->setLookAndFeel (look);
}

void WaveformPlayerComponent::showRepeatOptions()
{
    juce::PopupMenu menu;
    const char* labels[] = { "Off (play queue once)", "All (repeat queue)", "One (repeat selected preview)",
                            "Single (play once, then stop)" };
    for (int mode = 0; mode < 4; ++mode) menu.addItem (mode + 1, labels[mode], true, (int) queue.repeat == mode);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&repeatBtn),
        [safe = juce::Component::SafePointer<WaveformPlayerComponent> (this)] (int choice)
        {
            if (safe == nullptr || choice < 1 || choice > 4) return;
            safe->queue.repeat = (PlaybackQueue::Repeat) (choice - 1);
            safe->updateRepeatButton();
            safe->savePlaybackPreferences();
        });
}

void WaveformPlayerComponent::next (bool automatic)
{
    const bool resume = automatic || isPlaying() || waitingForBuffer || playWhenLoaded;
    if (auto entry = queue.next (automatic)) { loadEntry (*entry); if (resume) play(); }
}
void WaveformPlayerComponent::previous()
{
    const bool resume = isPlaying() || waitingForBuffer || playWhenLoaded;
    if (position() > 3.0) { seekTo (0); return; }
    if (auto entry = queue.previous()) { loadEntry (*entry); if (resume) play(); }
}
void WaveformPlayerComponent::setVolume (double value)
{
    volumeSlider.setValue (juce::jlimit (0.0, 1.0, value), juce::dontSendNotification);
    if (value > 0) { volumeSlider.getProperties().set ("rememberAudibleVolume", getVolume()); muteBtn.setToggleState (false, juce::dontSendNotification); }
    applyOutputGain(); savePlaybackPreferences();
}
void WaveformPlayerComponent::toggleMute()
{
    setMuted (getVolume() == 0 ? false : ! isMuted());
}
void WaveformPlayerComponent::toggleShuffle()
{
    shuffleBtn.setToggleState (! shuffleBtn.getToggleState(), juce::dontSendNotification);
    shuffleBtn.onClick();
}
void WaveformPlayerComponent::cycleRepeat() { repeatBtn.onClick(); }
void WaveformPlayerComponent::seekBySeconds (double seconds)
{
    const auto length = duration();
    if (! isLoaded || length <= 0) return;
    seekTo ((position() + seconds) / length);
}
void WaveformPlayerComponent::setMuted (bool value)
{
    muteBtn.setToggleState (value, juce::dontSendNotification);
    if (! value && getVolume() == 0)
        volumeSlider.setValue ((double) volumeSlider.getProperties().getWithDefault ("rememberAudibleVolume", 1.0), juce::dontSendNotification);
    RememberUI::iconButton (muteBtn, value ? "mute" : "volume", value ? "Unmute volume" : "Mute volume");
    muteBtn.setTooltip (RememberUI::shortcutTooltip (muteBtn.getTitle(), "M"));
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
    muteBtn.setTooltip (RememberUI::shortcutTooltip (muteBtn.getTitle(), "M"));
    const auto gain = normalize && waveform ? waveform->normalizationGain : 1.0f;
    metronome.setMasterGain (isMuted() ? 0.0f : (float) (volume * volume));
    transport.setGain (isMuted() ? 0.0f : (float) (volume * volume) * gain);
    normalizeBtn.setTooltip (currentEntry.auditionReader ? "Live MIDI uses its instrument level" : normalize && ! currentEntry.auditionReader && ! waveform ? "Measuring preview loudness..." : "Normalize preview loudness to -14 LUFS (1 dB decoded peak headroom)");
}
void WaveformPlayerComponent::savePlaybackPreferences()
{
    if (applyingPreferences || ! persistAudioState) return;
    preferencesTouched = true;
    juce::var patch (new juce::DynamicObject()); auto& o = *patch.getDynamicObject();
    o.setProperty ("playbackVolume", getVolume()); o.setProperty ("playbackMuted", isMuted()); o.setProperty ("normalizePlayback", normalize);
    o.setProperty ("metronomeVolume", metronome.getVolume());
    o.setProperty ("shufflePlayback", queue.shuffle); o.setProperty ("repeatPlayback", (int) queue.repeat); o.setProperty ("autoplaySelection", autoplay);
    auto safe = juce::Component::SafePointer<WaveformPlayerComponent> (this);
    ProjectScanner::editAsync ([patch] { return LibraryStore::patchJson (LibraryStore::configFile(), patch, true); }, [safe] (juce::Result result)
    { if (safe != nullptr && result.failed()) { safe->pathLabel.setText ("Cannot save playback preferences", juce::dontSendNotification); safe->pathLabel.setDescription (result.getErrorMessage()); } });
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

bool WaveformPlayerComponent::hasColorfulWaveforms() const { return waveformPreferences().colorful; }
bool WaveformPlayerComponent::hasSeparateWaveformChannels() const { return waveformPreferences().separate; }
void WaveformPlayerComponent::setColorfulWaveforms (bool value)
{
    if (waveformPreferences().colorful == value) return;
    waveformPreferences().colorful = value; saveWaveformPreferences();
}
void WaveformPlayerComponent::setSeparateWaveformChannels (bool value)
{
    if (waveformPreferences().separate == value) return;
    waveformPreferences().separate = value; saveWaveformPreferences();
}
void WaveformPlayerComponent::showWaveformOptions()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Colorful waveforms", true, hasColorfulWaveforms());
    menu.addItem (2, "Separate L/R channels", true, hasSeparateWaveformChannels());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [safe = juce::Component::SafePointer<WaveformPlayerComponent> (this)] (int choice)
        {
            if (! safe) return;
            if (choice == 1) safe->setColorfulWaveforms (! safe->hasColorfulWaveforms());
            if (choice == 2) safe->setSeparateWaveformChannels (! safe->hasSeparateWaveformChannels());
        });
}
