#pragma once
#include "OfflineRenderTiming.h"
#include "../../Shared/RememberLookAndFeel.h"
#include "../../Shared/BackgroundRetirement.h"

class OfflineRenderingDialog final : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    OfflineRenderingDialog();
    ~OfflineRenderingDialog() override;
    static void launch();
    static void shutdown();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    friend struct OfflineRenderingTestAccess;
    struct Work;
    void timerCallback() override;
    void refresh();
    void choose (int);
    void start();
    void next();
    void execute (OfflineRendering::Command, bool);
    void stop();
    void setBusy (bool);
    void updateText();
    void setStatus (const juce::String&);
    void begin (std::function<void (Work&)>, std::function<void (Work&)>);
    int getNumRows() override { return (int) plan.jobs.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    RememberUI::Style style { *this };
    juce::Viewport viewport;
    juce::Component body;
    RememberUI::ComboBox daw;
    RememberUI::Label title, source, destination, executable, settingsText, summary, status;
    std::vector<std::unique_ptr<RememberUI::Label>> settingLabels;
    RememberUI::TextButton project { RememberUI::tr ("Choose project...") }, folder { RememberUI::tr ("Choose folder...") }, target { RememberUI::tr ("Choose destination...") }, exeButton { RememberUI::tr ("Choose FL executable...") }, read { RememberUI::tr ("Read saved FL settings") };
    RememberUI::ToggleButton recursive { RememberUI::tr ("Include projects in subfolders (recursive scan)") }, beside { RememberUI::tr ("Save audio beside each FLP") }, previews { RememberUI::tr ("Use full renders as previews") };
    std::array<RememberUI::ToggleButton, 6> formats;
    RememberUI::TextButton renderButton { RememberUI::tr ("Start rendering") }, stopButton { RememberUI::tr ("Stop rendering") };
    RememberUI::RecordingListBox list;
    OfflineRendering::RememberOptions options;
    OfflineRendering::FlStudioSettings flSettings;
    OfflineRendering::BatchPlan plan;
    OfflineRendering::Estimate estimate;
    juce::File flExecutable;
    juce::StringArray rowStates;
    juce::String statusSource;
    std::shared_ptr<Work> work;
    std::function<void (Work&)> completed;
    std::unique_ptr<juce::FileChooser> chooser;
    int current = 0;
    bool batch = false, batchPreviews = false, refreshPending = false;
    RememberUI::LanguageCallback language { [this] { updateText(); } };
    juce::SharedResourcePointer<RememberUI::TooltipWindow> tooltips;
};
