#include "cpu_platform.h"
#include "runtime.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <new>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>

#include <WbemIdl.h>

namespace
{
    struct COM_deleter
    {
        void operator ()(IUnknown* ptr) const
        {
            ptr->Release();
        }
    };

    template <typename T>
    using unique_com_ptr = std::unique_ptr<T, COM_deleter>;

    auto make_bstr(const wchar_t* str)
    {
        struct deleter
        {
            void operator ()(BSTR ptr) const
            {
                SysFreeString(ptr);
            }
        };

        return std::unique_ptr<wchar_t[], deleter> { SysAllocString(str) };
    }

    auto bstr_length(BSTR str)
    {
        return *std::launder(reinterpret_cast<const DWORD*>(reinterpret_cast<const std::byte*>(str) - sizeof(DWORD))) / 2;
    }

    std::string utf16_to_utf8(const WCHAR* input, int input_length)
    {
        int output_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, input_length, nullptr, 0, nullptr, nullptr);

        if (output_length <= 0)
            error("failed to get UTF-8 string length");

        std::string output(output_length, '\0');

        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, input_length, &output[0], output_length, nullptr, nullptr) <= 0)
            error("failed to get UTF-8 string");

        return output;
    }

    auto create_WbemLocator()
    {
        void* locator;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, &locator)))
            error("failed to create WbemLocator");
        return unique_com_ptr<IWbemLocator>(static_cast<IWbemLocator*>(locator));
    }

    auto connect_server(IWbemLocator* locator, const wchar_t* server)
    {
        IWbemServices* services;
        if (FAILED(locator->ConnectServer(make_bstr(server).get(), nullptr, nullptr, nullptr, 0L, nullptr, nullptr, &services)))
            error("failed to connect to WBEM resource");
        return unique_com_ptr<IWbemServices>(services);
    }

    auto connect_WMI()
    {
        auto services = connect_server(create_WbemLocator().get(), L"root\\cimv2");

        if (FAILED(CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE)))
            error("CoSetProxyBlanket failed");

        return services;
    }

    auto exec_query(IWbemServices* services, const wchar_t* query)
    {
        IEnumWbemClassObject* enumerator;
        if (FAILED(services->ExecQuery(make_bstr(L"WQL").get(), make_bstr(query).get(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator)))
            error("failed to execute WMI query");
        return unique_com_ptr<IEnumWbemClassObject>(enumerator);
    }
}
#endif

CpuPlatform::CpuPlatform(Runtime* runtime)
    : Platform(runtime)
{
    #if defined(__APPLE__)
    size_t buf_len;
    sysctlbyname("machdep.cpu.brand_string", nullptr, &buf_len, nullptr, 0);
    std::string buf(buf_len, ' ');
    sysctlbyname("machdep.cpu.brand_string", (void*)buf.c_str(), &buf_len, nullptr, 0);
    device_name_ = buf;
    #elif defined(_WIN32)
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        error("CoInitializeEx failed");

    if (FAILED(CoInitializeSecurity(nullptr, -1L, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr)))
        error("CoInitializeSecurity failed");

    auto services = connect_WMI();

    auto query_result = exec_query(services.get(), L"SELECT * FROM Win32_Processor");

    IWbemClassObject* obj;
    ULONG returned;
    if (FAILED(query_result->Next(WBEM_INFINITE, 1, &obj, &returned)) || returned != 1)
        error("failed to get WMI query result");

    VARIANT value;
    CIMTYPE type;
    if (FAILED(obj->Get(L"Name", 0L, &value, &type, nullptr)))
        error("failed to get processor name");

    if (type != CIM_STRING)
        error("unexpected value type for processor name");

    device_name_ = utf16_to_utf8(value.bstrVal, bstr_length(value.bstrVal));
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
