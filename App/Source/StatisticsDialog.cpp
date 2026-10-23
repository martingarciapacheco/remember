#include "StatisticsDialog.h"

class StatisticsDialog::Row final : public juce::Component
{
public:
    class DragButton final : public RememberUI::TextButton
    {
    public:
        DragButton() : RememberUI::TextButton (RememberUI::tr ("Drag")) {}
        std::function<void()> begin;
        void mouseDown (const juce::MouseEvent& e) override { dragged = false; RememberUI::TextButton::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            RememberUI::TextButton::mouseDrag (e);
            if (! dragged && isEnabled() && e.mods.isLeftButtonDown() && e.getDistanceFromDragStart() >= 6)
            { dragged = true; if (begin) begin(); }
        }
    private:
        bool dragged = false;
    } drag;
    explicit Row (StatisticsDialog& owner) : dialog (owner)
    {
        RememberUI::iconButton (play, "play", RememberUI::tr ("Audition sample"));
        for (auto* button : std::initializer_list<RememberUI::TextButton*> { &play, &drag, &save, &locateButton })
        {
            RememberUI::bindColour (*button, RememberUI::TextButton::buttonColourId, Pal::Elevated);
            RememberUI::bindColour (*button, RememberUI::TextButton::textColourOffId, Pal::Text);
            addAndMakeVisible (*button);
        }
        drag.setTitle (RememberUI::tr ("Drag a copy of the original sample")); drag.setTooltip (RememberUI::tr ("Drag a copy to your DAW or a folder"));
        save.setTitle (RememberUI::tr ("Save a copy of the sample")); locateButton.setTitle (RememberUI::tr ("Locate missing sample"));
        play.onClick = [this] { dialog.audition (index); };
        save.onClick = [this] { dialog.exportFile (index, false, this); };
        drag.begin = [this] { dialog.exportFile (index, true, &drag); };
        locateButton.onClick = [this] { dialog.locate (index); };
    }
    void bind (size_t value, int rank, bool selected)
    {
        index = value; displayRank = rank; isSelected = selected;
        const auto& item = dialog.items()[index];
        sampleRow = dialog.category == ProjectStatistics::Category::Samples;
        setTitle (dialog.entryName (index)); setDescription (juce::String (item.projects) + RememberUI::tr (" projects. ") + item.path
            + (dialog.isHidden (index) ? RememberUI::tr (". Hidden entry") : ""));
        play.setVisible (sampleRow); play.setEnabled (item.available);
        drag.setVisible (sampleRow && item.available); drag.setEnabled (item.available);
        save.setVisible (sampleRow && item.available); save.setEnabled (item.available);
        locateButton.setVisible (sampleRow && ! item.available);
        setTooltipText (item.path);
        resized(); repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = e.mods.isPopupMenu();
        const auto it = std::find (dialog.visible.begin(), dialog.visible.end(), index);
        if (it != dialog.visible.end()) dialog.list.selectRow ((int) std::distance (dialog.visible.begin(), it));
        if (e.mods.isPopupMenu()) dialog.showEntryMenu (index, e.getScreenPosition());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (sampleRow && ! dragged && e.mods.isLeftButtonDown() && e.getDistanceFromDragStart() >= 6)
        { dragged = true; dialog.exportFile (index, true, this); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    { if (! dragged && sampleRow && e.mods.isLeftButtonDown() && getLocalBounds().contains (e.getPosition())) dialog.audition (index); }
    void paint (juce::Graphics& g) override
    {
        if (index >= dialog.items().size()) return;
        const auto& item = dialog.items()[index];
        g.setColour (RememberUI::colour (isSelected ? Pal::Selected : Pal::Content));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0, 2), RememberUI::controlRadius);
        g.setFont (RememberUI::font (12)); g.setColour (RememberUI::colour (Pal::Subtext));
        g.drawText (juce::String (displayRank), 6, 0, 34, getHeight(), juce::Justification::centred);
        const int left = sampleRow ? 78 : 48, right = getWidth() - (sampleRow ? 152 : 96);
        auto detail = juce::String (item.projects) + (item.projects == 1 ? RememberUI::tr (" project") : RememberUI::tr (" projects"));
        if (sampleRow) { if (! item.available) detail += RememberUI::tr (" / Sample missing"); }
        else detail += " / " + item.identity.upToFirstOccurrenceOf (":", false, false);
        if (dialog.isHidden (index)) detail += RememberUI::tr (" / Hidden");
        const auto nameFont = RememberUI::font (13, true), detailFont = RememberUI::font (12);
        const auto width = juce::jmax (0, right - left);
        const auto detailWidth = juce::jmin (width * 2 / 3, juce::GlyphArrangement::getStringWidthInt (detailFont, detail) + 2);
        const auto nameWidth = juce::jmin (juce::jmax (0, width - detailWidth - 10), juce::GlyphArrangement::getStringWidthInt (nameFont, dialog.entryName (index)) + 2);
        g.setFont (nameFont); g.setColour (RememberUI::colour (Pal::Text));
        g.drawText (dialog.entryName (index), left, 0, nameWidth, getHeight(), juce::Justification::centredLeft, true);
        g.setFont (detailFont); g.setColour (RememberUI::colour (Pal::Subtext));
        g.drawText (detail, left + nameWidth + 10, 0, juce::jmax (0, width - nameWidth - 10), getHeight(), juce::Justification::centredLeft, true);
        if (! sampleRow && dialog.data->read > 0)
        {
            auto bar = juce::Rectangle<float> ((float) getWidth() - 84, (getHeight() - 6) * 0.5f, 68, 6);
            g.setColour (RememberUI::colour (Pal::Elevated)); g.fillRoundedRectangle (bar, 3);
            bar.setWidth (bar.getWidth() * item.projects / dialog.data->read);
            g.setColour (RememberUI::colour (Pal::Accent)); g.fillRoundedRectangle (bar, 3);
        }
    }
    void resized() override
    {
        const int height = RememberUI::statisticsActionHeight, y = (getHeight() - height) / 2;
        play.setBounds (42, y, height, height);
        auto area = juce::Rectangle<int> (getWidth() - 140, y, 128, height);
        drag.setBounds (area.removeFromLeft (48)); area.removeFromLeft (6); save.setBounds (area.withWidth (74));
        locateButton.setBounds (getWidth() - 98, y, 86, height);
    }
private:
    void setTooltipText (const juce::String& path) { play.setTooltip (path); }
    StatisticsDialog& dialog;
    size_t index = 0; int displayRank = 1; bool sampleRow = true, isSelected = false, dragged = false;
    RememberUI::TextButton play, save { RememberUI::tr ("Save copy") }, locateButton { RememberUI::tr ("Locate...") };
};

StatisticsDialog::StatisticsDialog (WaveformPlayerComponent* main, bool persist) : mainPlayer (main), player (nullptr, persist)
{
    setSize (840, 640); setWantsKeyboardFocus (true);
    title.setText (RememberUI::tr ("Statistics"), juce::dontSendNotification); title.setFont (RememberUI::font (22, true));
    for (auto* label : { &title, &summary })
    {
        RememberUI::bindColour (*label, RememberUI::Label::textColourId, label == &title ? Pal::Text : Pal::Subtext);
        if (label != &title) label->setFont (RememberUI::font (12));
        label->setMinimumHorizontalScale (1.0f); label->setBorderSize (juce::BorderSize<int> (0)); addAndMakeVisible (*label);
    }
    for (auto* button : { &samples, &instruments, &effects, &refresh, &info, &close, &eye })
    {
        RememberUI::bindColour (*button, RememberUI::TextButton::buttonColourId, Pal::Elevated);
        RememberUI::bindColour (*button, RememberUI::TextButton::buttonOnColourId, Pal::Action);
        RememberUI::bindColour (*button, RememberUI::TextButton::textColourOffId, Pal::Text);
        RememberUI::bindColour (*button, RememberUI::TextButton::textColourOnId, Pal::OnAction);
        button->setTitle (button->getButtonText()); addAndMakeVisible (*button);
    }
    samples.onClick = [this] { setCategory (ProjectStatistics::Category::Samples); };
    instruments.onClick = [this] { setCategory (ProjectStatistics::Category::Instruments); };
    effects.onClick = [this] { setCategory (ProjectStatistics::Category::Effects); };
    refresh.onClick = [this] { load(); }; info.onClick = [this] { details(); };
    close.onClick = [this] { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (0); };
    RememberUI::placeholder (search, RememberUI::tr ("Search by name..."), Pal::Subtext);
    search.setFont (RememberUI::font (13)); search.setIndents (10, 0); search.setJustification (juce::Justification::centredLeft);
    search.setTitle (RememberUI::tr ("Search statistics by name"));
    RememberUI::bindColour (search, RememberUI::TextEditor::backgroundColourId, Pal::Elevated);
    RememberUI::bindColour (search, RememberUI::TextEditor::textColourId, Pal::Text);
    RememberUI::bindColour (search, RememberUI::TextEditor::focusedOutlineColourId, Pal::Accent);
    search.onTextChange = [this] { filter(); }; addAndMakeVisible (search);
    eye.onClick = [this] { showHidden = ! showHidden; updateEye(); filter(); }; updateEye();
    list.setModel (this); list.setRowHeight (RememberUI::statisticsRowHeight); list.setTitle (RememberUI::tr ("Statistics ranked by number of projects"));
    RememberUI::bindColour (list, juce::ListBox::backgroundColourId, Pal::Content); addAndMakeVisible (list);
    player.setCompactMode (true); player.setQueue ({}); addAndMakeVisible (player);
    setCategory (category); load();
}
StatisticsDialog::~StatisticsDialog()
{ generation->fetch_add (1); player.clearPreview(); list.setModel (nullptr); }
void StatisticsDialog::setStatus (const juce::String& message)
{ lastStatus = message; summary.setText (message, juce::dontSendNotification); summary.setTooltip (message); }
void StatisticsDialog::load()
{
    loaded = false; refresh.setEnabled (false); info.setEnabled (false); player.clearPreview();
    eye.setEnabled (false);
    visible.clear(); list.updateContent(); list.setVisible (false); summary.setText (RememberUI::tr ("Reading saved projects..."), juce::dontSendNotification);
    const auto request = generation->fetch_add (1) + 1; const auto token = generation;
    const auto next = std::make_shared<ProjectStatistics::Report>();
    const auto hidden = std::make_shared<ProjectStatistics::HiddenEntries>();
    ProjectScanner::queryAsync ([next, hidden, token, request]
    {
        const auto preferences = ProjectStatistics::readHiddenEntries (*hidden);
        if (preferences.failed()) return preferences;
        const auto snapshot = ProjectScanner::librarySnapshot();
        if (! snapshot || snapshot->loading) return juce::Result::fail (RememberUI::tr ("The library is still loading. Try Refresh shortly."));
        if (snapshot->error.isNotEmpty() || snapshot->stale) return juce::Result::fail (RememberUI::tr ("Library index unavailable. Refresh the library before Statistics."));
        *next = ProjectStatistics::collect (snapshot->entries, [token, request] { return token->load() != request; });
        return next->error.isEmpty() ? juce::Result::ok() : juce::Result::fail (next->error);
    }, [safe = juce::Component::SafePointer<StatisticsDialog> (this), next, hidden, token, request] (juce::Result result)
    {
        if (! safe || token->load() != request) return;
        if (result.failed()) next->error = RememberUI::diagnostic (result.getErrorMessage());
        // A captured menu can finish while Refresh is reading. Retain newer local edits.
        for (size_t c = 0; c < hidden->size(); ++c) for (const auto& edit : safe->pendingHiddenEdits[c])
        {
            if (safe->hiddenEntries[c].count (edit.first)) (*hidden)[c].insert (edit.first);
            else (*hidden)[c].erase (edit.first);
        }
        safe->hiddenEntries = std::move (*hidden);
        safe->publish (next);
    });
}
void StatisticsDialog::publish (std::shared_ptr<ProjectStatistics::Report> next)
{
    data = std::move (next); loaded = true; refresh.setEnabled (true); info.setEnabled (true); eye.setEnabled (data->error.isEmpty());
    updateSummary();
    lastStatus.clear();
    filter();
}
void StatisticsDialog::updateSummary()
{
    auto description = juce::String (data->read) + RememberUI::tr (" projects read / ") + juce::String (data->excluded) + RememberUI::tr (" unsupported / ")
        + juce::String (data->failed) + RememberUI::tr (" unreadable / ") + juce::String (data->unlinked) + RememberUI::tr (" unlinked");
    if (data->unclassifiedPlugins > 0) description += " / " + juce::String (data->unclassifiedPlugins) + RememberUI::tr (" unclassified (Details)");
    summary.setText (description, juce::dontSendNotification); summary.setTooltip (description);
}
void StatisticsDialog::setCategory (ProjectStatistics::Category next)
{
    if (category != next)
    {
        player.clearPreview(); list.deselectAllRows();
    }
    category = next;
    samples.setToggleState (category == ProjectStatistics::Category::Samples, juce::dontSendNotification);
    instruments.setToggleState (category == ProjectStatistics::Category::Instruments, juce::dontSendNotification);
    effects.setToggleState (category == ProjectStatistics::Category::Effects, juce::dontSendNotification);
    player.setVisible (category == ProjectStatistics::Category::Samples); filter(); resized();
}
juce::String StatisticsDialog::entryName (size_t index) const
{
    const auto& name = items()[index].name;
    if (category != ProjectStatistics::Category::Samples) return name;
    const auto dot = name.lastIndexOfChar ('.');
    return dot > 0 ? name.substring (0, dot) : name;
}
void StatisticsDialog::updateEye()
{
    const auto label = showHidden ? RememberUI::tr ("Hide hidden entries") : RememberUI::tr ("Show hidden entries");
    RememberUI::iconButton (eye, showHidden ? "eye" : "eye-off", label);
    eye.setTooltip (label); eye.setToggleState (showHidden, juce::dontSendNotification);
    eye.setDescription (showHidden ? RememberUI::tr ("Visible and hidden entries are shown together") : RememberUI::tr ("Hidden entries are excluded and the ranking is refilled"));
}
void StatisticsDialog::setHidden (ProjectStatistics::Category targetCategory, const juce::String& identity, bool hide)
{
    const auto c = (size_t) targetCategory;
    const bool before = hiddenEntries[c].count (identity) != 0;
    if (before == hide) return;
    if (hide) hiddenEntries[c].insert (identity); else hiddenEntries[c].erase (identity);
    const auto request = ++hiddenEditSequence; pendingHiddenEdits[c][identity] = request;
    filter();
    // The query worker is serial: preference deltas and future dialog reads keep their order,
    // without making a UI-only preference trigger a library reindex. Accepted writes survive close.
    ProjectScanner::queryAsync ([targetCategory, identity, hide]
    { return ProjectStatistics::saveHiddenEntry (targetCategory, identity, hide); },
        [safe = juce::Component::SafePointer<StatisticsDialog> (this), c, identity, before, request] (juce::Result result)
        {
            if (! safe) return;
            auto& pending = safe->pendingHiddenEdits[c];
            const auto edit = pending.find (identity);
            if (edit == pending.end() || edit->second != request) return;
            pending.erase (edit);
            if (result.failed())
            {
                if (before) safe->hiddenEntries[c].insert (identity); else safe->hiddenEntries[c].erase (identity);
                safe->filter(); safe->setStatus (RememberUI::tr ("Cannot save hidden entries: ") + RememberUI::diagnostic (result.getErrorMessage()));
            }
        });
}
void StatisticsDialog::showEntryMenu (size_t index, juce::Point<int> position)
{
    if (index >= items().size()) return;
    const auto item = items()[index]; const auto targetCategory = category;
    const bool hidden = isHidden (index);
    juce::PopupMenu menu; menu.addItem (hidden ? 4 : 1, hidden ? RememberUI::tr ("Unhide entry") : RememberUI::tr ("Hide entry"));
    if (targetCategory == ProjectStatistics::Category::Samples)
    {
        menu.addSeparator(); menu.addItem (2, RememberUI::tr ("Show file in Explorer"), item.available);
        menu.addItem (3, RememberUI::tr ("Copy location"), item.path.isNotEmpty());
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
        .withTargetScreenArea ({ position.x, position.y, 1, 1 }),
        [safe = juce::Component::SafePointer<StatisticsDialog> (this), item, targetCategory] (int action)
        { if (safe) safe->performMenuAction (action, targetCategory, item.identity, item.path); });
}
void StatisticsDialog::performMenuAction (int action, ProjectStatistics::Category targetCategory, const juce::String& identity, const juce::String& path)
{
    if (action == 1 || action == 4) setHidden (targetCategory, identity, action == 1);
    else if (action == 2 && targetCategory == ProjectStatistics::Category::Samples && juce::File::isAbsolutePath (path))
    {
        ProjectScanner::queryAsync ([path, reveal = revealFile]
        {
            const juce::File file (path);
            if (! file.existsAsFile()) return juce::Result::fail (RememberUI::tr ("The original sample is no longer available"));
            reveal (file); return juce::Result::ok();
        }, [safe = juce::Component::SafePointer<StatisticsDialog> (this)] (juce::Result result)
        { if (safe && result.failed()) safe->setStatus (RememberUI::diagnostic (result.getErrorMessage())); });
    }
    else if (action == 3 && targetCategory == ProjectStatistics::Category::Samples)
        copyLocation (path);
}
void StatisticsDialog::filter()
{
    visible.clear(); visibleRanks.clear(); const auto query = search.getText().trim();
    const int limit = category == ProjectStatistics::Category::Samples ? 1000 : 50;
    int rank = 0;
    for (size_t i = 0; i < items().size(); ++i)
    {
        if (! showHidden && isHidden (i)) continue;
        if (++rank > limit) break;
        if (entryName (i).containsIgnoreCase (query)) { visible.push_back (i); visibleRanks.push_back (rank); }
    }
    list.deselectAllRows(); list.updateContent(); list.setVisible (loaded && data->error.isEmpty() && ! visible.empty()); repaint();
}
juce::Component* StatisticsDialog::refreshComponentForRow (int row, bool selected, juce::Component* existing)
{
    if (row < 0 || row >= (int) visible.size()) { delete existing; return nullptr; }
    auto* component = dynamic_cast<Row*> (existing);
    if (! component) { delete existing; component = new Row (*this); }
    component->bind (visible[(size_t) row], visibleRanks[(size_t) row], selected); return component;
}
void StatisticsDialog::returnKeyPressed (int row)
{ if (row >= 0 && row < (int) visible.size()) audition (visible[(size_t) row]); }
bool StatisticsDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('f', juce::ModifierKeys::ctrlModifier, 0)) { search.grabKeyboardFocus(); search.selectAll(); return true; }
    return false;
}
void StatisticsDialog::audition (size_t index)
{
    if (category != ProjectStatistics::Category::Samples || index >= items().size()) return;
    const auto& item = items()[index];
    if (! item.available) { setStatus (RememberUI::tr ("Sample missing. Use Locate to choose its original file.")); return; }
    if (mainPlayer) mainPlayer->suspendOutput();
    ProjectEntry entry; entry.name = entryName (index); entry.previewOgg = juce::File (item.path);
    player.loadEntry (entry); if (autoPlay) player.play();
}
void StatisticsDialog::exportFile (size_t index, bool drag, juce::Component* source)
{
    if (category != ProjectStatistics::Category::Samples || index >= items().size() || ! items()[index].available) return;
    const juce::File file (items()[index].path);
    if (drag)
    {
        if (! startFileDrag ({ file.getFullPathName() }, false, source, [] {})) setStatus (RememberUI::tr ("Cannot start drag. Use Save copy instead."));
        return;
    }
    chooser = std::make_unique<juce::FileChooser> (RememberUI::tr ("Save a copy"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (file.getFileName()), "*" + file.getFileExtension());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<StatisticsDialog> (this), file] (const juce::FileChooser& selected)
        {
            const auto target = selected.getResult(); if (! safe || target == juce::File{}) return;
            ProjectScanner::queryAsync ([file, target]
            {
                if (file == target) return juce::Result::fail (RememberUI::tr ("Choose a different destination for the copy"));
                return file.copyFileTo (target) ? juce::Result::ok() : juce::Result::fail (RememberUI::tr ("Cannot save the copy"));
            }, [safe] (juce::Result result) { if (safe) safe->setStatus (result.wasOk() ? RememberUI::tr ("Copy saved") : RememberUI::diagnostic (result.getErrorMessage())); });
        });
}
void StatisticsDialog::locate (size_t index)
{
    if (category != ProjectStatistics::Category::Samples || index >= items().size()) return;
    const auto identity = items()[index].identity; const auto request = generation->load();
    chooser = std::make_unique<juce::FileChooser> (RememberUI::tr ("Locate original sample"), juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<StatisticsDialog> (this), identity, request] (const juce::FileChooser& selected)
        {
            const auto file = selected.getResult(); if (! safe || file == juce::File{} || safe->generation->load() != request) return;
            for (auto& item : safe->data->items[0]) if (item.identity == identity) { item.path = file.getFullPathName(); item.available = true; }
            safe->list.updateContent(); safe->list.repaint(); safe->setStatus (RememberUI::tr ("Sample located for this session. Saved projects were not changed."));
        });
}
void StatisticsDialog::details()
{
    auto message = RememberUI::tr ("FL Studio (.flp) and Ableton Live (.als). Saved projects in the whole library, including all loaded sounds. ") + RememberUI::tr ("Each sample path or plugin is counted once per project. Repeated recordings of the same project path count once.\n\n");
    juce::String text (message);
    text += RememberUI::tr ("Metadata estimate: ") + juce::String ((double) data->metadataBytes() / 1024.0, 1) + RememberUI::tr (" KiB (excludes player and temporary reader memory).\n");
    if (data->unclassifiedPlugins > 0) text += juce::String (data->unclassifiedPlugins) + RememberUI::tr (" plugins have unknown type/name and were omitted.\n");
    if (data->error.isNotEmpty()) text += RememberUI::diagnostic (data->error) + "\n";
    if (lastStatus.isNotEmpty()) text += lastStatus + "\n";
    text += "\n" + data->details.joinIntoString ("\n");
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, RememberUI::tr ("Statistics details"), text, RememberUI::tr ("OK"), this);
}
void StatisticsDialog::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));
    if (! list.isVisible())
    {
        g.setColour (RememberUI::colour (data->error.isNotEmpty() ? Pal::Error : Pal::Subtext)); g.setFont (RememberUI::font (14));
        const auto text = ! loaded ? RememberUI::tr ("Reading saved projects...") : data->error.isNotEmpty() ? RememberUI::diagnostic (data->error)
            : search.getText().trim().isNotEmpty() ? RememberUI::tr ("No matching names in this ranking.") : data->read == 0
            ? RememberUI::tr ("No readable supported projects. See Details.") : ! showHidden && ! items().empty()
            ? RememberUI::tr ("All entries are hidden. Use Show hidden entries.") : RememberUI::tr ("No items in this category.");
        g.drawFittedText (text, list.getBounds().reduced (20), juce::Justification::centred, 4);
    }
}
void StatisticsDialog::resized()
{
    auto area = getLocalBounds().reduced (20);
    auto row = area.removeFromTop (32); refresh.setBounds (row.removeFromRight (80)); title.setBounds (row);
    summary.setBounds (area.removeFromTop (24)); area.removeFromTop (12);
    row = area.removeFromTop (32); samples.setBounds (row.removeFromLeft (116)); row.removeFromLeft (8);
    instruments.setBounds (row.removeFromLeft (124)); row.removeFromLeft (8); effects.setBounds (row.removeFromLeft (108));
    area.removeFromTop (12); row = area.removeFromTop (34); eye.setBounds (row.removeFromRight (34)); row.removeFromRight (8); search.setBounds (row);
    area.removeFromTop (8);
    auto footer = area.removeFromBottom (32); close.setBounds (footer.removeFromRight (80)); info.setBounds (footer.removeFromLeft (80));
    area.removeFromBottom (8);
    if (category == ProjectStatistics::Category::Samples) { area.removeFromBottom (8); player.setBounds (area.removeFromBottom (130)); }
    area.removeFromBottom (8); list.setBounds (area);
}
void StatisticsDialog::launch (WaveformPlayerComponent* main)
{
    juce::DialogWindow::LaunchOptions options;
    auto* zoom = new RememberUI::ZoomView (std::make_unique<StatisticsDialog> (main)); zoom->setSize (840, 640);
    options.content.setOwned (zoom); options.dialogTitle = RememberUI::tr ("Statistics"); options.useNativeTitleBar = true;
    options.escapeKeyTriggersCloseButton = true; options.dialogBackgroundColour = RememberUI::colour (Pal::BgDeep); options.resizable = true;
    if (auto* window = RememberUI::launchDialog (options)) window->setResizeLimits (640, 480, 1600, 1200);
}
