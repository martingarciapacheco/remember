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
    setSize (320, 190);

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

    // ── Version footer ────────────────────────────────────────────────────────
    versionLabel.setText (juce::String ("v") + REMEMBER_VERSION + "  " + REMEMBER_GIT_HASH,
                          juce::dontSendNotification);
    versionLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (9.5f)));
    versionLabel.setColour (juce::Label::textColourId, juce::Colour (kSubtext).withAlpha (0.6f));
    versionLabel.setJustificationType (juce::Justification::centredRight);
    versionLabel.setInterceptsMouseClicks (false, false);
    versionLabel.setTooltip (REMEMBER_VERSION_LINE);
    addAndMakeVisible (versionLabel);

    if (! SettingsManager::getInstance().isConfigured())
        juce::MessageManager::callAsync ([this] { showSettingsPanel(); });

    startTimerHz (2);   // Idle rate; bumped to 10Hz while Recording
    refreshFromEngine();
}

RememberEditor::~RememberEditor() { stopTimer(); }

// ── Timer ─────────────────────────────────────────────────────────────────────

void RememberEditor::timerCallback()
{
    refreshFromEngine();
}

void RememberEditor::refreshFromEngine()
{
    auto& eng   = processor.getEngine();
    auto  state = eng.getState();
    int   stInt = (int) state;

    // Adapt poll rate: 10Hz only while Recording, 2Hz otherwise
    int desiredHz = (state == RecordingEngine::State::Recording
                     || state == RecordingEngine::State::Encoding) ? 10 : 2;
    if (getTimerInterval() != 1000 / desiredHz)
        startTimerHz (desiredHz);

    if (! nameEditor.hasKeyboardFocus (false))
    {
        auto name = processor.getProjectName();
        if (name != lastNameText)
        {
            nameEditor.setText (name, false);
            lastNameText = name;
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

        case RecordingEngine::State::Error:
            progress     = 0.0f;
            statusText   = "Error - check output folder in settings";
            statusColour = kError;
            break;
    }

    isRecording = recordingNow;

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
        auto barBounds = juce::Rectangle<int> (14, 140, getWidth() - 28, 4);
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
    bpmKeyLabel.setBounds (14, 98, getWidth() - 28, 26);

    // Status — below progress bar
    statusLabel.setBounds (14, 150, getWidth() - 28, 24);

    // Version footer — bottom-right corner, very small
    versionLabel.setBounds (getWidth() - 158, getHeight() - 14, 154, 12);
}

// ── Settings ──────────────────────────────────────────────────────────────────

void RememberEditor::showSettingsPanel()
{
    auto cur = SettingsManager::getInstance().getPreviewsFolder();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Select previews output folder",
        cur.exists() ? cur : juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            if (fc.getResults().isEmpty()) return;
            auto sel = fc.getResult();
            SettingsManager::getInstance().setPreviewsFolder (sel);
            processor.getEngine().setOutputFolder (sel);
            statusLabel.setText ("Output: " + sel.getFullPathName(),
                                 juce::dontSendNotification);
        });
}
