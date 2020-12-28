# AnyDSL Runtime Library
The runtime for the AnyDSL framework and its two frontends [artic](https://github.com/AnyDSL/artic) and [impala](https://github.com/AnyDSL/impala).

The runtime provides the following components:
- CMake files to build programs using artic or impala
- include files for basic runtime abstractions and math functions
- runtime library implementation to schedule and execute AnyDSL programs on different platforms
+ Host (CPU): standard platform for code and code emitted by `vectorize` and `parallel` (using TBB / C++11 threads)
+ CUDA: code emitted by `cuda` or `nvvm`
+ OpenCL: code emitted by `opencl`
+ HSA: code emitted by `amdgpu`

To prevent CMake from building a particular runtime component, disable it using CMake's `CMAKE_DISABLE_FIND_PACKAGE_<PackageName>` variable.
For example, pass `-DCMAKE_DISABLE_FIND_PACKAGE_OpenCL=TRUE` to cmake to disable the OpenCL runtime component.
