#include "WaveformPlayerComponent.h"
#include "ProjectListComponent.h"   // for Pal::

WaveformPlayerComponent::WaveformPlayerComponent()
    : thumbnail (512, formatManager, thumbCache)
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener (this);

    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&sourcePlayer);
    sourcePlayer.setSource (&transport);

    auto btnSetup = [&] (juce::TextButton& b, uint32_t bg, uint32_t fg)
    {
        b.setColour (juce::TextButton::buttonColourId,  juce::Colour (bg));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (fg));
    };

    btnSetup (playBtn,   Pal::Accent,   0xffffffff);
    btnSetup (openBtn,   Pal::Elevated, Pal::Text);
    btnSetup (assignBtn, Pal::Elevated, Pal::Subtext);

    playBtn.onClick  = [this] { togglePlay(); };

    openBtn.onClick  = [this]
    {
        if (currentEntry.projectPath.isNotEmpty())
            juce::File (currentEntry.projectPath).startAsProcess();
    };

    assignBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select project file",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.als;*.flp;*.ptx;*.logic;*.song;*.npr;*.rpp;*.cpr;*.ptx");

        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResults().isEmpty()) return;
                currentEntry.projectPath = fc.getResult().getFullPathName();
                currentEntry.save();
                pathLabel.setText (currentEntry.projectPath, juce::dontSendNotification);
            });
    };

    nameLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (14.0f).withStyle ("Bold")));
    nameLabel.setColour (juce::Label::textColourId, juce::Colour (Pal::Text));

    pathLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (10.5f)));
    pathLabel.setColour (juce::Label::textColourId, juce::Colour (Pal::Subtext));

    timeLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f)));
    timeLabel.setColour (juce::Label::textColourId, juce::Colour (Pal::Subtext));
    timeLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (playBtn);
    addAndMakeVisible (openBtn);
    addAndMakeVisible (assignBtn);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (pathLabel);
    addAndMakeVisible (timeLabel);

    // Timer starts only when playback begins (saves CPU while idle)
}

WaveformPlayerComponent::~WaveformPlayerComponent()
{
    stopTimer();
    transport.stop();
    transport.setSource (nullptr);
    sourcePlayer.setSource (nullptr);
    deviceManager.removeAudioCallback (&sourcePlayer);
    thumbnail.removeChangeListener (this);
}

// ── Public API ────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::loadEntry (const ProjectEntry& entry)
{
    stop();
    currentEntry = entry;
    isLoaded     = false;

    nameLabel.setText (entry.name, juce::dontSendNotification);
    pathLabel.setText (entry.pending ? "Unsaved project - save in the DAW to keep this audio"
                       : entry.projectPath.isEmpty()
                           ? "(no project file - press Assign path)"
                           : entry.projectPath,
                       juce::dontSendNotification);
    timeLabel.setText ("", juce::dontSendNotification);

    openBtn.setEnabled (entry.projectPath.isNotEmpty());
    assignBtn.setEnabled (! entry.pending);

    auto* reader = formatManager.createReaderFor (entry.previewOgg);
    if (reader == nullptr) return;

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transport.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
    thumbnail.setSource (new juce::FileInputSource (entry.previewOgg));
    isLoaded = true;

    playBtn.setButtonText (">");
    repaint();
}

void WaveformPlayerComponent::stop()
{
    transport.stop();
    transport.setPosition (0.0);
    playBtn.setButtonText (">");
    stopTimer();
}

// ── Playback ─────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::togglePlay()
{
    if (! isLoaded) return;
    if (transport.isPlaying())
    {
        transport.stop();
        playBtn.setButtonText (">");
        stopTimer();
    }
    else
    {
        if (transport.getCurrentPosition() >= transport.getLengthInSeconds() - 0.05)
            transport.setPosition (0.0);
        transport.start();
        playBtn.setButtonText ("||");
        startTimerHz (24);
    }
}

void WaveformPlayerComponent::play()
{
    if (! isLoaded || transport.isPlaying()) return;
    togglePlay();
}

void WaveformPlayerComponent::seekTo (double norm)
{
    if (isLoaded)
        transport.setPosition (juce::jlimit (0.0, transport.getLengthInSeconds(), norm * transport.getLengthInSeconds()));
}

double WaveformPlayerComponent::getPositionNormalized() const
{
    double len = transport.getLengthInSeconds();
    return len > 0.0 ? transport.getCurrentPosition() / len : 0.0;
}

void WaveformPlayerComponent::updateTimeLabel()
{
    if (! isLoaded) return;
    auto toStr = [] (double s)
    {
        int m = (int) s / 60, sec = (int) s % 60;
        return juce::String::formatted ("%d:%02d", m, sec);
    };
    timeLabel.setText (toStr (transport.getCurrentPosition()) + " / " +
                       toStr (transport.getLengthInSeconds()),
                       juce::dontSendNotification);
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::changeListenerCallback (juce::ChangeBroadcaster*) { repaint(); }

void WaveformPlayerComponent::timerCallback()
{
    if (transport.isPlaying())
    {
        updateTimeLabel();
        repaint();
    }

    if (transport.hasStreamFinished())
    {
        transport.setPosition (0.0);
        playBtn.setButtonText (">");
        stopTimer();
    }
}

void WaveformPlayerComponent::mouseDown (const juce::MouseEvent& e)
{
    if (waveArea.contains (e.getPosition()))
    {
        seekTo ((double) (e.x - waveArea.getX()) / (double) waveArea.getWidth());
        updateTimeLabel();
        repaint();
    }
}

void WaveformPlayerComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Only scrub if the drag started inside the waveform area — and clamp the
    // x position so dragging outside the bounds pins the playhead to the edge.
    if (! waveArea.contains (e.getMouseDownPosition())) return;

    int x = juce::jlimit (waveArea.getX(), waveArea.getRight(), e.x);
    double norm = (double) (x - waveArea.getX()) / (double) waveArea.getWidth();
    seekTo (norm);
    updateTimeLabel();
    repaint();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Pal::Surface));

    if (! isLoaded) return;

    // Waveform background
    g.setColour (juce::Colour (Pal::BgDeep));
    g.fillRoundedRectangle (waveArea.toFloat(), 4.0f);

    if (thumbnail.getTotalLength() > 0.0)
    {
        // Waveform fill
        g.setColour (juce::Colour (Pal::Accent).withAlpha (0.7f));
        thumbnail.drawChannels (g, waveArea, 0.0, thumbnail.getTotalLength(), 0.9f);

        // Playhead
        float posX = waveArea.getX() + (float) getPositionNormalized() * waveArea.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawLine (posX, (float) waveArea.getY(), posX, (float) waveArea.getBottom(), 1.5f);
    }
}

// ── Layout ─────────────────────────────────────────────────────────────────────

void WaveformPlayerComponent::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    // Top row: play + name + time
    auto topRow = area.removeFromTop (26);
    playBtn.setBounds   (topRow.removeFromLeft (34).reduced (0, 2));
    topRow.removeFromLeft (8);
    timeLabel.setBounds (topRow.removeFromRight (80));
    nameLabel.setBounds (topRow);

    area.removeFromTop (6);

    // Bottom row: open + assign + path
    auto btmRow = area.removeFromBottom (26);
    openBtn.setBounds   (btmRow.removeFromRight (110).reduced (2, 0));
    btmRow.removeFromRight (6);
    assignBtn.setBounds (btmRow.removeFromRight (130).reduced (2, 0));
    pathLabel.setBounds (btmRow);

    area.removeFromBottom (6);

    // Waveform occupies the middle
    waveArea = area;
}
