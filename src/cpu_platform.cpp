
#include "cpu_platform.h"


template<> template<>
Platform* PlatformFactory<CpuPlatform>::create(Runtime* runtime, const std::string&) {
    return new CpuPlatform(runtime);
}
