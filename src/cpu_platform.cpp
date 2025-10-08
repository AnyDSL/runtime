#include "cpu_platform.h"
#include "runtime.h"

#include <cstddef>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

CpuPlatform::CpuPlatform(Runtime* runtime)
    : Platform(runtime)
{
    #if defined(__APPLE__)
    size_t buf_len;
    sysctlbyname("machdep.cpu.brand_string", nullptr, &buf_len, nullptr, 0);
    device_name_.resize(buf_len, '\0');
    sysctlbyname("machdep.cpu.brand_string", device_name_.data(), &buf_len, nullptr, 0);
    #elif defined(_WIN32)
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0U, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        error("failed to open processor information registry key");

    DWORD cpu_name_type, cpu_name_size;
    if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, &cpu_name_type, nullptr, &cpu_name_size) != ERROR_SUCCESS)
        error("failed to query processor name string length");

    if (cpu_name_type != REG_SZ)
        error("unexpected type for processor name string");

    int cpu_name_length = cpu_name_size / sizeof(wchar_t);

    std::wstring buffer(cpu_name_length, '\0');
    if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, &cpu_name_type, reinterpret_cast<LPBYTE>(buffer.data()), &cpu_name_size) != ERROR_SUCCESS)
        error("failed to query processor name string");

    RegCloseKey(key);

    int u8_cpu_name_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), cpu_name_length, nullptr, 0, nullptr, nullptr);

    if (u8_cpu_name_length <= 0)
        error("failed to compute converted UTF-8 CPU name string length");

    device_name_.resize(u8_cpu_name_length, '\0');

    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), cpu_name_length, device_name_.data(), u8_cpu_name_length, nullptr, nullptr) <= 0)
        error("failed to convert CPU name string to UTF-8");
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
