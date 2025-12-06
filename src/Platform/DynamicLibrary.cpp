// Dynamic library loading (for plugins)
#include "DynamicLibrary.h"
#include "../Core/Logger.h"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace Kosmos {

DynamicLibrary::DynamicLibrary()
    : m_handle(nullptr) {
}

DynamicLibrary::~DynamicLibrary() {
    Unload();
}

bool DynamicLibrary::Load(const std::string& path) {
    if (m_handle) {
        Unload();
    }
    
    m_path = path;
    
#ifdef _WIN32
    m_handle = LoadLibraryA(path.c_str());
    if (!m_handle) {
        DWORD error = GetLastError();
        m_error = "Failed to load library: " + path + " (Error code: " + std::to_string(error) + ")";
        LOG_ERROR(m_error);
        return false;
    }
#else
    m_handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!m_handle) {
        m_error = "Failed to load library: " + path + " - " + std::string(dlerror());
        LOG_ERROR(m_error);
        return false;
    }
#endif
    
    LOG_INFO("Loaded dynamic library: " + path);
    return true;
}

void DynamicLibrary::Unload() {
    if (!m_handle) return;
    
#ifdef _WIN32
    FreeLibrary((HMODULE)m_handle);
#else
    dlclose(m_handle);
#endif
    
    m_handle = nullptr;
    LOG_INFO("Unloaded dynamic library: " + m_path);
}

void* DynamicLibrary::GetSymbol(const std::string& name) {
    if (!m_handle) {
        m_error = "Library not loaded";
        return nullptr;
    }
    
#ifdef _WIN32
    void* symbol = (void*)GetProcAddress((HMODULE)m_handle, name.c_str());
    if (!symbol) {
        m_error = "Symbol not found: " + name;
    }
#else
    void* symbol = dlsym(m_handle, name.c_str());
    const char* error = dlerror();
    if (error) {
        m_error = "Symbol not found: " + name + " - " + std::string(error);
        symbol = nullptr;
    }
#endif
    
    return symbol;
}

} // namespace Kosmos
