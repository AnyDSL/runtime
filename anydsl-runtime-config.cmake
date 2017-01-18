# Try to find Runtime library and include path.
# Once done this will define
#
# ANYDSL_RUNTIME_DIR
# ANYDSL_RUNTIME_LIBRARY
# ANYDSL_RUNTIME_CMAKE_DIR
# ANYDSL_RUNTIME_FOUND

cmake_minimum_required(VERSION 3.1)

find_path(ANYDSL_RUNTIME_DIR anydsl-runtime-config.cmake PATHS ${AnyDSL-runtime_DIR} $ENV{AnyDSL-runtime_DIR})
list(APPEND CMAKE_MODULE_PATH "${ANYDSL_RUNTIME_DIR}")
list(APPEND CMAKE_MODULE_PATH "${ANYDSL_RUNTIME_DIR}/cmake/modules")

function(generate_library_names OUT_VAR LIB)
    set(${OUT_VAR} ${LIB}.lib ${LIB}.so ${LIB}.a ${LIB}.dll ${LIB}.dylib lib${LIB} lib${LIB}.so lib${LIB}.a lib${LIB}.dll lib${LIB}.dylib PARENT_SCOPE)
endfunction()

find_path(ANYDSL_RUNTIME_CMAKE_DIR NAMES Runtime.cmake PATHS ${ANYDSL_RUNTIME_DIR}/build_debug/cmake ${ANYDSL_RUNTIME_DIR}/build_release/cmake ${ANYDSL_RUNTIME_DIR}/build/cmake)
generate_library_names(ANYDSL_RUNTIME_LIBS runtime)
find_library(ANYDSL_RUNTIME_LIBRARY NAMES ${ANYDSL_RUNTIME_LIBS} PATHS ${ANYDSL_RUNTIME_DIR}/build_debug/lib ${ANYDSL_RUNTIME_DIR}/build_release/lib ${ANYDSL_RUNTIME_DIR}/build/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AnyDSL-Runtime DEFAULT_MSG ANYDSL_RUNTIME_DIR ANYDSL_RUNTIME_LIBRARY ANYDSL_RUNTIME_CMAKE_DIR)

mark_as_advanced(ANYDSL_RUNTIME_DIR ANYDSL_RUNTIME_CMAKE_DIR)
