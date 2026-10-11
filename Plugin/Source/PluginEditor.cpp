#include "PluginEditor.h"
#include "SettingsManager.h"
#include "BuildInfo.h"

namespace
{
    juce::Font bodyFont()  { return juce::Font (juce::FontOptions{}.withHeight (12.5f)); }
    juce::Font titleFont() { return juce::Font (juce::FontOptions{}.withHeight (15.0f).withStyle ("Bold")); }
    juce::Font monoFont()  { return juce::Font (juce::FontOptions{}.withHeight (13.0f).withStyle ("Bold")); }
}

RememberEditor::RememberEditor (RememberProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (360, 220);

    // ── Project name editor ──────────────────────────────────────────────────
    nameEditor.setFont (bodyFont());
    nameEditor.setText (processor.getProjectName(), false);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (kElevated));
    nameEditor.setColour (juce::TextEditor::textColourId,       juce::Colour (kText));
    nameEditor.setColour (juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (kAccent));
    nameEditor.onReturnKey  = [this] { processor.setProjectName (nameEditor.getText()); nameEditor.unfocusAllComponents(); };
    nameEditor.onFocusLost  = [this] { processor.setProjectName (nameEditor.getText()); };
    addAndMakeVisible (nameEditor);

    // ── BPM / Key combined label ─────────────────────────────────────────────
    bpmKeyLabel.setFont (monoFont());
    bpmKeyLabel.setColour (juce::Label::textColourId, juce::Colour (kAccent));
    bpmKeyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (bpmKeyLabel);

    // ── Status ───────────────────────────────────────────────────────────────
    statusLabel.setFont (bodyFont());
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (kSubtext));
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    // ── Settings button ───────────────────────────────────────────────────────
    settingsButton.setTooltip ("Change output folder");
    settingsButton.setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
    settingsButton.setColour (juce::TextButton::textColourOffId, juce::Colour (kSubtext));
    settingsButton.onClick = [this] { showSettingsPanel(); };
    addAndMakeVisible (settingsButton);
    associationLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f)));
    associationLabel.setColour (juce::Label::textColourId, juce::Colour (kSubtext));
    addAndMakeVisible (associationLabel);
    associationButton.setColour (juce::TextButton::buttonColourId, juce::Colour (kElevated));
    associationButton.setColour (juce::TextButton::textColourOffId, juce::Colour (kText));
    associationButton.setTooltip ("Select a DAW file, keep a recording without a file, or return to automatic association");
    associationButton.onClick = [this] { showAssociationMenu(); };
    addAndMakeVisible (associationButton);

    // ── Version footer ────────────────────────────────────────────────────────
    versionLabel.setText (juce::String ("v") + REMEMBER_VERSION + "  " + REMEMBER_GIT_HASH,
                          juce::dontSendNotification);
    versionLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (9.5f)));
    versionLabel.setColour (juce::Label::textColourId, juce::Colour (kSubtext).withAlpha (0.6f));
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
    juce::String bpmKeyText = meta.isEmpty() ? "-" : meta;
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
    statusLabel.setTooltip (statusText + (settingsMessage.isNotEmpty() ? "\n" + settingsMessage : juce::String{})
        + (association.problem.isNotEmpty() ? "\n" + association.problem : juce::String{})
        + "\nSkipped captures: " + juce::String (eng.getDroppedCaptures())
        + "; short captures: " + juce::String (eng.getShortCaptures())
        + "; unknown positions: " + juce::String (eng.getUnknownPositions()));

    if (statusText != lastStatusText || stInt != lastStateInt)
    {
        statusLabel.setText (statusText, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (statusColour));
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
    // Background
    g.fillAll (juce::Colour (kBgDeep));

    // Header strip
    g.setColour (juce::Colour (kSurface));
    g.fillRect (0, 0, getWidth(), 36);

    // Title
    g.setFont (titleFont());
    g.setColour (juce::Colour (kAccent));
    g.drawText ("REMEMBER", 14, 0, 140, 36, juce::Justification::centredLeft);

    // Separator below header
    g.setColour (juce::Colour (kBorder));
    g.fillRect (0, 36, getWidth(), 1);

    // "Project" label above editor
    g.setFont (bodyFont());
    g.setColour (juce::Colour (kSubtext));
    g.drawText ("Project", 14, 44, 80, 14, juce::Justification::centredLeft);

    // Progress bar
    if (recordProgress > 0.0f || isRecording)
    {
        auto barBounds = juce::Rectangle<int> (14, 163, getWidth() - 28, 4);
        paintProgressBar (g, barBounds, recordProgress);
    }
}

void RememberEditor::paintProgressBar (juce::Graphics& g,
                                       juce::Rectangle<int> bounds,
                                       float progress)
{
    // Track
    g.setColour (juce::Colour (kElevated));
    g.fillRoundedRectangle (bounds.toFloat(), 2.0f);

    // Fill
    if (progress > 0.0f)
    {
        auto fill = bounds.toFloat().withWidth (bounds.getWidth() * progress);
        g.setColour (juce::Colour (kAccent));
        g.fillRoundedRectangle (fill, 2.0f);
    }
}

// ── Layout ────────────────────────────────────────────────────────────────────

void RememberEditor::resized()
{
    // Settings gear — top-right of header
    settingsButton.setBounds (getWidth() - 36, 0, 36, 36);

    // Project name editor — below "Project" label
    nameEditor.setBounds (14, 60, getWidth() - 28, 26);

    // BPM / Key — below editor
    associationLabel.setBounds (14, 92, getWidth() - 112, 20);
    associationButton.setBounds (getWidth() - 92, 90, 78, 24);
    bpmKeyLabel.setBounds (14, 121, getWidth() - 28, 26);

    // Status — below progress bar
    statusLabel.setBounds (14, 174, getWidth() - 28, 24);

    // Version footer — bottom-right corner, very small
    versionLabel.setBounds (getWidth() - 158, getHeight() - 14, 154, 12);
}

// ── Settings ──────────────────────────────────────────────────────────────────

void RememberEditor::showSettingsPanel()
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
        if (savingSettings)
        {
            processor.getEngine().setOutputFolder (settingsFolder);
            settingsMessage = "Output saved; reload plugin to apply";
        }
        else settingsMessage.clear();
    }
    savingSettings = false;
    if (wasInitial && ! request->configured) showSettingsPanel();
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
