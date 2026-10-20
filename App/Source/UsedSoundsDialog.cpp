#include "UsedSoundsDialog.h"
#include "../../Shared/LibraryStore.h"

struct UsedSoundsDialog::Workspace
{
    juce::File root { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Remember-used-sounds-" + juce::Uuid().toString()) };
    ~Workspace() { if (root.getFileName().startsWith ("Remember-used-sounds-")) root.deleteRecursively(); }
};

class UsedSoundsDialog::Row final : public juce::Component
{
public:
    class DragButton final : public juce::TextButton
    {
    public:
        DragButton() : juce::TextButton ("Drag") { setTitle ("Drag file to a folder"); setTooltip ("Drag a copy to Explorer or another application"); }
        std::function<void()> begin;
        void mouseDown (const juce::MouseEvent& e) override { started = false; juce::TextButton::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDrag (e);
            if (! started && e.mods.isLeftButtonDown() && e.getDistanceFromDragStart() >= 6 && isEnabled())
            { started = true; if (begin) begin(); }
        }
    private:
        bool started = false;
    } drag;
    explicit Row (UsedSoundsDialog& owner) : dialog (owner)
    {
        RememberUI::iconButton (play, "play", "Audition sound");
        for (auto* button : std::initializer_list<juce::TextButton*> { &play, &save, &locateButton, &drag })
        {
            RememberUI::bindColour (*button, juce::TextButton::buttonColourId, Pal::Elevated);
            RememberUI::bindColour (*button, juce::TextButton::textColourOffId, Pal::Text);
            addAndMakeVisible (*button);
        }
        save.setTitle ("Save a copy"); locateButton.setTitle ("Locate original sample");
    }
    void bind (size_t value, bool selected)
    {
        index = value; isSelected = selected;
        const auto& sound = dialog.data->sounds[index];
        setTitle (sound.name); setDescription (sound.channelName + ", " + sound.instrument);
        const auto file = dialog.exports[index];
        const bool missing = sound.sourcePath.isNotEmpty() && sound.sample == juce::File{};
        midiRow = sound.kind == UsedSounds::Kind::Midi;
        generatorRow = sound.kind == UsedSounds::Kind::Generator;
        if (generatorRow) setDescription (sound.channelName + ", " + sound.instrument + ", " + presetDetail()
            + ", " + (sound.inPlaylist ? "In playlist" : "Loaded in project") + ". Last saved project; parameters may have changed after loading the preset.");
        missingSample = missing;
        play.setVisible (sound.kind != UsedSounds::Kind::Generator);
        play.setEnabled (! missing && (bool) dialog.onAudition);
        locateButton.setVisible (missing);
        save.setVisible (true);
        drag.setVisible (true);
        drag.setEnabled (file != juce::File{}); save.setEnabled (file != juce::File{});
        const auto exportHint = file != juce::File{} ? juce::String ("Drag a copy to Explorer or another application")
            : sound.exportError.isNotEmpty() ? sound.exportError : "Original sample is unavailable";
        drag.setTooltip (exportHint); save.setTooltip (file != juce::File{} ? "Save a copy" : exportHint);
        play.onClick = [this] { dialog.audition (index); };
        save.onClick = [this] { dialog.exportFile (index, false, this); };
        drag.begin = [this] { dialog.exportFile (index, true, &drag); };
        locateButton.onClick = [this] { dialog.locate (index); };
        resized(); repaint();
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        dragged = false;
        for (size_t row = 0; row < dialog.visible.size(); ++row)
            if (dialog.visible[row] == index) { dialog.list.selectRow ((int) row); break; }
    }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! dragged && event.mods.isLeftButtonDown() && event.getDistanceFromDragStart() >= 6)
        { dragged = true; dialog.exportFile (index, true, this); }
    }
    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! dragged && event.mods.isLeftButtonDown() && getLocalBounds().contains (event.getPosition()) && play.isEnabled())
            dialog.audition (index);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& sound = dialog.data->sounds[index];
        g.setColour (RememberUI::colour (isSelected ? Pal::Selected : Pal::Content));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0, 2), RememberUI::controlRadius);
        const auto textRight = getWidth() - actionWidth() - 20;
        g.setFont (RememberUI::font (14, true)); g.setColour (RememberUI::colour (Pal::Text));
        g.drawText (sound.name, generatorRow ? 12 : 52, generatorRow ? 6 : 10, juce::jmax (0, textRight - (generatorRow ? 12 : 52)), 21, juce::Justification::centredLeft, true);
        g.setFont (RememberUI::font (12)); g.setColour (RememberUI::colour (Pal::Subtext));
        const auto detail = sound.kind == UsedSounds::Kind::Sample ? sound.channelName + " / " + (sound.sample == juce::File{} ? "Sample missing" : sound.sample.getFileExtension().substring (1).toUpperCase())
            : sound.kind == UsedSounds::Kind::Generator ? sound.channelName + " / " + (sound.inPlaylist ? "In playlist" : "Loaded in project")
            : (sound.sourcePath.isNotEmpty() ? "Sample audition" : "Default instrument") + juce::String (" / ") + juce::String ((int) sound.notes.size()) + " notes";
        if (generatorRow)
        {
            g.setColour (RememberUI::colour (Pal::Text));
            g.drawText (presetDetail(), 12, 27, juce::jmax (0, textRight - 12), 19, juce::Justification::centredLeft, true);
            g.setColour (RememberUI::colour (Pal::Subtext));
        }
        g.drawText (detail, generatorRow ? 12 : 52, generatorRow ? 47 : 32, juce::jmax (0, textRight - (generatorRow ? 12 : 52)), 19, juce::Justification::centredLeft, true);
        if (sound.kind == UsedSounds::Kind::Midi)
        {
            auto bounds = juce::Rectangle<float> (52, 58, (float) juce::jmax (1, textRight - 60), 46);
            g.setColour (RememberUI::colour (Pal::Elevated)); g.fillRoundedRectangle (bounds, RememberUI::smallRadius);
            int low = 127, high = 0; double end = 4;
            for (const auto& note : sound.notes) { low = juce::jmin (low, note.key); high = juce::jmax (high, note.key); end = juce::jmax (end, note.beat + note.length); }
            for (int i = 1; i < 8; ++i)
            { g.setColour (RememberUI::colour (Pal::Border)); g.drawVerticalLine ((int) (bounds.getX() + bounds.getWidth() * i / 8), bounds.getY(), bounds.getBottom()); }
            bounds = bounds.reduced (4);
            const int range = juce::jmax (12, high - low + 3);
            g.setColour (RememberUI::colour (Pal::Accent));
            // Bounded drawing cost even for very dense patterns; exports retain every note.
            const auto stride = juce::jmax<size_t> (1, sound.notes.size() / 2048);
            for (size_t i = 0; i < sound.notes.size(); i += stride)
            {
                const auto& note = sound.notes[i];
                g.fillRoundedRectangle (bounds.getX() + (float) (note.beat / end) * bounds.getWidth(),
                    bounds.getBottom() - (float) (note.key - low + 2) / range * bounds.getHeight(),
                    juce::jmax (2.0f, (float) (note.length / end) * bounds.getWidth()), juce::jmax (2.0f, bounds.getHeight() / range), 1.0f);
            }
        }
    }
    void resized() override
    {
        const int y = midiRow ? 14 : (getHeight() - RememberUI::recordingRowActionSize) / 2;
        play.setBounds (10, y, RememberUI::recordingRowActionSize, RememberUI::recordingRowActionSize);
        auto controls = juce::Rectangle<int> (getWidth() - actionWidth() - 10, y, actionWidth(), RememberUI::recordingRowActionSize);
        drag.setBounds (controls.removeFromLeft (60)); controls.removeFromLeft (8);
        save.setBounds (controls.removeFromLeft (100)); controls.removeFromLeft (8);
        locateButton.setBounds (controls.removeFromLeft (72));
    }
private:
    juce::String presetDetail() const
    {
        const auto& preset = dialog.data->sounds[index].presetName;
        return preset.isNotEmpty() ? "Preset: " + preset : juce::String ("Preset not available");
    }
    UsedSoundsDialog& dialog;
    size_t index = 0; bool isSelected = false, dragged = false;
    bool midiRow = false, generatorRow = false, missingSample = false;
    int actionWidth() const { return 168 + (missingSample ? 80 : 0); }
    juce::TextButton play, save { "Save copy..." }, locateButton { "Locate..." };
};

UsedSoundsDialog::UsedSoundsDialog (ProjectEntry source, Audition auditionCallback, std::function<void()> stopCallback, std::function<void()> closeCallback, WaveformPlayerComponent* player, bool persistPlaybackSettings)
    : entry (std::move (source)), onAudition (std::move (auditionCallback)), onStop (std::move (stopCallback)), onClose (std::move (closeCallback)), workspace (std::make_shared<Workspace>())
{
    setSize (840, 620);
    title.setText ("Used sounds", juce::dontSendNotification); title.setFont (RememberUI::font (22, true));
    projectLabel.setText (entry.name + " / Saved FL Studio project", juce::dontSendNotification);
    projectLabel.setTooltip (entry.projectPath);
    hint.setText ("MIDI exports contain notes. Audio exports copy the original sample.", juce::dontSendNotification);
    for (auto* label : { &title, &projectLabel, &status, &hint })
    {
        RememberUI::bindColour (*label, juce::Label::textColourId, label == &title ? Pal::Text : Pal::Subtext);
        if (label != &title) label->setFont (RememberUI::font (12));
        label->setMinimumHorizontalScale (1.0f); addAndMakeVisible (*label);
    }
    status.setJustificationType (juce::Justification::topLeft);
    for (auto* button : { &samples, &midis, &plugins, &refresh, &stop, &close })
    {
        RememberUI::bindColour (*button, juce::TextButton::buttonColourId, Pal::Elevated);
        RememberUI::bindColour (*button, juce::TextButton::buttonOnColourId, Pal::Action);
        RememberUI::bindColour (*button, juce::TextButton::textColourOffId, Pal::Text);
        RememberUI::bindColour (*button, juce::TextButton::textColourOnId, Pal::OnAction);
        button->setTitle (button->getButtonText()); addAndMakeVisible (*button);
    }
    samples.onClick = [this] { setCategory (UsedSounds::Kind::Sample); };
    midis.onClick = [this] { setCategory (UsedSounds::Kind::Midi); };
    plugins.onClick = [this] { setCategory (UsedSounds::Kind::Generator); };
    refresh.onClick = [this] { load(); };
    stop.onClick = [this] { stopAudition(); setStatus ("Stopped"); };
    stop.setEnabled ((bool) onStop);
    close.onClick = [this] { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (0); };
    scope.addItem ("In playlist", 1); scope.addItem ("All loaded sounds", 2);
    scope.setTitle ("Sound usage"); scope.setSelectedId (1, juce::dontSendNotification);
    scope.onChange = [this] { filter(); }; addAndMakeVisible (scope);
    list.setModel (this); list.setRowHeight (RememberUI::soundRowHeight); list.setTitle ("Project sounds");
    RememberUI::bindColour (list, juce::ListBox::backgroundColourId, Pal::Content); addAndMakeVisible (list);
    if (player != nullptr)
    {
        projectPlayer = player;
        auditionPlayer = std::make_unique<WaveformPlayerComponent> (nullptr, persistPlaybackSettings);
        auditionPlayer->setCompactMode (true); auditionPlayer->setQueue ({}); addAndMakeVisible (*auditionPlayer);
        auto observeAudition = std::move (onAudition);
        onAudition = [this, observeAudition] (const ProjectEntry& sound)
        {
            if (projectPlayer != nullptr) projectPlayer->suspendOutput();
            auditionPlayer->loadEntry (sound);
            if (autoPlayAudition) auditionPlayer->play();
            if (observeAudition) observeAudition (sound);
        };
        auto observeStop = std::move (onStop);
        onStop = [this, observeStop]
        {
            auditionPlayer->clearPreview();
            if (observeStop) observeStop();
        };
        stop.setEnabled (true);
    }
    setCategory (category); load(); resized();
}
UsedSoundsDialog::~UsedSoundsDialog()
{
    generation->fetch_add (1); stopAudition();
    list.setModel (nullptr);
    if (onClose) onClose();
}
void UsedSoundsDialog::setStatus (const juce::String& message)
{ status.setText (message, juce::dontSendNotification); status.setTooltip (message); }
void UsedSoundsDialog::load()
{
    loaded = false; refresh.setEnabled (false); scope.setEnabled (false);
    visible.clear(); list.updateContent(); list.setVisible (false); setStatus ("Reading saved project...");
    const auto request = generation->fetch_add (1) + 1; const auto token = generation;
    stopAudition();
    auto next = std::make_shared<UsedSounds::Inventory>();
    auto files = std::make_shared<std::vector<juce::File>>();
    const auto lease = workspace;
    ProjectScanner::queryAsync ([source = entry, token, request, next, files, lease]
    {
        const auto cancel = [token, request] { return token->load() != request; };
        *next = UsedSounds::readProject (juce::File (source.projectPath), cancel);
        if (next->result.failed()) return next->result;
        const auto staging = lease->root.getChildFile (juce::Uuid().toString().substring (0, 12));
        const auto directoryResult = staging.createDirectory();
        const auto bpm = source.bpmOverride > 0 ? source.bpmOverride : next->bpm > 0 ? next->bpm : source.bpm;
        const auto key = source.keyOverride.isNotEmpty() ? source.keyOverride : source.key;
        for (auto& sound : next->sounds)
        {
            if (cancel()) return juce::Result::fail ("Cancelled");
            juce::File file;
            if (sound.kind == UsedSounds::Kind::Sample) file = sound.sample;
            else if (sound.kind == UsedSounds::Kind::Midi || sound.kind == UsedSounds::Kind::Generator)
            {
                file = staging.getChildFile (UsedSounds::exportName (source.name, sound, bpm, key));
                const auto result = directoryResult.failed() ? directoryResult
                    : sound.kind == UsedSounds::Kind::Generator ? UsedSounds::writePreset (sound, file) : UsedSounds::writeMidi (sound, bpm, file);
                sound.presetData.reset(); // The staging file owns the exported state now.
                if (result.failed())
                {
                    file = {}; sound.exportError = result.getErrorMessage();
                    next->warnings.addIfNotAlreadyThere ((sound.kind == UsedSounds::Kind::Generator
                        ? "Some preset exports are unavailable: " : "Some MIDI exports are unavailable: ") + sound.exportError);
                }
            }
            files->push_back (file);
        }
        return juce::Result::ok();
    }, [safe = juce::Component::SafePointer<UsedSoundsDialog> (this), token, request, next, files] (juce::Result result)
    {
        if (! safe || token->load() != request) return;
        safe->loaded = true; safe->refresh.setEnabled (true);
        if (result.failed())
        { next->result = result; next->sounds.clear(); files->clear(); }
        safe->data = next; safe->exports = std::move (*files);
        safe->scope.setEnabled (result.wasOk() && next->playlistKnown);
        if (! next->playlistKnown) safe->scope.setSelectedId (2, juce::dontSendNotification);
        safe->filter();
    });
}
void UsedSoundsDialog::setCategory (UsedSounds::Kind kind)
{
    category = kind;
    list.setRowHeight (kind == UsedSounds::Kind::Midi ? RememberUI::midiSoundRowHeight
        : kind == UsedSounds::Kind::Generator ? RememberUI::generatorSoundRowHeight : RememberUI::soundRowHeight);
    samples.setToggleState (kind == UsedSounds::Kind::Sample, juce::dontSendNotification);
    midis.setToggleState (kind == UsedSounds::Kind::Midi, juce::dontSendNotification);
    plugins.setToggleState (kind == UsedSounds::Kind::Generator, juce::dontSendNotification);
    hint.setText (kind == UsedSounds::Kind::Generator
        ? "Drag copies the saved plugin state as an FL Studio preset (.fst)."
        : "MIDI exports contain notes. Audio exports copy the original sample.", juce::dontSendNotification);
    filter();
}
void UsedSoundsDialog::filter()
{
    visible.clear(); int sampleCount = 0, midiCount = 0, generatorCount = 0;
    if (loaded && data->result.wasOk()) for (size_t i = 0; i < data->sounds.size(); ++i)
    {
        const auto& sound = data->sounds[i];
        if (scope.getSelectedId() == 1 && data->playlistKnown && ! sound.inPlaylist) continue;
        if (sound.kind == UsedSounds::Kind::Sample) ++sampleCount;
        else if (sound.kind == UsedSounds::Kind::Midi) ++midiCount;
        else ++generatorCount;
        if (sound.kind == category) visible.push_back (i);
    }
    const bool available = loaded && data->result.wasOk();
    samples.setButtonText ("Samples (" + (available ? juce::String (sampleCount) : "--") + ")");
    midis.setButtonText ("MIDI (" + (available ? juce::String (midiCount) : "--") + ")");
    plugins.setButtonText ("Generators (" + (available ? juce::String (generatorCount) : "--") + ")");
    list.deselectAllRows(); list.updateContent(); list.repaint();
    list.setVisible (available && ! visible.empty());
    if (loaded) setStatus (data->result.failed() ? data->result.getErrorMessage() : data->warnings.joinIntoString ("\n"));
    repaint();
}
juce::Component* UsedSoundsDialog::refreshComponentForRow (int row, bool selected, juce::Component* existing)
{
    auto* component = dynamic_cast<Row*> (existing);
    if (! component) component = new Row (*this);
    const bool valid = row >= 0 && row < (int) visible.size(); component->setVisible (valid);
    if (valid) component->bind (visible[(size_t) row], selected);
    return component;
}
void UsedSoundsDialog::returnKeyPressed (int row)
{ if (row >= 0 && row < (int) visible.size()) audition (visible[(size_t) row]); }
void UsedSoundsDialog::audition (size_t index)
{
    if (index >= data->sounds.size() || ! onAudition) return;
    const auto& selected = data->sounds[index];
    if (selected.kind == UsedSounds::Kind::Generator) return;
    // Retain only the immutable note vector, not a copy of every queued pattern.
    const std::shared_ptr<const std::vector<UsedSounds::Note>> notes (data, &selected.notes);
    UsedSounds::Sound sound; sound.kind = selected.kind; sound.name = selected.name;
    sound.sample = selected.sample; sound.sourcePath = selected.sourcePath; sound.rootNote = selected.rootNote;
    stopAudition(); const auto token = playGeneration; const auto request = token->load();
    setStatus ("Preparing " + sound.name + "...");
    auto factory = std::make_shared<std::function<std::unique_ptr<juce::AudioFormatReader>()>>();
    const auto bpm = entry.bpmOverride > 0 ? entry.bpmOverride : data->bpm > 0 ? data->bpm : entry.bpm;
    ProjectScanner::queryAsync ([sound, notes, factory, token, request, bpm]
    {
        const auto cancelled = [token, request] { return token->load() != request; };
        if (cancelled()) return juce::Result::fail ("Cancelled");
        if (sound.kind == UsedSounds::Kind::Sample) return sound.sample.existsAsFile() ? juce::Result::ok() : juce::Result::fail ("Sample is missing. Locate its original file.");
        if (notes->empty() || ! std::isfinite (bpm) || bpm < 10 || bpm > 1000)
            return juce::Result::fail ("No playable notes or project tempo.");
        if (sound.sourcePath.isNotEmpty() && ! sound.sample.existsAsFile())
            return juce::Result::fail ("Locate the original sample to audition this MIDI.");
        const auto settings = ProjectScanner::readSettings(); if (settings.result.failed()) return settings.result;
        *factory = [sound, notes, bpm, voice = UsedSounds::voiceFromId (settings.midiPreviewVoice)]
        { return UsedSounds::createPreviewReader (sound, notes, bpm, voice); };
        return juce::Result::ok();
    }, [safe = juce::Component::SafePointer<UsedSoundsDialog> (this), token, request, factory, sound, notes, bpm] (juce::Result result)
    {
        if (! safe || token->load() != request) return;
        if (result.failed()) { safe->setStatus (result.getErrorMessage()); return; }
        auto preview = safe->entry; preview.previewOgg = sound.kind == UsedSounds::Kind::Midi ? juce::File{} : sound.sample; preview.name = sound.name;
        if (sound.kind == UsedSounds::Kind::Midi)
        { preview.auditionReader = *factory; preview.auditionNotes = notes; }
        preview.bpm = bpm; preview.bpmOverride = bpm;
        preview.storageError.clear(); preview.previewLocked = false;
        safe->setStatus ("Playing " + sound.name + (sound.kind == UsedSounds::Kind::Midi ? " / Basic audition, up to 30 seconds" : ""));
        if (safe->onAudition) safe->onAudition (preview);
    });
}
void UsedSoundsDialog::stopAudition()
{
    playGeneration->fetch_add (1);
    if (onStop) onStop();
}
void UsedSoundsDialog::locate (size_t index)
{
    if (index >= data->sounds.size()) return;
    const auto path = data->sounds[index].sourcePath;
    chooser = std::make_unique<juce::FileChooser> ("Locate original sample", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<UsedSoundsDialog> (this), path, token = generation, request = generation->load()] (const juce::FileChooser& selected)
        {
            const auto file = selected.getResult();
            if (! safe || token->load() != request || file == juce::File{}) return;
            for (size_t i = 0; i < safe->data->sounds.size(); ++i)
                if (safe->data->sounds[i].sourcePath == path)
                {
                    safe->data->sounds[i].sample = file;
                    if (safe->data->sounds[i].kind == UsedSounds::Kind::Sample) safe->exports[i] = file;
                }
            safe->list.updateContent(); safe->list.repaint(); safe->setStatus ("Sample located for this session. The FL Studio project was not changed.");
        });
}
void UsedSoundsDialog::exportFile (size_t index, bool drag, juce::Component* source)
{
    if (index >= exports.size() || exports[index] == juce::File{}) return;
    const auto file = exports[index]; const auto lease = workspace;
    if (drag)
    {
        if (! startFileDrag ({ file.getFullPathName() }, false, source, [lease] {}))
            setStatus ("Cannot start drag. Use Save copy instead.");
        return;
    }
    chooser = std::make_unique<juce::FileChooser> ("Save a copy", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (file.getFileName()), "*" + file.getFileExtension());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<UsedSoundsDialog> (this), file, lease] (const juce::FileChooser& selected)
        {
            const auto target = selected.getResult(); if (target == juce::File{} || ! safe) return;
            ProjectScanner::queryAsync ([file, target, lease]
            {
                if (file == target) return juce::Result::fail ("Choose a different destination for the copy.");
                return file.copyFileTo (target) ? juce::Result::ok() : juce::Result::fail ("Cannot save the copy. Check the destination.");
            }, [safe, target] (juce::Result result) { if (safe) safe->setStatus (result.wasOk() ? "Saved " + target.getFullPathName() : result.getErrorMessage()); });
        });
}
void UsedSoundsDialog::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));
    if (loaded && data->result.failed())
    {
        auto area = list.getBounds().reduced (24);
        g.setFont (RememberUI::font (16, true)); g.setColour (RememberUI::colour (Pal::Error));
        g.drawText ("Could not read used sounds", area.removeFromTop (30), juce::Justification::centred);
        g.setFont (RememberUI::font (13)); g.setColour (RememberUI::colour (Pal::Subtext));
        g.drawFittedText (data->result.getErrorMessage(), area, juce::Justification::centredTop, 5, 1.0f);
    }
    if (loaded && data->result.wasOk() && visible.empty())
    {
        g.setFont (RememberUI::font (14)); g.setColour (RememberUI::colour (Pal::Subtext));
        g.drawText ("No sounds in this view. Try All loaded sounds.", list.getBounds().reduced (12), juce::Justification::centred);
    }
}
void UsedSoundsDialog::resized()
{
    auto area = getLocalBounds().reduced (20);
    auto row = area.removeFromTop (32); refresh.setBounds (row.removeFromRight (80)); row.removeFromRight (8); title.setBounds (row);
    projectLabel.setBounds (area.removeFromTop (24)); area.removeFromTop (16);
    row = area.removeFromTop (32);
    const int width = juce::jmin (128, (row.getWidth() - 192) / 3);
    samples.setBounds (row.removeFromLeft (width)); row.removeFromLeft (6);
    midis.setBounds (row.removeFromLeft (width)); row.removeFromLeft (6);
    plugins.setBounds (row.removeFromLeft (width)); row.removeFromLeft (12); scope.setBounds (row);
    area.removeFromTop (12);
    auto footer = area.removeFromBottom (36); close.setBounds (footer.removeFromRight (80)); footer.removeFromRight (8); stop.setBounds (footer.removeFromRight (80));
    hint.setVisible (auditionPlayer == nullptr);
    if (auditionPlayer != nullptr)
    {
        area.removeFromBottom (8); auditionPlayer->setBounds (area.removeFromBottom (130));
        status.setBounds (area.removeFromBottom (24)); area.removeFromBottom (8);
    }
    else
    {
        hint.setBounds (area.removeFromBottom (24)); area.removeFromBottom (8);
        status.setBounds (area.removeFromBottom (72)); area.removeFromBottom (8);
    }
    list.setBounds (area);
}
void UsedSoundsDialog::launch (ProjectEntry entry, Audition audition, std::function<void()> stop, std::function<void()> close, WaveformPlayerComponent* player)
{
    juce::DialogWindow::LaunchOptions options;
    auto* zoom = new RememberUI::ZoomView (std::make_unique<UsedSoundsDialog> (std::move (entry), std::move (audition), std::move (stop), std::move (close), player));
    zoom->setSize (840, 620); options.content.setOwned (zoom);
    options.dialogTitle = "Used sounds"; options.useNativeTitleBar = true; options.escapeKeyTriggersCloseButton = true;
    options.dialogBackgroundColour = RememberUI::colour (Pal::BgDeep); options.resizable = true;
    if (auto* window = options.launchAsync()) window->setResizeLimits (640, 480, 1600, 1200);
}
