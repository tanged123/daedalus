cmake_minimum_required(VERSION 3.20)
project(
  daedalus
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# libwebsockets (may need pkg-config or find_package depending on install)
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(LWS libwebsockets)
endif()
if(NOT LWS_FOUND)
  find_package(Libwebsockets REQUIRED)
endif()

# =============================================================================
# Main library
# =============================================================================
add_library(daedalus_lib STATIC src/daedalus/app.cpp)

target_include_directories(daedalus_lib
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(
  daedalus_lib PUBLIC glfw OpenGL::GL nlohmann_json::nlohmann_json
                      Threads::Threads)

if(LWS_FOUND)
  target_include_directories(daedalus_lib PUBLIC ${LWS_INCLUDE_DIRS})
  target_link_libraries(daedalus_lib PUBLIC ${LWS_LIBRARIES})
endif()

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
  find_package(GTest REQUIRED)

  add_executable(daedalus_tests tests/test_main.cpp)

  target_link_libraries(daedalus_tests PRIVATE daedalus_lib GTest::gtest
                                               GTest::gtest_main)

  add_test(NAME daedalus_tests COMMAND daedalus_tests)
endif()
