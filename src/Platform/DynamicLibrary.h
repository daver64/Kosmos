#pragma once

#include <string>

namespace Kosmos {

class DynamicLibrary {
public:
    DynamicLibrary();
    ~DynamicLibrary();
    
    // Load a dynamic library (.so/.dll/.dylib)
    bool Load(const std::string& path);
    
    // Unload the library
    void Unload();
    
    // Get a function pointer from the library
    void* GetSymbol(const std::string& name);
    
    // Template helper for typed function pointers
    template<typename T>
    T GetFunction(const std::string& name) {
        return reinterpret_cast<T>(GetSymbol(name));
    }
    
    // Check if library is loaded
    bool IsLoaded() const { return m_handle != nullptr; }
    
    // Get last error message
    std::string GetError() const { return m_error; }
    
private:
    void* m_handle;
    std::string m_error;
    std::string m_path;
};

} // namespace Kosmos
