#include "DeviceTests.h"
#include "../App/Source/WaveformPlayerComponent.h"
#include <iostream>
#include <stdexcept>

struct PlaybackCursorTestAccess
{
    static bool displaySynced (const WaveformPlayerComponent& player) { return ! player.cursorVBlank.isEmpty(); }
    static double position (const WaveformPlayerComponent& player) { return player.cursorSeconds; }
    static double lastFrame (const WaveformPlayerComponent& player) { return player.lastVBlankTime; }
    static void draw (WaveformPlayerComponent& player) { player.drawPlaybackFrame(); }
};

namespace
{
void check (bool value, const char* error) { if (! value) throw std::runtime_error (error); }
template <class Condition> void await (Condition condition, const char* stage)
{
    const auto end = juce::Time::getMillisecondCounter() + 5000;
    while (! condition())
    { if (juce::Time::getMillisecondCounter() >= end) throw std::runtime_error (std::string ("Device fixture timed out: ") + stage); juce::MessageManager::getInstance()->runDispatchLoopUntil (10); }
}
struct Device;
struct State { bool fail = false; int opens = 0, closes = 0; Device* device = nullptr; };
struct Device final : juce::AudioIODevice
{
    explicit Device (State& state) : juce::AudioIODevice ("Fixture output", "Fixture"), owner (state) { owner.device = this; }
    ~Device() override { close(); owner.device = nullptr; }
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 128 }; }
    int getDefaultBufferSize() override { return 128; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& out, double, int) override
    { if (owner.fail) { error = "Fixture output unavailable"; return error; } error.clear(); opened = true; outputs = out; ++owner.opens; return {}; }
    void close() override { stop(); if (opened) { opened = false; ++owner.closes; } }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* cb) override { callback = cb; if (cb) cb->audioDeviceAboutToStart (this); }
    void stop() override { if (callback) { auto* cb = callback; callback = nullptr; cb->audioDeviceStopped(); } }
    bool isPlaying() override { return callback != nullptr && opened; }
    juce::String getLastError() override { return error; }
    int getCurrentBufferSizeSamples() override { return 128; }
    double getCurrentSampleRate() override { return 48000; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    float render()
    {
        juce::AudioBuffer<float> output (2, 128); output.clear();
        if (callback) callback->audioDeviceIOCallbackWithContext (nullptr, 0, output.getArrayOfWritePointers(), 2, 128, {});
        return output.getMagnitude (0, 128);
    }
    void disconnect() { opened = false; error = "Fixture disconnected"; if (callback) callback->audioDeviceError (error); }
    State& owner;
    bool opened = false;
    juce::String error;
    juce::BigInteger outputs;
    juce::AudioIODeviceCallback* callback = nullptr;
};
struct Type final : juce::AudioIODeviceType
{
    explicit Type (State& state) : juce::AudioIODeviceType ("Fixture"), owner (state) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? juce::StringArray{} : juce::StringArray { "Fixture output" }; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return new Device (owner); }
    State& owner;
};
struct Manager final : juce::AudioDeviceManager
{
    explicit Manager (State& state) : owner (state) {}
    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override { types.add (new Type (owner)); }
    State& owner;
};
}

void testDevice (const ProjectEntry& entry)
{
    State state;
    Manager manager (state);
    WaveformPlayerComponent player (&manager, false);
    player.setSize (700, 260);
    player.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    player.setVisible (true);
    check (state.opens == 0, "An idle player opened its device");
    player.loadEntry (entry);
    await ([&] { return player.hasLoadedEntry(); }, "load");
    check (state.opens == 0, "Preview loading opened its device");
    player.play();
    await ([&] { if (player.getPlaybackError().isNotEmpty()) throw std::runtime_error (player.getPlaybackError().toStdString()); return player.isPlaying(); }, "initial play");
    check (state.opens == 1 && state.device && state.device->render() > 0.001f, "Ready playback produced no audio");
    juce::TextButton* playbackAudioButton = nullptr;
    for (auto* child : player.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Audio")
            playbackAudioButton = button;
    check (playbackAudioButton && playbackAudioButton->onClick, "Playback Audio button is not reachable");
    check (PlaybackCursorTestAccess::displaySynced (player), "Visible playback has no display-synchronised cursor");
    const auto initialFrame = PlaybackCursorTestAccess::lastFrame (player);
    await ([&]
    {
        state.device->render();
        return PlaybackCursorTestAccess::lastFrame (player) > initialFrame && PlaybackCursorTestAccess::position (player) > 0;
    }, "native display frame moves cursor");
    playbackAudioButton->onClick();
    juce::Component::SafePointer<juce::DialogWindow> playbackDialog;
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        if (auto* dialog = dynamic_cast<juce::DialogWindow*> (juce::Desktop::getInstance().getComponent (i));
            dialog && dialog->getName() == "Audio output") playbackDialog = dialog;
    check (playbackDialog != nullptr, "Audio settings did not open during playback");
    check (player.isPlaying() && PlaybackCursorTestAccess::displaySynced (player)
           && state.device->render() > 0.001f,
           "Opening Audio settings slowed the playhead or interrupted playback");
    playbackDialog->exitModalState (0);
    await ([&] { return playbackDialog == nullptr; }, "close Audio settings during playback");
    check (player.isPlaying() && PlaybackCursorTestAccess::displaySynced (player),
           "Closing Audio settings slowed the playhead or interrupted playback");
    player.setVisible (false);
    check (player.isPlaying() && ! PlaybackCursorTestAccess::displaySynced (player) && player.getTimerInterval() == 250,
           "Hiding the native component retained display-rate cursor work");
    player.setVisible (true);
    check (PlaybackCursorTestAccess::displaySynced (player), "Showing the native component did not restore its frame callback");
    player.seekBySeconds (0.4);
    check (PlaybackCursorTestAccess::position (player) >= 0.4, "Seeking did not update the cursor immediately");
    await ([&] { return state.device->render() > 0.001f; }, "output after seek prefill");
    PlaybackCursorTestAccess::draw (player);
    const auto cursorBeforeHide = PlaybackCursorTestAccess::position (player);
    player.setUiActive (false);
    check (player.isPlaying() && ! PlaybackCursorTestAccess::displaySynced (player)
           && player.getTimerInterval() == 250 && state.device->render() > 0.001f,
           "Hiding the app interrupted playback or retained visual-rate polling");
    PlaybackCursorTestAccess::draw (player);
    check (PlaybackCursorTestAccess::position (player) == cursorBeforeHide, "Hidden playback still updates its cursor");
    player.setUiActive (true);
    check (PlaybackCursorTestAccess::displaySynced (player) && player.getTimerInterval() == 50,
           "Restoring the app did not restore display-synchronised playback");
    player.togglePlay();
    check (! player.isPlaying() && ! PlaybackCursorTestAccess::displaySynced (player), "Pause retained playback or its frame callback");
    await ([&] { return ! player.isAudioDeviceOpen(); }, "idle release after pause");
    check (state.closes >= 1, "Pause did not release the device after its idle delay");
    player.setUiActive (true);
    player.play();
    await ([&] { return player.isPlaying(); }, "resume");
    check (state.opens == 2 && state.device, "Resuming failed to reopen the device");
    state.device->disconnect();
    await ([&] { return ! player.isAudioDeviceOpen() && player.getPlaybackError().contains ("Fixture disconnected"); }, "disconnect");
    check (! player.isPlaying(), "Disconnected output retained active transport");
    player.play();
    await ([&] { return player.isPlaying(); }, "recover");
    check (state.opens == 3 && state.device->render() > 0.001f, "Playback failed to recover after disconnect");
    const auto beforeAudition = PlaybackCursorTestAccess::position (player);
    player.suspendOutput();
    check (! player.isPlaying() && ! player.isAudioDeviceOpen() && player.hasLoadedEntry() && player.isVisible()
           && PlaybackCursorTestAccess::position (player) >= beforeAudition,
           "Audition suspension clears the main project or keeps its exclusive audio device open");
    player.play();
    await ([&] { return player.isPlaying(); }, "resume main project after audition suspension");
    check (state.opens == 4 && state.device->render() > 0.001f, "Main project cannot resume after audition suspension");

    player.stop();
    check (PlaybackCursorTestAccess::position (player) == 0 && ! PlaybackCursorTestAccess::displaySynced (player),
           "Stop did not reset the cursor or detach its display callback");
    await ([&] { return ! player.isAudioDeviceOpen(); }, "idle release after stop");
    juce::TextButton* repeat = nullptr;
    for (auto* child : player.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getTitle() == "Repeat: off") repeat = button;
    check (repeat != nullptr, "Device fixture has no Repeat options");
    auto* repeatContext = dynamic_cast<RememberUI::ContextMenuButton*> (repeat);
    check (repeatContext && repeatContext->onContextMenu, "Device Repeat button lacks context options");
    repeatContext->onContextMenu();
    auto* menu = juce::Component::getCurrentlyModalComponent();
    check (menu != nullptr, "Device fixture Repeat menu did not open");
    menu->exitModalState (4);
    await ([&] { return repeat->getTitle() == "Repeat: single"; }, "choose Single");
    auto following = entry; following.projectId = "single-following";
    int loads = 0;
    player.onEntryLoaded = [&](const ProjectEntry&) { ++loads; };
    player.setQueue ({ entry, following }); player.loadEntry (entry);
    await ([&] { return player.hasLoadedEntry(); }, "load Single preview");
    player.play(); await ([&] { return player.isPlaying(); }, "play Single preview");
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (player.isPlaying())
    {
        check (state.device != nullptr && juce::Time::getMillisecondCounter() < deadline, "Single did not finish its preview");
        state.device->render();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    }
    await ([&] { check (loads == 1, "Single repeated or advanced after audio finished"); return ! player.isAudioDeviceOpen(); }, "Single releases idle device");
    player.next(); await ([&] { return loads == 2 && player.hasLoadedEntry(); }, "manual Next in Single");
    check (! player.isPlaying(), "Single manual Next unexpectedly started playback while stopped");
    player.onEntryLoaded = {};
    std::cout << "PASS: Single renders to the end once, stops, releases the fake device and retains manual Next\n";
    state.fail = true;
    player.play();
    await ([&] { return player.getPlaybackError().contains ("Fixture output unavailable"); }, "open error");
    check (! player.isAudioDeviceOpen() && ! player.isPlaying(), "An output failure was reported as open/playing");

    // Open the native selector through the component's actual button, then
    // destroy its owner before modal completion. The selector refers to the
    // manager, so its content must go away before that manager is released.
    {
        State dialogState; dialogState.fail = true; Manager dialogManager (dialogState);
        auto dialogPlayer = std::make_unique<WaveformPlayerComponent> (&dialogManager, false);
        auto unreadable = entry; unreadable.previewOgg = entry.previewOgg.getSiblingFile ("absent-fixture-preview.ogg");
        dialogPlayer->loadEntry (unreadable);
        await ([&] { return dialogPlayer->getPlaybackError().contains ("Cannot read preview"); }, "unreadable preview before selector");
        juce::TextButton* audioButton = nullptr;
        for (auto* child : dialogPlayer->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Audio")
                audioButton = button;
        check (audioButton && audioButton->onClick, "Audio output selector is not reachable");
        audioButton->onClick();
        juce::Component::SafePointer<juce::DialogWindow> window;
        for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
            if (auto* dialog = dynamic_cast<juce::DialogWindow*> (juce::Desktop::getInstance().getComponent (i));
                dialog && dialog->getName() == "Audio output") window = dialog;
        check (window != nullptr && window->getContentComponent() != nullptr, "Audio output selector did not open");
        window->setVisible (false);
        check (dialogPlayer->getPlaybackError().contains ("Fixture output unavailable"), "Missing selector output had no error");
        dialogState.fail = false;
        check (dialogManager.initialise (0, 2, nullptr, false).isEmpty(), "Cannot simulate selecting a recovered output");
        await ([&] { return dialogPlayer->isAudioDeviceOpen() && dialogPlayer->getPlaybackError().contains ("Cannot read preview"); }, "selector recovery preserves preview error");
        dialogPlayer->loadEntry (entry);
        await ([&] { return dialogPlayer->hasLoadedEntry() && dialogPlayer->getPlaybackError().isEmpty(); }, "valid preview after selector recovery");
        dialogPlayer.reset();
        check (window == nullptr || window->getContentComponent() == nullptr, "Output selector retained a destroyed manager");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
    std::cout << "PASS: device laziness, output audio, background playback, idle release/reopen, disconnect/retry and initialization errors (fake driver)\n";
    std::cout << "PASS: recovery through output selector and player closure while native selector is open\n";
}
