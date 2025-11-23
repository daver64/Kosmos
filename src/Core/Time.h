#pragma once

#include <string>
#include <chrono>

namespace Kosmos {

class TimeSystem {
public:
    TimeSystem();
    
    void Update(double realDeltaTime);
    
    // Get current simulation time
    double GetSimulationTime() const { return m_simulationTime; }
    double GetJulianDate() const { return m_julianDate; }
    double GetDeltaTime() const { return m_deltaTime; }
    
    // Time acceleration
    void SetTimeAcceleration(double factor);
    double GetTimeAcceleration() const { return m_timeAcceleration; }
    
    // Pause/Resume
    void Pause() { m_paused = true; }
    void Resume() { m_paused = false; }
    bool IsPaused() const { return m_paused; }
    
    // Time conversions
    static double UnixToJulian(double unixTime);
    static double JulianToUnix(double julianDate);
    
private:
    double m_simulationTime;     // Seconds since epoch
    double m_julianDate;          // Julian date
    double m_deltaTime;           // Simulation delta time
    double m_timeAcceleration;    // Time multiplier
    bool m_paused;
};

} // namespace Kosmos
