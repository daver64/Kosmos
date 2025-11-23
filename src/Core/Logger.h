#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <ctime>

namespace Kosmos {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logger {
public:
    static Logger& GetInstance();
    
    void SetLogLevel(LogLevel level) { m_minLevel = level; }
    void SetLogFile(const std::string& filename);
    
    void Log(LogLevel level, const std::string& message);
    
    // Convenience methods
    void Debug(const std::string& message) { Log(LogLevel::DEBUG, message); }
    void Info(const std::string& message) { Log(LogLevel::INFO, message); }
    void Warning(const std::string& message) { Log(LogLevel::WARNING, message); }
    void Error(const std::string& message) { Log(LogLevel::ERROR, message); }
    void Fatal(const std::string& message) { Log(LogLevel::FATAL, message); }
    
private:
    Logger();
    ~Logger();
    
    LogLevel m_minLevel;
    std::ofstream m_logFile;
    
    const char* LevelToString(LogLevel level);
    std::string GetTimestamp();
};

// Convenience macros
#define LOG_DEBUG(msg) Kosmos::Logger::GetInstance().Debug(msg)
#define LOG_INFO(msg) Kosmos::Logger::GetInstance().Info(msg)
#define LOG_WARNING(msg) Kosmos::Logger::GetInstance().Warning(msg)
#define LOG_ERROR(msg) Kosmos::Logger::GetInstance().Error(msg)
#define LOG_FATAL(msg) Kosmos::Logger::GetInstance().Fatal(msg)

} // namespace Kosmos
