#include "ConfigurationTests.h"
#include "AppearanceTests.h"
#include "TypographyTests.h"
#include "../Plugin/Source/RecordingEngine.h"
#include "../Plugin/Source/PluginEditor.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/BackgroundRetirement.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

void testPluginFrontend (const juce::File& root)
{
    testFrontendTypography();
    testAppearanceContrast();
    auto* originalLookAndFeel = &juce::LookAndFeel::getDefaultLookAndFeel();
    const auto library = root.getChildFile ("design-library");
    if (library.createDirectory().failed()) throw std::runtime_error ("Cannot create plugin design library");
    juce::var config (new juce::DynamicObject());
    config.getDynamicObject()->setProperty ("previewsFolder", library.getFullPathName());
    config.getDynamicObject()->setProperty ("recordSeconds", 90);
    config.getDynamicObject()->setProperty ("oggQuality", 6);
    config.getDynamicObject()->setProperty ("recordMono", true);
    config.getDynamicObject()->setProperty ("futureSetting", "preserved");
    if (LibraryStore::writeJsonAtomically (LibraryStore::configFile(), config).failed())
        throw std::runtime_error ("Cannot configure plugin design library");
    {
        RememberProcessor processor ([] { return HostProject{}; });
        processor.setProjectName ("Midnight sketches - long project name");
        std::unique_ptr<RememberEditor> editor (static_cast<RememberEditor*> (processor.createEditor()));
        const auto ready = [&]
        {
            const auto deadline = juce::Time::getMillisecondCounter() + 7000;
            for (;;)
            {
                editor->timerCallback();
                for (auto* child : editor->getChildren())
                    if (auto* button = dynamic_cast<juce::TextButton*> (child);
                        button && button->getProperties()["rememberIcon"].toString() == "settings" && button->isEnabled()) return;
                if (juce::Time::getMillisecondCounter() >= deadline) throw std::runtime_error ("Plugin design settings timed out");
                juce::Thread::sleep (2);
            }
        };
        ready();
        if (&juce::LookAndFeel::getDefaultLookAndFeel() != originalLookAndFeel)
            throw std::runtime_error ("Plugin changed the host global LookAndFeel");
        for (auto* child : editor->getChildren())
            if (! editor->getLocalBounds().contains (child->getBounds()))
                throw std::runtime_error ("Plugin layout clipped a control");
        const auto artifacts = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts");
        if (artifacts.createDirectory().failed()) throw std::runtime_error ("Cannot create plugin artifacts");
        const auto save = [&] (const char* name, float scale, juce::Component* component = nullptr)
        {
            auto& target = component != nullptr ? *component : *editor;
            const auto snapshot = target.createComponentSnapshot (target.getLocalBounds(), true, scale, juce::SoftwareImageType{});
            juce::FileOutputStream output (artifacts.getChildFile (name));
            juce::PNGImageFormat png;
            if (! output.openedOk() || ! output.setPosition (0) || output.truncate().failed() || ! png.writeImageToStream (snapshot, output))
                throw std::runtime_error ("Cannot render plugin frontend");
        };
        save ("apple-plugin.png", 1); save ("apple-plugin-2x.png", 2);
       #if JUCE_WINDOWS
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child);
                button && button->getProperties()["rememberIcon"].toString() == "settings") button->onClick();
        auto* menu = juce::Component::getCurrentlyModalComponent();
        if (menu == nullptr || &menu->getLookAndFeel() != &editor->getLookAndFeel())
            throw std::runtime_error ("Plugin settings menu is absent or uses the host theme");
        save ("apple-plugin-settings.png", 1, menu);
        menu->exitModalState (3); // Same callback as choosing Appearance > Dark.
        const auto dispatchUntil = juce::Time::getMillisecondCounter() + 25;
        do
        {
            MSG message;
            while (PeekMessageW (&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage (&message); DispatchMessageW (&message); }
            juce::Thread::sleep (1);
        } while (juce::Time::getMillisecondCounter() < dispatchUntil);
       #else
        editor->applyAppearance (true);
       #endif
        ready();
        if (! RememberUI::isDark() || LibraryStore::readJson (LibraryStore::configFile())["appearance"].toString() != "dark")
            throw std::runtime_error ("Plugin failed to save dark appearance");
        for (auto* child : editor->getChildren())
            if (auto* text = dynamic_cast<juce::TextEditor*> (child))
                if (text->findColour (juce::TextEditor::textColourId) != RememberUI::colour (Pal::Text)
                    || ! text->getText().contains ("Midnight sketches")) throw std::runtime_error ("Plugin appearance lost text or colour");
        save ("apple-plugin-dark.png", 1); save ("apple-plugin-dark-2x.png", 2);
        editor.reset(); RememberUI::setDark (false);
        editor.reset (static_cast<RememberEditor*> (processor.createEditor())); ready();
        if (! RememberUI::isDark()) throw std::runtime_error ("Plugin reopen failed to restore dark appearance");
        editor->applyAppearance (false); ready();
        if (RememberUI::isDark() || LibraryStore::readJson (LibraryStore::configFile())["appearance"].toString() != "light")
            throw std::runtime_error ("Plugin failed to restore light appearance");
        if (SettingsManager::getInstance().readSnapshot().previews != library)
            throw std::runtime_error ("Appearance edit changed the output folder");
        const auto settings = SettingsManager::getInstance().readSnapshot();
        if (settings.seconds != 90 || settings.quality != 6 || ! settings.mono
            || LibraryStore::readJson (LibraryStore::configFile())["futureSetting"].toString() != "preserved")
            throw std::runtime_error ("Appearance edit replaced unrelated settings");
        const auto validConfig = LibraryStore::configFile().loadFileAsString();
        const juce::String brokenConfig = "{broken-settings";
        if (! LibraryStore::configFile().replaceWithText (brokenConfig)) throw std::runtime_error ("Cannot set failed appearance fixture");
        editor->applyAppearance (true); ready();
        if (RememberUI::isDark() || LibraryStore::configFile().loadFileAsString() != brokenConfig)
            throw std::runtime_error ("Failed appearance edit changed theme or overwrote malformed settings");
        bool appearanceError = false;
        for (auto* child : editor->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child)) appearanceError |= label->getText().containsIgnoreCase ("config");
        if (! appearanceError) throw std::runtime_error ("Failed appearance edit hid its error");
        if (! LibraryStore::configFile().replaceWithText (validConfig)) throw std::runtime_error ("Cannot restore appearance fixture");
        editor->applyOutputFolder (root.getChildFile ("unavailable-design-folder"));
        ready();
        bool errorVisible = false;
        for (auto* child : editor->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child)) errorVisible |= label->getText().contains ("Output folder is unavailable");
        if (! errorVisible) throw std::runtime_error ("Plugin frontend lost actionable error text");
        save ("apple-plugin-error.png", 1);
    }
    if (&juce::LookAndFeel::getDefaultLookAndFeel() != originalLookAndFeel)
        throw std::runtime_error ("Plugin closure changed the host theme");
    std::cout << "PASS: plugin frontend bounds, 1x/2x snapshots and host LookAndFeel isolation\n";
}

namespace
{
void check (bool value, const char* reason) { if (! value) throw std::runtime_error (reason); }
template <typename Predicate> void await (Predicate predicate, const char* reason)
{
    const auto end = juce::Time::getMillisecondCounter() + 7000;
    while (! predicate())
    {
        check (juce::Time::getMillisecondCounter() < end, reason);
        juce::Thread::sleep (2);
    }
}
struct Head : juce::AudioPlayHead
{
    bool playing = true;
    double time = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result; result.setTimeInSeconds (time); result.setIsPlaying (playing); result.setBpm (120);
        return result;
    }
};
void capture (RecordingEngine& engine, double rate, float level, double seconds = 1.2)
{
    Head head;
    const auto total = (int) std::llround (rate * seconds);
    for (int done = 0; done < total;)
    {
        const auto count = juce::jmin (128, total - done);
        juce::AudioBuffer<float> audio (2, count);
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (audio.getWritePointer (ch), level, count);
        head.time = done / rate; engine.processBlock (audio, &head); done += count;
    }
    head.playing = false; head.time = seconds;
    juce::AudioBuffer<float> stopped (2, 128); stopped.clear(); engine.processBlock (stopped, &head);
}
void saved (RecordingEngine& engine, bool pending = false)
{
    await ([&] { return engine.getState() == (pending ? RecordingEngine::State::Pending : RecordingEngine::State::Done)
                        || engine.getState() == RecordingEngine::State::Error; }, "Configuration capture did not finish");
    if (engine.getState() == RecordingEngine::State::Error) throw std::runtime_error (engine.getLastError().toStdString());
}
struct BlockedMetadata
{
    explicit BlockedMetadata (juce::File file) : worker ([this, file]
    {
        result = LibraryStore::updateJson (file, [this] (juce::DynamicObject&)
        { entered.signal(); release.wait (10000); return juce::Result::ok(); });
    }) {}
    ~BlockedMetadata() { release.signal(); if (worker.joinable()) worker.join(); }
    juce::WaitableEvent entered, release { true };
    juce::Result result = juce::Result::ok();
    std::thread worker;
};
void audioIs (const juce::File& folder, double rate, int channels, float level)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
    check (reader && reader->sampleRate == rate && reader->numChannels == (unsigned int) channels,
           "A queued capture changed format across configuration versions");
    juce::AudioBuffer<float> sample (channels, 1);
    check (reader->read (&sample, 0, 1, 100, true, true) && std::abs (sample.getSample (0, 0) - level) < 0.00001f,
           "Captures were reordered or written to the wrong library");
}
bool settingsReady (RememberEditor& editor)
{
    editor.timerCallback();
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*> (editor.getChildComponent (i)))
            if (button->getButtonText() == "...") return button->isEnabled();
    return false;
}
bool statusContains (RememberEditor& editor, const juce::String& value)
{
    editor.timerCallback();
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
        if (auto* label = dynamic_cast<juce::Label*> (editor.getChildComponent (i)))
            if (label->getText().contains (value)) return true;
    return false;
}
struct ReadGate
{
    SettingsManager::Snapshot read()
    {
        onMessageThread.store (juce::MessageManager::getInstance()->isThisTheMessageThread());
        const auto snapshot = SettingsManager::getInstance().readSnapshot();
        entered.signal(); release.wait (10000); return snapshot;
    }
    juce::WaitableEvent entered, release { true };
    std::atomic<bool> onMessageThread { false };
};
struct ReleaseGate
{
    std::shared_ptr<ReadGate> gate;
    ~ReleaseGate() { gate->release.signal(); }
};
struct DiagnosticGate final : juce::Logger
{
    DiagnosticGate() : previous (juce::Logger::getCurrentLogger()) { juce::Logger::setCurrentLogger (this); }
    ~DiagnosticGate() override
    {
        release.signal();
        const auto deadline = juce::Time::getMillisecondCounter() + 7000;
        while (BackgroundRetirement::pending() != 0 && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (2);
        juce::Logger::setCurrentLogger (previous);
    }
    void logMessage (const juce::String& message) override
    {
        if (message.startsWith ("Processor prepared"))
        {
            onMessageThread.store (juce::MessageManager::getInstance()->isThisTheMessageThread());
            prepared.signal(); release.wait (10000);
        }
        if (message == "Plugin editor created.")
        {
            editorOnMessageThread.store (juce::MessageManager::getInstance()->isThisTheMessageThread());
            editor.signal();
        }
    }
    juce::Logger* previous;
    juce::WaitableEvent prepared, editor, release { true };
    std::atomic<bool> onMessageThread { true }, editorOnMessageThread { true };
};
juce::String stateFolder (RememberProcessor& processor)
{
    juce::MemoryBlock state; processor.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    check (xml != nullptr, "Cannot read processor's snapshot state");
    return xml->getStringAttribute ("previewsFolder");
}
}

void testConfiguration (const juce::File& fixtureRoot)
{
    check (LibraryStore::dataDirectory().isAChildOf (fixtureRoot)
           && LibraryStore::configFile().isAChildOf (fixtureRoot), "Tests are not using their isolated profile");
    const auto root = fixtureRoot.getChildFile ("configuration"); check (root.createDirectory().wasOk(), "Cannot create configuration fixture");
    const auto first = root.getChildFile ("library-A"), second = root.getChildFile ("library-B");
    check (first.createDirectory().wasOk() && second.createDirectory().wasOk(), "Cannot create configuration libraries");
    const auto config = root.getChildFile ("config.json");
    juce::var value (new juce::DynamicObject()); auto* fields = value.getDynamicObject();
    fields->setProperty ("previewsFolder", first.getFullPathName()); fields->setProperty ("recordSeconds", 45);
    fields->setProperty ("oggQuality", 9); fields->setProperty ("recordMono", true); fields->setProperty ("dataRevision", 17);
    check (LibraryStore::writeJsonAtomically (config, value).wasOk(), "Cannot create settings snapshot");
    const auto original = SettingsManager::readSnapshot (config);
    check (original.previews == first && original.seconds == 45 && original.quality == 9 && original.mono
           && original.revision == 17 && original.fingerprint.isNotEmpty(), "Settings fields/version came from different snapshots");
    const auto stamp = config.getLastModificationTime(); const auto size = config.getSize();
    fields->setProperty ("previewsFolder", second.getFullPathName()); fields->setProperty ("oggQuality", 8);
    check (LibraryStore::writeJsonAtomically (config, value).wasOk() && config.setLastModificationTime (stamp), "Cannot replace settings with reused stamp");
    const auto changed = SettingsManager::readSnapshot (config);
    check (config.getSize() == size && changed.revision == original.revision && changed.modifiedMs == original.modifiedMs
           && changed.previews == second && changed.quality == 8 && changed.fingerprint != original.fingerprint,
           "External settings edit with reused revision/date/size was not invalidated");
    check (config.replaceWithText ("{"), "Cannot create invalid settings fixture");
    const auto invalid = SettingsManager::readSnapshot (config);
    check (invalid.previews == juce::File{} && invalid.fingerprint.isEmpty() && invalid.seconds == 60,
           "Invalid configuration reused fields from a previous snapshot");
    check (config.replaceWithText ("[1,2,3]"), "Cannot create array-root settings fixture");
    const auto arrayRoot = SettingsManager::readSnapshot (config);
    check (arrayRoot.previews == juce::File{} && arrayRoot.fingerprint.isEmpty() && arrayRoot.seconds == 60
           && LibraryStore::patchJson (config, value, true).failed() && config.loadFileAsString() == "[1,2,3]",
           "Array-root configuration was accepted or silently reset by a plugin settings writer");
    check (LibraryStore::patchJson (config, juce::var (juce::Array<juce::var> { 1, 2 }), true).failed(),
           "An array patch reached an object-only settings writer");
    std::cout << "PASS: coherent configuration versions, external edits with reused revision/date/size, invalid JSON\n";

    const auto project = root.getChildFile ("Settings.rpp"); check (project.replaceWithText ("configuration project"), "Cannot create settings project");
    SettingsManager::Snapshot oldSettings; oldSettings.previews = first; oldSettings.seconds = 15;
    oldSettings.quality = 4; oldSettings.revision = 18; oldSettings.fingerprint = "old-settings";
    auto newSettings = oldSettings; newSettings.previews = second; newSettings.seconds = 30;
    newSettings.quality = 9; newSettings.mono = true; newSettings.revision = 19; newSettings.fingerprint = "new-settings";
    {
        RecordingEngine engine ([project] { return HostProject { true, "Settings", project.getFullPathName(), "REAPER" }; });
        engine.prepare (8000, 128, oldSettings);
        await ([&] { return engine.isProjectConfirmed(); }, "Settings project was not confirmed");
        capture (engine, 8000, 0.2f); saved (engine);
        juce::XmlElement state ("RememberState"); engine.saveState (state);
        const auto originalId = state.getStringAttribute ("projectId"), otherId = juce::Uuid().toString();
        const auto a = first.getChildFile (originalId), b = second.getChildFile (otherId);
        check (b.createDirectory().wasOk(), "Cannot seed other-library identity");
        juce::var existing (new juce::DynamicObject()); existing.getDynamicObject()->setProperty ("projectId", otherId);
        existing.getDynamicObject()->setProperty ("projectPath", project.getFullPathName());
        existing.getDynamicObject()->setProperty ("projectName", "Settings"); existing.getDynamicObject()->setProperty ("favorite", true);
        check (LibraryStore::writeJsonAtomically (b.getChildFile ("metadata.json"), existing).wasOk(), "Cannot seed other-library metadata");
        BlockedMetadata block (a.getChildFile ("metadata.json")); check (block.entered.wait (1000), "Settings fixture did not block metadata");
        capture (engine, 8000, 0.3f); capture (engine, 8000, 0.4f);
        const auto begin = juce::Time::getMillisecondCounterHiRes(); engine.prepareAsync (16000, 128, [newSettings] { return newSettings; });
        check (juce::Time::getMillisecondCounterHiRes() - begin < 200, "Settings preparation waited for the previous commit");
        capture (engine, 16000, 0.9f); block.release.signal(); saved (engine);
        audioIs (a, 8000, 2, 0.4f); audioIs (b, 16000, 1, 0.9f);
        const auto aMeta = LibraryStore::readJson (a.getChildFile ("metadata.json"));
        const auto bMeta = LibraryStore::readJson (b.getChildFile ("metadata.json"));
        check ((int) aMeta.getProperty ("settingsRevision", 0) == 18 && (int) aMeta.getProperty ("oggQuality", 0) == 4
               && aMeta.getProperty ("settingsFingerprint", "").toString() == "old-settings"
               && (int) bMeta.getProperty ("settingsRevision", 0) == 19 && (int) bMeta.getProperty ("recordSeconds", 0) == 30
               && bMeta.getProperty ("settingsFingerprint", "").toString() == "new-settings"
               && (bool) bMeta.getProperty ("favorite", false), "Audio/settings version or organisation was mixed across libraries");
        engine.prepare (8000, 128, oldSettings); capture (engine, 8000, 0.7f); saved (engine);
        audioIs (a, 8000, 2, 0.7f); audioIs (b, 16000, 1, 0.9f);
        engine.saveState (state); check (state.getStringAttribute ("projectId") == originalId, "Returning to a library lost its entry identity");
        std::cout << "PASS: blocked old commits retain their library/settings; new captures adopt the new library; returning preserves identity\n";
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Settings fixture retirement did not finish");
    juce::String pendingId;
    {
        RecordingEngine engine ([] { return HostProject{}; });
        engine.prepare (8000, 128, oldSettings); engine.setProjectName ("Pending settings");
        capture (engine, 8000, 0.2f); saved (engine, true);
        juce::XmlElement state ("RememberState"); engine.saveState (state); pendingId = state.getStringAttribute ("projectId");
        engine.prepare (16000, 128, newSettings); capture (engine, 16000, 0.6f); saved (engine, true);
        audioIs (first.getChildFile (".pending").getChildFile (pendingId), 8000, 2, 0.2f);
        audioIs (second.getChildFile (".pending").getChildFile (pendingId), 16000, 1, 0.6f);
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Pending settings cleanup did not finish");
    check (! first.getChildFile (".pending").getChildFile (pendingId).exists()
           && ! second.getChildFile (".pending").getChildFile (pendingId).exists(), "Changing libraries stranded an owned pending capture");
    std::cout << "PASS: normal close cleans only owned pending captures across changed libraries\n";

    const auto sharedConfig = LibraryStore::configFile();
    {
        fields->setProperty ("previewsFolder", second.getFullPathName()); fields->setProperty ("recordSeconds", 15);
        fields->setProperty ("oggQuality", 8); fields->setProperty ("recordMono", true); fields->setProperty ("dataRevision", 20);
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot seed default engine preparation settings");
        const auto expected = SettingsManager::getInstance().readSnapshot();
        RecordingEngine engine ([] { return HostProject{}; });
        engine.setOutputFolder (first);
        const auto began = juce::Time::getMillisecondCounterHiRes(); engine.prepare (8000, 128);
        const auto prepareMs = juce::Time::getMillisecondCounterHiRes() - began;
        check (prepareMs < 100, "Default engine preparation waited for shared settings");
        capture (engine, 8000, 0.45f); saved (engine, true);
        const auto id = engine.getAssociationInfo().id;
        const auto folder = second.getChildFile (".pending").getChildFile (id);
        audioIs (folder, 8000, 1, 0.45f);
        const auto metadata = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
        check ((int) metadata.getProperty ("recordSeconds", 0) == 15 && (int) metadata.getProperty ("oggQuality", 0) == 8
               && metadata.getProperty ("settingsFingerprint", "").toString() == expected.fingerprint
               && ! first.getChildFile (".pending").getChildFile (id).exists(),
               "Default engine preparation reused its older destination or split the current settings");
        std::cout << "PASS: default engine prepare uses asynchronous current shared settings/destination; prepare=" << prepareMs << " ms\n";
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Default engine preparation did not retire");
    {
        fields->setProperty ("previewsFolder", first.getFullPathName()); fields->setProperty ("recordSeconds", 15);
        fields->setProperty ("oggQuality", 4); fields->setProperty ("recordMono", false); fields->setProperty ("dataRevision", 21);
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot seed delayed-bank settings");
        const auto older = SettingsManager::getInstance().readSnapshot();
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        RecordingEngine engine ([] { return HostProject{}; });
        engine.prepareAsync (8000, 128, [gate] { return gate->read(); });
        check (gate->entered.wait (1000), "Cannot hold older bank settings");
        capture (engine, 8000, 0.25f);
        const auto id = engine.getAssociationInfo().id;
        fields->setProperty ("previewsFolder", second.getFullPathName()); fields->setProperty ("recordSeconds", 30);
        fields->setProperty ("oggQuality", 9); fields->setProperty ("recordMono", true); fields->setProperty ("dataRevision", 22);
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot write newer bank settings");
        const auto newer = SettingsManager::getInstance().readSnapshot();
        engine.prepareAsync (16000, 128, [] { return SettingsManager::getInstance().readSnapshot(); });
        await ([&] { return ! engine.isPreparingSettings(); }, "New bank waited for older settings");
        capture (engine, 16000, 0.75f);
        juce::Thread::sleep (80);
        const auto a = first.getChildFile (".pending").getChildFile (id), b = second.getChildFile (".pending").getChildFile (id);
        check (! a.getChildFile ("metadata.json").existsAsFile() && ! b.getChildFile ("metadata.json").existsAsFile(),
               "A newer capture overtook an older bank waiting for settings");
        gate->release.signal(); saved (engine, true);
        audioIs (a, 8000, 2, 0.25f); audioIs (b, 16000, 1, 0.75f);
        const auto aMeta = LibraryStore::readJson (a.getChildFile ("metadata.json")), bMeta = LibraryStore::readJson (b.getChildFile ("metadata.json"));
        juce::XmlElement state ("RememberState"); engine.saveState (state);
        check (aMeta.getProperty ("settingsFingerprint", "").toString() == older.fingerprint
               && bMeta.getProperty ("settingsFingerprint", "").toString() == newer.fingerprint
               && state.getStringAttribute ("previewsFolder") == second.getFullPathName()
               && engine.getDroppedCaptures() == 0 && engine.getReservedCaptureBytes() <= 64 * 1024 * 1024,
               "Out-of-order settings mixed bank versions, changed the latest destination or exceeded its reservation");
        std::cout << "PASS: settings finish out of order across 8/16 kHz banks; capture ordering, distinct libraries/formats and latest destination survive\n";
        fields->setProperty ("recordSeconds", 45);
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Delayed settings banks did not retire");
    fields->setProperty ("previewsFolder", first.getFullPathName()); fields->setProperty ("oggQuality", 9);
    fields->setProperty ("customSetting", "preserved");
    check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot seed plugin editor settings");
    {
        auto processor = std::make_unique<RememberProcessor> ([] { return HostProject{}; });
        auto editor = std::unique_ptr<RememberEditor> (static_cast<RememberEditor*> (processor->createEditor()));
        await ([&] { return settingsReady (*editor); }, "Plugin editor did not load settings asynchronously");
        BlockedMetadata block (sharedConfig); check (block.entered.wait (1000), "Settings writer was not blocked");
        const auto begin = juce::Time::getMillisecondCounterHiRes(); editor->applyOutputFolder (second);
        const auto editMs = juce::Time::getMillisecondCounterHiRes() - begin;
        check (editMs < 100 && ! settingsReady (*editor) && statusContains (*editor, "Saving output folder"),
               "Plugin folder selection waited for a held configuration transaction");
       #if JUCE_WINDOWS
        std::atomic<bool> dispatched { false };
        check (juce::MessageManager::callAsync ([&] { dispatched.store (true); }), "Cannot post blocked-settings dispatch check");
        const auto end = juce::Time::getMillisecondCounter() + 1000;
        while (! dispatched.load() && juce::Time::getMillisecondCounter() < end)
        {
            MSG message;
            while (PeekMessageW (&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage (&message); DispatchMessageW (&message); }
            juce::Thread::sleep (1);
        }
        check (dispatched.load() && ! settingsReady (*editor), "Settings save blocked message dispatch or completed before release");
       #endif
        const auto closing = juce::Time::getMillisecondCounterHiRes(); editor.reset(); processor.reset();
        const auto closeMs = juce::Time::getMillisecondCounterHiRes() - closing;
        check (closeMs < 100 && BackgroundRetirement::pending() > 0, "Editor closure waited for or freed a blocked settings worker");
        block.release.signal();
        await ([] { return BackgroundRetirement::pending() == 0; }, "Settings worker did not retire after closing its editor");
        const auto persisted = LibraryStore::readJson (sharedConfig);
        check (persisted.getProperty ("previewsFolder", "").toString() == second.getFullPathName()
               && (int) persisted.getProperty ("recordSeconds", 0) == 45 && (int) persisted.getProperty ("oggQuality", 0) == 9
               && (bool) persisted.getProperty ("recordMono", false) && persisted.getProperty ("customSetting", "").toString() == "preserved",
               "Closing the editor lost its accepted settings edit or reset unrelated parameters");
        std::cout << "PASS: plugin folder edit keeps dispatch alive under a held transaction; edit=" << editMs << " ms, close=" << closeMs
                  << " ms; accepted write finishes after editor/processor closure\n";
    }
    {
        RememberProcessor processor ([] { return HostProject{}; });
        auto editor = std::unique_ptr<RememberEditor> (static_cast<RememberEditor*> (processor.createEditor()));
        await ([&] { return settingsReady (*editor); }, "Cannot load settings for error feedback");
        editor->applyOutputFolder (root.getChildFile ("missing-output"));
        await ([&] { return settingsReady (*editor); }, "Unavailable output selection did not finish");
        check (statusContains (*editor, "Output folder is unavailable"), "Plugin settings error was not shown");
        for (int i = 0; i < 20; ++i) editor->timerCallback();
        check (statusContains (*editor, "Output folder is unavailable") && SettingsManager::getInstance().getPreviewsFolder() == second,
               "Engine timer erased a settings error or changed the output despite failure");
        editor->applyOutputFolder (first); await ([&] { return settingsReady (*editor); }, "Output retry did not finish");
        check (statusContains (*editor, "Output saved; reload plugin to apply") && ! statusContains (*editor, "Output folder is unavailable")
               && SettingsManager::getInstance().getPreviewsFolder() == first, "Successful output retry did not clear the error or explain preparation");
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Editor settings reads did not retire");
    for (int i = 0; i < 20; ++i)
    {
        RememberProcessor processor ([] { return HostProject{}; });
        std::unique_ptr<RememberEditor> editor (static_cast<RememberEditor*> (processor.createEditor()));
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Immediate editor closure left settings threads alive");
    std::cout << "PASS: persistent plugin settings errors, successful retry and 20 immediate editor closes during asynchronous settings reads\n";

    // The provider reads the real fixture config, then holds that exact
    // snapshot. Constructor/state callbacks must return while it is held.
    {
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        auto calls = std::make_shared<std::atomic<int>> (0);
        const auto began = juce::Time::getMillisecondCounterHiRes();
        RememberProcessor processor ([] { return HostProject{}; }, [gate, calls]
        { return calls->fetch_add (1) == 0 ? gate->read() : SettingsManager::getInstance().readSnapshot(); });
        const auto constructMs = juce::Time::getMillisecondCounterHiRes() - began;
        check (constructMs < 100 && gate->entered.wait (1000) && ! gate->onMessageThread.load(),
               "Processor construction read settings on the caller or waited for their I/O");
        fields->setProperty ("previewsFolder", second.getFullPathName());
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot change settings during constructor read");
        processor.prepareToPlay (16000, 128);
        await ([&] { return ! processor.getEngine().isPreparingSettings(); }, "New preparation waited for the older constructor read");
        const auto saving = juce::Time::getMillisecondCounterHiRes();
        check (stateFolder (processor) == second.getFullPathName(), "State saving reread settings instead of the prepared destination");
        const auto saveMs = juce::Time::getMillisecondCounterHiRes() - saving;
        check (saveMs < 50, "State saving waited for a blocked settings read");
        capture (processor.getEngine(), 16000, 0.35f); gate->release.signal(); saved (processor.getEngine(), true);
        const auto id = processor.getEngine().getAssociationInfo().id;
        audioIs (second.getChildFile (".pending").getChildFile (id), 16000, 1, 0.35f);
        check (stateFolder (processor) == second.getFullPathName() && ! first.getChildFile (".pending").getChildFile (id).exists(),
               "A delayed constructor snapshot overwrote preparation or redirected its recording");
        std::cout << "PASS: held constructor settings read stays off UI; constructor=" << constructMs << " ms, save=" << saveMs
                  << " ms; newer preparation/destination wins\n";
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Constructor settings worker did not retire");
    {
        fields->setProperty ("previewsFolder", first.getFullPathName());
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot seed preparation destination");
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        auto calls = std::make_shared<std::atomic<int>> (0);
        RememberProcessor processor ([] { return HostProject{}; }, [gate, calls]
        { return calls->fetch_add (1) == 1 ? gate->read() : SettingsManager::getInstance().readSnapshot(); });
        await ([&] { return stateFolder (processor) == first.getFullPathName(); }, "Cannot initialise preparation fixture");
        fields->setProperty ("previewsFolder", second.getFullPathName()); fields->setProperty ("recordSeconds", 15);
        fields->setProperty ("oggQuality", 9); fields->setProperty ("recordMono", true); fields->setProperty ("dataRevision", 23);
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot write pending preparation settings");
        const auto expected = SettingsManager::getInstance().readSnapshot();
        const auto began = juce::Time::getMillisecondCounterHiRes(); processor.prepareToPlay (8000, 128);
        const auto prepareMs = juce::Time::getMillisecondCounterHiRes() - began;
        check (prepareMs < 100 && gate->entered.wait (1000) && ! gate->onMessageThread.load()
               && processor.getEngine().isPreparingSettings() && processor.getEngine().getTotalSeconds() == 0,
               "Preparation waited for/read settings on its caller or reported a stale duration");
        auto editor = std::unique_ptr<RememberEditor> (static_cast<RememberEditor*> (processor.createEditor()));
        Head head; juce::AudioBuffer<float> block (2, 128);
        juce::FloatVectorOperations::fill (block.getWritePointer (0), 0.8f, 128);
        juce::FloatVectorOperations::fill (block.getWritePointer (1), -0.2f, 128);
        processor.getEngine().processBlock (block, &head);
        check (statusContains (*editor, "Capturing audio; reading recording settings"), "Pending preparation was hidden by a stale duration");
        const auto saving = juce::Time::getMillisecondCounterHiRes();
        check (stateFolder (processor) == first.getFullPathName(), "State guessed an unconfirmed preparation destination");
        const auto saveMs = juce::Time::getMillisecondCounterHiRes() - saving;
        check (saveMs < 50, "State saving waited for the pending preparation");
       #if JUCE_WINDOWS
        std::atomic<bool> dispatched { false }; juce::MessageManager::callAsync ([&] { dispatched.store (true); });
        const auto deadline = juce::Time::getMillisecondCounter() + 1000;
        while (! dispatched.load() && juce::Time::getMillisecondCounter() < deadline)
        {
            MSG message;
            while (PeekMessageW (&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage (&message); DispatchMessageW (&message); }
            juce::Thread::sleep (1);
        }
        check (dispatched.load(), "Held preparation prevented message dispatch");
       #endif
        // The configured duration is 15 s; keep settings held through 17 s.
        // The first and final configured samples must survive late trimming.
        for (int done = 128; done < 17 * 8000; done += 128)
        { head.time = done / 8000.0; processor.getEngine().processBlock (block, &head); }
        head.playing = false; head.time = 17; processor.getEngine().processBlock (block, &head);
        const auto id = processor.getEngine().getAssociationInfo().id;
        const auto folder = second.getChildFile (".pending").getChildFile (id);
        check (! folder.getChildFile ("metadata.json").existsAsFile(), "Unconfirmed settings published guessed audio");
        fields->setProperty ("previewsFolder", first.getFullPathName()); fields->setProperty ("recordSeconds", 30);
        fields->setProperty ("oggQuality", 2); fields->setProperty ("recordMono", false); fields->setProperty ("dataRevision", 24);
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot replace settings during preparation");
        gate->release.signal(); saved (processor.getEngine(), true);
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
        juce::AudioBuffer<float> sample (1, 1);
        check (reader && reader->numChannels == 1 && reader->sampleRate == 8000 && reader->lengthInSamples == 120000,
               "Delayed settings changed channel count, rate or duration");
        for (auto offset : { 0, 127, 119999 })
            check (reader->read (&sample, 0, 1, offset, true, true) && std::abs (sample.getSample (0, 0) - 0.3f) < 0.00001f,
                   "Delayed preparation lost the first sample or failed to average both mono channels");
        const auto metadata = LibraryStore::readJson (folder.getChildFile ("metadata.json"));
        check (metadata.getProperty ("settingsFingerprint", "").toString() == expected.fingerprint
               && (int) metadata.getProperty ("settingsRevision", 0) == 23 && (int) metadata.getProperty ("recordSeconds", 0) == 15
               && (int) metadata.getProperty ("oggQuality", 0) == 9 && (bool) metadata.getProperty ("recordMono", false)
               && stateFolder (processor) == second.getFullPathName() && processor.getEngine().getDroppedCaptures() == 0,
               "A later external edit mixed fields or redirected the accepted capture");
        std::cout << "PASS: settings held beyond 15 s preserve frame zero, averaged mono, duration, quality, destination and fingerprint; prepare="
                  << prepareMs << " ms, save=" << saveMs << " ms; dispatch remains live\n";
        fields->setProperty ("previewsFolder", second.getFullPathName()); fields->setProperty ("recordSeconds", 45);
        fields->setProperty ("oggQuality", 9); fields->setProperty ("recordMono", true);
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot restore configuration for restoration fixture");
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Preparation fixture did not retire");
    juce::MemoryBlock manualState; juce::String manualId;
    {
        RememberProcessor seed ([] { return HostProject{}; });
        seed.getEngine().prepare (8000, 128, oldSettings); capture (seed.getEngine(), 8000, 0.2f, 2.4); saved (seed.getEngine(), true);
        seed.getEngine().keepRecording ("Processor restoration idea");
        await ([&] { return seed.getEngine().getAssociationInfo().confirmed; }, "Cannot seed kept processor state");
        manualId = seed.getEngine().getAssociationInfo().id; seed.getStateInformation (manualState);
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Kept processor seed did not retire");
    {
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        auto calls = std::make_shared<std::atomic<int>> (0);
        RememberProcessor processor ([] { return HostProject{}; }, [gate, calls]
        {
            return calls->fetch_add (1) == 1 ? gate->read() : SettingsManager::getInstance().readSnapshot();
        });
        await ([&] { return stateFolder (processor) == second.getFullPathName(); }, "Processor did not initialise the current library");
        const auto restoring = juce::Time::getMillisecondCounterHiRes();
        processor.setStateInformation (manualState.getData(), (int) manualState.getSize());
        const auto restoreMs = juce::Time::getMillisecondCounterHiRes() - restoring;
        check (restoreMs < 50 && gate->entered.wait (1000) && ! gate->onMessageThread.load(),
               "Processor restoration waited for/read settings on UI");
        check (! processor.getEngine().getAssociationInfo().confirmed, "Restoration confirmed a destination before reading its settings");
        fields->setProperty ("previewsFolder", first.getFullPathName());
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot change output during restore read");
        processor.prepareToPlay (16000, 128);
        await ([&] { return ! processor.getEngine().isPreparingSettings(); }, "New preparation waited for the older restore read");
        const auto prepared = SettingsManager::getInstance().readSnapshot();
        capture (processor.getEngine(), 16000, 0.7f);
        const auto previous = first.getChildFile (manualId);
        audioIs (previous, 8000, 2, 0.2f);
        check (stateFolder (processor) == first.getFullPathName() && ! second.getChildFile (manualId).exists(),
               "A blocked restoration changed audio or selected the template's/older destination");
        gate->release.signal();
        await ([&] { return processor.getEngine().getAssociationInfo().confirmed; }, "Processor manual restoration did not confirm after reading");
        saved (processor.getEngine()); audioIs (previous, 16000, 1, 0.7f);
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (previous, true)));
        juce::AudioBuffer<float> tail (1, 1);
        check (reader && reader->lengthInSamples == 38400 && reader->read (&tail, 0, 1, 28000, true, true)
               && std::abs (tail.getSample (0, 0) - 0.2f) < 0.0001f, "Restoration changed the kept tail during resampling");
        const auto metadata = LibraryStore::readJson (previous.getChildFile ("metadata.json"));
        check (metadata.getProperty ("settingsFingerprint", "").toString() == prepared.fingerprint
               && (int) metadata.getProperty ("recordSeconds", 0) == 45 && (int) metadata.getProperty ("oggQuality", 0) == 9
               && processor.getEngine().getAssociationInfo().id == manualId && ! second.getChildFile (manualId).exists(),
               "Restore mixed configuration/identity or its older read created an entry in the wrong library");
        std::cout << "PASS: processor restore=" << restoreMs
                  << " ms during held settings read; newer preparation wins, kept identity/tail and exact settings fingerprint survive\n";
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Restored processor settings worker did not retire");
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (manualState.getData(), (int) manualState.getSize());
        check (xml != nullptr, "Cannot read ordered-restoration seed");
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        RecordingEngine engine ([] { return HostProject{}; });
        engine.setOutputFolder (first); engine.restoreState (*xml);
        engine.prepareAsync (8000, 128, [gate] { return gate->read(); });
        check (gate->entered.wait (1000), "Cannot hold settings behind restoration");
        engine.keepRecording ("Ordered restoration edit", manualId);
        capture (engine, 8000, 0.55f);
        juce::Thread::sleep (80);
        check (! engine.getAssociationInfo().confirmed, "A later edit crossed the pending restoration barrier");
        gate->release.signal();
        await ([&] { return engine.getAssociationInfo().confirmed && engine.getProjectName() == "Ordered restoration edit"; },
               "A later keep/name edit was lost behind asynchronous restoration");
        saved (engine); audioIs (first.getChildFile (manualId), 8000, 1, 0.55f);
        check (engine.getAssociationInfo().id == manualId && engine.getLastError().isEmpty(),
               "Ordered restoration changed identity or rejected its accepted edit");
        std::cout << "PASS: queued keep/name edit waits for pending restoration, then preserves identity and captured prefix\n";
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Ordered restoration did not retire");
    {
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        auto processor = std::make_unique<RememberProcessor> ([] { return HostProject{}; }, [gate] { return gate->read(); });
        check (gate->entered.wait (1000), "Cannot hold settings read for processor closure");
        const auto closing = juce::Time::getMillisecondCounterHiRes(); processor.reset();
        const auto closeMs = juce::Time::getMillisecondCounterHiRes() - closing;
        check (closeMs < 100 && BackgroundRetirement::pending() > 0, "Processor closure waited for/freed a retained settings reader");
        gate->release.signal(); await ([] { return BackgroundRetirement::pending() == 0; }, "Closed processor settings read did not retire");
        std::cout << "PASS: processor close during held settings read=" << closeMs << " ms; native context retires after release\n";
    }
    {
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        auto calls = std::make_shared<std::atomic<int>> (0);
        auto processor = std::make_unique<RememberProcessor> ([] { return HostProject{}; }, [gate, calls]
        { return calls->fetch_add (1) == 1 ? gate->read() : SettingsManager::getInstance().readSnapshot(); });
        const auto current = SettingsManager::getInstance().readSnapshot().previews;
        await ([&] { return stateFolder (*processor) == current.getFullPathName(); }, "Cannot initialise settings-close fixture");
        processor->prepareToPlay (8000, 128);
        check (gate->entered.wait (1000) && processor->getEngine().isPreparingSettings(), "Cannot hold a preparation read for closure");
        capture (processor->getEngine(), 8000, 0.4f);
        const auto closing = juce::Time::getMillisecondCounterHiRes(); processor.reset();
        const auto closeMs = juce::Time::getMillisecondCounterHiRes() - closing;
        check (closeMs < 100 && BackgroundRetirement::pending() > 0, "Closure waited for or freed the pending preparation job");
        gate->release.signal(); await ([] { return BackgroundRetirement::pending() == 0; }, "Closed preparation job did not retire");
        check (RecordingEngine::getProcessCaptureBytes() == 0, "Closing a pending preparation stranded its capture reservation");
        std::cout << "PASS: close during held preparation=" << closeMs << " ms; native settings job and capture bank retire after release\n";
    }
    {
        fields->setProperty ("previewsFolder", first.getFullPathName());
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot seed settings-error fixture");
        RememberProcessor processor ([] { return HostProject{}; });
        auto& engine = processor.getEngine(); engine.prepare (8000, 128, oldSettings);
        capture (engine, 8000, 0.2f, 2.4); saved (engine, true); engine.keepRecording ("Settings error retention");
        await ([&] { return engine.getAssociationInfo().confirmed; }, "Cannot confirm settings-error seed");
        const auto folder = first.getChildFile (engine.getAssociationInfo().id);
        const auto revision = LibraryStore::readJson (folder.getChildFile ("metadata.json")).getProperty ("audioRevision", "").toString();
        check (sharedConfig.replaceWithText ("{"), "Cannot damage preparation settings");
        processor.prepareToPlay (8000, 128); capture (engine, 8000, 0.9f);
        await ([&] { return engine.getState() == RecordingEngine::State::Error; }, "Damaged settings were accepted as defaults");
        check (engine.getLastError().contains (sharedConfig.getFullPathName())
               && LibraryStore::readJson (folder.getChildFile ("metadata.json")).getProperty ("audioRevision", "").toString() == revision,
               "Damaged settings hid their path or changed a confirmed audio revision");
        audioIs (folder, 8000, 2, 0.2f);
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot restore preparation settings");
        processor.prepareToPlay (8000, 128); capture (engine, 8000, 0.7f); saved (engine);
        audioIs (folder, 8000, 1, 0.7f);
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (LibraryStore::audioFile (folder, true)));
        juce::AudioBuffer<float> tail (1, 1);
        check (reader && reader->lengthInSamples == 19200 && reader->read (&tail, 0, 1, 15000, true, true)
               && std::abs (tail.getSample (0, 0) - 0.2f) < 0.00001f, "Settings retry lost the previous confirmed tail");
        fields->setProperty ("previewsFolder", second.getFullPathName());
        check (LibraryStore::writeJsonAtomically (sharedConfig, value).wasOk(), "Cannot restore settings after error fixture");
        std::cout << "PASS: damaged settings preserve confirmed audio, show their path and recover on a new preparation\n";
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Settings-error fixture did not retire");
    {
        auto gate = std::make_shared<ReadGate>(); ReleaseGate release { gate };
        auto entered = std::make_shared<std::atomic<int>> (0);
        std::vector<std::shared_ptr<SettingsManager::Request>> requests;
        for (int i = 0; i < 40; ++i)
            requests.push_back (SettingsManager::readSnapshotAsync ([gate, entered]
            { entered->fetch_add (1); return gate->read(); }));
        await ([&] { return entered->load() == 32; }, "Settings worker limit failed to admit its bounded jobs");
        int rejected = 0;
        for (const auto& request : requests)
            if (request->complete.load (std::memory_order_acquire))
            {
                check (request->result.failed() && request->result.getErrorMessage().contains ("busy"), "Settings saturation silently dropped a request");
                ++rejected;
            }
        check (rejected == 8, "Repeated blocked settings requests escaped the 32-worker bound");
        gate->release.signal();
        await ([] { return BackgroundRetirement::pending() == 0; }, "Saturated settings jobs did not retire");
        for (int i = 0; i < 32; ++i)
            check (requests[(size_t) i]->complete.load (std::memory_order_acquire) && requests[(size_t) i]->result.wasOk(),
                   "An accepted settings read was lost after saturation");
        auto retry = SettingsManager::getInstance().readSnapshotAsync();
        await ([&] { return retry->complete.load (std::memory_order_acquire); }, "Settings reservation was not reusable after native completion");
        check (retry->result.wasOk(), "Settings retry failed after releasing the worker budget");
        std::cout << "PASS: 40 blocked settings requests admit 32 native jobs, reject 8 with retry details and release all reservations\n";
    }
    await ([] { return BackgroundRetirement::pending() == 0; }, "Settings budget retry did not retire");
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        auto processor = std::make_unique<RememberProcessor> ([] { return HostProject{}; });
        await ([&] { return stateFolder (*processor).isNotEmpty(); }, "Cannot initialise processor for blocked logging");
        DiagnosticGate log;
        const auto preparing = juce::Time::getMillisecondCounterHiRes();
        processor->prepareToPlay (16000, 128);
        const auto prepareMs = juce::Time::getMillisecondCounterHiRes() - preparing;
        check (prepareMs < 100 && log.prepared.wait (1000) && ! log.onMessageThread.load(),
               "Preparation waited for or wrote its log on the caller thread");
        const auto opening = juce::Time::getMillisecondCounterHiRes();
        std::unique_ptr<RememberEditor> editor (static_cast<RememberEditor*> (processor->createEditor()));
        const auto editorMs = juce::Time::getMillisecondCounterHiRes() - opening;
        check (editorMs < 100, "Opening the editor waited for a held diagnostic write");
       #if JUCE_WINDOWS
        std::atomic<bool> dispatched { false }; juce::MessageManager::callAsync ([&] { dispatched.store (true); });
        const auto deadline = juce::Time::getMillisecondCounter() + 1000;
        while (! dispatched.load() && juce::Time::getMillisecondCounter() < deadline)
        {
            MSG message;
            while (PeekMessageW (&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage (&message); DispatchMessageW (&message); }
            juce::Thread::sleep (1);
        }
        check (dispatched.load(), "Held logging prevented message dispatch");
       #endif
        editor.reset();
        const auto closing = juce::Time::getMillisecondCounterHiRes(); processor.reset();
        const auto closeMs = juce::Time::getMillisecondCounterHiRes() - closing;
        check (closeMs < 100 && BackgroundRetirement::pending() > 0, "Processor closure waited for or freed the held logger worker");
        log.release.signal();
        check (log.editor.wait (1000) && ! log.editorOnMessageThread.load(), "Editor diagnostic was lost or written on the message thread");
        await ([] { return BackgroundRetirement::pending() == 0; }, "Diagnostic worker did not retire after release");
        std::cout << "PASS: held diagnostic write stays off caller; prepare=" << prepareMs << " ms, editor=" << editorMs
                  << " ms, close=" << closeMs << " ms; editor event survives closure and message dispatch remains live; attempt=" << attempt + 1 << '\n';
    }
}
