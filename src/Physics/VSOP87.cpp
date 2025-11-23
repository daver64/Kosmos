#include "VSOP87.h"
#include "../Core/Logger.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace Kosmos {

VSOP87::VSOP87()
    : m_variant('A')
{
}

bool VSOP87::LoadData(const std::string& dataPath, char variant) {
    m_variant = variant;
    
    // Map of planet abbreviations to enum values (as used in VSOP87 filenames)
    const std::map<std::string, Planet> planetAbbreviations = {
        {"mer", Planet::Mercury},
        {"ven", Planet::Venus},
        {"ear", Planet::Earth},
        {"mar", Planet::Mars},
        {"jup", Planet::Jupiter},
        {"sat", Planet::Saturn},
        {"ura", Planet::Uranus},
        {"nep", Planet::Neptune}
    };
    
    const std::map<Planet, std::string> planetFullNames = {
        {Planet::Mercury, "Mercury"},
        {Planet::Venus, "Venus"},
        {Planet::Earth, "Earth"},
        {Planet::Mars, "Mars"},
        {Planet::Jupiter, "Jupiter"},
        {Planet::Saturn, "Saturn"},
        {Planet::Uranus, "Uranus"},
        {Planet::Neptune, "Neptune"}
    };
    
    // Try to load each planet's data
    int loaded = 0;
    for (const auto& [abbrev, planet] : planetAbbreviations) {
        std::string filename = dataPath + "/VSOP87" + variant + "." + abbrev;
        if (ParseDataFile(filename, planet)) {
            loaded++;
            LOG_INFO("Loaded VSOP87" + std::string(1, variant) + " data for " + planetFullNames.at(planet));
        } else {
            LOG_WARNING("Could not load VSOP87 data for " + planetFullNames.at(planet) + " from " + filename);
        }
    }
    
    if (loaded == 0) {
        LOG_ERROR("No VSOP87 data files found in " + dataPath);
        return false;
    }
    
    LOG_INFO("VSOP87" + std::string(1, variant) + " loaded with data for " + std::to_string(loaded) + " planets");
    return true;
}

bool VSOP87::ParseDataFile(const std::string& filename, Planet planet) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    PlanetData data;
    
    // Initialize series structures
    for (int i = 0; i < 3; ++i) {
        CoordinateSeries* series = nullptr;
        if (i == 0) series = &data.x;
        else if (i == 1) series = &data.y;
        else series = &data.z;
        
        series->powers.resize(6);  // T^0 through T^5
    }
    
    std::string line;
    int currentCoord = -1;
    int currentPower = -1;
    int linesRead = 0;
    int dataLines = 0;
    
    while (std::getline(file, line)) {
        linesRead++;
        // Skip short lines
        if (line.length() < 50) continue;
        
        // Check if this is a header line (has "VARIABLE" in it)
        if (line.find("VARIABLE") != std::string::npos) {
            // Extract variable number (1, 2, or 3) and power from header
            // Format: "VARIABLE N (LBR)       *T**P"
            size_t varPos = line.find("VARIABLE");
            if (varPos != std::string::npos && varPos + 9 < line.length()) {
                currentCoord = (line[varPos + 9] - '1');  // '1', '2', or '3' -> 0, 1, 2
            }
            
            size_t tPos = line.find("*T**");
            if (tPos != std::string::npos && tPos + 4 < line.length()) {
                currentPower = (line[tPos + 4] - '0');  // '0', '1', etc.
            }
            
            if (dataLines == 0) {
                LOG_INFO("First header: coord=" + std::to_string(currentCoord) + ", power=" + std::to_string(currentPower));
            }
            continue;
        }
        
        // Parse data line
        // Format: 4310 index i1 i2 i3... A B1 B2 Phase Frequency
        // We want fields 15 (A), 18 (Phase=B), 19 (Frequency=C)
        // Data lines start with whitespace followed by "4310"
        if (currentCoord >= 0 && currentPower >= 0) {
            // Trim leading whitespace and check if it starts with a digit
            size_t firstDigit = line.find_first_of("0123456789");
            if (firstDigit == std::string::npos) continue;
            
            dataLines++;
            std::istringstream iss(line);
            std::vector<double> fields;
            double value;
            
            // Read all numeric fields
            while (iss >> value) {
                fields.push_back(value);
            }
            
            if (dataLines == 1) {
                LOG_INFO("First data line has " + std::to_string(fields.size()) + " fields");
            }
            
            // Check if we have enough fields (should be 19)
            if (fields.size() >= 19) {
                Term term;
                term.A = fields[14];  // Field 15 (0-indexed = 14)
                term.B = fields[17];  // Field 18 (0-indexed = 17)
                term.C = fields[18];  // Field 19 (0-indexed = 18)
                
                CoordinateSeries* series = nullptr;
                if (currentCoord == 0) series = &data.x;
                else if (currentCoord == 1) series = &data.y;
                else series = &data.z;
                
                if (series && currentPower < 6) {
                    series->powers[currentPower].terms.push_back(term);
                }
            }
        }
    }
    
    // Debug: log number of terms loaded BEFORE moving
    int totalTerms = 0;
    for (int coord = 0; coord < 3; coord++) {
        const CoordinateSeries* series = (coord == 0) ? &data.x : (coord == 1) ? &data.y : &data.z;
        for (const auto& power : series->powers) {
            totalTerms += power.terms.size();
        }
    }
    LOG_INFO("Loaded " + std::to_string(totalTerms) + " terms for planet");
    
    m_planetData[planet] = std::move(data);
    
    return true;
}

bool VSOP87::HasData(Planet planet) const {
    return m_planetData.find(planet) != m_planetData.end();
}

double VSOP87::JulianDateToMillennia(double jd) {
    // Convert Julian Date to millennia from J2000
    return (jd - J2000_JD) / 365250.0;
}

double VSOP87::EvaluateSeries(const CoordinateSeries& series, double T) const {
    double result = 0.0;
    double T_power = 1.0;
    
    // Sum over powers of T
    for (size_t power = 0; power < series.powers.size(); ++power) {
        double sum = 0.0;
        
        // Sum all terms for this power
        for (const Term& term : series.powers[power].terms) {
            sum += term.A * std::cos(term.B + term.C * T);
        }
        
        result += sum * T_power;
        T_power *= T;
    }
    
    return result;
}

double VSOP87::EvaluateSeriesDerivative(const CoordinateSeries& series, double T) const {
    double result = 0.0;
    double T_power = 1.0;
    double T_prev = 0.0;
    
    // Sum over powers of T
    for (size_t power = 0; power < series.powers.size(); ++power) {
        double sum = 0.0;
        double deriv_sum = 0.0;
        
        // Sum all terms for this power
        for (const Term& term : series.powers[power].terms) {
            double angle = term.B + term.C * T;
            sum += term.A * std::cos(angle);
            deriv_sum += -term.A * term.C * std::sin(angle);
        }
        
        // d/dT of (sum * T^power) = deriv_sum * T^power + sum * power * T^(power-1)
        result += deriv_sum * T_power;
        if (power > 0) {
            result += sum * power * T_prev;
        }
        
        T_prev = T_power;
        T_power *= T;
    }
    
    return result;
}

glm::dvec3 VSOP87::GetPosition(Planet planet, double julianDate) const {
    auto it = m_planetData.find(planet);
    if (it == m_planetData.end()) {
        LOG_ERROR("No VSOP87 data for requested planet");
        return glm::dvec3(0.0);
    }
    
    const PlanetData& data = it->second;
    double T = JulianDateToMillennia(julianDate);
    
    glm::dvec3 position;
    position.x = EvaluateSeries(data.x, T);
    position.y = EvaluateSeries(data.y, T);
    position.z = EvaluateSeries(data.z, T);
    
    // Convert from AU to meters
    return position * AU;
}

glm::dvec3 VSOP87::GetVelocity(Planet planet, double julianDate) const {
    auto it = m_planetData.find(planet);
    if (it == m_planetData.end()) {
        LOG_ERROR("No VSOP87 data for requested planet");
        return glm::dvec3(0.0);
    }
    
    const PlanetData& data = it->second;
    double T = JulianDateToMillennia(julianDate);
    
    glm::dvec3 velocity;
    velocity.x = EvaluateSeriesDerivative(data.x, T);
    velocity.y = EvaluateSeriesDerivative(data.y, T);
    velocity.z = EvaluateSeriesDerivative(data.z, T);
    
    // Convert from AU/millennium to AU/day
    velocity /= 365250.0;
    
    // Convert from AU/day to meters/second
    velocity *= AU / 86400.0;
    
    return velocity;
}

void VSOP87::GetPositionVelocity(Planet planet, double julianDate,
                                 glm::dvec3& position, glm::dvec3& velocity) const {
    auto it = m_planetData.find(planet);
    if (it == m_planetData.end()) {
        LOG_ERROR("No VSOP87 data for requested planet");
        position = glm::dvec3(0.0);
        velocity = glm::dvec3(0.0);
        return;
    }
    
    const PlanetData& data = it->second;
    double T = JulianDateToMillennia(julianDate);
    
    // Debug log
    static bool logged = false;
    if (!logged && planet == Planet::Earth) {
        LOG_INFO("VSOP87 Earth: JD=" + std::to_string(julianDate) + ", T=" + std::to_string(T));
        logged = true;
    }
    
    // Evaluate position
    position.x = EvaluateSeries(data.x, T);
    position.y = EvaluateSeries(data.y, T);
    position.z = EvaluateSeries(data.z, T);
    
    // Debug log for Earth
    if (planet == Planet::Earth) {
        static bool logged = false;
        if (!logged) {
            LOG_INFO("VSOP87 Earth raw position (before AU conversion): " + std::to_string(position.x) + ", " + 
                     std::to_string(position.y) + ", " + std::to_string(position.z));
            logged = true;
        }
    }
    
    // Evaluate velocity
    velocity.x = EvaluateSeriesDerivative(data.x, T);
    velocity.y = EvaluateSeriesDerivative(data.y, T);
    velocity.z = EvaluateSeriesDerivative(data.z, T);
    
    // Convert position from AU to meters
    position *= AU;
    
    // Debug log after conversion
    if (planet == Planet::Earth) {
        static bool logged2 = false;
        if (!logged2) {
            LOG_INFO("VSOP87 Earth position after AU conversion (m): " + std::to_string(position.x) + ", " + 
                     std::to_string(position.y) + ", " + std::to_string(position.z));
            logged2 = true;
        }
    }
    
    // Convert velocity from AU/millennium to meters/second
    velocity = velocity * AU / (365250.0 * 86400.0);
}

glm::dvec3 VSOP87::EclipticToEquatorial(const glm::dvec3& ecliptic) {
    // Rotation matrix from ecliptic to equatorial coordinates
    // Rotates by obliquity of ecliptic (epsilon) around X axis
    double cosEps = std::cos(OBLIQUITY_J2000);
    double sinEps = std::sin(OBLIQUITY_J2000);
    
    glm::dvec3 equatorial;
    equatorial.x = ecliptic.x;
    equatorial.y = ecliptic.y * cosEps - ecliptic.z * sinEps;
    equatorial.z = ecliptic.y * sinEps + ecliptic.z * cosEps;
    
    return equatorial;
}

double VSOP87::CalendarToJulianDate(int year, int month, int day,
                                    int hour, int minute, double second) {
    // Algorithm from "Astronomical Algorithms" by Jean Meeus
    
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    
    int A = year / 100;
    int B = 2 - A + (A / 4);
    
    double jd = static_cast<int>(365.25 * (year + 4716)) +
                static_cast<int>(30.6001 * (month + 1)) +
                day + B - 1524.5;
    
    // Add time of day
    double dayFraction = (hour + minute / 60.0 + second / 3600.0) / 24.0;
    jd += dayFraction;
    
    return jd;
}

} // namespace Kosmos
