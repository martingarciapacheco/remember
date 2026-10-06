#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SettingsManager.h"
#include "RememberLogger.h"
#include "BuildInfo.h"

RememberProcessor::RememberProcessor (RecordingEngine::ProjectDetector detector)
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      engine (std::move (detector))
{
    RememberLogger::ensureInitialised ("plugin",
        juce::String ("Plugin ") + REMEMBER_VERSION_LINE);

    juce::Logger::writeToLog (juce::String ("Processor created. Host: ")
        + juce::PluginHostType().getHostDescription());

    engine.setOutputFolder (SettingsManager::getInstance().getPreviewsFolder());
}

void RememberProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.setOutputFolder (SettingsManager::getInstance().getPreviewsFolder());
    juce::Logger::writeToLog ("Processor prepared at " + juce::String (sampleRate, 0) + " Hz.");
}

void RememberProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;
    engine.processBlock (buffer, getPlayHead(), isNonRealtime());
    // Plugin is transparent — audio passes through unchanged
}

void RememberProcessor::setProjectName (const juce::String& name)
{
    engine.setProjectName (name);
}

// ── State persistence ─────────────────────────────────────────────────────────

void RememberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("RememberState");
    engine.saveState (xml);
    xml.setAttribute ("previewsFolder", SettingsManager::getInstance()
                                            .getPreviewsFolder().getFullPathName());
    copyXmlToBinary (xml, destData);
}

void RememberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || xml->getTagName() != "RememberState")
        return;

    // Shared current settings win over old folders embedded in DAW templates.
    engine.setOutputFolder (SettingsManager::getInstance().getPreviewsFolder());
    engine.restoreState (*xml);
    juce::Logger::writeToLog ("Processor state restored.");
}

// ── Factory ───────────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* RememberProcessor::createEditor()
{
    auto* editor = new RememberEditor (*this);
    juce::Logger::writeToLog ("Plugin editor created.");
    return editor;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RememberProcessor();
}
