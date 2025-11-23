#pragma once

#include <vector>
#include <string>
#include <map>
#include <glm/glm.hpp>

namespace Kosmos {

/**
 * @brief VSOP87 planetary theory implementation
 * 
 * VSOP87 (Variations Séculaires des Orbites Planétaires) provides high-precision
 * analytical positions for planets from Mercury to Neptune.
 * 
 * Supported variants:
 * - VSOP87A: Rectangular heliocentric ecliptic J2000
 * - VSOP87B: Spherical heliocentric ecliptic J2000
 * - VSOP87C: Rectangular heliocentric ecliptic of date
 * - VSOP87D: Spherical heliocentric ecliptic of date
 * - VSOP87E: Rectangular barycentric ecliptic J2000
 * 
 * We'll primarily use VSOP87A for simplicity.
 */
class VSOP87 {
public:
    enum class Planet {
        Mercury = 0,
        Venus = 1,
        Earth = 2,
        Mars = 3,
        Jupiter = 4,
        Saturn = 5,
        Uranus = 6,
        Neptune = 7,
        EarthMoonBarycenter = 2  // EMB is same as Earth in most variants
    };
    
    enum class Coordinate {
        X = 0,  // or L (longitude) for spherical
        Y = 1,  // or B (latitude) for spherical
        Z = 2   // or R (radius) for spherical
    };
    
    /**
     * @brief Single term in a VSOP87 series
     * Each term is: A * cos(B + C * T)
     * where T is time in Julian millennia from J2000
     */
    struct Term {
        double A;  // Amplitude
        double B;  // Phase
        double C;  // Frequency
    };
    
    /**
     * @brief Series for one coordinate and power of T
     * Position = Sum over powers[i] of: Sum of terms[j] * T^i
     */
    struct Series {
        std::vector<Term> terms;
    };
    
    /**
     * @brief All series for one coordinate (X, Y, or Z)
     * Series are organized by powers of T: T^0, T^1, T^2, ..., T^5
     */
    struct CoordinateSeries {
        std::vector<Series> powers;  // Index 0 = T^0, 1 = T^1, etc.
    };
    
    /**
     * @brief Complete VSOP87 data for one planet
     */
    struct PlanetData {
        CoordinateSeries x;  // X coordinate series (or L for spherical)
        CoordinateSeries y;  // Y coordinate series (or B for spherical)
        CoordinateSeries z;  // Z coordinate series (or R for spherical)
    };
    
public:
    VSOP87();
    ~VSOP87() = default;
    
    /**
     * @brief Load VSOP87 data from files
     * @param dataPath Path to directory containing VSOP87 data files
     * @param variant Which variant to load (A, B, C, D, or E)
     * @return true if successful
     */
    bool LoadData(const std::string& dataPath, char variant = 'A');
    
    /**
     * @brief Calculate heliocentric position of a planet
     * @param planet Which planet
     * @param julianDate Julian Date (days since J2000.0 = JD 2451545.0)
     * @return Position in AU (rectangular coordinates)
     */
    glm::dvec3 GetPosition(Planet planet, double julianDate) const;
    
    /**
     * @brief Calculate heliocentric velocity of a planet
     * @param planet Which planet
     * @param julianDate Julian Date
     * @return Velocity in AU/day
     */
    glm::dvec3 GetVelocity(Planet planet, double julianDate) const;
    
    /**
     * @brief Get both position and velocity efficiently
     */
    void GetPositionVelocity(Planet planet, double julianDate, 
                            glm::dvec3& position, glm::dvec3& velocity) const;
    
    /**
     * @brief Check if data is loaded for a planet
     */
    bool HasData(Planet planet) const;
    
    /**
     * @brief Convert ecliptic coordinates to equatorial (J2000)
     * @param ecliptic Position in ecliptic coordinates
     * @return Position in equatorial coordinates
     */
    static glm::dvec3 EclipticToEquatorial(const glm::dvec3& ecliptic);
    
    /**
     * @brief Get Julian Date from calendar date
     * @param year Year
     * @param month Month (1-12)
     * @param day Day of month
     * @param hour Hour (0-23)
     * @param minute Minute (0-59)
     * @param second Second (0-59.999...)
     * @return Julian Date
     */
    static double CalendarToJulianDate(int year, int month, int day,
                                       int hour = 0, int minute = 0, double second = 0.0);
    
    // Constants
    static constexpr double J2000_JD = 2451545.0;  // J2000 epoch (2000-01-01 12:00:00 TT)
    static constexpr double OBLIQUITY_J2000 = 0.40909280422232891;  // 23.4392911 degrees
    static constexpr double AU = 1.495978707e11;  // Astronomical Unit in meters
    
private:
    std::map<Planet, PlanetData> m_planetData;
    char m_variant;
    
    /**
     * @brief Parse a VSOP87 data file
     */
    bool ParseDataFile(const std::string& filename, Planet planet);
    
    /**
     * @brief Evaluate a coordinate series at given time
     * @param series The series to evaluate
     * @param T Time in Julian millennia from J2000
     * @return Coordinate value
     */
    double EvaluateSeries(const CoordinateSeries& series, double T) const;
    
    /**
     * @brief Evaluate derivative of series (for velocity)
     */
    double EvaluateSeriesDerivative(const CoordinateSeries& series, double T) const;
    
    /**
     * @brief Convert Julian Date to Julian millennia from J2000
     */
    static double JulianDateToMillennia(double jd);
};

} // namespace Kosmos
