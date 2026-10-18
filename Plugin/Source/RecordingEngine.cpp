#include "RecordingCore.h"

RecordingEngine::RecordingEngine (ProjectDetector detector) : core (std::make_shared<Core> (std::move (detector))) {}
RecordingEngine::~RecordingEngine() { BackgroundRetirement::retire (std::move (core)); }
void RecordingEngine::prepare (double rate, int block) { core->prepare (rate, block); }
void RecordingEngine::prepare (double rate, int block, const SettingsManager::Snapshot& settings) { core->prepare (rate, block, settings); }
void RecordingEngine::prepareAsync (double rate, int block, SettingsManager::Reader reader) { core->prepareAsync (rate, block, std::move (reader)); }
bool RecordingEngine::isPreparingSettings() const { return core->isPreparingSettings(); }
void RecordingEngine::finishCapture() { core->finishCapture(); }
void RecordingEngine::setCaptureOptions (CaptureOptions value) { core->setCaptureOptions (value); }
void RecordingEngine::applySharedCaptureOptions (CaptureOptions value) { core->applySharedCaptureOptions (value); }
CaptureOptions RecordingEngine::getCaptureOptions() const { return core->getCaptureOptions(); }
bool RecordingEngine::isPreviewLocked() const { return core->isPreviewLocked(); }
void RecordingEngine::processBlock (const juce::AudioBuffer<float>& audio, juce::AudioPlayHead* head, bool offline) { core->processBlock (audio, head, offline); }
void RecordingEngine::setOutputFolder (const juce::File& folder) { core->setOutputFolder (folder); }
void RecordingEngine::loadOutputFolderFromSettings (SettingsManager::Reader reader) { core->loadOutputFolderFromSettings (std::move (reader)); }
void RecordingEngine::logDiagnostic (juce::String message) { core->logDiagnostic (std::move (message)); }
void RecordingEngine::setProjectName (const juce::String& name) { core->setProjectName (name); }
void RecordingEngine::associateProjectFile (const juce::File& file, const juce::String& id) { core->associateProjectFile (file, id); }
void RecordingEngine::keepRecording (const juce::String& name, const juce::String& id) { core->keepRecording (name, id); }
void RecordingEngine::useAutomaticAssociation (const juce::String& id) { core->useAutomaticAssociation (id); }
RecordingEngine::AssociationInfo RecordingEngine::getAssociationInfo() const { return core->getAssociationInfo(); }
juce::String RecordingEngine::getProjectName() const { return core->getProjectName(); }
bool RecordingEngine::isProjectConfirmed() const { return core->isProjectConfirmed(); }
void RecordingEngine::saveState (juce::XmlElement& xml) const { core->saveState (xml); }
void RecordingEngine::restoreState (const juce::XmlElement& xml, SettingsManager::Reader reader) { core->restoreState (xml, std::move (reader)); }
RecordingEngine::State RecordingEngine::getState() const noexcept { return core->getState(); }
float RecordingEngine::getProgress() const noexcept { return core->getProgress(); }
juce::String RecordingEngine::getDetectedKey() const { return core->getDetectedKey(); }
double RecordingEngine::getDetectedBpm() const noexcept { return core->getDetectedBpm(); }
juce::String RecordingEngine::getLastSavedTime() const { return core->getLastSavedTime(); }
juce::String RecordingEngine::getLastError() const { return core->getLastError(); }
uint64_t RecordingEngine::getDroppedCaptures() const noexcept { return core->getDroppedCaptures(); }
uint64_t RecordingEngine::getShortCaptures() const noexcept { return core->getShortCaptures(); }
uint64_t RecordingEngine::getUnknownPositions() const noexcept { return core->getUnknownPositions(); }
size_t RecordingEngine::getReservedCaptureBytes() const noexcept { return core->getReservedCaptureBytes(); }
size_t RecordingEngine::getProcessCaptureBytes() noexcept { return Core::getProcessCaptureBytes(); }
juce::String RecordingEngine::getTransportWarning() const { return core->getTransportWarning(); }
int RecordingEngine::getTotalSeconds() const noexcept { return core->getTotalSeconds(); }
