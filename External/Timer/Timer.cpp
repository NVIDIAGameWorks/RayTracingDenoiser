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

    float relativeDelta = Abs(m_Delta - m_SmoothedDelta) / ( Min( m_Delta, m_SmoothedDelta ) + 1e-7f );
    float f = relativeDelta / ( 1.0f + relativeDelta );

    m_SmoothedDelta = Lerp(m_SmoothedDelta, m_Delta, Max( f, 1.0f / 32.0f ));
    m_VerySmoothedDelta = Lerp(m_VerySmoothedDelta, m_Delta, Max( f, 1.0f / 64.0f ));
}

void Timer::SaveCurrentTime()
{
    QueryPerformanceCounter((LARGE_INTEGER*)&m_Time);
}
