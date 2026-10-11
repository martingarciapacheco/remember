#pragma once
#include <JuceHeader.h>
#include "LibraryStore.h"
#include <mutex>

namespace RememberLogger
{
// Called only from control/background threads. Four files per app/plugin,
// bounded throughout the session, coordinated with other host processes.
class RotatingLogger final : public juce::Logger
{
public:
    explicit RotatingLogger (juce::File path) : file (std::move (path)),
        processLock ("Remember-log-" + juce::String::toHexString (file.getFullPathName().toLowerCase().hashCode64())) {}
    ~RotatingLogger() override
    {
        if (juce::Logger::getCurrentLogger() == this) juce::Logger::setCurrentLogger (nullptr);
    }
    void logMessage (const juce::String& message) override
    {
        juce::ScopedLock guard (lock);
        if (! processLock.enter (100)) return;
        struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock { processLock };
        const auto line = juce::Time::getCurrentTime().toISO8601 (true) + " " + message.substring (0, 8192) + "\n";
        if (file.getSize() + (juce::int64) line.getNumBytesAsUTF8() > limit)
        {
            for (int i = 3; i >= 1; --i)
            {
                const auto target = file.getSiblingFile (file.getFileName() + "." + juce::String (i));
                const auto source = i == 1 ? file : file.getSiblingFile (file.getFileName() + "." + juce::String (i - 1));
                if (source.existsAsFile() && ! source.moveFileTo (target)) return;
            }
        }
        juce::FileOutputStream stream (file);
        if (! stream.openedOk()) return;
        stream.setPosition (file.getSize());
        stream.write (line.toRawUTF8(), line.getNumBytesAsUTF8());
        stream.flush();
    }
    static constexpr juce::int64 limit = 1024 * 1024;
private:
    juce::File file;
    juce::CriticalSection lock;
    juce::InterProcessLock processLock;
};
inline void ensureInitialised (const juce::String& name, const juce::String& headerLine)
{
    static std::once_flag flag;
    static std::unique_ptr<RotatingLogger> logger;
    std::call_once (flag, [&]
    {
        auto folder = LibraryStore::dataDirectory().getChildFile ("logs");
        if (LibraryStore::ensureDirectory (folder).failed()) return;
        logger = std::make_unique<RotatingLogger> (folder.getChildFile (name + ".log"));
        juce::Logger::setCurrentLogger (logger.get());
        logger->logMessage (headerLine);
    });
}
}
