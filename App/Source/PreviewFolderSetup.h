#pragma once
#include "ProjectScanner.h"
#include "../../Shared/RememberLookAndFeel.h"

class PreviewFolderSetup final : public juce::Component
{
public:
    explicit PreviewFolderSetup (std::function<void()> done) : onDone (std::move (done))
    {
        title.setText ("Create a folder for your previews", juce::dontSendNotification);
        title.setFont (RememberUI::font (22, true));
        description.setText ("This folder stores the audio recorded by the Remember plugin for each project's preview.\n\n"
            "We recommend creating a dedicated folder, such as Remember Previews, to keep these files from filling up other locations on your computer.\n\n"
            "In the next window, create a new folder or select an existing one. You can change it later in Settings.", juce::dontSendNotification);
        description.setFont (RememberUI::font (14)); description.setJustificationType (juce::Justification::topLeft);
        RememberUI::bindColour (title, juce::Label::textColourId, Pal::Text);
        RememberUI::bindColour (description, juce::Label::textColourId, Pal::Subtext);
        RememberUI::bindColour (error, juce::Label::textColourId, Pal::Error);
        error.setFont (RememberUI::font (12));
        choose.setTitle ("Create or choose previews folder");
        RememberUI::bindColour (choose, juce::TextButton::buttonColourId, Pal::Action);
        RememberUI::bindColour (choose, juce::TextButton::textColourOffId, Pal::OnAction);
        for (auto* child : std::initializer_list<juce::Component*> { &title, &description, &error, &choose, &later }) addAndMakeVisible (child);
        choose.onClick = [this] { pick(); };
        later.onClick = [this] { close(); };
        setSize (560, 360);
    }
    static void launch (juce::Component* parent, std::function<void()> done)
    {
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Set up preview storage";
        options.content.setOwned (new PreviewFolderSetup (std::move (done)));
        options.componentToCentreAround = parent;
        options.dialogBackgroundColour = RememberUI::colour (Pal::BgDeep);
        options.useNativeTitleBar = true; options.resizable = false;
        options.escapeKeyTriggersCloseButton = true; RememberUI::launchDialog (options);
    }
    // Worker-only: preserve all existing plugin and app settings.
    static juce::Result saveFolder (const juce::File& folder)
    {
        if (! folder.isDirectory() || ! folder.hasWriteAccess())
            return juce::Result::fail ("Choose a writable folder or create a new one in the folder picker.");
        const auto settings = ProjectScanner::readSettings();
        if (settings.result.failed()) return settings.result;
        return LibraryStore::updateJson (LibraryStore::configFile(), [folder] (juce::DynamicObject& config)
        { config.setProperty ("previewsFolder", folder.getFullPathName()); return juce::Result::ok(); }, true);
    }
    void paint (juce::Graphics& g) override { g.fillAll (RememberUI::colour (Pal::BgDeep)); }
    void resized() override
    {
        title.setBounds (24, 20, getWidth() - 48, 36);
        description.setBounds (24, 72, getWidth() - 48, getHeight() - 168);
        error.setBounds (24, getHeight() - 92, getWidth() - 48, 36);
        choose.setBounds (getWidth() - 228, getHeight() - 48, 204, 32);
        later.setBounds (getWidth() - 328, getHeight() - 48, 92, 32);
    }
private:
    void close() { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (0); }
    void pick()
    {
        chooser = std::make_unique<juce::FileChooser> ("Create or choose a dedicated previews folder",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory), juce::String{}, true);
        choose.setEnabled (false);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<PreviewFolderSetup> (this)] (const juce::FileChooser& selected)
            {
                if (safe == nullptr) return;
                const auto folder = selected.getResult();
                if (folder == juce::File{}) { safe->choose.setEnabled (true); return; }
                safe->later.setEnabled (false);
                ProjectScanner::editAsync ([folder]
                {
                    return saveFolder (folder);
                }, [safe] (juce::Result result)
                {
                    if (safe == nullptr) return;
                    safe->choose.setEnabled (true); safe->later.setEnabled (true);
                    if (result.failed()) { safe->error.setText (result.getErrorMessage(), juce::dontSendNotification); return; }
                    ProjectScanner::refreshAsync();
                    auto completed = safe->onDone;
                    if (completed) completed();
                    if (safe != nullptr) safe->close();
                });
            });
    }
    RememberUI::Style style { *this };
    juce::Label title, description, error;
    juce::TextButton choose { "Create or choose folder..." }, later { "Later" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::function<void()> onDone;
};
