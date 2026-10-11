#include "../Shared/BackgroundRetirement.h"
#include <windows.h>
#include <process.h>

namespace
{
class BlockedJob final : public BackgroundRetirement::Job
{
public:
    BlockedJob (HANDLE unblock, HANDLE cleaned) : gate (unblock), completion (cleaned)
    {
        worker = reinterpret_cast<HANDLE> (_beginthreadex (nullptr, 0, &run, this, 0, nullptr));
    }
    ~BlockedJob() override { if (worker) CloseHandle (worker); SetEvent (completion); }
    void requestStop() override {}
    bool readyToRelease() const override { return worker && WaitForSingleObject (worker, 0) == WAIT_OBJECT_0; }
private:
    static unsigned __stdcall run (void* context)
    {
        WaitForSingleObject (static_cast<BlockedJob*> (context)->gate, 10000);
        return 0;
    }
    BackgroundRetirement::ModuleReference module;
    HANDLE gate, completion, worker = nullptr;
};
}
extern "C" __declspec(dllexport) void __cdecl retireBlockedJob (HANDLE gate, HANDLE completion)
{
    BackgroundRetirement::retire (std::make_shared<BlockedJob> (gate, completion));
}
