cmake_minimum_required(VERSION 3.21)
project(Galahad VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(galahad_core
    src/OscCodec.cpp
    src/OscUdpTransport.cpp
    src/LiveController.cpp
    src/RealtimeEvent.cpp
)

target_include_directories(galahad_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(galahad_core PUBLIC ws2_32)
endif()

option(GALAHAD_BUILD_EXAMPLE "Build example executable" ON)

if(GALAHAD_BUILD_EXAMPLE)
    add_executable(galahad_cli examples/cli/main.cpp)
    target_link_libraries(galahad_cli PRIVATE galahad_core)
endif()
