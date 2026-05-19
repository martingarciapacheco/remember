#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SettingsManager.h"
#include "RememberLogger.h"
#include "BuildInfo.h"

RememberProcessor::RememberProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
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
    engine.setProjectName  (projectName);
}

void RememberProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;
    engine.processBlock (buffer, getPlayHead());
    // Plugin is transparent — audio passes through unchanged
}

void RememberProcessor::updateTrackProperties (const TrackProperties& properties)
{
    // Ableton and other hosts call this with the track/instance name
    // In JUCE 8+, TrackProperties::name is std::optional<String>
    if (properties.name.has_value() && properties.name->isNotEmpty() && projectName.isEmpty())
        setProjectName (*properties.name);
}

void RememberProcessor::setProjectName (const juce::String& name)
{
    // Persist the rename into the existing metadata.json BEFORE we change our
    // internal name — otherwise we'd look up the new folder, which doesn't exist.
    if (! name.isEmpty() && name != projectName)
        engine.writePersistedProjectName (name);

    projectName = name;
    engine.setProjectName (name);
}

// ── State persistence ─────────────────────────────────────────────────────────

void RememberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("RememberState");
    xml.setAttribute ("projectName",    projectName);
    xml.setAttribute ("previewsFolder", SettingsManager::getInstance()
                                            .getPreviewsFolder().getFullPathName());
    copyXmlToBinary (xml, destData);
}

void RememberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || xml->getTagName() != "RememberState")
        return;

    // Restore project name from the DAW project state
    auto savedName = xml->getStringAttribute ("projectName");
    if (savedName.isNotEmpty())
    {
        // Assign directly — we DON'T want setProjectName's side effect of
        // rewriting metadata.json with the (potentially stale) DAW name.
        projectName = savedName;
        engine.setProjectName (savedName);
    }

    // Honour a previews folder set from inside the plugin
    auto savedFolder = xml->getStringAttribute ("previewsFolder");
    if (savedFolder.isNotEmpty())
    {
        juce::File f (savedFolder);
        if (f.isDirectory())
        {
            SettingsManager::getInstance().setPreviewsFolder (f);
            engine.setOutputFolder (f);
        }
    }

    // The user may have renamed this project from the browser app between DAW
    // sessions. metadata.json is the source of truth — pull the latest name.
    auto persistedName = engine.readPersistedProjectName();
    if (persistedName.isNotEmpty() && persistedName != projectName)
    {
        projectName = persistedName;
        engine.setProjectName (persistedName);
    }
}

// ── Factory ───────────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* RememberProcessor::createEditor()
{
    return new RememberEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RememberProcessor();
}
