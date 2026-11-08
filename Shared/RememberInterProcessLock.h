#pragma once
#include <JuceHeader.h>

#if JUCE_MAC
 #include <cerrno>
 #include <chrono>
 #include <fcntl.h>
 #include <mutex>
 #include <sys/file.h>
 #include <unistd.h>

namespace Remember
{
// POSIX record locks are process-scoped: separate VST3 instances in one DAW
// must instead conflict through independently opened flock descriptions.
class InterProcessLock
{
public:
    explicit InterProcessLock (const juce::String& name)
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Caches/Remember/Locks");
        file = folder.getChildFile (juce::SHA256 (name.toRawUTF8(), name.getNumBytesAsUTF8()).toHexString());
    }
    ~InterProcessLock() { while (depth > 0) exit(); }
    bool enter (int timeoutMilliseconds)
    {
        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now() + std::chrono::milliseconds (juce::jmax (0, timeoutMilliseconds));
        if (timeoutMilliseconds < 0) local.lock();
        else if (! local.try_lock_until (deadline)) return false;
        if (depth > 0) { ++depth; return true; }
        const auto folder = file.getParentDirectory();
        if (folder.createDirectory().failed() && ! folder.isDirectory()) { local.unlock(); return false; }
        handle = open (file.getFullPathName().toRawUTF8(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (handle < 0) { local.unlock(); return false; }
        for (;;)
        {
            if (flock (handle, LOCK_EX | LOCK_NB) == 0) { depth = 1; return true; }
            const auto error = errno;
            if ((error != EINTR && error != EWOULDBLOCK)
                || (timeoutMilliseconds >= 0 && Clock::now() >= deadline))
            { close (handle); handle = -1; local.unlock(); return false; }
            juce::Thread::sleep (2);
        }
    }
    void exit()
    {
        if (depth == 0) return;
        if (--depth == 0) { flock (handle, LOCK_UN); close (handle); handle = -1; }
        local.unlock();
    }
private:
    juce::File file;
    std::recursive_timed_mutex local;
    int handle = -1, depth = 0;
    JUCE_DECLARE_NON_COPYABLE (InterProcessLock)
};
}
#else
namespace Remember { using InterProcessLock = juce::InterProcessLock; }
#endif
