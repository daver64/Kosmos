#include "Time.h"
#include <cmath>

namespace Kosmos {

TimeSystem::TimeSystem()
    : m_simulationTime(0.0)
    , m_julianDate(2451545.0)  // J2000.0 epoch
    , m_deltaTime(0.0)
    , m_timeAcceleration(1.0)
    , m_paused(false)
{
}

void TimeSystem::Update(double realDeltaTime) {
    if (m_paused) {
        m_deltaTime = 0.0;
        return;
    }
    
    m_deltaTime = realDeltaTime * m_timeAcceleration;
    m_simulationTime += m_deltaTime;
    
    // Update Julian date (86400 seconds per day)
    m_julianDate += m_deltaTime / 86400.0;
}

void TimeSystem::SetTimeAcceleration(double factor) {
    m_timeAcceleration = std::max(0.0, factor);
}

double TimeSystem::UnixToJulian(double unixTime) {
    // Unix epoch: 1970-01-01 00:00:00 UTC
    // Julian date of Unix epoch: 2440587.5
    return 2440587.5 + (unixTime / 86400.0);
}

double TimeSystem::JulianToUnix(double julianDate) {
    return (julianDate - 2440587.5) * 86400.0;
}

} // namespace Kosmos
