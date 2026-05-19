#pragma once
#include <JuceHeader.h>

// Singleton-style helper that wires a juce::FileLogger as the global logger
// the first time it's used. Logs go to:
//
//   %APPDATA%/Remember/logs/<name>.log
//
// 1 MB cap (FileLogger rolls the file once it exceeds that).
namespace RememberLogger
{
    inline void ensureInitialised (const juce::String& name,
                                   const juce::String& headerLine)
    {
        static std::once_flag flag;
        std::call_once (flag, [&]
        {
            auto logsFolder = juce::File::getSpecialLocation (
                                  juce::File::userApplicationDataDirectory)
                                  .getChildFile ("Remember").getChildFile ("logs");
            logsFolder.createDirectory();

            auto logFile = logsFolder.getChildFile (name + ".log");
            auto* logger = new juce::FileLogger (logFile, headerLine, 1024 * 1024);
            juce::Logger::setCurrentLogger (logger);
        });
    }
}
