cmake_minimum_required(VERSION 3.16)
project(my_native_addon CXX)

# Point this at the unpacked Kosmos SDK (or pass -DKosmosSDK_DIR=...).
set(KOSMOS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Kosmos SDK folder")
add_subdirectory(${KOSMOS_SDK_DIR} kosmos-sdk EXCLUDE_FROM_ALL)

add_executable(my-native-addon src/main.cpp)
target_compile_features(my-native-addon PRIVATE cxx_std_17)
target_link_libraries(my-native-addon PRIVATE kosmos::link)
set_target_properties(my-native-addon PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
