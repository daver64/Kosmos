#include "Config.h"
#include <fstream>
#include <iostream>

namespace Kosmos {

Config& Config::GetInstance() {
    static Config instance;
    return instance;
}

bool Config::LoadFromFile(const std::string& filename) {
    // TODO: Implement JSON parsing
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }
    
    // Placeholder for JSON parsing
    return true;
}

bool Config::SaveToFile(const std::string& filename) {
    // TODO: Implement JSON serialization
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to save config file: " << filename << std::endl;
        return false;
    }
    
    return true;
}

template<typename T>
T Config::Get(const std::string& key, const T& defaultValue) const {
    auto it = m_config.find(key);
    if (it != m_config.end()) {
        try {
            return std::any_cast<T>(it->second);
        } catch (const std::bad_any_cast&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

template<typename T>
void Config::Set(const std::string& key, const T& value) {
    m_config[key] = value;
}

bool Config::Has(const std::string& key) const {
    return m_config.find(key) != m_config.end();
}

// Explicit template instantiations
template int Config::Get<int>(const std::string&, const int&) const;
template double Config::Get<double>(const std::string&, const double&) const;
template bool Config::Get<bool>(const std::string&, const bool&) const;
template std::string Config::Get<std::string>(const std::string&, const std::string&) const;

template void Config::Set<int>(const std::string&, const int&);
template void Config::Set<double>(const std::string&, const double&);
template void Config::Set<bool>(const std::string&, const bool&);
template void Config::Set<std::string>(const std::string&, const std::string&);

} // namespace Kosmos
