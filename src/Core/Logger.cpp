#include "Logger.h"
#include <iomanip>
#include <sstream>

namespace Kosmos {

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : m_minLevel(LogLevel::INFO)
{
}

Logger::~Logger() {
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

void Logger::SetLogFile(const std::string& filename) {
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
    
    m_logFile.open(filename, std::ios::out | std::ios::app);
    if (!m_logFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level < m_minLevel) {
        return;
    }
    
    std::string timestamp = GetTimestamp();
    const char* levelStr = LevelToString(level);
    
    // Console output
    std::ostream& out = (level >= LogLevel::ERROR) ? std::cerr : std::cout;
    out << "[" << timestamp << "] [" << levelStr << "] " << message << std::endl;
    
    // File output
    if (m_logFile.is_open()) {
        m_logFile << "[" << timestamp << "] [" << levelStr << "] " << message << std::endl;
        m_logFile.flush();
    }
}

const char* Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

std::string Logger::GetTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace Kosmos
