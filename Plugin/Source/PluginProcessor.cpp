#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SettingsManager.h"
#include "../../Shared/LibraryStore.h"

RememberProcessor::RememberProcessor (RecordingEngine::ProjectDetector detector, SettingsManager::Reader reader)
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      settingsReader (reader ? std::move (reader) : [] { return SettingsManager::getInstance().readSnapshot(); }),
      engine (std::move (detector))
{
    engine.loadOutputFolderFromSettings (settingsReader);
}

void RememberProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepareAsync (sampleRate, samplesPerBlock, settingsReader);
    engine.logDiagnostic ("Processor prepared at " + juce::String (sampleRate, 0) + " Hz.");
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
    copyXmlToBinary (xml, destData);
}

void RememberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || xml->getTagName() != "RememberState")
        return;

    // Shared current settings win over old folders embedded in DAW templates.
    engine.restoreState (*xml, settingsReader);
}

// ── Factory ───────────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* RememberProcessor::createEditor()
{
    auto* editor = new RememberEditor (*this);
    engine.logDiagnostic ("Plugin editor created.");
    return editor;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    // Resolve/validate an explicitly requested validation profile before any
    // processor worker can initialise settings or a logger in this DLL.
    LibraryStore::dataDirectory();
    return new RememberProcessor();
}
