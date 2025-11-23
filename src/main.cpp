#include "Core/Application.h"
#include "Core/Logger.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        Kosmos::Logger::GetInstance().SetLogFile("kosmos.log");
        LOG_INFO("Starting Kosmos Space Flight Simulator");
        
        Kosmos::Application app;
        
        if (!app.Initialize(argc, argv)) {
            LOG_FATAL("Failed to initialize application");
            return 1;
        }
        
        app.Run();
        app.Shutdown();
        
        LOG_INFO("Kosmos exited successfully");
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        LOG_FATAL(std::string("Exception: ") + e.what());
        return 1;
    }
}
