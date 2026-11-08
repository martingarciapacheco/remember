// A small native test independent of JUCE: catches OS completion mistakes before
// paying for both full universal builds. Run only on macOS.
#include "../Shared/BackgroundRetirement.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <pthread.h>
#include <thread>
#include <mach/mach.h>

int main()
{
    BackgroundRetirement::ModuleReference module;
    if (! module.valid()) { std::cerr << "Cannot retain main code image\n"; return 1; }
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        std::atomic<bool> release { false };
        pthread_t thread {};
        if (pthread_create (&thread, nullptr, [] (void* context) -> void*
            {
                auto& gate = *static_cast<std::atomic<bool>*> (context);
                while (! gate.load()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
                return nullptr;
            }, &release) != 0) return 2;
        const auto port = pthread_mach_thread_np (thread);
        BackgroundRetirement::NativeCompletion completion;
        const bool captured = completion.capture (reinterpret_cast<uintptr_t> (thread));
        const bool premature = completion.finished();
        release.store (true);
        pthread_join (thread, nullptr);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (2);
        while (! completion.finished() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        if (! captured || premature || ! completion.finished())
        {
            mach_port_type_t type = 0;
            const auto typeResult = mach_port_type (mach_task_self(), port, &type);
            thread_basic_info_data_t info {};
            mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
            const auto infoResult = thread_info (port, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t> (&info), &count);
            std::cerr << "Native completion failed: captured=" << captured << " premature=" << premature
                      << " portResult=" << typeResult << " portType=" << type
                      << " infoResult=" << infoResult << " state=" << info.run_state << '\n';
            return 3;
        }
    }
    std::cout << "PASS: 32 native thread completions retain live workers and release terminated workers\n";
}
