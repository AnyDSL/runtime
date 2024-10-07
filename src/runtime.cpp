#include <sstream>
#include <fstream>

#include "anydsl_runtime.h"

#include "runtime.h"
#include "platform.h"
#include "dummy_platform.h"
#include "cpu_platform.h"

#ifndef AnyDSL_runtime_HAS_CUDA_SUPPORT
void register_cuda_platform(Runtime* runtime) { runtime->register_platform<DummyPlatform>("CUDA"); }
#endif
#ifndef AnyDSL_runtime_HAS_OPENCL_SUPPORT
void register_opencl_platform(Runtime* runtime) { runtime->register_platform<DummyPlatform>("OpenCL"); }
#endif
#ifndef AnyDSL_runtime_HAS_HSA_SUPPORT
void register_hsa_platform(Runtime* runtime) { runtime->register_platform<DummyPlatform>("HSA"); }
#endif
#ifndef AnyDSL_runtime_HAS_PAL_SUPPORT
void register_pal_platform(Runtime* runtime) { runtime->register_platform<DummyPlatform>("PAL"); }
#endif
#ifndef AnyDSL_runtime_HAS_LEVELZERO_SUPPORT
void register_levelzero_platform(Runtime* runtime) { runtime->register_platform<DummyPlatform>("Level Zero"); }
#endif

Runtime::Runtime(std::pair<ProfileLevel, ProfileLevel> profile)
    : profile_(profile)
    , cache_dir_("")
{}

void Runtime::display_info() const {
    info("Available platforms:");
    for (auto& p: platforms_) {
        info("    * %: % device(s)", p->name(), p->dev_count());
        for (size_t d=0; d<p->dev_count(); ++d)
            info("      + (%) %", d, p->device_name(DeviceId(d)));
    }
}

const char* Runtime::device_name(PlatformId plat, DeviceId dev) const {
    check_device(plat, dev);
    return platforms_[plat]->device_name(dev);
}

bool Runtime::device_check_feature_support(PlatformId plat, DeviceId dev, const char* feature) const {
    check_device(plat, dev);
    return platforms_[plat]->device_check_feature_support(dev, feature);
}

void* Runtime::alloc(PlatformId plat, DeviceId dev, int64_t size) {
    check_device(plat, dev);
    return platforms_[plat]->alloc(dev, size);
}

void* Runtime::alloc_host(PlatformId plat, DeviceId dev, int64_t size) {
    check_device(plat, dev);
    return platforms_[plat]->alloc_host(dev, size);
}

void* Runtime::alloc_unified(PlatformId plat, DeviceId dev, int64_t size) {
    check_device(plat, dev);
    return platforms_[plat]->alloc_unified(dev, size);
}

void* Runtime::get_device_ptr(PlatformId plat, DeviceId dev, void* ptr) {
    check_device(plat, dev);
    return platforms_[plat]->get_device_ptr(dev, ptr);
}

void Runtime::release(PlatformId plat, DeviceId dev, void* ptr) {
    check_device(plat, dev);
    platforms_[plat]->release(dev, ptr);
}

void Runtime::release_host(PlatformId plat, DeviceId dev, void* ptr) {
    check_device(plat, dev);
    platforms_[plat]->release_host(dev, ptr);
}

void Runtime::copy(
    PlatformId plat_src, DeviceId dev_src, const void* src, int64_t offset_src,
    PlatformId plat_dst, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    check_device(plat_src, dev_src);
    check_device(plat_dst, dev_dst);
    if (plat_src == plat_dst) {
        // Copy from same platform
        platforms_[plat_src]->copy(dev_src, src, offset_src, dev_dst, dst, offset_dst, size);
        debug("Copy between devices % and % on platform %", dev_src, dev_dst, plat_src);
    } else {
        // Copy from another platform
        if (plat_src == 0) {
            // Source is the CPU platform
            platforms_[plat_dst]->copy_from_host(src, offset_src, dev_dst, dst, offset_dst, size);
            debug("Copy from host to device % on platform %", dev_dst, plat_dst);
        } else if (plat_dst == 0) {
            // Destination is the CPU platform
            platforms_[plat_src]->copy_to_host(dev_src, src, offset_src, dst, offset_dst, size);
            debug("Copy to host from device % on platform %", dev_src, plat_src);
        } else {
            error("Cannot copy memory between different platforms");
        }
    }
}

void Runtime::launch_kernel(PlatformId plat, DeviceId dev, const LaunchParams& launch_params) {
    check_device(plat, dev);
    assert(launch_params.grid[0] > 0 && launch_params.grid[0] % launch_params.block[0] == 0 &&
           launch_params.grid[1] > 0 && launch_params.grid[1] % launch_params.block[1] == 0 &&
           launch_params.grid[2] > 0 && launch_params.grid[2] % launch_params.block[2] == 0 &&
           "The grid size is not a multiple of the block size");
    platforms_[plat]->launch_kernel(dev, launch_params);
}

void Runtime::synchronize(PlatformId plat, DeviceId dev) {
    check_device(plat, dev);
    platforms_[plat]->synchronize(dev);
}

#ifdef _WIN32
#include <direct.h>
#define PATH_DIR_SEPARATOR '\\'
#define create_directory(d) _mkdir(d)
#else
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#define PATH_DIR_SEPARATOR '/'
#define create_directory(d) { umask(0); mkdir(d, 0755); }
#endif

#if _XOPEN_SOURCE >= 500 || _POSIX_C_SOURCE >= 200112L || /* Glibc versions <= 2.19: */ _BSD_SOURCE
static std::string get_self_directory() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path)-1);
    if (len != -1) {
        path[len] = '\0';

        for (int i = len-1; i >= 0; --i) {
            if (path[i] == PATH_DIR_SEPARATOR)
                return std::string(&path[0], i);
        }
    }
    return std::string();
}
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
static std::string get_self_directory() {
    char path[PATH_MAX];
    uint32_t size = (uint32_t)sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char resolved[PATH_MAX];
        if (realpath(path, resolved)) {
            std::string resolved_path = std::string(resolved);
            for (int i = resolved_path.size()-1; i >= 0; --i) {
                if (resolved_path[i] == PATH_DIR_SEPARATOR)
                    return std::string(resolved_path, 0, i);
            }
        }
    }
    return std::string();
}
#elif defined(_WIN32)
#include <Windows.h>
static std::string get_self_directory() {
    CHAR path[MAX_PATH];
    DWORD nSize = (DWORD)sizeof(path);
    DWORD length = GetModuleFileNameA(NULL, path, nSize);
    if ((length == 0) || (length == MAX_PATH))
        return std::string();

    std::string resolved_path(path);
    for (std::size_t i = resolved_path.size() - 1; i >= 0; --i) {
        if (resolved_path[i] == PATH_DIR_SEPARATOR)
            return std::string(resolved_path, 0, i);
    }

    return std::string();
}
#else
static std::string get_self_directory() {
    return std::string();
}
#endif

void Runtime::set_cache_directory(const std::string& dir) {
    cache_dir_ = dir;
}

std::string Runtime::get_cache_directory() const {
    if (cache_dir_.empty()) {
        std::string cache_path = get_self_directory();
        if (!cache_path.empty())
            cache_path += PATH_DIR_SEPARATOR;
        return cache_path + "cache";
    } else {
        return cache_dir_;
    }
}

std::string Runtime::get_cached_filename(const std::string& str, const std::string& ext) const {
    size_t key = std::hash<std::string>{}(str);
    std::stringstream hex_stream;
    hex_stream << std::hex << key;
    return get_cache_directory() + PATH_DIR_SEPARATOR + hex_stream.str() + ext;
}

inline std::string read_stream(std::istream& stream) {
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::string Runtime::load_file(const std::string& filename) const {
    auto file_it = files_.find(filename);
    if (file_it != files_.end())
        return file_it->second;

    std::ifstream src_file(filename);
    if (!src_file)
        error("Can't open source file '%'", filename);
    return read_stream(src_file);
}

void Runtime::store_file(const std::string& filename, const std::string& str) const {
    store_file(filename, reinterpret_cast<const std::byte*>(str.data()), str.length());
}

void Runtime::store_file(const std::string& filename, const std::byte* data, size_t size) const {
    std::ofstream dst_file(filename, std::ofstream::binary);
    if (!dst_file)
        error("Can't open destination file '%'", filename);
    dst_file.write(reinterpret_cast<const char*>(data), size);
}

std::string Runtime::load_from_cache(const std::string& key, const std::string& ext) const {
    std::string filename = get_cached_filename(key, ext);
    std::ifstream src_file(filename, std::ifstream::binary);
    if (!src_file.is_open())
        return std::string();
    // prevent collision by storing the key in the cached file
    size_t size = 0;
    if (!src_file.read(reinterpret_cast<char*>(&size), sizeof(size_t)))
        return std::string();
    auto buf = std::make_unique<char[]>(size);
    if (!src_file.read(buf.get(), size))
        return std::string();
    if (std::memcmp(key.data(), buf.get(), size))
        return std::string();
    debug("Loading from cache: %", filename);
    return read_stream(src_file);
}

void Runtime::store_to_cache(const std::string& key, const std::string& str, const std::string ext) const {
    std::string filename = get_cached_filename(key, ext);
    create_directory(get_cache_directory().c_str());
    debug("Storing to cache: %", filename);
    std::ofstream dst_file(filename, std::ofstream::binary);
    size_t size = key.size();
    dst_file.write(reinterpret_cast<char*>(&size), sizeof(size_t));
    dst_file.write(key.data(), size);
    dst_file.write(str.data(), str.size());
}

#if _POSIX_VERSION >= 200112L || _XOPEN_SOURCE >= 600
void* Runtime::aligned_malloc(size_t size, size_t alignment) {
    void* p = nullptr;
    posix_memalign(&p, alignment, size);
    return p;
}
void Runtime::aligned_free(void* ptr) {
    free(ptr);
}
#elif _ISOC11_SOURCE
void* Runtime::aligned_malloc(size_t size, size_t alignment) {
    return ::aligned_alloc(alignment, size);
}
void Runtime::aligned_free(void* ptr) {
    ::free(ptr);
}
#elif defined(_WIN32) || defined(__CYGWIN__)
#include <malloc.h>
void* Runtime::aligned_malloc(size_t size, size_t alignment) {
    return ::_aligned_malloc(size, alignment);
}
void Runtime::aligned_free(void* ptr) {
    ::_aligned_free(ptr);
}
#else
#error "There is no way to allocate aligned memory on this system"
#endif

void Runtime::check_device(PlatformId plat, DeviceId dev) const {
    assert((size_t)dev < platforms_[plat]->dev_count() && "Invalid device");
    unused(plat, dev);
}
