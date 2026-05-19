#pragma once
#include <JuceHeader.h>

// Singleton-style helper that wires a juce::FileLogger as the global logger
// the first time it's used. Logs go to:
//
//   %APPDATA%/Remember/logs/<name>.log
//
// 1 MB cap (FileLogger rolls the file once it exceeds that).
//
// The plugin can be instantiated many times in the same DAW session — we
// only want ONE logger, so initialisation is std::call_once'd. The logger
// is intentionally leaked: it must outlive every plugin instance, and the
// host process tears the whole DLL down on exit anyway.
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
            // 1 MB cap; FileLogger truncates when exceeded.
            auto* logger = new juce::FileLogger (logFile, headerLine, 1024 * 1024);
            juce::Logger::setCurrentLogger (logger);
        });
    }
}
