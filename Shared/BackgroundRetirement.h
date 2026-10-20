#pragma once
#include <cstdint>
#include <memory>

namespace BackgroundRetirement
{
// Implementations retain their buffers/context until every native thread exits.
// requestStop must only publish cancellation; cleanup belongs to the workers.
struct Job
{
    virtual ~Job() = default;
    virtual void requestStop() = 0;
    virtual bool readyToRelease() const = 0;
};
void retire (std::shared_ptr<Job>);
size_t pending() noexcept;
class ModuleReference
{
public:
    ModuleReference();
    ~ModuleReference();
    ModuleReference (const ModuleReference&) = delete;
    ModuleReference& operator= (const ModuleReference&) = delete;
    bool valid() const;
private:
    void* module = nullptr;
};

// JUCE clears its thread-running flag just before the native entry point returns.
// DLL lifetime requires an OS completion handle for that final interval too.
class NativeCompletion
{
public:
    NativeCompletion() = default;
    ~NativeCompletion();
    NativeCompletion (const NativeCompletion&) = delete;
    NativeCompletion& operator= (const NativeCompletion&) = delete;
    bool capture (uintptr_t threadId);
    bool finished() const;
private:
    void* handle = nullptr;
    bool failed = false;
};
}
