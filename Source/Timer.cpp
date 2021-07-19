#include "Timer.h"

#include <assert.h>
#include <algorithm>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <time.h>
    #ifdef USE_MONOTONIC_TIMER
        constexpr clockid_t CLOCKID = CLOCK_MONOTONIC;
    #else
        constexpr clockid_t CLOCKID = CLOCK_REALTIME;
    #endif
#else
    #error "Undefined platform"
#endif

inline uint64_t GetTicks()
{
#if defined(_WIN32)
    uint64_t ticks;
    QueryPerformanceCounter((LARGE_INTEGER*)&ticks);
    return ticks;
#elif defined(__linux__)
    struct timespec spec;
    int res = clock_gettime(CLOCKID, &spec);
    assert(res == 0);
    return uint64_t(spec.tv_sec) * 1000000000ull + spec.tv_nsec;
#endif
}

Timer::Timer()
{
#if defined(_WIN32)
    uint64_t ticksPerSecond = 1;
    BOOL res = QueryPerformanceFrequency((LARGE_INTEGER*)&ticksPerSecond);
    assert(res == TRUE);

    m_InvTicksPerMs = 1000.0 / ticksPerSecond;
#elif defined(__linux__)
    m_InvTicksPerMs = 1.0 / 1000000.0;
#endif

    SaveCurrentTime();
}

double Timer::GetTimeStamp()
{
    return GetTicks() * m_InvTicksPerMs;
}

void Timer::UpdateElapsedTimeSinceLastSave()
{
    double ms = (GetTicks() - m_Time) * m_InvTicksPerMs;
    m_Delta = float(ms);

    float relativeDelta = std::abs(m_Delta - m_SmoothedDelta) / (std::min(m_Delta, m_SmoothedDelta) + 1e-7f);
    float f = relativeDelta / (1.0f + relativeDelta);

    m_SmoothedDelta = m_SmoothedDelta + (m_Delta - m_SmoothedDelta) * std::max(f, 1.0f / 32.0f);
    m_VerySmoothedDelta = m_VerySmoothedDelta + (m_Delta - m_VerySmoothedDelta) * std::max(f, 1.0f / 64.0f);
}

void Timer::SaveCurrentTime()
{
    m_Time = GetTicks();
}
