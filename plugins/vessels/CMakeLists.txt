cmake_minimum_required(VERSION 3.20)

# LunaE3 probe vessel plugin
add_library(LunaE3 SHARED
    LunaE3.cpp
)

target_include_directories(LunaE3 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Set proper extension for plugin
set_target_properties(LunaE3 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "LunaE3"
)

if(WIN32)
    set_target_properties(LunaE3 PROPERTIES SUFFIX ".dll")
elseif(APPLE)
    set_target_properties(LunaE3 PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(LunaE3 PROPERTIES SUFFIX ".so")
endif()

# Install plugin
install(TARGETS LunaE3
    LIBRARY DESTINATION share/kosmos/plugins/vessels
    RUNTIME DESTINATION share/kosmos/plugins/vessels
)
