#pragma once
#include "UsedSounds.h"
#include "ProjectScanner.h"
#include "WaveformPlayerComponent.h"
#include "../../Shared/RememberLookAndFeel.h"

class UsedSoundsDialog final : public juce::Component, private juce::ListBoxModel
{
public:
    using Audition = std::function<void (const ProjectEntry&)>;
    UsedSoundsDialog (ProjectEntry, Audition = {}, std::function<void()> stop = {}, std::function<void()> close = {}, WaveformPlayerComponent* projectPlayer = nullptr, bool persistPlaybackSettings = true);
    ~UsedSoundsDialog() override;
    static void launch (ProjectEntry, Audition, std::function<void()> stop, std::function<void()> close = {}, WaveformPlayerComponent* player = nullptr);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool hasLoaded() const { return loaded; }
    const UsedSounds::Inventory& inventory() const { return *data; }
    void setCategory (UsedSounds::Kind);
private:
    struct Workspace;
    class Row;
    friend class Row;
    friend struct UsedSoundsTestAccess;
    std::function<bool (const juce::StringArray&, bool, juce::Component*, std::function<void()>)> startFileDrag
        = juce::DragAndDropContainer::performExternalDragDropOfFiles;
    void load();
    void filter();
    void audition (size_t);
    void stopAudition();
    void locate (size_t);
    void exportFile (size_t, bool drag, juce::Component*);
    void setStatus (const juce::String&);
    int getNumRows() override { return (int) visible.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int, bool, juce::Component*) override;
    void returnKeyPressed (int row) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { returnKeyPressed (row); }
    RememberUI::Style uiStyle { *this };
    ProjectEntry entry;
    Audition onAudition;
    std::function<void()> onStop;
    std::function<void()> onClose;
    juce::Component::SafePointer<WaveformPlayerComponent> projectPlayer;
    std::unique_ptr<WaveformPlayerComponent> auditionPlayer;
    bool autoPlayAudition = true;
    std::shared_ptr<Workspace> workspace;
    std::shared_ptr<UsedSounds::Inventory> data { std::make_shared<UsedSounds::Inventory>() };
    std::shared_ptr<std::atomic<uint64_t>> generation { std::make_shared<std::atomic<uint64_t>> (0) };
    std::shared_ptr<std::atomic<uint64_t>> playGeneration { std::make_shared<std::atomic<uint64_t>> (0) };
    std::vector<size_t> visible;
    std::vector<juce::File> exports;
    UsedSounds::Kind category = UsedSounds::Kind::Sample;
    bool loaded = false;
    RememberUI::Label title, projectLabel, status, hint;
    RememberUI::TextButton samples { RememberUI::tr ("Samples") }, midis { "MIDI" }, plugins { RememberUI::tr ("Generators") }, refresh { RememberUI::tr ("Refresh") }, stop { RememberUI::tr ("Stop") }, close { RememberUI::tr ("Close") };
    RememberUI::ComboBox scope;
    RememberUI::RecordingListBox list;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::SharedResourcePointer<RememberUI::TooltipWindow> tooltips;
    RememberUI::LanguageCallback languageUpdates { [this]
    {
        const auto selected = list.getSelectedRows();
        setCategory (category);
        list.setSelectedRows (selected, juce::dontSendNotification);
    } };
};
