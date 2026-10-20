#include "BackgroundRetirement.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace BackgroundRetirement
{
namespace
{
struct Queue
{
    std::mutex mutex;
    std::vector<std::shared_ptr<Job>> jobs;
    std::atomic<size_t> count { 0 };
    bool running = false;
};
Queue& queue() { static Queue value; return value; }
void drain()
{
    for (;;)
    {
        std::vector<std::shared_ptr<Job>> finished;
        bool done = false;
        {
            auto& owner = queue();
            std::lock_guard<std::mutex> lock (owner.mutex);
            for (auto it = owner.jobs.begin(); it != owner.jobs.end();)
            {
                if ((*it)->readyToRelease())
                { finished.push_back (std::move (*it)); it = owner.jobs.erase (it); }
                else ++it;
            }
            done = owner.jobs.empty();
            if (done) owner.running = false;
        }
        // Destructors run outside the queue lock and only after OS completion.
        const auto count = finished.size();
        finished.clear();
        queue().count.fetch_sub (count);
        if (done) return;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}
#if defined(_WIN32)
DWORD WINAPI entry (void* context)
{
    const auto module = static_cast<HMODULE> (context);
    drain(); // All C++ stack objects are gone before unloading our own DLL.
    FreeLibraryAndExitThread (module, 0);
}
bool start()
{
    HMODULE module = nullptr;
    if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                              reinterpret_cast<LPCWSTR> (&entry), &module)) return false;
    if (const auto thread = CreateThread (nullptr, 0, &entry, module, 0, nullptr))
    { CloseHandle (thread); return true; }
    FreeLibrary (module);
    return false;
}
#else
bool start()
{
    try { std::thread (drain).detach(); return true; }
    catch (...) { return false; }
}
#endif
}

void retire (std::shared_ptr<Job> job)
{
    if (! job) return;
    job->requestStop();
    auto& owner = queue();
    std::lock_guard<std::mutex> lock (owner.mutex);
    owner.jobs.push_back (std::move (job));
    owner.count.fetch_add (1);
    if (! owner.running)
    {
        owner.running = true;
        if (! start()) owner.running = false;
        // On OS thread-creation failure, keep ownership and retry on the next
        // retirement rather than freeing resources beneath a running worker.
    }
}
size_t pending() noexcept { return queue().count.load(); }

ModuleReference::ModuleReference()
{
   #if defined(_WIN32)
    HMODULE handle = nullptr;
    if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR> (&retire), &handle)) module = handle;
   #endif
}
ModuleReference::~ModuleReference()
{
   #if defined(_WIN32)
    if (module) FreeLibrary (static_cast<HMODULE> (module));
   #endif
}
bool ModuleReference::valid() const
{
   #if defined(_WIN32)
    return module != nullptr;
   #else
    return true;
   #endif
}

NativeCompletion::~NativeCompletion()
{
   #if defined(_WIN32)
    if (handle) CloseHandle (handle);
   #endif
}
bool NativeCompletion::capture (uintptr_t id)
{
   #if defined(_WIN32)
    handle = OpenThread (SYNCHRONIZE, FALSE, (DWORD) id);
    failed = handle == nullptr;
    return ! failed;
   #else
    (void) id; return true;
   #endif
}
bool NativeCompletion::finished() const
{
   #if defined(_WIN32)
    return ! failed && (! handle || WaitForSingleObject (handle, 0) == WAIT_OBJECT_0);
   #else
    return true;
   #endif
}
}
