cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(AnyDSL-runtime)

set(PACKAGE_VERSION "0.3.9")
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "limited config" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(RUNTIME_JIT "enable jit support in the runtime" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug or Release" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_path(Artic_DIR NAMES artic-config.cmake PATHS ${Artic_DIR} $ENV{Artic_DIR} ${CMAKE_BINARY_DIR}/share/anydsl/cmake)
find_path(Impala_DIR NAMES impala-config.cmake PATHS ${Impala_DIR} $ENV{Impala_DIR} ${CMAKE_BINARY_DIR}/share/anydsl/cmake)

add_subdirectory(src)

message(STATUS "Using Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "Using Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
if(DEFINED CMAKE_BUILD_TYPE)
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

export(TARGETS ${RUNTIME_LIBRARIES} FILE ${CMAKE_BINARY_DIR}/share/anydsl/cmake/anydsl_runtime-exports.cmake)
configure_file(cmake/anydsl_runtime-config.cmake.in ${CMAKE_BINARY_DIR}/share/anydsl/cmake/anydsl_runtime-config.cmake @ONLY)
