cmake_minimum_required(VERSION 3.16)
project(kosmosavd CXX)

# kosmosavd, the KosmosAV server: C++17, SQLite 3 and OpenSSL (libcrypto). Linux only.
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_executable(kosmosavd kosmosavd.cpp)
target_link_libraries(kosmosavd PRIVATE OpenSSL::Crypto SQLite::SQLite3)
