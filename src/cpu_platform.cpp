#include "cpu_platform.h"
#include "runtime.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif


CpuPlatform::CpuPlatform(Runtime* runtime)
    : Platform(runtime)
{
    #if defined(__APPLE__)
    size_t buf_len;
    sysctlbyname("machdep.cpu.brand_string", nullptr, &buf_len, nullptr, 0);
    char buf[buf_len];
    sysctlbyname("machdep.cpu.brand_string", &buf, &buf_len, nullptr, 0);
    device_name_ = buf;
    #elif defined(_WIN32)
    // TODO
    #else
    std::ifstream cpuinfo("/proc/cpuinfo");

    if (!cpuinfo)
        error("failed to open /proc/cpuinfo");

    #if defined __arm__ || __aarch64__
    std::string model_string = "CPU part\t: ";
    #else // x86, x86_64
    std::string model_string = "model name\t: ";
    #endif

    std::search(std::istreambuf_iterator<char>(cpuinfo), {}, model_string.begin(), model_string.end());
    std::getline(cpuinfo >> std::ws, device_name_);
    #endif
}
