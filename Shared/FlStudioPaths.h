#pragma once
#include <JuceHeader.h>

namespace FlStudioPaths
{
// Worker-only discovery. Image-Line documents the macOS installation root at
// FL Studio.app/Contents/Resources/FL; never search entire disks by sample name.
inline juce::Array<juce::File> macFactoryDataDirectories (const juce::Array<juce::File>& applicationRoots)
{
    juce::Array<juce::File> roots;
    for (const auto& folder : applicationRoots)
    {
        const auto apps = folder.findChildFiles (juce::File::findDirectories, false, "FL Studio*.app");
        for (int i = 0; i < juce::jmin (64, apps.size()); ++i)
        {
            const auto data = apps[i].getChildFile ("Contents/Resources/FL/Data");
            if (data.isDirectory()) roots.addIfNotAlreadyThere (data);
        }
    }
    return roots;
}

inline juce::Array<juce::File> factoryDataDirectories()
{
   #if JUCE_MAC
    return macFactoryDataDirectories ({ juce::File ("/Applications"),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Applications") });
   #else
    juce::Array<juce::File> roots;
    for (const auto* variable : { "ProgramFiles", "ProgramFiles(x86)" })
    {
        const auto path = juce::SystemStats::getEnvironmentVariable (variable, {});
        if (path.isEmpty()) continue;
        const auto installs = juce::File (path).getChildFile ("Image-Line").findChildFiles (
            juce::File::findDirectories, false, "FL Studio*");
        for (int i = 0; i < juce::jmin (64, installs.size()); ++i)
            roots.addIfNotAlreadyThere (installs[i].getChildFile ("Data"));
    }
    return roots;
   #endif
}
}
