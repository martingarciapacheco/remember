#include "SettingsManager.h"

SettingsManager& SettingsManager::getInstance()
{
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
{
    auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    configFile   = appData.getChildFile ("Remember").getChildFile ("config.json");
    load();
}

void SettingsManager::load()
{
    if (! configFile.existsAsFile())
        return;

    auto json = juce::JSON::parse (configFile.loadFileAsString());
    if (! json.isObject())
        return;

    auto path = json.getProperty ("previewsFolder", "").toString();
    if (path.isNotEmpty())
        previewsFolder = juce::File (path);
}

void SettingsManager::save()
{
    configFile.getParentDirectory().createDirectory();

    // Preserve all fields owned by other consumers (e.g. the browser app's
    // "projectsRootFolder") — only mutate the one we care about.
    auto existing = configFile.existsAsFile()
                        ? juce::JSON::parse (configFile.loadFileAsString())
                        : juce::var{};

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    if (existing.isObject())
        for (auto& prop : existing.getDynamicObject()->getProperties())
            obj->setProperty (prop.name, prop.value);

    obj->setProperty ("previewsFolder", previewsFolder.getFullPathName());

    configFile.replaceWithText (juce::JSON::toString (juce::var (obj.get())));
}

juce::File SettingsManager::getPreviewsFolder() const
{
    return previewsFolder;
}

void SettingsManager::setPreviewsFolder (const juce::File& folder)
{
    previewsFolder = folder;
    save();
}

bool SettingsManager::isConfigured() const
{
    return previewsFolder != juce::File{} && previewsFolder.isDirectory();
}
