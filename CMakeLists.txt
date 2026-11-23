cmake_minimum_required(VERSION 3.20)

project(ssc32_hil VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
include(GNUInstallDirs)

option(SSC32_HIL_BUILD_TESTING "Build the test suite" ON)
option(SSC32_HIL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(SSC32_HIL_SSC32_SOURCE_DIR "" CACHE PATH
    "Use an existing ssc32 source checkout instead of fetching it")

if(SSC32_HIL_SSC32_SOURCE_DIR)
  set(SSC32_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  add_subdirectory("${SSC32_HIL_SSC32_SOURCE_DIR}" ssc32-dependency)
else()
  find_package(ssc32 0.1 CONFIG QUIET)
  if(NOT ssc32_FOUND)
    set(SSC32_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      ssc32_dependency
      GIT_REPOSITORY https://github.com/MartinPeris/ssc32.git
      GIT_TAG d096754e7a47da0b737cd35f9fdd2fa9545e29a0
      GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(ssc32_dependency)
  endif()
endif()

FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(ssc32_hil_core
  src/config.cpp
  src/evidence.cpp
  src/plan.cpp
  src/recording_transport.cpp
  src/runner.cpp
)
add_library(ssc32_hil::core ALIAS ssc32_hil_core)
target_compile_features(ssc32_hil_core PUBLIC cxx_std_17)
target_include_directories(ssc32_hil_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ssc32_hil_core
  PUBLIC ssc32::ssc32
  PRIVATE tomlplusplus::tomlplusplus
)

add_executable(ssc32-hil src/main.cpp)
target_link_libraries(ssc32-hil PRIVATE ssc32_hil::core)

function(ssc32_hil_enable_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
      $<$<BOOL:${SSC32_HIL_WARNINGS_AS_ERRORS}>:-Werror>
    )
  endif()
endfunction()

ssc32_hil_enable_warnings(ssc32_hil_core)
ssc32_hil_enable_warnings(ssc32-hil)

install(TARGETS ssc32-hil RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY profiles scenarios DESTINATION ${CMAKE_INSTALL_DATADIR}/ssc32-hil)

if(SSC32_HIL_BUILD_TESTING)
  enable_testing()

  add_executable(ssc32_hil_tests tests/ssc32_hil_tests.cpp)
  target_link_libraries(ssc32_hil_tests PRIVATE ssc32_hil::core)
  target_compile_features(ssc32_hil_tests PRIVATE cxx_std_17)
  target_compile_definitions(ssc32_hil_tests PRIVATE
    SSC32_HIL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  ssc32_hil_enable_warnings(ssc32_hil_tests)
  add_test(NAME ssc32_hil_tests COMMAND ssc32_hil_tests)

  add_executable(ssc32_hil_coverage_tests tests/coverage_tests.cpp)
  target_link_libraries(ssc32_hil_coverage_tests PRIVATE ssc32_hil::core)
  target_compile_features(ssc32_hil_coverage_tests PRIVATE cxx_std_17)
  ssc32_hil_enable_warnings(ssc32_hil_coverage_tests)
  add_test(NAME ssc32_hil_coverage_tests COMMAND ssc32_hil_coverage_tests)

  add_test(
    NAME ssc32_hil_cli_tests
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.sh
            $<TARGET_FILE:ssc32-hil> ${CMAKE_CURRENT_SOURCE_DIR}
  )

  if(UNIX)
    add_executable(ssc32_hil_pty_test tests/pty_integration.cpp)
    target_link_libraries(ssc32_hil_pty_test PRIVATE ssc32_hil::core)
    target_compile_features(ssc32_hil_pty_test PRIVATE cxx_std_17)
    ssc32_hil_enable_warnings(ssc32_hil_pty_test)
    add_test(NAME ssc32_hil_pty_test COMMAND ssc32_hil_pty_test)
  endif()
endif()
