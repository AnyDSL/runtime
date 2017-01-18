#ifndef ANYDSL_RUNTIME_H
#define ANYDSL_RUNTIME_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANYDSL_DEVICE(p, d) ((p) | ((d) << 4))

enum {
    ANYDSL_HOST = 0,
    ANYDSL_CUDA = 1,
    ANYDSL_OPENCL = 2
};

void anydsl_info(void);

void* anydsl_alloc(int32_t, int64_t);
void* anydsl_alloc_host(int32_t, int64_t);
void* anydsl_alloc_unified(int32_t, int64_t);
void* anydsl_get_device_ptr(int32_t, void*);
void  anydsl_release(int32_t, void*);
void  anydsl_release_host(int32_t, void*);

void anydsl_copy(int32_t, const void*, int64_t, int32_t, void*, int64_t, int64_t);

void anydsl_set_block_size(int32_t, int32_t, int32_t, int32_t);
void anydsl_set_grid_size(int32_t, int32_t, int32_t, int32_t);
void anydsl_set_kernel_arg(int32_t, int32_t, void*, int32_t);
void anydsl_set_kernel_arg_ptr(int32_t, int32_t, void*);
void anydsl_set_kernel_arg_struct(int32_t, int32_t, void*, int32_t);
void anydsl_load_kernel(int32_t, const char*, const char*);
void anydsl_launch_kernel(int32_t);
void anydsl_synchronize(int32_t);

float anydsl_random_val();
void anydsl_random_seed(uint32_t);

long long anydsl_get_micro_time();
long long anydsl_get_kernel_time();

int32_t anydsl_isinff(float);
int32_t anydsl_isnanf(float);
int32_t anydsl_isfinitef(float);
int32_t anydsl_isinf(double);
int32_t anydsl_isnan(double);
int32_t anydsl_isfinite(double);

void anydsl_print_char(char);
void anydsl_print_short(int16_t);
void anydsl_print_int(int32_t);
void anydsl_print_long(int64_t);
void anydsl_print_float(float);
void anydsl_print_double(double);
void anydsl_print_string(char*);

void* anydsl_aligned_malloc(size_t, size_t);
void anydsl_aligned_free(void*);

void anydsl_parallel_for(int32_t, int32_t, int32_t, void*, void*);
int32_t anydsl_spawn_thread(void*, void*);
void anydsl_sync_thread(int32_t);

#ifdef __cplusplus
}
#include "anydsl_runtime.hpp"
#endif

#endif
