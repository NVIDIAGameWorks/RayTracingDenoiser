#include "Timer.h"

#define NDC_DONT_CARE
#include "MathLib/MathLib.h"

#include <assert.h>
#include <windows.h>

Timer::Timer() // TODO: do it OS independent!
{
    uint64_t ticksPerSecond = 0;
    BOOL res = QueryPerformanceFrequency((LARGE_INTEGER*)&ticksPerSecond);
    assert(res == TRUE);

    res = QueryPerformanceCounter((LARGE_INTEGER*)&m_Time);
    assert(res == TRUE);

    m_InvTicksPerMs = 1000.0 / ticksPerSecond;
}

double Timer::GetTimeStamp()
{
    uint64_t currentTime;
    QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);

    return currentTime * m_InvTicksPerMs;
}

void Timer::UpdateElapsedTimeSinceLastSave()
{
    uint64_t currentTime;
    QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);
    double ms = (currentTime - m_Time) * m_InvTicksPerMs;

    m_Delta = float(ms);

    float relativeDelta = Abs(m_Delta - m_SmoothedDelta) / m_Delta;

    float f1 = Clamp( relativeDelta, 1.0f / 30.0f, 1.0f );
    m_SmoothedDelta = Lerp(m_SmoothedDelta, m_Delta, f1);

    float f2 = Clamp( relativeDelta, 1.0f / 120.0f, 1.0f );
    m_VerySmoothedDelta = Lerp(m_VerySmoothedDelta, m_Delta, f2);
}

void Timer::SaveCurrentTime()
{
    QueryPerformanceCounter((LARGE_INTEGER*)&m_Time);
}
