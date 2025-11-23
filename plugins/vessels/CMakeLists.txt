cmake_minimum_required(VERSION 3.20)

# Example vessel plugin - Delta Glider style spacecraft
add_library(DeltaGlider SHARED
    DeltaGlider.cpp
)

target_include_directories(DeltaGlider PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Set proper extension for plugin
set_target_properties(DeltaGlider PROPERTIES
    PREFIX ""
    OUTPUT_NAME "DeltaGlider"
)

if(WIN32)
    set_target_properties(DeltaGlider PROPERTIES SUFFIX ".dll")
elseif(APPLE)
    set_target_properties(DeltaGlider PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(DeltaGlider PROPERTIES SUFFIX ".so")
endif()

# Install plugin
install(TARGETS DeltaGlider
    LIBRARY DESTINATION share/kosmos/plugins/vessels
    RUNTIME DESTINATION share/kosmos/plugins/vessels
)
