cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

project(JapaneseSentenceSegmenter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


option(JSS_BUILD_TESTS "Build the unit tests" ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library: everything graded lives here.
add_library(jss_core
  core/src/dictionary.cpp
  core/src/trie.cpp
  core/src/utf8.cpp
)
target_include_directories(jss_core PUBLIC core/include)

# Command-line tool: `jss lookup <word>`.
add_executable(jss cli/main.cpp)
target_link_libraries(jss PRIVATE jss_core)
target_compile_definitions(jss PRIVATE
  JSS_DEFAULT_DICT="${CMAKE_CURRENT_SOURCE_DIR}/data/dict.tsv"
)
if(WIN32)
  target_link_libraries(jss PRIVATE shell32)
endif()

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
    tests/test_dictionary.cpp
    tests/test_predictive.cpp
    tests/test_trie.cpp
    tests/test_utf8.cpp
  )
  target_link_libraries(jss_tests PRIVATE jss_core Catch2::Catch2WithMain)
  target_compile_definitions(jss_tests PRIVATE
    JSS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    JSS_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  )

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(jss_tests)
endif()
