#include "OfflineRenderingDialog.h"
#include "DawLauncher.h"
#include "../../Shared/LibraryStore.h"
#include <cmath>

namespace
{
juce::Component::SafePointer<OfflineRenderingDialog> active;
juce::String settingsVersion (const juce::File& exe)
{
    const auto name = exe.getParentDirectory().getFileName();
    if (! name.startsWith ("FL Studio ")) return {};
    const auto number = name.fromFirstOccurrenceOf ("FL Studio ", false, false);
    if (! number.containsOnly ("0123456789")) return {};
    const int version = number.getIntValue();
    return version >= 2024 && version <= 2099 ? juce::String (version - 2000)
         : version >= 20 && version < 100 ? number : juce::String{};
}
juce::String secondsText (double seconds)
{ return juce::String ((int) std::ceil (seconds / 60.0)) + " min"; }
}
struct OfflineRenderingDialog::Work final : juce::Thread, BackgroundRetirement::Job
{
    Work (std::function<void (Work&)> f) : Thread ("Remember offline render"), action (std::move (f)) {}
    ~Work() override { jassert (! isThreadRunning()); }
    void requestStop() override { signalThreadShouldExit(); }
    bool readyToRelease() const override { return ! isThreadRunning() && completion.finished(); }
    void run() override
    {
        if (! completion.capture ((uintptr_t) juce::Thread::getCurrentThreadId()))
            result = juce::Result::fail ("Cannot track offline rendering worker.");
        else try { action (*this); }
        catch (...) { result = juce::Result::fail ("Offline rendering worker failed."); }
        done.store (true);
    }
    BackgroundRetirement::ModuleReference module;
    BackgroundRetirement::NativeCompletion completion;
    std::function<void (Work&)> action;
    std::atomic<bool> done { false };
    juce::Result result = juce::Result::ok();
    OfflineRendering::RememberOptions options;
    OfflineRendering::BatchPlan plan;
    OfflineRendering::FlStudioSettings settings;
    OfflineRendering::Estimate estimate;
    OfflineRendering::RenderResult rendered;
    juce::File exe;
    bool defaultSupported = true;
};
OfflineRenderingDialog::OfflineRenderingDialog()
{
    setSize (840, 720);
    title.setFont (RememberUI::font (20, true));
    settingsText.setFont (RememberUI::font (14, true));
    for (auto* component : std::initializer_list<juce::Component*> { &title, &daw, &viewport, &summary, &status, &renderButton, &stopButton }) addAndMakeVisible (component);
    viewport.setViewedComponent (&body, false); viewport.setScrollBarsShown (true, false);
    for (auto* component : std::initializer_list<juce::Component*> { &source, &destination, &executable, &project, &folder, &target, &exeButton, &read, &recursive, &beside, &previews, &settingsText, &list }) body.addAndMakeVisible (component);
    daw.addItem ("FL Studio", 1); daw.setSelectedId (1, juce::dontSendNotification);
    daw.setTitle (RememberUI::tr ("DAW")); daw.setTextWhenNothingSelected (RememberUI::tr ("Unavailable"));
    daw.onChange = [this] { refresh(); };
    list.setModel (this); list.setRowHeight (44);
    constexpr const char* names[] { "WAV", "MP3", "OGG", "FLAC", "MID", "M4A" };
    for (size_t i = 0; i < formats.size(); ++i)
    { formats[i].setButtonText (names[i]); body.addAndMakeVisible (formats[i]); formats[i].onClick = [this] { refresh(); }; }
    formats.back().setEnabled (false); formats.back().setTooltip (RememberUI::tr ("Not supported yet"));
    formats[4].setEnabled (false); formats[4].setTooltip (RememberUI::tr ("Not supported yet"));
    project.onClick = [this] { choose (0); }; folder.onClick = [this] { choose (1); };
    exeButton.setTooltip (RememberUI::tr ("Select FL Studio executable"));
    target.onClick = [this] { choose (2); }; exeButton.onClick = [this] { choose (3); };
    read.onClick = [this] { refresh(); };
    recursive.onClick = [this] { options.includeSubfolders = recursive.getToggleState(); refresh(); };
    beside.onClick = [this] { options.saveBesideProject = beside.getToggleState(); refresh(); };
    previews.onClick = [this] { options.useRendersAsPreviews = previews.getToggleState(); refresh(); };
    renderButton.onClick = [this] { start(); }; stopButton.onClick = [this] { stop(); };
    startTimer (80); updateText(); setBusy (true);
    begin ([] (Work& job)
    {
        const auto config = LibraryStore::readJson (LibraryStore::configFile());
        job.options = OfflineRendering::optionsFromConfig (config);
        job.defaultSupported = config.getProperty ("offlineDefaultDaw", "fl-studio").toString() == "fl-studio";
        for (const auto& app : DawLauncher::discover()) if (app.name == "FL Studio") { job.exe = app.executable; break; }
    }, [this] (Work& job)
    {
        options = job.options; flExecutable = job.exe;
        daw.setSelectedId (job.defaultSupported ? 1 : 0, juce::dontSendNotification);
        beside.setToggleState (options.saveBesideProject, juce::dontSendNotification);
        recursive.setToggleState (options.includeSubfolders, juce::dontSendNotification);
        previews.setToggleState (options.useRendersAsPreviews, juce::dontSendNotification);
        for (auto& format : formats) format.setToggleState (format.getButtonText() != "M4A" && format.getButtonText() != "MID" && options.formats.contains (format.getButtonText().toLowerCase()), juce::dontSendNotification);
        refresh();
    });
}
OfflineRenderingDialog::~OfflineRenderingDialog()
{ stopTimer(); if (work) { work->requestStop(); BackgroundRetirement::retire (std::move (work)); } list.setModel (nullptr); }
void OfflineRenderingDialog::shutdown()
{ if (active) { active->stop(); if (active->work) BackgroundRetirement::retire (std::move (active->work)); active->completed = {}; } }
void OfflineRenderingDialog::begin (std::function<void (Work&)> run, std::function<void (Work&)> finish)
{
    jassert (! work); completed = std::move (finish); work = std::make_shared<Work> (std::move (run));
    if (! work->module.valid() || ! work->startThread())
    { completed = {}; work.reset(); setStatus ("Failed"); setBusy (false); }
}
void OfflineRenderingDialog::timerCallback()
{
    if (! work || ! work->done.load()) return;
    auto finished = std::move (work); auto callback = std::move (completed);
    if (finished->result.failed()) { batch = false; setStatus (finished->result.getErrorMessage()); setBusy (false); }
    else if (callback) callback (*finished);
    BackgroundRetirement::retire (std::move (finished));
    if (refreshPending && ! work && ! batch) { refreshPending = false; refresh(); }
}
void OfflineRenderingDialog::setBusy (bool busy)
{
    body.setEnabled (! busy); daw.setEnabled (! busy); renderButton.setEnabled (! busy && plan.result.wasOk() && ! plan.jobs.empty());
    stopButton.setEnabled (batch || work != nullptr);
}
void OfflineRenderingDialog::choose (int kind)
{
    constexpr const char* titles[] { "Select an FL Studio project", "Select a folder containing FL Studio projects", "Select a render destination", "Select FL Studio executable" };
    chooser = std::make_unique<juce::FileChooser> (RememberUI::tr (titles[kind]), juce::File{}, kind == 0 ? "*.flp" : kind == 3 ? "FL64.exe;FL.exe" : "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | ((kind == 1 || kind == 2) ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles),
        [safe = juce::Component::SafePointer<OfflineRenderingDialog> (this), kind] (const juce::FileChooser& chosen)
        {
            const auto file = chosen.getResult(); if (! safe || file == juce::File{}) return;
            if (kind == 3) safe->flExecutable = file;
            else if (kind == 2) safe->options.customDestination = file.getFullPathName();
            else { safe->options.sourcePath = file.getFullPathName(); safe->options.sourceIsFolder = kind == 1; }
            safe->refresh();
        });
}
void OfflineRenderingDialog::refresh()
{
    if (work) { refreshPending = true; return; }
    if (batch) return;
    if (daw.getSelectedId() != 1) { plan.jobs.clear(); list.updateContent(); setStatus ("Select FL Studio to continue."); updateText(); setBusy (false); return; }
    juce::StringArray selected; for (auto& format : formats) if (format.getToggleState() && format.getButtonText() != "M4A" && format.getButtonText() != "MID") selected.add (format.getButtonText().toLowerCase());
    options.formats = selected;
    setBusy (true); setStatus ("Reading projects and settings...");
    begin ([opts = options, exe = flExecutable, selected] (Work& job)
    {
        const auto cancel = [&job] { return job.threadShouldExit(); };
        job.result = OfflineRendering::saveOptions (opts); if (job.result.failed()) return;
        job.settings = OfflineRendering::readFlStudioSettings (settingsVersion (exe));
        auto sources = OfflineRendering::collectProjects (juce::File::isAbsolutePath (opts.sourcePath) ? juce::File (opts.sourcePath) : juce::File{}, opts.includeSubfolders, cancel);
        job.plan = OfflineRendering::planBatch (exe, sources, opts, selected, cancel);
        job.estimate = OfflineRendering::estimateBatch (job.plan, job.settings, LibraryStore::readJson (LibraryStore::configFile()), cancel);
    }, [this] (Work& job)
    {
        flSettings = job.settings; plan = std::move (job.plan); estimate = job.estimate;
        rowStates.clear(); for (size_t i = 0; i < plan.jobs.size(); ++i) rowStates.add ("Pending");
        list.updateContent(); updateText(); setBusy (false);
        setStatus (plan.result.failed() ? plan.result.getErrorMessage() : juce::String{});
    });
}
void OfflineRenderingDialog::start()
{
    if (work || batch || plan.result.failed() || plan.jobs.empty()) return;
    auto go = [safe = juce::Component::SafePointer<OfflineRenderingDialog> (this)] (bool use)
    { if (safe) { safe->batch = true; safe->batchPreviews = use; safe->current = 0; safe->setBusy (true); safe->next(); } };
    if (! options.useRendersAsPreviews) { go (false); return; }
    setBusy (true);
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, RememberUI::tr ("Use full renders for this batch?"),
        RememberUI::tr ("Assign the highest-fidelity readable render to every project in this batch. Manual preview choices remain protected."),
        RememberUI::tr ("Use renders"), RememberUI::tr ("Render only"), RememberUI::tr ("Cancel batch"), this,
        juce::ModalCallbackFunction::create ([go, safe = juce::Component::SafePointer<OfflineRenderingDialog> (this)] (int answer)
        { if (! safe) return; if (answer != 0) go (answer == 1); else safe->setBusy (false); }));
}
void OfflineRenderingDialog::next()
{
    if (! batch || current >= (int) plan.jobs.size()) { batch = false; setBusy (false); return; }
    const auto previous = plan.jobs[(size_t) current];
    const auto safeThis = juce::Component::SafePointer<OfflineRenderingDialog> (this);
    begin ([previous] (Work& job)
    {
        job.result = OfflineRendering::requireFlStudioClosed(); if (job.result.failed()) return;
        job.plan.jobs.push_back (OfflineRendering::flStudioCommand (previous.executable, previous.project, previous.destination, previous.formats));
    }, [this, safeThis] (Work& job)
    {
        if (! batch) { setBusy (false); return; }
        const auto command = job.plan.jobs.front();
        if (command.result.failed()) { batch = false; setStatus (command.result.getErrorMessage()); setBusy (false); return; }
        if (command.existingOutputs.isEmpty()) { execute (command, false); return; }
        juce::String names; for (const auto& file : command.existingOutputs) names += "\n" + file.getFullPathName();
        juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, RememberUI::tr ("Overwrite existing audio?"), RememberUI::tr ("The following output files already exist:") + names,
            RememberUI::tr ("Overwrite"), RememberUI::tr ("Skip project"), RememberUI::tr ("Cancel batch"), this,
            juce::ModalCallbackFunction::create ([safe = safeThis, command] (int answer)
            {
                if (! safe || ! safe->batch) return;
                if (answer == 1) safe->execute (command, true);
                else if (answer == 2) { safe->rowStates.set (safe->current++, "Skipped"); safe->list.repaint(); safe->next(); }
                else safe->stop();
            }));
    });
}
void OfflineRenderingDialog::execute (OfflineRendering::Command command, bool approved)
{
    if (! batch) return;
    rowStates.set (current, "Rendering"); list.repaint();
    begin ([command, approved, use = batchPreviews] (Work& job)
    {
        job.rendered = OfflineRendering::render (command, approved, [&job] { return job.threadShouldExit(); });
        if (job.rendered.result.wasOk()) job.result = OfflineRendering::recordCompletedRender (command, job.rendered, OfflineRendering::readProjectTiming (command.project).seconds, use);
    }, [this] (Work& job)
    {
        rowStates.set (current++, job.rendered.result.wasOk() ? "Completed" : "Failed"); list.repaint();
        if (job.rendered.result.failed()) { setStatus (job.rendered.result.getErrorMessage()); batch = false; setBusy (false); }
        else { ProjectScanner::refreshAsync(); next(); }
    });
}
void OfflineRenderingDialog::stop()
{ batch = false; refreshPending = false; if (work) work->requestStop(); else setBusy (false); }
void OfflineRenderingDialog::setStatus (const juce::String& text)
{ statusSource = text; status.setText (RememberUI::diagnostic (text), juce::dontSendNotification); }
void OfflineRenderingDialog::updateText()
{
    setStatus (statusSource);
    title.setText (RememberUI::tr ("Offline rendering"), juce::dontSendNotification);
    source.setText (options.sourcePath, juce::dontSendNotification); source.setTooltip (options.sourcePath);
    destination.setText (options.customDestination, juce::dontSendNotification); destination.setTooltip (options.customDestination);
    executable.setText (flExecutable.getFullPathName(), juce::dontSendNotification);
    target.setEnabled (! options.saveBesideProject); recursive.setEnabled (options.sourceIsFolder);
    settingsText.setText (RememberUI::tr ("Saved FL settings (read only)."), juce::dontSendNotification);
    settingsText.setTooltip (RememberUI::tr ("Changing these FL settings from Remember has not been enabled."));
    settingLabels.clear();
    const auto unknown = RememberUI::localize ("Unknown");
    struct Field { const char* key; const char* caption; const char* choices; };
    const Field fields[] {
        { "RenderMode", "Mode", "" },
        { "LMBox", "Tail", "Cut remainder|Leave remainder|Wrap remainder" },
        { "SampleRate", "Sample rate", "" },
        { "WFBox", "WAV bit depth", "16 bit|24 bit|32 bit float" },
        { "ChannelsBox", "Channels", "Stereo|Mono (merged)|Mono (left only)|Mono (right only)" },
        { "MP3BitrateSlider", "MP3 bitrate", "bitrate" }, { "OggBitrateSlider", "OGG bitrate", "bitrate" },
        { "FlacBitDepthBox", "FLAC bit depth", "16 bit|24 bit" }, { "FlacCompressionSlider", "FLAC compression", "number" },
        { "IPBox", "Resampling", "Linear|6-point Hermite|64-point sinc|128-point sinc|256-point sinc|512-point sinc" },
        { "PlugHQBox", "HQ for all plugins", "bool" }, { "NoPolyBox", "Disable maximum polyphony", "bool" },
        { "DitherBox", "Dithering", "bool" }, { "PlaylistMarkersBox", "Save playlist markers", "bool" },
        { "LoopMarkersBox", "Save loop markers", "bool" }, { "NoteMarkersBox", "Save note markers", "bool" },
        { "AcidBox", "Save tempo information", "bool" }, { "BatchBox", "Split mixer tracks", "bool" },
        { "SplitMasterBox", "Include Master", "bool" }, { "SplitCurrentBox", "Include Current", "bool" },
        { "LCBox", "Trim PDC silence", "bool" }, { "InputFXBox", "Enable insert effects", "bool" }, { "MasterFXBox", "Enable master effects", "bool" }
    };
    for (const auto& field : fields)
    {
        auto value = unknown;
        if (flSettings.values.contains (field.key))
        {
            const auto number = (int) flSettings.values[field.key];
            const juce::String kind (field.choices);
            if (kind == "bool" && (number == 0 || number == 1)) value = RememberUI::localize (number ? "Setting enabled" : "Setting disabled");
            else if (kind == "number") value = juce::String (number);
            else if (juce::String (field.key) == "SampleRate") value = juce::String (number) + " Hz";
            else if (kind == "bitrate") { if (number == 13) value = "320 kbps"; }
            else if (juce::String (field.key) == "IPBox")
            {
                // Only mappings established by the saved-settings probe.
                if (number == 8) value = "512-point sinc";
                else if (number == 0) value = RememberUI::localize ("Linear");
            }
            else
            {
                const auto choices = juce::StringArray::fromTokens (kind, "|", "");
                if (number >= 0 && number < choices.size()) value = RememberUI::localize (choices[number]);
            }
        }
        auto label = std::make_unique<RememberUI::Label>();
        label->setText (RememberUI::localize (field.caption) + "\n" + value, juce::dontSendNotification);
        label->setTooltip (RememberUI::localize (field.caption) + ": " + value);
        body.addAndMakeVisible (*label); settingLabels.push_back (std::move (label));
    }
    summary.setText (RememberUI::tr (estimate.historicalProjects > 0 ? "Estimated time (includes render history): " : "Time reference (1× audio, no history): ") + secondsText (estimate.timeSeconds)
        + "\n" + RememberUI::tr ("Estimated audio size: ") + juce::File::descriptionOfSizeInBytes (estimate.bytes)
        + (estimate.unknownSize || estimate.unknownProjects ? " + ?" : ""), juce::dontSendNotification);
    summary.setTooltip (RememberUI::tr ("Some durations or sizes are unknown. Estimates exclude variable tempo, plugin loading and effects tails."));
    if (plan.jobs.empty()) summary.setText (RememberUI::tr ("Select a source and at least one output format."), juce::dontSendNotification);
    list.repaint(); resized(); repaint();
}
void OfflineRenderingDialog::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= (int) plan.jobs.size()) return;
    g.setFont (RememberUI::font (13)); g.setColour (RememberUI::colour (Pal::Text));
    g.drawText (plan.jobs[(size_t) row].project.getFileNameWithoutExtension(), 10, 0, width - 170, height, juce::Justification::centredLeft, true);
    g.setColour (RememberUI::colour (Pal::Subtext)); g.drawText (RememberUI::tr (rowStates[row]), width - 150, 0, 140, height, juce::Justification::centredRight);
}
void OfflineRenderingDialog::paint (juce::Graphics& g) { g.fillAll (RememberUI::colour (Pal::BgDeep)); }
void OfflineRenderingDialog::resized()
{
    auto bounds = getLocalBounds().reduced (20);
    auto top = bounds.removeFromTop (34); daw.setBounds (top.removeFromRight (190)); title.setBounds (top);
    bounds.removeFromTop (12);
    auto footer = bounds.removeFromBottom (34); stopButton.setBounds (footer.removeFromRight (180)); footer.removeFromRight (8); renderButton.setBounds (footer.removeFromRight (180));
    bounds.removeFromBottom (8); status.setBounds (bounds.removeFromBottom (36)); summary.setBounds (bounds.removeFromBottom (36));
    viewport.setBounds (bounds); body.setSize (juce::jmax (0, viewport.getMaximumVisibleWidth()), 1280);
    auto area = body.getLocalBounds().withTrimmedRight (4);
    auto row = area.removeFromTop (32); exeButton.setBounds (row.removeFromRight (190)); executable.setBounds (row); area.removeFromTop (8);
    row = area.removeFromTop (32); project.setBounds (row.removeFromLeft (190)); row.removeFromLeft (8); folder.setBounds (row.removeFromLeft (190));
    source.setBounds (area.removeFromTop (32)); recursive.setBounds (area.removeFromTop (32)); beside.setBounds (area.removeFromTop (32));
    row = area.removeFromTop (32); target.setBounds (row.removeFromLeft (190)); row.removeFromLeft (8); destination.setBounds (row);
    previews.setBounds (area.removeFromTop (36)); area.removeFromTop (12);
    row = area.removeFromTop (32); for (auto& format : formats) format.setBounds (row.removeFromLeft (90));
    area.removeFromTop (8); read.setBounds (area.removeFromTop (32).removeFromLeft (250));
    settingsText.setBounds (area.removeFromTop (40));
    for (size_t i = 0; i < settingLabels.size(); i += 2)
    {
        row = area.removeFromTop (54); const auto half = row.getWidth() / 2;
        settingLabels[i]->setBounds (row.removeFromLeft (half));
        if (i + 1 < settingLabels.size()) settingLabels[i + 1]->setBounds (row);
    }
    area.removeFromTop (12); list.setBounds (area);
}
void OfflineRenderingDialog::launch()
{
    if (active) { if (auto* window = active->findParentComponentOfClass<juce::DialogWindow>()) window->toFront (true); return; }
    juce::DialogWindow::LaunchOptions options; auto content = std::make_unique<OfflineRenderingDialog>(); active = content.get();
    auto* zoom = new RememberUI::ZoomView (std::move (content)); zoom->setSize (840, 720); options.content.setOwned (zoom);
    options.dialogTitle = RememberUI::tr ("Offline rendering"); options.useNativeTitleBar = true; options.escapeKeyTriggersCloseButton = true;
    options.dialogBackgroundColour = RememberUI::colour (Pal::BgDeep); options.resizable = true;
    if (auto* window = RememberUI::launchDialog (options)) window->setResizeLimits (640, 480, 1600, 1200);
}
