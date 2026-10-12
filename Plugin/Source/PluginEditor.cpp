#include "PluginEditor.h"
#include "SettingsManager.h"
#include "BuildInfo.h"

namespace
{
    juce::Font bodyFont()  { return RememberUI::font (13.0f); }
    juce::Font titleFont() { return RememberUI::font (22.0f, true); }
}

RememberEditor::RememberEditor (RememberProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (440, 496);
    for (auto* toggle : { &manualToggle, &lockToggle, &replaceToggle })
    {
        toggle->setTitle (toggle->getButtonText());
        toggle->onClick = [this] { saveCaptureOptions(); };
        addAndMakeVisible (*toggle);
    }
    lockToggle.setTooltip ("Shared with app Settings. Stops updates in all Remember instances; unlock to capture again.");
    replaceToggle.setTooltip ("Off: a short capture keeps the old tail. On: only the new capture is kept.");
    manualToggle.setTooltip ("Only this project. Saved with the DAW project; app Settings sets the default for new instances.");
    manualToggle.onClick = [this]
    {
        auto options = processor.getEngine().getCaptureOptions();
        options.manual = manualToggle.getToggleState();
        processor.getEngine().setCaptureOptions (options);
        processor.updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
        refreshFromEngine();
    };
    RememberUI::bindColour (captureButton, juce::TextButton::buttonColourId, Pal::Action);
    RememberUI::bindColour (captureButton, juce::TextButton::textColourOffId, Pal::OnAction);
    captureButton.onClick = [this] { processor.getEngine().armCapture(); refreshFromEngine(); };
    addAndMakeVisible (captureButton);

    // ── Project name editor ──────────────────────────────────────────────────
    nameEditor.setFont (RememberUI::font (16.0f, true));
    nameEditor.setIndents (10, 9);
    nameEditor.setTitle ("Project name");
    nameEditor.setJustification (juce::Justification::centredLeft);
    RememberUI::bindColour (nameEditor, juce::TextEditor::backgroundColourId, kElevated);
    RememberUI::bindColour (nameEditor, juce::TextEditor::textColourId, kText);
    nameEditor.setColour (juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
    RememberUI::bindColour (nameEditor, juce::TextEditor::focusedOutlineColourId, kAccent);
    nameEditor.setText (processor.getProjectName(), false);
    nameEditor.onReturnKey  = [this] { processor.setProjectName (nameEditor.getText()); nameEditor.unfocusAllComponents(); };
    nameEditor.onFocusLost  = [this] { processor.setProjectName (nameEditor.getText()); };
    addAndMakeVisible (nameEditor);

    // ── BPM / Key combined label ─────────────────────────────────────────────
    bpmKeyLabel.setFont (RememberUI::font (14.0f, true));
    RememberUI::bindColour (bpmKeyLabel, juce::Label::textColourId, kAccent);
    bpmKeyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (bpmKeyLabel);

    // ── Status ───────────────────────────────────────────────────────────────
    statusLabel.setFont (bodyFont());
    RememberUI::bindColour (statusLabel, juce::Label::textColourId, kSubtext);
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    // ── Settings button ───────────────────────────────────────────────────────
    RememberUI::iconButton (settingsButton, "settings", "Recording settings");
    settingsButton.setTooltip ("Settings: appearance and output folder");
    settingsButton.setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
    RememberUI::bindColour (settingsButton, juce::TextButton::textColourOffId, kSubtext);
    settingsButton.onClick = [this] { showSettingsPanel(); };
    addAndMakeVisible (settingsButton);
    associationLabel.setFont (RememberUI::font (12.0f));
    RememberUI::bindColour (associationLabel, juce::Label::textColourId, kSubtext);
    addAndMakeVisible (associationLabel);
    RememberUI::bindColour (associationButton, juce::TextButton::buttonColourId, kElevated);
    RememberUI::bindColour (associationButton, juce::TextButton::textColourOffId, kText);
    associationButton.setTooltip ("Select a DAW file, keep a recording without a file, or return to automatic association");
    associationButton.onClick = [this] { showAssociationMenu(); };
    addAndMakeVisible (associationButton);

    // ── Version footer ────────────────────────────────────────────────────────
    versionLabel.setText (juce::String ("v") + REMEMBER_VERSION + "  " + REMEMBER_GIT_HASH,
                          juce::dontSendNotification);
    versionLabel.setFont (RememberUI::font (11.0f));
    RememberUI::bindColour (versionLabel, juce::Label::textColourId, kSubtext, 1.0f);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    versionLabel.setTooltip (REMEMBER_VERSION_LINE);
    addAndMakeVisible (versionLabel);

    settingsButton.setEnabled (false);
    settingsRequest = SettingsManager::getInstance().readSnapshotAsync();

    startTimerHz (2);   // Idle rate; bumped to 10Hz while Recording
    refreshFromEngine();
    editorReady = true;
}

RememberEditor::~RememberEditor() { stopTimer(); }

// ── Timer ─────────────────────────────────────────────────────────────────────

void RememberEditor::timerCallback()
{
    refreshFromEngine();
}

void RememberEditor::refreshFromEngine()
{
    refreshSettingsRequest();
    auto& eng   = processor.getEngine();
    auto  state = eng.getState();
    int   stInt = (int) state;
    const auto options = eng.getCaptureOptions();
    for (auto* toggle : { &manualToggle, &lockToggle, &replaceToggle }) toggle->setEnabled (! settingsRequest);
    if (! settingsRequest)
    {
        manualToggle.setToggleState (options.manual, juce::dontSendNotification);
        lockToggle.setToggleState (options.locked, juce::dontSendNotification);
        replaceToggle.setToggleState (options.replace, juce::dontSendNotification);
    }
    captureButton.setEnabled (! settingsRequest && options.manual && ! eng.isPreviewLocked() && ! eng.isCaptureArmed() && state != RecordingEngine::State::Recording);
    captureButton.setButtonText (eng.isCaptureArmed() ? "Armed for playback" : "Capture next play");
    const auto association = eng.getAssociationInfo();
    const bool followsFile = association.confirmed && association.source != RecordingEngine::Association::ManualRecording;
    nameEditor.setReadOnly (followsFile);
    nameEditor.setTooltip (followsFile ? "Name follows the associated project filename"
                                      : "Name this recording; Associate can keep it permanently without a project file");
    associationLabel.setText (association.problem.isEmpty() ? association.description : "Project search unavailable", juce::dontSendNotification);
    associationLabel.setTooltip (association.description + "\nEvidence: " + association.confidence
        + (association.path.isNotEmpty() ? "\n" + association.path : juce::String{})
        + (association.problem.isNotEmpty() ? "\n" + association.problem : juce::String{}));

    // Adapt poll rate: 10Hz only while Recording, 2Hz otherwise
    int desiredHz = (state == RecordingEngine::State::Recording
                     || state == RecordingEngine::State::Encoding) ? 10 : 2;
    if (getTimerInterval() != 1000 / desiredHz)
        startTimerHz (desiredHz);

    if (! nameEditor.hasKeyboardFocus (false))
    {
        auto name = processor.getProjectName();
        if (name != nameEditor.getText())
        {
            nameEditor.setText (name, false);
        }
    }

    // BPM + Key
    double bpm = eng.getDetectedBpm();
    auto   key = eng.getDetectedKey();

    juce::String meta;
    if (bpm > 0.0) meta += juce::String (bpm, 1) + " BPM";
    if (key.isNotEmpty())
    {
        if (meta.isNotEmpty()) meta += "  |  ";
        meta += key;
    }
    juce::String bpmKeyText = meta.isEmpty() ? "Tempo and key appear after recording" : meta;
    if (bpmKeyText != lastBpmKeyText)
    {
        bpmKeyLabel.setText (bpmKeyText, juce::dontSendNotification);
        lastBpmKeyText = bpmKeyText;
    }

    // Status + progress
    juce::String statusText;
    uint32_t     statusColour = kSubtext;
    float        progress     = recordProgress;
    bool         recordingNow = false;

    switch (state)
    {
        case RecordingEngine::State::Idle:
            progress     = 0.0f;
            statusText   = "Play from the start to record a preview";
            statusColour = kSubtext;
            break;

        case RecordingEngine::State::Recording:
        {
            recordingNow = true;
            progress     = eng.getProgress();
            int total    = eng.getTotalSeconds();
            int secs     = (int) (progress * (float) total);
            statusText   = "Recording  " + juce::String (secs) + "s / " + juce::String (total) + "s";
            statusColour = kAccent;
            break;
        }
        case RecordingEngine::State::Encoding:
            progress     = 1.0f;
            statusText   = "Analysing & saving...";
            statusColour = kAccent;
            break;

        case RecordingEngine::State::Done:
            progress     = 1.0f;
            statusText   = "Saved  " + eng.getLastSavedTime();
            statusColour = kSuccess;
            break;

        case RecordingEngine::State::Pending:
            progress     = 1.0f;
            statusText   = eng.getProjectName().isEmpty() ? "Audio pending - name or associate it"
                                                        : "Pending audio - Associate to keep it";
            statusColour = kSubtext;
            break;

        case RecordingEngine::State::Error:
            progress     = 0.0f;
            statusText   = eng.getLastError().isNotEmpty() ? eng.getLastError() : "Cannot save preview; see Remember log";
            statusColour = kError;
            break;
    }

    isRecording = recordingNow;
    if (eng.isPreparingSettings())
    {
        statusText = recordingNow ? "Capturing audio; reading recording settings..." : "Reading recording settings...";
        progress = 0.0f;
        statusColour = kAccent;
    }
    const auto warning = eng.getTransportWarning();
    if (warning.isNotEmpty() && ! recordingNow && state != RecordingEngine::State::Error)
    {
        statusText = warning;
        statusColour = kSubtext;
    }
    if (settingsMessage.isNotEmpty() && ! recordingNow && state != RecordingEngine::State::Encoding
        && state != RecordingEngine::State::Error)
    {
        statusText = settingsMessage;
        statusColour = settingsFailed ? kError : kSubtext;
    }
    if (association.problem.isNotEmpty() && ! recordingNow && state != RecordingEngine::State::Encoding
        && state != RecordingEngine::State::Error)
    {
        statusText = "Project search unavailable; check Settings or Associate a file";
        statusColour = kError;
    }
    if (options.locked && state != RecordingEngine::State::Error) statusText = "Preview updates locked; unlock to record";
    else if (options.manual && state == RecordingEngine::State::Idle) statusText = eng.isCaptureArmed() ? "Armed: play from any position" : "Choose a position and arm the capture";
    statusLabel.setTooltip (statusText + (settingsMessage.isNotEmpty() ? "\n" + settingsMessage : juce::String{})
        + (association.problem.isNotEmpty() ? "\n" + association.problem : juce::String{})
        + "\nSkipped captures: " + juce::String (eng.getDroppedCaptures())
        + "; short captures: " + juce::String (eng.getShortCaptures())
        + "; unknown positions: " + juce::String (eng.getUnknownPositions()));

    if (statusText != lastStatusText || stInt != lastStateInt)
    {
        statusLabel.setText (statusText, juce::dontSendNotification);
        RememberUI::bindColour (statusLabel, juce::Label::textColourId, statusColour);
        lastStatusText = statusText;
    }

    // Repaint only when progress visibly changed (>= 1% of bar width)
    if (std::abs (progress - lastProgress) > 0.01f || stInt != lastStateInt)
    {
        recordProgress = progress;
        lastProgress   = progress;
        repaint();
    }

    lastStateInt = stInt;
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void RememberEditor::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (kBgDeep));
    g.setFont (titleFont());
    g.setColour (RememberUI::colour (kText));
    g.drawText ("Remember", 24, 16, 210, 28, juce::Justification::centredLeft);
    g.setFont (bodyFont());
    g.setColour (RememberUI::colour (kSubtext));
    g.drawText ("Capture the idea. Keep the moment.", 24, 46, getWidth() - 48, 22, juce::Justification::centredLeft);
    RememberUI::panel (g, juce::Rectangle<float> (16, 84, (float) getWidth() - 32, 140));
    g.setFont (bodyFont());
    g.setColour (RememberUI::colour (kSubtext));
    g.drawText ("Project", 28, 96, 160, 18, juce::Justification::centredLeft);
    g.setColour (RememberUI::colour (kBorder));
    g.drawHorizontalLine (228, 28, (float) getWidth() - 28);
    paintProgressBar (g, { 28, 304, getWidth() - 56, 5 }, recordProgress);
    g.setFont (RememberUI::font (12));
    g.setColour (RememberUI::colour (kSubtext));
    g.drawText (processor.getEngine().getCaptureOptions().manual ? "Manual preview" : "Automatic preview", 28, 326, 160, 22, juce::Justification::centredLeft);
}

void RememberEditor::paintProgressBar (juce::Graphics& g,
                                       juce::Rectangle<int> bounds,
                                       float progress)
{
    // Track
    g.setColour (RememberUI::colour (kElevated));
    g.fillRoundedRectangle (bounds.toFloat(), 2.0f);

    // Fill
    if (progress > 0.0f)
    {
        auto fill = bounds.toFloat().withWidth (bounds.getWidth() * progress);
        g.setColour (RememberUI::colour (kAccent));
        g.fillRoundedRectangle (fill, 2.0f);
    }
}

// ── Layout ────────────────────────────────────────────────────────────────────

void RememberEditor::resized()
{
    settingsButton.setBounds (getWidth() - 60, 16, 36, 36);
    nameEditor.setBounds (28, 120, getWidth() - 56, 38);
    associationLabel.setBounds (28, 171, getWidth() - 152, 36);
    associationButton.setBounds (getWidth() - 116, 173, 88, 32);
    bpmKeyLabel.setBounds (28, 239, getWidth() - 56, 24);
    statusLabel.setBounds (28, 264, getWidth() - 56, 32);
    manualToggle.setBounds (28, 348, getWidth() - 56, 30);
    lockToggle.setBounds (28, 378, getWidth() - 56, 30);
    replaceToggle.setBounds (28, 408, getWidth() - 56, 30);
    captureButton.setBounds (28, 446, 174, 32);
    versionLabel.setBounds (getWidth() - 200, 455, 172, 18);
}

// ── Settings ──────────────────────────────────────────────────────────────────

void RememberEditor::showSettingsPanel()
{
    if (settingsRequest) return;
    juce::PopupMenu menu, appearance;
    menu.setLookAndFeel (&getLookAndFeel());
    appearance.addItem (2, "Light", true, ! RememberUI::isDark());
    appearance.addItem (3, "Dark", true, RememberUI::isDark());
    menu.addSubMenu ("Appearance", appearance);
    menu.addSeparator();
    menu.addItem (1, "Change previews output folder...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton),
        [safe = juce::Component::SafePointer<RememberEditor> (this)] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1) safe->chooseOutputFolder();
            else if (result == 2 || result == 3) safe->applyAppearance (result == 3);
        });
}

void RememberEditor::applyAppearance (bool dark)
{
    if (settingsRequest) return;
    settingsMessage = "Saving appearance...";
    settingsFailed = false;
    settingsButton.setEnabled (false);
    settingsRequest = SettingsManager::getInstance().setAppearanceAsync (dark);
    refreshFromEngine();
}

void RememberEditor::chooseOutputFolder()
{
    if (settingsRequest) return;

    fileChooser = std::make_unique<juce::FileChooser> (
        "Select previews output folder",
        settingsFolder != juce::File{} ? settingsFolder : juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<RememberEditor> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResults().isEmpty()) return;
            safe->applyOutputFolder (fc.getResult());
        });
}

void RememberEditor::applyOutputFolder (const juce::File& folder)
{
    if (settingsRequest) return;
    savingSettings = true; settingsFailed = false;
    settingsMessage = "Saving output folder...";
    settingsButton.setEnabled (false);
    settingsRequest = SettingsManager::getInstance().setPreviewsFolderAsync (folder);
    refreshFromEngine();
}

void RememberEditor::refreshSettingsRequest()
{
    if (! editorReady || ! settingsRequest || ! settingsRequest->complete.load (std::memory_order_acquire)) return;
    const auto request = std::move (settingsRequest);
    settingsButton.setEnabled (true);
    const bool wasInitial = initialSettings; initialSettings = false;
    settingsFailed = request->result.failed();
    if (settingsFailed) settingsMessage = request->result.getErrorMessage();
    else
    {
        settingsFolder = request->snapshot.previews;
        processor.getEngine().applySharedCaptureOptions (request->snapshot.capture);
        RememberUI::setDark (request->snapshot.darkAppearance);
        if (savingSettings)
        {
            processor.getEngine().setOutputFolder (settingsFolder);
            settingsMessage = "Output saved; reload plugin to apply";
        }
        else settingsMessage.clear();
    }
    savingSettings = false;
    if (wasInitial && ! request->configured) chooseOutputFolder();
}

void RememberEditor::showAssociationMenu()
{
    const auto info = processor.getEngine().getAssociationInfo();
    juce::PopupMenu menu;
    menu.addItem (1, "Select project file...");
    menu.addItem (2, "Keep recording without a file", ! info.confirmed || info.source == RecordingEngine::Association::ManualRecording);
    menu.addSeparator();
    menu.addItem (3, "Use automatic association");
    const auto name = nameEditor.getText();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&associationButton),
        [safe = juce::Component::SafePointer<RememberEditor> (this), id = info.id, name] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1) safe->chooseProjectFile (id);
            else if (result == 2) safe->processor.getEngine().keepRecording (name, id);
            else if (result == 3) safe->processor.getEngine().useAutomaticAssociation (id);
        });
}
void RememberEditor::chooseProjectFile (const juce::String& expectedId)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Associate this recording with a DAW project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.flp;*.als;*.rpp;*.cpr;*.npr;*.song;*.ptx;*.bwproject;*.logicx");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<RememberEditor> (this), expectedId] (const juce::FileChooser& chooser)
        {
            if (safe != nullptr && ! chooser.getResults().isEmpty())
                safe->processor.getEngine().associateProjectFile (chooser.getResult(), expectedId);
        });
}

void RememberEditor::saveCaptureOptions()
{
    if (settingsRequest) return;
    settingsMessage = "Saving shared recording options...";
    settingsFailed = false;
    settingsButton.setEnabled (false);
    settingsRequest = SettingsManager::getInstance().setCaptureOptionsAsync (
        { manualToggle.getToggleState(), lockToggle.getToggleState(), replaceToggle.getToggleState() });
}
