# Try to find Runtime library and include path.
# Once done this will define
#
# RUNTIME_DIR
# RUNTIME_LIBRARY
# RUNTIME_CMAKE_DIR
# RUNTIME_FOUND

cmake_minimum_required(VERSION 3.1)

find_path(RUNTIME_DIR runtime-config.cmake PATHS ${RUNTIME_DIR} $ENV{RUNTIME_DIR})
list(APPEND CMAKE_MODULE_PATH "${RUNTIME_DIR}")
list(APPEND CMAKE_MODULE_PATH "${RUNTIME_DIR}/cmake/modules")

function(generate_library_names OUT_VAR LIB)
    set(${OUT_VAR} ${LIB}.lib ${LIB}.so ${LIB}.a ${LIB}.dll ${LIB}.dylib lib${LIB} lib${LIB}.so lib${LIB}.a lib${LIB}.dll lib${LIB}.dylib PARENT_SCOPE)
endfunction()

find_path(RUNTIME_CMAKE_DIR NAMES Runtime.cmake PATHS ${RUNTIME_DIR}/build_debug/cmake ${RUNTIME_DIR}/build_release/cmake ${RUNTIME_DIR}/build/cmake)
find_path(RUNTIME_DIR NAMES cmake/Runtime.cmake.in platforms/intrinsics_thorin.impala PATHS ${RUNTIME_DIR}/runtime)
generate_library_names(RUNTIME_LIBS runtime)
find_library(RUNTIME_LIBRARY NAMES ${RUNTIME_LIBS} PATHS ${RUNTIME_DIR}/build_debug/lib ${RUNTIME_DIR}/build_release/lib ${RUNTIME_DIR}/build/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Runtime DEFAULT_MSG RUNTIME_DIR RUNTIME_LIBRARY RUNTIME_CMAKE_DIR)

mark_as_advanced(RUNTIME_DIR RUNTIME_CMAKE_DIR)
