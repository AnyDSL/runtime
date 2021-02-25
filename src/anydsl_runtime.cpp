#include <random>
#include <chrono>
#include <locale>

#include "anydsl_runtime.h"
// Make sure the definition for runtime() matches
// the declaration in anydsl_jit.h
#include "anydsl_jit.h"

#include "runtime.h"
#include "platform.h"
#include "dummy_platform.h"
#include "cpu_platform.h"

#ifdef AnyDSL_runtime_HAS_TBB_SUPPORT
#define NOMINMAX
#include <tbb/flow_graph.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>
#else
#include <thread>
#endif

struct RuntimeSingleton {
    Runtime runtime;

    RuntimeSingleton()
        : runtime(detect_profile_level())
    {
        runtime.register_platform<CpuPlatform>();
        register_cuda_platform(&runtime);
        register_opencl_platform(&runtime);
        register_hsa_platform(&runtime);
    }

    static ProfileLevel detect_profile_level() {
        auto profile = ProfileLevel::None;
        const char* env_var = std::getenv("ANYDSL_PROFILE");
        if (env_var) {
            std::string env_str = env_var;
            for (auto& c: env_str)
                c = std::toupper(c, std::locale());
            if (env_str == "FULL")
                profile = ProfileLevel::Full;
        }
        return profile;
    }
};

Runtime& runtime() {
    static RuntimeSingleton singleton;
    return singleton.runtime;
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

void anydsl_copy(
    int32_t mask_src, const void* src, int64_t offset_src,
    int32_t mask_dst, void* dst, int64_t offset_dst, int64_t size) {
    runtime().copy(
        to_platform(mask_src), to_device(mask_src), src, offset_src,
        to_platform(mask_dst), to_device(mask_dst), dst, offset_dst, size);
}

void anydsl_launch_kernel(
    int32_t mask, const char* file_name, const char* kernel_name,
    const uint32_t* grid, const uint32_t* block,
    void** arg_data,
    const uint32_t* arg_sizes,
    const uint32_t* arg_aligns,
    const uint32_t* arg_alloc_sizes,
    const uint8_t* arg_types,
    uint32_t num_args) {
    LaunchParams launch_params = {
        file_name,
        kernel_name,
        grid,
        block,
        {
            arg_data,
            arg_sizes,
            arg_aligns,
            arg_alloc_sizes,
            reinterpret_cast<const KernelArgType*>(arg_types),
        },
        num_args
    };
    runtime().launch_kernel(to_platform(mask), to_device(mask), launch_params);
}

void anydsl_synchronize(int32_t mask) {
    runtime().synchronize(to_platform(mask), to_device(mask));
}

uint64_t anydsl_get_micro_time() {
    using namespace std::chrono;
    return duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

uint64_t anydsl_get_kernel_time() {
    return runtime().kernel_time().load();
}

int32_t anydsl_isinff(float x)    { return std::isinf(x); }
int32_t anydsl_isnanf(float x)    { return std::isnan(x); }
int32_t anydsl_isfinitef(float x) { return std::isfinite(x); }
int32_t anydsl_isinf(double x)    { return std::isinf(x); }
int32_t anydsl_isnan(double x)    { return std::isnan(x); }
int32_t anydsl_isfinite(double x) { return std::isfinite(x); }

void anydsl_print_i16(int16_t s)  { std::cout << s; }
void anydsl_print_i32(int32_t i)  { std::cout << i; }
void anydsl_print_i64(int64_t l)  { std::cout << l; }
void anydsl_print_u16(uint16_t s) { std::cout << s; }
void anydsl_print_u32(uint32_t i) { std::cout << i; }
void anydsl_print_u64(uint64_t l) { std::cout << l; }
void anydsl_print_f32(float f)    { std::cout << f; }
void anydsl_print_f64(double d)   { std::cout << d; }
void anydsl_print_char(char c)    { std::cout << c; }
void anydsl_print_string(char* s) { std::cout << s; }
void anydsl_print_flush()         { std::cout << std::flush; }

void* anydsl_aligned_malloc(size_t size, size_t align) {
    return Runtime::aligned_malloc(size, align);
}

void anydsl_aligned_free(void* ptr) {
    return Runtime::aligned_free(ptr);
}

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

#ifndef AnyDSL_runtime_HAS_TBB_SUPPORT // C++11 threads version
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

[[noreturn]] void tbb_required() {
    error("Flow Graph implementation only available in TBB, rebuild runtime with TBB support!");
}
int32_t anydsl_create_graph() { tbb_required(); }
int32_t anydsl_create_task(int32_t, Closure) { tbb_required(); }
void anydsl_create_edge(int32_t, int32_t) { tbb_required(); }
void anydsl_execute_graph(int32_t, int32_t) { tbb_required(); }
#else // TBB version
void anydsl_parallel_for(int32_t num_threads, int32_t lower, int32_t upper, void* args, void* fun) {
    tbb::task_arena limited((num_threads == 0) ? tbb::task_arena::automatic : num_threads);
    tbb::task_group tg;

    void (*fun_ptr) (void*, int32_t, int32_t) = reinterpret_cast<void (*) (void*, int32_t, int32_t)>(fun);

    limited.execute([&] {
        tg.run([&] {
            tbb::parallel_for(tbb::blocked_range<int32_t>(lower, upper),
                [=] (const tbb::blocked_range<int32_t>& range) {
                    fun_ptr(args, range.begin(), range.end());
                });
        });
    });

    limited.execute([&] { tg.wait(); });
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
        id = int32_t(task_pool.size());
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

static std::unordered_map<int32_t, tbb::flow::graph*> graph_pool;
static std::unordered_map<int32_t, tbb::flow::graph_node*> node_pool;
static std::vector<int32_t> free_graph_ids;
static std::vector<int32_t> free_node_ids;

int32_t anydsl_create_graph() {
    int32_t id;
    if (free_graph_ids.size()) {
        id = free_graph_ids.back();
        free_graph_ids.pop_back();
    } else {
        id = int32_t(graph_pool.size());
    }

    tbb::flow::graph* graph = new tbb::flow::graph();
    graph_pool[id] = graph;
    return id;
}

int32_t anydsl_create_task(int32_t graph_id, Closure closure) {
    int32_t id;
    if (free_node_ids.size()) {
        id = free_node_ids.back();
        free_node_ids.pop_back();
    } else {
        id = int32_t(node_pool.size());
    }

    auto graph = graph_pool.find(graph_id);
    if (graph == graph_pool.end())
        assert(0 && "Trying to find invalid graph id");

    auto node = new tbb::flow::continue_node<tbb::flow::continue_msg>(*graph->second,
        [=](const tbb::flow::continue_msg &) {
                closure.fn(closure.payload);
        });
    node_pool[id] = node;
    return id;
}

void anydsl_create_edge(int32_t task1_id, int32_t task2_id) {
    auto node1 = node_pool.find(task1_id);
    auto node2 = node_pool.find(task2_id);
    if (node1 == node_pool.end() || node2 == node_pool.end())
        assert(0 && "Trying to find invalid task id");

    tbb::flow::make_edge(
        (tbb::flow::continue_node<tbb::flow::continue_msg>&)*node1->second,
        (tbb::flow::continue_node<tbb::flow::continue_msg>&)*node2->second);
}

void anydsl_execute_graph(int32_t graph_id, int32_t root_id) {
    auto graph = graph_pool.find(graph_id);
    if (graph != graph_pool.end()) {
        auto root = node_pool.find(root_id);
        if (root == node_pool.end())
            assert(0 && "Trying to find invalid task id");
        ((tbb::flow::continue_node<tbb::flow::continue_msg>*)root->second)->try_put(tbb::flow::continue_msg());
        graph->second->wait_for_all();
    } else {
        assert(0 && "Trying to execute invalid graph id");
    }
}
#endif
