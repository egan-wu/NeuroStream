cmake_minimum_required(VERSION 3.20)
project(NeuroStream
    VERSION 0.1.0
    DESCRIPTION "Behavioral simulator for SSD-NPU streaming co-design"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

include(FetchContent)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(doctest)

add_library(neurostream_core
    src/config.cpp
    src/trace.cpp
    src/injector.cpp
    src/scenario.cpp
    src/predictor.cpp
    src/scheduler.cpp
    src/npu_cache.cpp
    src/latency_histogram.cpp
    src/report_writer.cpp)
target_include_directories(neurostream_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(neurostream_core PUBLIC yaml-cpp::yaml-cpp)

add_executable(neurostream src/main.cpp)
target_link_libraries(neurostream PRIVATE neurostream_core)

enable_testing()
add_subdirectory(tests)
