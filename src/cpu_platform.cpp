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
#elif defined(__linux__)
#include <unistd.h>
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

void get_cpu_info(int* cores, int* threads) {
    *cores = 0;
    *threads = 0;

#if defined(_WIN32)
    // Windows: Use GetLogicalProcessorInformationEx for cores/threads
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* buffer =
        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)malloc(len);

    if (GetLogicalProcessorInformationEx(RelationProcessorCore, buffer, &len)) {
        int core_count = 0;
        int thread_count = 0;
        char* ptr = (char*)buffer;
        char* end = ptr + len;
        while (ptr < end) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* info =
                (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)ptr;
            if (info->Relationship == RelationProcessorCore) {
                core_count++;
                // Count bits set in info->Processor.GroupMask->Mask
                DWORD_PTR mask = info->Processor.GroupMask[0].Mask;
                int bit_count = 0;
                while (mask) {
                    bit_count += (mask & 1);
                    mask >>= 1;
                }
                thread_count += bit_count;
            }
            ptr += info->Size;
        }
        *cores = core_count;
        *threads = thread_count;
    }
    free(buffer);

#elif defined(__APPLE__)
    // macOS: Use sysctlbyname
    int mib[2];
    size_t len;
    int ncpu = 0, nthreads = 0;

    mib[0] = CTL_HW; mib[1] = HW_PHYSCNT;
    len = sizeof(ncpu);
    sysctl(mib, 2, &ncpu, &len, NULL, 0);
    mib[1] = HW_LOGICALCPU;
    len = sizeof(nthreads);
    sysctl(mib, 2, &nthreads, &len, NULL, 0);

    *cores = ncpu;
    *threads = nthreads;
#elif defined(__linux__)
    // Linux: Use sysconf
    *cores = sysconf(_SC_NPROCESSORS_ONLN);
    *threads = sysconf(_SC_NPROCESSORS_CONF);
#endif
}

// Portable function to get the total system memory in bytes
size_t get_total_memory() {
    size_t mem = 0;

#if defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        mem = (size_t)status.ullTotalPhys;
    }
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    size_t len = sizeof(mem);
    sysctl(mib, 2, &mem, &len, NULL, 0);
#elif defined(__linux__)
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        mem = (size_t)pages * (size_t)page_size;
    }
#endif
    return mem;
}

int CpuPlatform::device_nodes(DeviceId) const {
    int cores = 0;
    int threads = 0;

    get_cpu_info(&cores, &threads);

    return cores;
}

int CpuPlatform::device_threads(DeviceId) const {
    int cores = 0;
    int threads = 0;

    get_cpu_info(&cores, &threads);

    return threads;
}

uint64_t CpuPlatform::device_memory(DeviceId) const {
    return get_total_memory();
}
