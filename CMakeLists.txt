cmake_minimum_required(VERSION 3.20)
project(
  daedalus
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Options
# =============================================================================
option(BUILD_TESTING "Build test suite" ON)
option(ENABLE_COVERAGE "Enable code coverage flags" OFF)

# =============================================================================
# Coverage flags
# =============================================================================
if(ENABLE_COVERAGE)
  add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
  add_link_options(--coverage)
endif()

# =============================================================================
# Dependencies
# =============================================================================

# ImGui Bundle (from Nix derivation — includes Dear ImGui, Hello ImGui, ImPlot,
# imgui-node-editor)
find_package(imgui_bundle CONFIG REQUIRED)

# IXWebSocket (source-only via FetchContent — not in nixpkgs)
include(FetchContent)
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.6)
set(USE_TLS
    ON
    CACHE BOOL "")
set(USE_OPEN_SSL
    ON
    CACHE BOOL "")
FetchContent_MakeAvailable(ixwebsocket)

find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

# =============================================================================
# Main library
# =============================================================================
add_library(
  daedalus_lib STATIC
  src/daedalus/app.cpp src/daedalus/protocol/schema.cpp
  src/daedalus/protocol/client.cpp src/daedalus/data/signal_tree.cpp
  src/daedalus/views/plotter.cpp)

target_include_directories(daedalus_lib
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(
  daedalus_lib PUBLIC imgui_bundle::imgui_bundle ixwebsocket::ixwebsocket
                      nlohmann_json::nlohmann_json OpenGL::GL Threads::Threads)

# =============================================================================
# Main executable
# =============================================================================
add_executable(daedalus src/main.cpp)
target_link_libraries(daedalus PRIVATE daedalus_lib)

# =============================================================================
# Tests
# =============================================================================
if(BUILD_TESTING)
  enable_testing()
  include(GoogleTest)
  find_package(GTest REQUIRED)

  add_executable(
    daedalus_tests
    tests/test_main.cpp
    tests/protocol/test_telemetry.cpp
    tests/protocol/test_schema.cpp
    tests/protocol/test_client.cpp
    tests/data/test_signal_buffer.cpp
    tests/data/test_signal_tree.cpp
    tests/data/test_telemetry_queue.cpp
    tests/views/test_plotter.cpp)

  target_link_libraries(daedalus_tests PRIVATE daedalus_lib GTest::gtest
                                               GTest::gtest_main)

  gtest_discover_tests(daedalus_tests)
endif()
