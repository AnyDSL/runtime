#include "cpu_platform.h"
#include "runtime.h"

#include <cstddef>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <condition_variable>
#include <mutex>

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

bool CpuPlatform::device_check_feature_support(DeviceId, const char* feature) const {
    using namespace std::literals;

    if (feature == "event"sv)
        return true;
    return false;
}

struct CpuEvent {
    std::mutex mutex;
    std::condition_variable cv;
    bool recorded = false;
    std::chrono::high_resolution_clock::time_point pointOfRecord;
};

EventId CpuPlatform::create_event(DeviceId) {
    CpuEvent* event = new CpuEvent;
    return (EventId)reinterpret_cast<uintptr_t>(event);
}

void CpuPlatform::destroy_event(DeviceId, EventId event) {
    auto eventPtr = reinterpret_cast<CpuEvent*>((uintptr_t)event);
    delete eventPtr;
}

void CpuPlatform::record_event(DeviceId, EventId event) {
    auto eventPtr = reinterpret_cast<CpuEvent*>((uintptr_t)event);

    std::unique_lock lk(eventPtr->mutex);
    eventPtr->recorded      = true;
    eventPtr->pointOfRecord = std::chrono::high_resolution_clock::now();
    lk.unlock();

    eventPtr->cv.notify_all();
}

bool CpuPlatform::check_event(DeviceId, EventId event) {
    auto eventPtr = reinterpret_cast<CpuEvent*>((uintptr_t)event);
    return eventPtr->recorded;
}

uint64_t CpuPlatform::query_us_event(DeviceId dev, EventId event_start, EventId event_end) {
    if (!check_event(dev, event_start) || !check_event(dev, event_end)) return UINT64_MAX;

    auto eventStartPtr = reinterpret_cast<CpuEvent*>((uintptr_t)event_start);
    auto eventEndPtr = reinterpret_cast<CpuEvent*>((uintptr_t)event_end);

    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(eventEndPtr->pointOfRecord - eventStartPtr->pointOfRecord).count();
}

void CpuPlatform::sync_event(DeviceId, EventId event){
    auto eventPtr = reinterpret_cast<CpuEvent*>((uintptr_t)event);

    std::unique_lock lk(eventPtr->mutex);
    eventPtr->cv.wait(lk, [eventPtr]() { return eventPtr->recorded; });
}
