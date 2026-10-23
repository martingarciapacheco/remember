#pragma once
#include "ProjectStatistics.h"
#include "WaveformPlayerComponent.h"
#include <map>

class StatisticsDialog final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit StatisticsDialog (WaveformPlayerComponent* mainPlayer = nullptr, bool persistPlaybackSettings = true);
    ~StatisticsDialog() override;
    static void launch (WaveformPlayerComponent*);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool hasLoaded() const { return loaded; }
    const ProjectStatistics::Report& report() const { return *data; }
    void setCategory (ProjectStatistics::Category);
private:
    friend struct StatisticsTestAccess;
    class Row;
    void load();
    void publish (std::shared_ptr<ProjectStatistics::Report>);
    void filter();
    void updateSummary();
    juce::String entryName (size_t) const;
    bool isHidden (size_t index) const { return hiddenEntries[(size_t) category].count (items()[index].identity) != 0; }
    void setHidden (ProjectStatistics::Category, const juce::String&, bool);
    void showEntryMenu (size_t, juce::Point<int> screenPosition);
    void performMenuAction (int, ProjectStatistics::Category, const juce::String&, const juce::String&);
    void updateEye();
    void audition (size_t);
    void exportFile (size_t, bool, juce::Component*);
    void locate (size_t);
    void setStatus (const juce::String&);
    void details();
    const std::vector<ProjectStatistics::Item>& items() const { return data->items[(size_t) category]; }
    int getNumRows() override { return (int) visible.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int, bool, juce::Component*) override;
    void returnKeyPressed (int) override;
    RememberUI::Style uiStyle { *this };
    juce::Component::SafePointer<WaveformPlayerComponent> mainPlayer;
    WaveformPlayerComponent player;
    std::shared_ptr<ProjectStatistics::Report> data { std::make_shared<ProjectStatistics::Report>() };
    std::shared_ptr<std::atomic<uint64_t>> generation { std::make_shared<std::atomic<uint64_t>> (0) };
    std::vector<size_t> visible;
    std::vector<int> visibleRanks;
    ProjectStatistics::HiddenEntries hiddenEntries;
    std::array<std::map<juce::String, uint64_t>, 3> pendingHiddenEdits;
    uint64_t hiddenEditSequence = 0;
    ProjectStatistics::Category category = ProjectStatistics::Category::Samples;
    bool loaded = false, autoPlay = true, showHidden = false;
    juce::String lastStatus;
    RememberUI::Label title, summary;
    RememberUI::TextButton samples { RememberUI::tr ("Samples") }, instruments { RememberUI::tr ("Instruments") }, effects { RememberUI::tr ("Effects") }, refresh { RememberUI::tr ("Refresh") }, info { RememberUI::tr ("Details") }, close { RememberUI::tr ("Close") };
    RememberUI::TextEditor search;
    RememberUI::TextButton eye;
    RememberUI::RecordingListBox list;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::SharedResourcePointer<RememberUI::TooltipWindow> tooltips;
    RememberUI::LanguageCallback languageUpdates { [this]
    {
        if (loaded)
        {
            const auto selected = list.getSelectedRows();
            updateSummary(); updateEye(); filter();
            list.setSelectedRows (selected, juce::dontSendNotification);
        }
        repaint();
    } };
    std::function<void (const juce::String&)> copyLocation = juce::SystemClipboard::copyTextToClipboard;
    std::function<void (const juce::File&)> revealFile = [] (const juce::File& file) { file.revealToUser(); };
    std::function<bool (const juce::StringArray&, bool, juce::Component*, std::function<void()>)> startFileDrag
        = juce::DragAndDropContainer::performExternalDragDropOfFiles;
};
