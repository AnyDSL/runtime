#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <locale>
#include <random>
#include <unordered_map>
#include <vector>
#include <memory>

#if !defined(_WIN32) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__)))
#include <unistd.h>
#endif

#ifdef ENABLE_TBB
#include <tbb/tbb.h>
#include <tbb/parallel_for.h>
#include <tbb/task_scheduler_init.h>
#else
#include <thread>
#endif

#include "anydsl_runtime.h"

#include "runtime.h"
#include "platform.h"
#include "cpu_platform.h"
#include "dummy_platform.h"
#ifdef ENABLE_CUDA
#include "cuda_platform.h"
#endif
#ifdef ENABLE_OPENCL
#include "opencl_platform.h"
#endif
#ifdef ENABLE_HSA
#include "hsa_platform.h"
#endif

Runtime& runtime() {
    static std::unique_ptr<Runtime> runtime(new Runtime());
    return *runtime;
}

Runtime::Runtime() {
    profile_ = ProfileLevel::None;
    char* env_var = std::getenv("ANYDSL_PROFILE");
    if (env_var) {
        std::string env_str = env_var;
        for (auto& c: env_str)
            c = std::toupper(c, std::locale());
        if (env_str == "FULL")
            profile_ = ProfileLevel::Full;
    }

    register_platform<CpuPlatform>();
#ifdef ENABLE_CUDA
    register_platform<CudaPlatform>();
#else
    register_platform<DummyPlatform>("CUDA");
#endif
#ifdef ENABLE_OPENCL
    register_platform<OpenCLPlatform>();
#else
    register_platform<DummyPlatform>("OpenCL");
#endif
#ifdef ENABLE_HSA
    register_platform<HSAPlatform>();
#else
    register_platform<DummyPlatform>("HSA");
#endif
}

inline PlatformId to_platform(int32_t m) {
    return PlatformId(m & 0x0F);
}

inline DeviceId to_device(int32_t m) {
    return DeviceId(m >> 4);
}

void anydsl_info(void) {
    runtime().display_info();
}

void* anydsl_alloc(int32_t mask, int64_t size) {
    return runtime().alloc(to_platform(mask), to_device(mask), size);
}

void* anydsl_alloc_host(int32_t mask, int64_t size) {
    return runtime().alloc_host(to_platform(mask), to_device(mask), size);
}

void* anydsl_alloc_unified(int32_t mask, int64_t size) {
    return runtime().alloc_unified(to_platform(mask), to_device(mask), size);
}

void* anydsl_get_device_ptr(int32_t mask, void* ptr) {
    return runtime().get_device_ptr(to_platform(mask), to_device(mask), ptr);
}

void anydsl_release(int32_t mask, void* ptr) {
    runtime().release(to_platform(mask), to_device(mask), ptr);
}

void anydsl_release_host(int32_t mask, void* ptr) {
    runtime().release_host(to_platform(mask), to_device(mask), ptr);
}

void anydsl_copy(int32_t mask_src, const void* src, int64_t offset_src,
                 int32_t mask_dst, void* dst, int64_t offset_dst, int64_t size) {
    runtime().copy(to_platform(mask_src), to_device(mask_src), src, offset_src,
                   to_platform(mask_dst), to_device(mask_dst), dst, offset_dst, size);
}

void anydsl_launch_kernel(int32_t mask,
                          const char* file, const char* kernel,
                          const uint32_t* grid, const uint32_t* block,
                          void** args, const uint32_t* sizes, const uint8_t* types,
                          uint32_t num_args) {
    runtime().launch_kernel(to_platform(mask), to_device(mask),
                            file, kernel,
                            grid, block,
                            args, sizes, reinterpret_cast<const KernelArgType*>(types),
                            num_args);
}

void anydsl_synchronize(int32_t mask) {
    runtime().synchronize(to_platform(mask), to_device(mask));
}

#if _POSIX_VERSION >= 200112L || _XOPEN_SOURCE >= 600
void* anydsl_aligned_malloc(size_t size, size_t alignment) {
    void* p = nullptr;
    posix_memalign(&p, alignment, size);
    return p;
}
void anydsl_aligned_free(void* ptr) { free(ptr); }
#elif _ISOC11_SOURCE
void* anydsl_aligned_malloc(size_t size, size_t alignment) { return ::aligned_alloc(alignment, size); }
void anydsl_aligned_free(void* ptr) { ::free(ptr); }
#elif defined(_WIN32) || defined(__CYGWIN__)
#include <malloc.h>

void* anydsl_aligned_malloc(size_t size, size_t alignment) { return ::_aligned_malloc(size, alignment); }
void anydsl_aligned_free(void* ptr) { ::_aligned_free(ptr); }
#else
#error "There is no way to allocate aligned memory on this system"
#endif

uint64_t anydsl_get_micro_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

std::atomic<uint64_t> anydsl_kernel_time(0);

uint64_t anydsl_get_kernel_time() {
    return anydsl_kernel_time;
}

int32_t anydsl_isinff(float x)    { return std::isinf(x); }
int32_t anydsl_isnanf(float x)    { return std::isnan(x); }
int32_t anydsl_isfinitef(float x) { return std::isfinite(x); }
int32_t anydsl_isinf(double x)    { return std::isinf(x); }
int32_t anydsl_isnan(double x)    { return std::isnan(x); }
int32_t anydsl_isfinite(double x) { return std::isfinite(x); }

void anydsl_print_i16(int16_t s)  { std::cout << s << std::flush; }
void anydsl_print_i32(int32_t i)  { std::cout << i << std::flush; }
void anydsl_print_i64(int64_t l)  { std::cout << l << std::flush; }
void anydsl_print_f32(float f)    { std::cout << f << std::flush; }
void anydsl_print_f64(double d)   { std::cout << d << std::flush; }
void anydsl_print_char(char c)    { std::cout << c << std::flush; }
void anydsl_print_string(char* s) { std::cout << s << std::flush; }

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if (defined (__clang__) && !__has_feature(cxx_thread_local))
#pragma message("Runtime random function is not thread-safe")
static std::mt19937 std_gen;
#else
static thread_local std::mt19937 std_gen;
#endif
static std::uniform_real_distribution<float>   std_dist_f32;
static std::uniform_int_distribution<uint64_t> std_dist_u64;

void anydsl_random_seed(uint32_t seed) {
    std_gen.seed(seed);
}

float anydsl_random_val_f32() {
    return std_dist_f32(std_gen);
}

uint64_t anydsl_random_val_u64() {
    return std_dist_u64(std_gen);
}

#ifndef ENABLE_TBB // C++11 threads version
static std::unordered_map<int32_t, std::thread> thread_pool;
static std::vector<int32_t> free_ids;

void anydsl_parallel_for(int32_t num_threads, int32_t lower, int32_t upper, void* args, void* fun) {
    // Get number of available hardware threads
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        // hardware_concurrency is implementation defined, may return 0
        num_threads = (num_threads == 0) ? 1 : num_threads;
    }

    void (*fun_ptr) (void*, int32_t, int32_t) = reinterpret_cast<void (*) (void*, int32_t, int32_t)>(fun);
    const int32_t linear = (upper - lower) / num_threads;

    // Create a pool of threads to execute the task
    std::vector<std::thread> pool(num_threads);

    for (int i = 0, a = lower, b = lower + linear; i < num_threads - 1; a = b, b += linear, i++) {
        pool[i] = std::thread([=]() {
            fun_ptr(args, a, b);
        });
    }

    pool[num_threads - 1] = std::thread([=]() {
        fun_ptr(args, lower + (num_threads - 1) * linear, upper);
    });

    // Wait for all the threads to finish
    for (int i = 0; i < num_threads; i++)
        pool[i].join();
}

int32_t anydsl_spawn_thread(void* args, void* fun) {
    int32_t (*fun_ptr) (void*) = reinterpret_cast<int32_t (*) (void*)>(fun);

    int32_t id;
    if (free_ids.size()) {
        id = free_ids.back();
        free_ids.pop_back();
    } else {
        id = static_cast<int32_t>(thread_pool.size());
    }

    auto spawned = std::make_pair(id, std::thread([=](){ fun_ptr(args); }));
    thread_pool.emplace(std::move(spawned));
    return id;
}

void anydsl_sync_thread(int32_t id) {
    auto thread = thread_pool.find(id);
    if (thread != thread_pool.end()) {
        thread->second.join();
        free_ids.push_back(thread->first);
        thread_pool.erase(thread);
    } else {
        assert(0 && "Trying to synchronize on invalid thread id");
    }
}
#else // TBB version
void anydsl_parallel_for(int32_t num_threads, int32_t lower, int32_t upper, void* args, void* fun) {
    tbb::task_scheduler_init init((num_threads == 0) ? tbb::task_scheduler_init::automatic : num_threads);
    void (*fun_ptr) (void*, int32_t, int32_t) = reinterpret_cast<void (*) (void*, int32_t, int32_t)>(fun);

    tbb::parallel_for(tbb::blocked_range<int32_t>(lower, upper), [=] (const tbb::blocked_range<int32_t>& range) {
        fun_ptr(args, range.begin(), range.end());
    });
}

static std::unordered_map<int32_t, tbb::task*> task_pool;
static std::vector<int32_t> free_ids;

class RuntimeTask : public tbb::task {
public:
    RuntimeTask(void* args, void* fun)
        : args_(args), fun_(fun)
    {}

    tbb::task* execute() {
        int32_t (*fun_ptr) (void*) = reinterpret_cast<int32_t (*) (void*)>(fun_);
        fun_ptr(args_);
        return nullptr;
    }

private:
    void* args_;
    void* fun_;
};

int32_t anydsl_spawn_thread(void* args, void* fun) {
    int32_t id;
    if (free_ids.size()) {
        id = free_ids.back();
        free_ids.pop_back();
    } else {
        id = task_pool.size();
    }

    tbb::task* root = new (tbb::task::allocate_root()) RuntimeTask(args, fun);
    root->set_ref_count(2);
    tbb::task* child = new (root->allocate_child()) RuntimeTask(args, fun);
    root->spawn(*child);
    task_pool[id] = root;
    return id;
}

void anydsl_sync_thread(int32_t id) {
    auto task = task_pool.find(id);
    if (task != task_pool.end()) {
        task->second->wait_for_all();
        tbb::task::destroy(*task->second);
        free_ids.push_back(task->first);
        task_pool.erase(task);
    } else {
        assert(0 && "Trying to synchronize on invalid task id");
    }
}
#endif
