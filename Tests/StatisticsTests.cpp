#include "StatisticsTests.h"
#include "../App/Source/StatisticsDialog.h"
#include <stdexcept>
#include <iostream>

struct StatisticsTestAccess
{
    static void search (StatisticsDialog& d, const juce::String& text) { d.search.setText (text, false); d.filter(); }
    static int rows (StatisticsDialog& d) { return (int) d.visible.size(); }
    static juce::ListBox& list (StatisticsDialog& d) { return d.list; }
    static WaveformPlayerComponent& player (StatisticsDialog& d) { return d.player; }
    static void audition (StatisticsDialog& d, size_t index) { d.autoPlay = false; d.audition (index); }
    static void data (StatisticsDialog& d, const ProjectStatistics::Report& report) { d.publish (std::make_shared<ProjectStatistics::Report> (report)); }
    static bool waveform (StatisticsDialog& d) { return d.player.waveform != nullptr; }
    static juce::String loadedName (WaveformPlayerComponent& p) { return p.currentEntry.name; }
    static void eye (StatisticsDialog& d) { d.eye.onClick(); }
    static void hide (StatisticsDialog& d, ProjectStatistics::Category c, const juce::String& identity, bool hidden) { d.setHidden (c, identity, hidden); }
    static juce::String name (StatisticsDialog& d, size_t i) { return d.entryName (i); }
    static juce::String visibleIdentity (StatisticsDialog& d, size_t row) { return d.items()[d.visible[row]].identity; }
    static juce::TextEditor& searchBox (StatisticsDialog& d) { return d.search; }
    static juce::TextButton& eyeButton (StatisticsDialog& d) { return d.eye; }
    static void interceptLocations (StatisticsDialog& d, juce::String& copied, std::shared_ptr<std::atomic<bool>> revealed, const juce::File& expected)
    {
        d.copyLocation = [&copied] (const juce::String& path) { copied = path; };
        d.revealFile = [revealed, expected] (const juce::File& file) { revealed->store (file == expected); };
    }
    static void drag (StatisticsDialog& d, size_t index, juce::String& path, bool& move)
    {
        d.startFileDrag = [&path, &move] (const juce::StringArray& files, bool mayMove, juce::Component*, std::function<void()> done)
        { path = files[0]; move = mayMove; if (done) done(); return true; };
        d.exportFile (index, true, &d);
    }
};
namespace
{
void check (bool pass, const char* message) { if (! pass) throw std::runtime_error (message); }
template<class Condition> void waitFor (Condition condition)
{
    const auto until = juce::Time::getMillisecondCounter() + 15000;
    while (! condition()) { check (juce::Time::getMillisecondCounter() < until, "Statistics fixture timed out"); juce::MessageManager::getInstance()->runDispatchLoopUntil (5); }
}
void snapshot (juce::Component& component, const juce::String& name)
{
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts"); directory.createDirectory();
    juce::FileOutputStream stream (directory.getChildFile (name)); stream.setPosition (0); stream.truncate(); juce::PNGImageFormat png;
    check (png.writeImageToStream (component.createComponentSnapshot (component.getLocalBounds(), true, 1, juce::SoftwareImageType{}), stream), "Statistics snapshot failed");
}
void number (juce::MemoryOutputStream& out, int id, int value)
{ out.writeByte ((char) id); if (id < 64) out.writeByte ((char) value); else if (id < 128) out.writeShort ((short) value); else out.writeInt (value); }
void blob (juce::MemoryOutputStream& out, int id, const void* bytes, size_t size)
{
    out.writeByte ((char) id); size_t n = size;
    do { const auto part = n & 127; n >>= 7; out.writeByte ((char) (part | (n ? 128 : 0))); } while (n);
    out.write (bytes, size);
}
void text (juce::MemoryOutputStream& out, int id, const juce::String& value, bool unicode = true)
{
    juce::MemoryOutputStream bytes;
    if (unicode) { for (const auto c : value) bytes.writeShort ((short) c); bytes.writeShort (0); }
    else bytes.write (value.toRawUTF8(), value.getNumBytesAsUTF8() + 1);
    blob (out, id, bytes.getData(), bytes.getDataSize());
}
juce::File flFixture (const juce::File& folder, const juce::File& audio)
{
    juce::MemoryOutputStream data; text (data, 199, "26.0.1", false);
    data.writeByte ((char) 172); data.writeRepeatedByte (0, 3);
    number (data, 64, 0); number (data, 21, 0); text (data, 196, audio.getFullPathName());
    number (data, 64, 1); number (data, 21, 4); text (data, 201, "Fruity Wrapper");
    juce::MemoryOutputStream wrapper; wrapper.writeInt (12); wrapper.writeInt (54); wrapper.writeInt64 (6); wrapper.write ("Serum\0", 6);
    blob (data, 213, wrapper.getData(), wrapper.getDataSize());
    number (data, 64, 2); number (data, 21, 0); text (data, 196, audio.getFullPathName());
    number (data, 64, 3); number (data, 21, 5); text (data, 201, "Automation");
    number (data, 64, 4); number (data, 21, 4); text (data, 201, "FL Keys");
    number (data, 64, 5); number (data, 21, 0); text (data, 196, "Samples/missing.wav");
    juce::MemoryOutputStream flags; flags.writeRepeatedByte (0, 12); blob (data, 236, flags.getData(), flags.getDataSize());
    text (data, 201, "Fruity Limiter"); number (data, 98, 0);
    text (data, 201, "Fruity Wrapper"); wrapper.reset(); wrapper.writeInt (12); wrapper.writeInt (54); wrapper.writeInt64 (9); wrapper.write ("Pro-Q 3\0\0", 9);
    blob (data, 213, wrapper.getData(), wrapper.getDataSize()); number (data, 98, 1);
    juce::MemoryOutputStream file; file.write ("FLhd", 4); file.writeInt (6); file.writeShort (0); file.writeShort (6); file.writeShort (96);
    file.write ("FLdt", 4); file.writeInt ((int) data.getDataSize()); file.write (data.getData(), data.getDataSize());
    auto target = folder.getChildFile ("Statistics.flp"); check (target.replaceWithData (file.getData(), file.getDataSize()), "FL statistics fixture"); return target;
}
juce::String sampleRef (const juce::String& path)
{
    juce::XmlElement p ("Path"); p.setAttribute ("Value", path);
    return "<SampleRef><FileRef>" + p.toString (juce::XmlElement::TextFormat{}.withoutHeader()) + "</FileRef></SampleRef>";
}
juce::File alsFixture (const juce::File& folder, const juce::File& audio)
{
    const auto external = [] (const char* name, const char* tag, const char* extra)
    { return "<PluginDevice><PluginDesc><" + juce::String (tag) + "><" + (juce::String (tag) == "VstPluginInfo" ? "PlugName" : "Name")
        + " Value=\"" + name + "\"/>" + extra + "</" + tag + "></PluginDesc></PluginDevice>"; };
    const juce::String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Ableton Creator=\"Ableton Live 12\"><LiveSet><Tracks><MidiTrack><DeviceChain><DeviceChain><Devices>"
        + external ("Serum", "VstPluginInfo", "<Flags Value=\"1287\"/>")
        + external ("EchoBoy", "VstPluginInfo", "<Flags Value=\"1280\"/>")
        + external ("Pro-Q 3", "Vst3PluginInfo", "<DeviceType Value=\"2\"/>")
        + "<AutoFilter2/><MxDeviceAudioEffect><PatchSlot><Value><MxPatchRef><FileRef><Path Value=\"C:/Max/ClipGain.amxd\"/></FileRef></MxPatchRef></Value></PatchSlot></MxDeviceAudioEffect>"
        + "<MxDeviceInstrument><PatchSlot><Value><MxDPatchRef><FileRef><Path Value=\"C:/Max/Poli.amxd\"/></FileRef></MxDPatchRef></Value></PatchSlot></MxDeviceInstrument>"
        + "<InstrumentGroupDevice><Branches><InstrumentBranch><DeviceChain><MidiToAudioDeviceChain><Devices><OriginalSimpler>"
        + sampleRef (audio.getFullPathName()) + "</OriginalSimpler><Operator/></Devices></MidiToAudioDeviceChain></DeviceChain></InstrumentBranch></Branches></InstrumentGroupDevice>"
        + "<Reverb><SourceContext>" + sampleRef ("C:/ignored.wav") + "</SourceContext></Reverb></Devices></DeviceChain></DeviceChain>"
        + "<AudioClip>" + sampleRef (audio.getFullPathName()) + "</AudioClip></MidiTrack></Tracks></LiveSet></Ableton>";
    auto target = folder.getChildFile ("Statistics.als"); juce::MemoryOutputStream compressed;
    { juce::GZIPCompressorOutputStream gzip (&compressed, 6, false, 15 + 16); check (gzip.write (xml.toRawUTF8(), xml.getNumBytesAsUTF8()), "gzip fixture"); }
    check (target.replaceWithData (compressed.getData(), compressed.getDataSize()), "ALS statistics fixture"); return target;
}
}
void testStatistics (const juce::File& root)
{
    struct RestoreConfig
    {
        juce::File file { LibraryStore::configFile() };
        bool existed = file.existsAsFile(); juce::String text { file.loadFileAsString() };
        ~RestoreConfig()
        {
            waitFor ([] { return ProjectScanner::pendingJobs() == 0; });
            if (existed) file.replaceWithText (text); else file.deleteFile();
            ProjectScanner::refreshAsync();
        }
    } restore;
    auto folder = root.getChildFile ("statistics"); folder.getChildFile ("Samples").createDirectory();
    const auto audio = folder.getChildFile ("Samples/Tone & texture.wav");
    {
        juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (audio);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (44100).withNumChannels (1).withBitsPerSample (16));
        check (writer != nullptr, "Statistics sample writer"); juce::AudioBuffer<float> samples (1, 44100);
        for (int i = 0; i < samples.getNumSamples(); ++i) samples.setSample (0, i, (float) (0.5 * std::sin (i * juce::MathConstants<double>::twoPi * 440 / 44100)));
        writer->writeFromAudioSampleBuffer (samples, 0, 44100);
    }
    const auto flp = flFixture (folder, audio), als = alsFixture (folder, audio);
    const auto fl = ProjectStatistics::readProject (flp), live = ProjectStatistics::readProject (als);
    check (fl.result.wasOk(), fl.result.getErrorMessage().toRawUTF8()); check (live.result.wasOk(), live.result.getErrorMessage().toRawUTF8());
    check (fl.items[0].size() == 3 && fl.items[1].size() == 2 && fl.items[2].size() == 2, "FL categories or automation exclusion");
    check (live.items[0].size() == 2 && live.items[1].size() == 4 && live.items[2].size() == 5 && live.unclassifiedPlugins == 0, "Ableton nested devices, references or classifications");
    auto broken = folder.getChildFile ("broken.als"); broken.replaceWithText ("<Ableton><LiveSet><Tracks>");
    check (ProjectStatistics::readProject (broken).result.failed(), "Truncated ALS accepted");
    auto entry = [] (const juce::File& file) { ProjectEntry e; e.projectPath = file.getFullPathName(); return e; };
    juce::Array<ProjectEntry> entries { entry (flp), entry (flp), entry (als), entry (broken), entry (folder.getChildFile ("other.rpp")), ProjectEntry{} };
    const auto report = ProjectStatistics::collect (entries);
    check (report.read == 2 && report.failed == 1 && report.excluded == 1 && report.unlinked == 1, "Coverage or duplicate project counting");
    check (report.items[0].size() == 2 && report.items[0][0].projects == 2 && report.items[0][0].available, "Sample path grouping or project deduplication");
    check (report.items[1][0].name == "Serum" && report.items[1][0].projects == 2, "Cross-DAW instrument count");
    check (report.items[2][0].name == "Pro-Q 3" && report.items[2][0].projects == 2, "Cross-DAW effect count");
    check (ProjectStatistics::collect (entries, [] { return true; }).error == "Cancelled", "Statistics cancellation");
    {
        juce::String manySamples, manyPlugins;
        for (int i = 0; i < 1100; ++i) manySamples += sampleRef ("Samples/sample-" + juce::String (i).paddedLeft ('0', 4) + ".wav");
        for (int i = 0; i < 65; ++i)
        {
            const auto suffix = juce::String (i).paddedLeft ('0', 2);
            manyPlugins += "<PluginDevice><PluginDesc><Vst3PluginInfo><Name Value=\"Instrument " + suffix + "\"/><DeviceType Value=\"1\"/></Vst3PluginInfo></PluginDesc></PluginDevice>";
            manyPlugins += "<PluginDevice><PluginDesc><Vst3PluginInfo><Name Value=\"Effect " + suffix + "\"/><DeviceType Value=\"2\"/></Vst3PluginInfo></PluginDesc></PluginDevice>";
        }
        const auto wrap = [] (const juce::String& samples, const juce::String& devices)
        { return "<Ableton><LiveSet><Tracks><MidiTrack><DeviceChain><Devices>" + devices + "</Devices></DeviceChain><AudioClip>" + samples + "</AudioClip></MidiTrack></Tracks></LiveSet></Ableton>"; };
        const auto many = folder.getChildFile ("many.als"), extra = folder.getChildFile ("extra.als");
        many.replaceWithText (wrap (manySamples, manyPlugins));
        extra.replaceWithText (wrap (sampleRef ("Samples/sample-1099.wav"), "<PluginDevice><PluginDesc><Vst3PluginInfo><Name Value=\"Instrument 64\"/><DeviceType Value=\"1\"/></Vst3PluginInfo></PluginDesc></PluginDevice><PluginDevice><PluginDesc><Vst3PluginInfo><Name Value=\"Effect 64\"/><DeviceType Value=\"2\"/></Vst3PluginInfo></PluginDesc></PluginDevice>"));
        const auto capped = ProjectStatistics::collect ({ entry (many), entry (extra) });
        check (capped.read == 2 && capped.items[0].size() == 1100 && capped.items[1].size() == 65 && capped.items[2].size() == 65, "Full ranking was discarded before hidden-entry refill");
        check (capped.items[0][0].name == "sample-1099.wav" && capped.items[0][0].projects == 2
            && capped.items[1][0].name == "Instrument 64" && capped.items[2][0].name == "Effect 64", "Ranking must sort by project count before limiting");
    }
    WaveformPlayerComponent mainPlayer (nullptr, false);
    ProjectEntry mainEntry; mainEntry.name = "Main project preview"; mainEntry.previewOgg = audio; mainPlayer.loadEntry (mainEntry);
    waitFor ([&] { return mainPlayer.hasLoadedEntry(); });
    {
        StatisticsDialog dialog (&mainPlayer, false); waitFor ([&] { return dialog.hasLoaded(); }); StatisticsTestAccess::data (dialog, report);
        check (StatisticsTestAccess::rows (dialog) == 2, "Statistics sample rows");
        check (StatisticsTestAccess::name (dialog, 0) == "Tone & texture" && StatisticsTestAccess::list (dialog).getRowHeight() == RememberUI::statisticsRowHeight, "Sample extension is visible or rows are not compact");
        {
            juce::String copied; const auto revealed = std::make_shared<std::atomic<bool>> (false);
            StatisticsTestAccess::interceptLocations (dialog, copied, revealed, audio);
            const auto open = [&]
            {
                auto* row = StatisticsTestAccess::list (dialog).getComponentForRowNumber (0); check (row != nullptr, "Statistics row missing");
                const auto now = juce::Time::getCurrentTime();
                juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), { 100, 20 }, juce::ModifierKeys::rightButtonModifier, 1, 0, 0, 0, 0, row, row, now, { 100, 20 }, now, 1, false);
                row->mouseDown (event);
                auto* popup = juce::Component::getCurrentlyModalComponent(); check (popup != nullptr, "Statistics context menu did not open"); return popup;
            };
            const auto select = [&] (juce::Component* popup, int steps)
            {
                for (int i = 0; i < steps; ++i) check (popup->keyPressed (juce::KeyPress (juce::KeyPress::downKey)), "Statistics menu navigation failed");
                check (popup->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)), "Statistics menu action failed");
                waitFor ([] { return juce::Component::getCurrentlyModalComponent() == nullptr; });
            };
            auto* popup = open(); snapshot (*popup, "statistics-sample-context-menu.png"); select (popup, 3);
            waitFor ([&] { return copied == audio.getFullPathName(); });
            select (open(), 2); waitFor ([&] { return revealed->load(); });
            popup = open(); dialog.setCategory (ProjectStatistics::Category::Instruments); select (popup, 1);
            dialog.setCategory (ProjectStatistics::Category::Samples);
            waitFor ([&] { return StatisticsTestAccess::rows (dialog) == 1; });
            waitFor ([] { return ProjectScanner::pendingJobs() == 0; });
            check (StatisticsTestAccess::rows (dialog) == 1 && ! StatisticsTestAccess::player (dialog).hasLoadedEntry(), "Recycled menu hid the wrong category or started an audition");
            ProjectStatistics::HiddenEntries saved; check (ProjectStatistics::readHiddenEntries (saved).wasOk() && saved[0].count (report.items[0][0].identity) == 1, "Hidden entry was not persisted");
            {
                StatisticsDialog reopened (nullptr, false); waitFor ([&] { return reopened.hasLoaded(); });
                StatisticsTestAccess::data (reopened, report);
                check (StatisticsTestAccess::rows (reopened) == 1, "Reopened Statistics lost the saved hidden entry");
                StatisticsTestAccess::eye (reopened);
                check (StatisticsTestAccess::rows (reopened) == 2, "Reopened Statistics cannot reveal the saved hidden entry");
            }
            StatisticsTestAccess::eye (dialog); check (StatisticsTestAccess::rows (dialog) == 2, "Eye does not show hidden and visible entries together");
            snapshot (dialog, "statistics-hidden-entry.png"); select (open(), 1);
            waitFor ([&] { ProjectStatistics::HiddenEntries current; return ProjectStatistics::readHiddenEntries (current).wasOk() && current[0].empty(); });
            waitFor ([] { return ProjectScanner::pendingJobs() == 0; });
            check (ProjectStatistics::readHiddenEntries (saved).wasOk() && saved[0].empty(), "Unhide entry was not persisted");
            StatisticsTestAccess::eye (dialog);
            for (const auto type : { ProjectStatistics::Category::Instruments, ProjectStatistics::Category::Effects })
            {
                dialog.setCategory (type);
                const auto c = (size_t) type; const auto count = (int) report.items[c].size();
                const auto identity = StatisticsTestAccess::visibleIdentity (dialog, 0);
                auto* pluginMenu = open();
                snapshot (*pluginMenu, c == 1 ? "statistics-instrument-context-menu.png" : "statistics-effect-context-menu.png");
                select (pluginMenu, 1);
                waitFor ([&] { return StatisticsTestAccess::rows (dialog) == count - 1; });
                waitFor ([] { return ProjectScanner::pendingJobs() == 0; });
                check (ProjectStatistics::readHiddenEntries (saved).wasOk() && saved[c].count (identity) == 1,
                    "Plugin context menu failed to hide and persist its entry");
                StatisticsTestAccess::eye (dialog);
                check (StatisticsTestAccess::rows (dialog) == count, "Plugin eye failed to reveal hidden entry");
                select (open(), 1);
                waitFor ([&] { ProjectStatistics::HiddenEntries current; return ProjectStatistics::readHiddenEntries (current).wasOk() && current[c].empty(); });
                StatisticsTestAccess::eye (dialog);
            }
            dialog.setCategory (ProjectStatistics::Category::Samples);
        }
        StatisticsTestAccess::search (dialog, "TONE &"); check (StatisticsTestAccess::rows (dialog) == 1, "Name search is not case insensitive");
        StatisticsTestAccess::search (dialog, "nonexistent"); check (StatisticsTestAccess::rows (dialog) == 0, "No-results search");
        dialog.setSize (640, 480); snapshot (dialog, "statistics-search-empty-minimum.png");
        StatisticsTestAccess::search (dialog, "");
        juce::String dragged; bool move = true; StatisticsTestAccess::drag (dialog, 0, dragged, move);
        check (! move && dragged == audio.getFullPathName(), "Statistics drag must copy original");
        StatisticsTestAccess::audition (dialog, 0); waitFor ([&] { return StatisticsTestAccess::player (dialog).hasLoadedEntry(); });
        waitFor ([&] { return StatisticsTestAccess::waveform (dialog); });
        check (mainPlayer.hasLoadedEntry() && StatisticsTestAccess::loadedName (mainPlayer) == mainEntry.name, "Statistics audition changed main player");
        dialog.setSize (640, 480); snapshot (dialog, "statistics-samples-minimum.png");
        const auto checkAlignment = [&]
        {
            const auto listBounds = StatisticsTestAccess::list (dialog).getBounds();
            const auto surface = dialog.getLocalArea (&StatisticsTestAccess::player (dialog), StatisticsTestAccess::player (dialog).surfaceBounds());
            check (surface.getX() == listBounds.getX() && surface.getRight() == listBounds.getRight(), "Statistics player surface does not align with list");
            check (StatisticsTestAccess::searchBox (dialog).getX() == listBounds.getX()
                && StatisticsTestAccess::eyeButton (dialog).getRight() == listBounds.getRight()
                && StatisticsTestAccess::searchBox (dialog).getRight() + 8 == StatisticsTestAccess::eyeButton (dialog).getX(), "Search/eye edges do not align with list");
            for (auto* child : dialog.getChildren()) if (auto* label = dynamic_cast<juce::Label*> (child))
                check (! label->getText().contains ("Audition:") && ! label->getText().contains (" / Top "), "Removed status/counter label remains visible");
        }; checkAlignment();
        dialog.setSize (1024, 768); snapshot (dialog, "statistics-samples-normal.png");
        checkAlignment();
        const auto originalMode = RememberUI::appearanceMode;
        RememberUI::setAppearanceMode (RememberAppearance::Light); snapshot (dialog, "statistics-samples-light-normal.png");
        RememberUI::setAppearanceMode (originalMode);
        dialog.setCategory (ProjectStatistics::Category::Instruments); snapshot (dialog, "statistics-instruments-normal.png");
        dialog.setCategory (ProjectStatistics::Category::Effects); dialog.setSize (640, 480); snapshot (dialog, "statistics-effects-minimum.png");
        check (StatisticsTestAccess::rows (dialog) == 6, "Effects tab ranking");
        ProjectStatistics::Report large; large.read = 500;
        for (int i = 0; i < 1100; ++i)
        { ProjectStatistics::Item item; item.name = "A very long sample name with accented notes and evolving textures " + juce::String (i); item.path = folder.getChildFile ("Samples/" + item.name + ".wav").getFullPathName(); item.identity = ProjectStatistics::pathIdentity (item.path); item.projects = 100; large.items[0].push_back (item); }
        for (size_t c = 1; c < 3; ++c) for (int i = 0; i < 65; ++i)
        { ProjectStatistics::Item item; item.name = "Plugin " + juce::String (i); item.identity = "external:" + item.name; item.projects = 50; large.items[c].push_back (item); }
        StatisticsTestAccess::data (dialog, large); dialog.setCategory (ProjectStatistics::Category::Samples);
        check (StatisticsTestAccess::rows (dialog) == 1000 && StatisticsTestAccess::list (dialog).getNumChildComponents() < 40, "Statistics list is not recycled");
        snapshot (dialog, "statistics-long-missing-minimum.png");
        StatisticsTestAccess::search (dialog, "textures 1099"); check (StatisticsTestAccess::rows (dialog) == 0, "Search must stay inside visible top"); StatisticsTestAccess::search (dialog, "");
        for (size_t c = 0; c < 3; ++c)
        {
            const auto type = (ProjectStatistics::Category) c; dialog.setCategory (type);
            StatisticsTestAccess::hide (dialog, type, large.items[c][0].identity, true);
            check (StatisticsTestAccess::rows (dialog) == (c == 0 ? 1000 : 50)
                && StatisticsTestAccess::visibleIdentity (dialog, 0) == large.items[c][1].identity
                && StatisticsTestAccess::visibleIdentity (dialog, c == 0 ? 999 : 49) == large.items[c][c == 0 ? 1000 : 50].identity, "Hiding must refill the top in every category");
            StatisticsTestAccess::eye (dialog);
            check (StatisticsTestAccess::rows (dialog) == (c == 0 ? 1000 : 50)
                && StatisticsTestAccess::visibleIdentity (dialog, 0) == large.items[c][0].identity
                && StatisticsTestAccess::visibleIdentity (dialog, c == 0 ? 999 : 49) == large.items[c][c == 0 ? 999 : 49].identity,
                "Eye must show hidden and visible entries together without exceeding the category cap");
            StatisticsTestAccess::search (dialog, c == 0 ? "textures 1099" : "Plugin 64");
            check (StatisticsTestAccess::rows (dialog) == 0, "Eye search must remain inside the capped displayed ranking");
            StatisticsTestAccess::search (dialog, ""); StatisticsTestAccess::eye (dialog);
            check (StatisticsTestAccess::rows (dialog) == (c == 0 ? 1000 : 50)
                && StatisticsTestAccess::visibleIdentity (dialog, 0) == large.items[c][1].identity,
                "Closing the eye must restore the refilled visible ranking");
            StatisticsTestAccess::hide (dialog, type, large.items[c][0].identity, false);
        }
        waitFor ([] { return ProjectScanner::pendingJobs() == 0; });
        std::cout << "Statistics metadata estimate (1100 long sample paths + 65 instruments + 65 effects retained for refill): " << large.metadataBytes() << " bytes\n";
        ProjectStatistics::Report empty; StatisticsTestAccess::data (dialog, empty); snapshot (dialog, "statistics-library-empty-minimum.png");
        ProjectStatistics::Report failed; failed.error = "Library index unavailable. Refresh the library before Statistics.";
        StatisticsTestAccess::data (dialog, failed); snapshot (dialog, "statistics-library-error-minimum.png");
    }
    {
        const juce::String identity = "external:close-persistence-fixture";
        {
            StatisticsDialog closing (nullptr, false); waitFor ([&] { return closing.hasLoaded(); });
            StatisticsTestAccess::hide (closing, ProjectStatistics::Category::Instruments, identity, true);
            StatisticsTestAccess::hide (closing, ProjectStatistics::Category::Effects, identity, true);
            StatisticsTestAccess::hide (closing, ProjectStatistics::Category::Instruments, identity, false);
        }
        waitFor ([] { return ProjectScanner::pendingJobs() == 0; });
        ProjectStatistics::HiddenEntries saved;
        check (ProjectStatistics::readHiddenEntries (saved).wasOk() && saved[1].count (identity) == 0 && saved[2].count (identity) == 1,
            "Closing Statistics dropped accepted writes or mixed instrument/effect identities");
        check (ProjectStatistics::saveHiddenEntry (ProjectStatistics::Category::Effects, identity, false).wasOk(), "Hidden fixture cleanup");
    }
    std::cout << "Statistics readers, rankings, search, independent player and copy-drag passed\n";
}
