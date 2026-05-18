#include "SettingsDialog.h"
#include "ProjectScanner.h"
#include "ProjectListComponent.h"   // for Pal::

// ── ListBoxModel for the project folders list ────────────────────────────────
class SettingsDialog::FoldersListModel : public juce::ListBoxModel
{
public:
    explicit FoldersListModel (SettingsDialog& o) : owner (o) {}

    int getNumRows() override { return owner.projectsFolders.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (row < 0 || row >= owner.projectsFolders.size()) return;
        g.fillAll (juce::Colour (sel ? Pal::Elevated : Pal::BgDeep));
        g.setColour (juce::Colour (Pal::Border));
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);

        g.setColour (juce::Colour (Pal::Text));
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (12.0f)));
        // Leave room on the right for the per-row remove button (32px)
        g.drawText (owner.projectsFolders.getReference (row).getFullPathName(),
                    8, 0, w - 40, h, juce::Justification::centredLeft, true);
    }

    juce::Component* refreshComponentForRow (int row, bool /*sel*/,
                                             juce::Component* existing) override
    {
        auto* btn = dynamic_cast<juce::TextButton*> (existing);
        if (btn == nullptr) btn = new juce::TextButton ("X");

        if (row < 0 || row >= owner.projectsFolders.size())
        {
            btn->setVisible (false);
            return btn;
        }

        btn->setVisible (true);
        btn->setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        btn->setColour (juce::TextButton::textColourOffId, juce::Colour (Pal::Subtext));
        btn->setWantsKeyboardFocus (false);
        btn->onClick = [this, row]
        {
            if (row >= 0 && row < owner.projectsFolders.size())
            {
                owner.projectsFolders.remove (row);
                owner.rebuildFoldersList();
                owner.refreshLabels();
            }
        };
        return btn;
    }

private:
    SettingsDialog& owner;
};

// ── SettingsDialog ────────────────────────────────────────────────────────────

SettingsDialog::SettingsDialog (bool firstRunFlag) : firstRun (firstRunFlag)
{
    setSize (560, 440);

    previewsFolder  = ProjectScanner::getPreviewsFolder();
    projectsFolders = ProjectScanner::getProjectsRootFolders();

    auto styleLabel = [] (juce::Label& l, float h, uint32_t col, bool bold = false)
    {
        l.setFont (juce::Font (juce::FontOptions{}.withHeight (h)
                               .withStyle (bold ? "Bold" : "Regular")));
        l.setColour (juce::Label::textColourId, juce::Colour (col));
    };

    header.setText (firstRun ? "Welcome to Remember" : "Settings",
                    juce::dontSendNotification);
    styleLabel (header, 18.0f, Pal::Text, true);
    addAndMakeVisible (header);

    styleLabel (previewsCaption, 13.0f, Pal::Text, true);
    styleLabel (previewsHint,    11.0f, Pal::Subtext);
    styleLabel (previewsPath,    12.0f, Pal::Accent);
    addAndMakeVisible (previewsCaption);
    addAndMakeVisible (previewsHint);
    addAndMakeVisible (previewsPath);

    styleLabel (projectsCaption, 13.0f, Pal::Text, true);
    styleLabel (projectsHint,    11.0f, Pal::Subtext);
    addAndMakeVisible (projectsCaption);
    addAndMakeVisible (projectsHint);

    foldersModel = std::make_unique<FoldersListModel> (*this);
    foldersList.setModel (foldersModel.get());
    foldersList.setRowHeight (26);
    foldersList.setColour (juce::ListBox::backgroundColourId, juce::Colour (Pal::Elevated));
    foldersList.setOutlineThickness (0);
    addAndMakeVisible (foldersList);

    auto styleBtn = [] (juce::TextButton& b, uint32_t bg, uint32_t fg)
    {
        b.setColour (juce::TextButton::buttonColourId,  juce::Colour (bg));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (fg));
    };

    styleBtn (previewsBrowse,  Pal::Elevated, Pal::Text);
    styleBtn (addProjectsBtn,  Pal::Elevated, Pal::Text);
    styleBtn (okBtn,           Pal::Accent,   0xffffffff);
    styleBtn (cancelBtn,       Pal::Elevated, Pal::Subtext);

    previewsBrowse.onClick = [this] { pickPreviews(); };
    addProjectsBtn.onClick = [this] { addProjectFolder(); };
    okBtn         .onClick = [this] { commit(); };
    cancelBtn     .onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    addAndMakeVisible (previewsBrowse);
    addAndMakeVisible (addProjectsBtn);
    addAndMakeVisible (okBtn);
    if (firstRun)
        cancelBtn.setVisible (false);   // explicit so resized() skips it
    else
        addAndMakeVisible (cancelBtn);

    refreshLabels();
}

void SettingsDialog::refreshLabels()
{
    previewsPath.setText (previewsFolder.getFullPathName().isEmpty()
                             ? "(not set)" : previewsFolder.getFullPathName(),
                          juce::dontSendNotification);

    // OK is enabled when previews is valid AND at least one valid project folder
    bool anyProjectsValid = false;
    for (auto& f : projectsFolders)
        if (f.isDirectory()) { anyProjectsValid = true; break; }

    okBtn.setEnabled (previewsFolder.isDirectory() && anyProjectsValid);
}

void SettingsDialog::rebuildFoldersList()
{
    foldersList.updateContent();
    foldersList.repaint();
}

void SettingsDialog::pickPreviews()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Select previews folder (where recordings are saved)",
        previewsFolder.isDirectory()
            ? previewsFolder
            : juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<SettingsDialog> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            if (! fc.getResults().isEmpty())
                safe->previewsFolder = fc.getResult();
            safe->refreshLabels();
        });
}

void SettingsDialog::addProjectFolder()
{
    auto start = projectsFolders.isEmpty()
                     ? juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                     : projectsFolders.getReference (0);
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);

    chooser = std::make_unique<juce::FileChooser> (
        "Add a folder to search for DAW projects",
        start);

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<SettingsDialog> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            if (fc.getResults().isEmpty()) return;
            auto picked = fc.getResult();
            // Skip duplicates
            for (auto& f : safe->projectsFolders)
                if (f == picked) return;
            safe->projectsFolders.add (picked);
            safe->rebuildFoldersList();
            safe->refreshLabels();
        });
}

void SettingsDialog::commit()
{
    if (previewsFolder.isDirectory())
        ProjectScanner::setPreviewsFolder (previewsFolder);

    // Drop invalid entries before saving
    juce::Array<juce::File> valid;
    for (auto& f : projectsFolders)
        if (f.isDirectory()) valid.add (f);
    ProjectScanner::setProjectsRootFolders (valid);

    if (onCompleted) onCompleted();

    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (1);
}

void SettingsDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Pal::BgDeep));

    // Header strip
    g.setColour (juce::Colour (Pal::Surface));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (juce::Colour (Pal::Border));
    g.fillRect (0, 48, getWidth(), 1);
}

void SettingsDialog::resized()
{
    auto area = getLocalBounds();

    auto headerArea = area.removeFromTop (48).reduced (16, 0);
    header.setBounds (headerArea);

    area.removeFromTop (16);
    area = area.reduced (16, 0);

    // Previews block
    auto block1 = area.removeFromTop (78);
    previewsCaption.setBounds (block1.removeFromTop (20));
    previewsHint   .setBounds (block1.removeFromTop (16));
    auto row1 = block1.removeFromTop (28);
    previewsBrowse.setBounds (row1.removeFromRight (100));
    row1.removeFromRight (10);
    previewsPath  .setBounds (row1);

    area.removeFromTop (12);

    // Project folders block (caption + hint + list + add button)
    projectsCaption.setBounds (area.removeFromTop (20));
    projectsHint   .setBounds (area.removeFromTop (16));
    area.removeFromTop (4);

    auto bottomFixed = 60;   // reserved for OK/Cancel
    auto addBtnRow   = area.removeFromBottom (32);
    addProjectsBtn.setBounds (addBtnRow.removeFromLeft (120));

    auto listArea = area.withTrimmedBottom (bottomFixed - 32);
    foldersList.setBounds (listArea);

    auto bottom = getLocalBounds().removeFromBottom (52).reduced (16, 10);
    okBtn.setBounds (bottom.removeFromRight (90));
    if (cancelBtn.isVisible())
    {
        bottom.removeFromRight (8);
        cancelBtn.setBounds (bottom.removeFromRight (90));
    }
}

void SettingsDialog::launch (bool firstRun, std::function<void()> onDone)
{
    auto* dialog = new SettingsDialog (firstRun);
    dialog->onCompleted = onDone;

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle               = firstRun ? "Remember - Setup" : "Remember - Settings";
    opts.content.setOwned          (dialog);
    opts.componentToCentreAround   = nullptr;
    opts.escapeKeyTriggersCloseButton = ! firstRun;
    opts.useNativeTitleBar         = true;
    opts.resizable                 = false;
    opts.dialogBackgroundColour    = juce::Colour (Pal::BgDeep);
    opts.launchAsync();
}
