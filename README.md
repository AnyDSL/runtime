# AnyDSL Runtime Library
The runtime for the AnyDSL framework and its two frontends [artic](https://github.com/AnyDSL/artic) and [impala](https://github.com/AnyDSL/impala).

The runtime provides the following components:
- CMake logic to build programs using artic or impala
- include files for basic runtime abstractions and math functions
- runtime library implementation to schedule and execute AnyDSL programs on different platforms
  + Host (CPU): standard platform for code
    + TBB / C++11 threads: code emitted by `parallel`
    + LLVM w/ RV support: code emitted by `vectorize`
  + CUDA: code emitted by `cuda` or `nvvm`
  + OpenCL: code emitted by `opencl`
  + HSA: code emitted by `amdgpu`

CMake automatically search for available components on the current system.
To prevent CMake from building a particular runtime component, disable it using CMake's `CMAKE_DISABLE_FIND_PACKAGE_<PackageName>` variable.
For example, pass `-DCMAKE_DISABLE_FIND_PACKAGE_OpenCL=TRUE` to cmake to disable the OpenCL runtime component.

Although not required, feel free to specify `Artic_DIR` or `Impala_DIR` for your convenience to later automatically find the correct paths when building AnyDSL programs using the `anydsl_runtime_wrap()` function.

To enable JIT support, please pass `-DRUNTIME_JIT=ON` to cmake.
This will require atleast one of artic or impala as dependencies and thereby locate LLVM as well as [thorin](https://github.com/AnyDSL/thorin) too.
