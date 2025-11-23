#pragma once

#include <map>
#include <string>
#include <any>

namespace Kosmos {

class Config {
public:
    static Config& GetInstance();
    
    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename);
    
    // Getters with defaults
    template<typename T>
    T Get(const std::string& key, const T& defaultValue) const;
    
    // Setters
    template<typename T>
    void Set(const std::string& key, const T& value);
    
    bool Has(const std::string& key) const;
    
private:
    Config() = default;
    std::map<std::string, std::any> m_config;
};

} // namespace Kosmos
