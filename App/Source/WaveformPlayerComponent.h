#pragma once
#include <JuceHeader.h>
#include "ProjectScanner.h"

class WaveformPlayerComponent : public juce::Component,
                                public juce::ChangeListener,
                                public juce::Timer
{
public:
    WaveformPlayerComponent();
    ~WaveformPlayerComponent() override;

    void loadEntry (const ProjectEntry& entry);
    void stop();
    void play();         // start playback if a preview is loaded
    void togglePlay();   // public wrapper for keyboard shortcut

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

private:
    void   seekTo (double normalizedPos);
    double getPositionNormalized() const;
    void   updateTimeLabel();

    juce::AudioDeviceManager        deviceManager;
    juce::AudioFormatManager        formatManager;
    juce::AudioTransportSource      transport;
    juce::AudioSourcePlayer         sourcePlayer;
    juce::AudioThumbnailCache       thumbCache { 16 };
    juce::AudioThumbnail            thumbnail;

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::FileChooser>             fileChooser;

    ProjectEntry currentEntry;
    bool         isLoaded { false };

    // Buttons
    juce::TextButton playBtn   { ">" };
    juce::TextButton openBtn   { "Open in DAW" };
    juce::TextButton assignBtn { "Assign path" };

    juce::Label nameLabel;
    juce::Label pathLabel;
    juce::Label timeLabel;

    // Waveform area cached bounds (set in resized)
    juce::Rectangle<int> waveArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPlayerComponent)
};
