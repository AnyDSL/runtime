
#include "cpu_platform.h"


template<> template<>
Platform* PlatformFactory<CpuPlatform>::create(Runtime* runtime, const std::string& reference) {
    return new CpuPlatform(runtime);
}
