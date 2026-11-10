#pragma once

// Opt-in test-build diagnostics. No clocks, output or state in release builds.
#if defined (REMEMBER_TRACE_STARTUP)
 #include <chrono>
 #include <cstdio>
namespace RememberStartupTrace
{
inline void mark (const char* name)
{
    using Clock = std::chrono::steady_clock;
    static const auto origin = Clock::now();
    const auto elapsed = std::chrono::duration<double, std::milli> (Clock::now() - origin).count();
    std::fprintf (stderr, "STARTUP_TRACE %.3f %s\n", elapsed, name);
    std::fflush (stderr);
}
struct Scope
{
    explicit Scope (const char* value) : name (value) { mark (name); }
    ~Scope() { mark (name); }
    const char* name;
};
}
#else
namespace RememberStartupTrace
{
inline void mark (const char*) {}
struct Scope { explicit Scope (const char*) {} };
}
#endif
