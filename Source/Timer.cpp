#include "Timer.h"

#include <assert.h>
#include <algorithm>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__) || defined(__SCE__)
    #include <time.h>
    #ifdef USE_MONOTONIC_TIMER
        constexpr clockid_t CLOCKID = CLOCK_MONOTONIC;
    #else
        constexpr clockid_t CLOCKID = CLOCK_REALTIME;
    #endif
#else
    #error "Undefined platform"
#endif

inline uint64_t _GetTicks()
{
#if defined(_WIN32)
    uint64_t ticks;
    QueryPerformanceCounter((LARGE_INTEGER*)&ticks);
    return ticks;
#elif defined(__linux__) || defined(__SCE__)
    struct timespec spec;
    int res = clock_gettime(CLOCKID, &spec);
    assert(res == 0);
    return uint64_t(spec.tv_sec) * 1000000000ull + spec.tv_nsec;
#endif
}

nrd::Timer::Timer()
{
#if defined(_WIN32)
    uint64_t ticksPerSecond = 1;
    QueryPerformanceFrequency((LARGE_INTEGER*)&ticksPerSecond);

    m_InvTicksPerMs = 1000.0 / ticksPerSecond;
#elif defined(__linux__) || defined(__SCE__)
    m_InvTicksPerMs = 1.0 / 1000000.0;
#endif

    SaveCurrentTime();
}

double nrd::Timer::GetTimeStamp()
{
    return _GetTicks() * m_InvTicksPerMs;
}

void nrd::Timer::UpdateElapsedTimeSinceLastSave()
{
    double ms = (_GetTicks() - m_Time) * m_InvTicksPerMs;
    m_Delta = float(ms);

    float relativeDelta = std::abs(m_Delta - m_SmoothedDelta) / (std::min(m_Delta, m_SmoothedDelta) + 1e-7f);
    float f = relativeDelta / (1.0f + relativeDelta);

    m_SmoothedDelta = m_SmoothedDelta + (m_Delta - m_SmoothedDelta) * std::max(f, 1.0f / 32.0f);
    m_VerySmoothedDelta = m_VerySmoothedDelta + (m_Delta - m_VerySmoothedDelta) * std::max(f, 1.0f / 64.0f);
}

void nrd::Timer::SaveCurrentTime()
{
    m_Time = _GetTicks();
}
