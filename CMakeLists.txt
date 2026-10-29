cmake_minimum_required(VERSION 3.20)
project(JapaneseSentenceSegmenter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(JSS_BUILD_TESTS "Build the unit tests" ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library: everything graded lives here.
add_library(jss_core
  core/src/version.cpp
)
target_include_directories(jss_core PUBLIC core/include)

# Command-line tool used by the eval and benchmark scripts.
add_executable(jss cli/main.cpp)
target_link_libraries(jss PRIVATE jss_core)

if(JSS_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)

  enable_testing()
  add_executable(jss_tests
    tests/test_smoke.cpp
  )
  target_link_libraries(jss_tests PRIVATE jss_core Catch2::Catch2WithMain)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(jss_tests)
endif()
