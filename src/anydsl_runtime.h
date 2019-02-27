#ifndef ANYDSL_RUNTIME_H
#define ANYDSL_RUNTIME_H

#include <stdint.h>
#include <stdlib.h>
#ifdef USING_MPI
#include <mpi.h>
#else
#include "log.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ANYDSL_DEVICE(p, d) ((p) | ((d) << 4))

enum {
    ANYDSL_HOST = 0,
    ANYDSL_CUDA = 1,
    ANYDSL_OPENCL = 2,
    ANYDSL_HSA = 3
};

void anydsl_info(void);

void* anydsl_alloc(int32_t, int64_t);
void* anydsl_alloc_host(int32_t, int64_t);
void* anydsl_alloc_unified(int32_t, int64_t);
void* anydsl_get_device_ptr(int32_t, void*);
void  anydsl_release(int32_t, void*);
void  anydsl_release_host(int32_t, void*);

void anydsl_copy(int32_t, const void*, int64_t, int32_t, void*, int64_t, int64_t);

void anydsl_launch_kernel(int32_t,
                          const char*, const char*,
                          const uint32_t*, const uint32_t*,
                          void**, const uint32_t*, const uint32_t*, const uint8_t*,
                          uint32_t);
void anydsl_synchronize(int32_t);

void anydsl_random_seed(uint32_t);
float    anydsl_random_val_f32();
uint64_t anydsl_random_val_u64();

uint64_t anydsl_get_micro_time();
uint64_t anydsl_get_kernel_time();

int32_t anydsl_isinff(float);
int32_t anydsl_isnanf(float);
int32_t anydsl_isfinitef(float);
int32_t anydsl_isinf(double);
int32_t anydsl_isnan(double);
int32_t anydsl_isfinite(double);

void anydsl_print_i16(int16_t);
void anydsl_print_i32(int32_t);
void anydsl_print_i64(int64_t);
void anydsl_print_f32(float);
void anydsl_print_f64(double);
void anydsl_print_char(char);
void anydsl_print_string(char*);
void anydsl_print_flush();

void* anydsl_aligned_malloc(size_t, size_t);
void anydsl_aligned_free(void*);

void anydsl_parallel_for(int32_t, int32_t, int32_t, void*, void*);
int32_t anydsl_spawn_thread(void*, void*);
void anydsl_sync_thread(int32_t);

struct Closure {
    void (*fn)(uint64_t);
    uint64_t payload;
};

int32_t anydsl_create_graph();
int32_t anydsl_create_task(int32_t, Closure);
void    anydsl_create_edge(int32_t, int32_t);
void    anydsl_execute_graph(int32_t, int32_t);

#ifdef RUNTIME_ENABLE_JIT
void  anydsl_link(const char*);
int32_t anydsl_compile(const char*, uint32_t, uint32_t);
void *anydsl_lookup_function(int32_t, const char*);
#endif

//COMMUNICATOR
#ifdef USING_MPI
MPI_Op anydsl_comm_get_max() { return MPI_MAX; }
MPI_Op anydsl_comm_get_sum() { return MPI_SUM; }
MPI_Datatype anydsl_comm_get_int() { return MPI_INT; }
MPI_Datatype anydsl_comm_get_double() { return MPI_DOUBLE; }
MPI_Datatype anydsl_comm_get_char() { return MPI_CHAR; }
MPI_Datatype anydsl_comm_get_byte() { return MPI_BYTE; }
MPI_Comm anydsl_comm_get_world() { return MPI_COMM_WORLD; }
MPI_Status* anydsl_comm_get_status_ignore() { return MPI_STATUS_IGNORE; }

int anydsl_comm_init() { return MPI_Init(NULL, NULL); }
int anydsl_comm_initialized(int* flag) { return MPI_Initialized(flag); }
int anydsl_comm_size(MPI_Comm comm, int* size) { return MPI_Comm_size(comm, size); }
int anydsl_comm_rank(MPI_Comm comm, int* rank) { return MPI_Comm_rank(comm, rank); }
int anydsl_comm_allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    return MPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
}
int anydsl_comm_send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
    return MPI_Send(buf, count, datatype, dest, tag, comm);
}
int anydsl_comm_recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status) {
    return MPI_Recv(buf, count, datatype, source, tag, comm, status);
}
int anydsl_comm_irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request) {
    return MPI_Irecv(buf, count, datatype, source, tag, comm, request);
}
int anydsl_comm_wait(MPI_Request *request, MPI_Status *status) {
    return MPI_Wait(request, status);
}
int anydsl_comm_probe(int source, int tag, MPI_Comm comm, MPI_Status *status) {
    return MPI_Probe(source, tag, comm, status);
}
int anydsl_comm_get_count(const MPI_Status *status, MPI_Datatype datatype, int *count) {
    return MPI_Get_count(status, datatype, count);
}
int anydsl_comm_gather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    return MPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm);
}
int anydsl_comm_allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    return MPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
}
int anydsl_comm_barrier(MPI_Comm comm) {
    return MPI_Barrier(comm);
}
double anydsl_comm_wtime() {
    return MPI_Wtime();
}
int anydsl_comm_finalize() {
    return MPI_Finalize();
}
int anydsl_comm_finalized(int* flag) {
    return MPI_Finalized(flag);
}
int anydsl_comm_type_contiguous(int count, MPI_Datatype oldtype, MPI_Datatype* newtype) {
    return MPI_Type_contiguous(count, oldtype, newtype);
}
int anydsl_comm_type_commit(MPI_Datatype* datatype) {
    return MPI_Type_commit(datatype);
}
#else
[[noreturn]] void anydsl_comm_not_available() {
    error("AnyDSL communicator only available if MPI is available!");
    exit(255);
}
typedef void* MPI_Op;
typedef void* MPI_Datatype;
typedef void* MPI_Comm;
typedef void* MPI_Status;
typedef void* MPI_Request;

MPI_Op anydsl_comm_get_max() { anydsl_comm_not_available(); }
MPI_Op anydsl_comm_get_sum() { anydsl_comm_not_available(); }
MPI_Datatype anydsl_comm_get_int() { anydsl_comm_not_available(); }
MPI_Datatype anydsl_comm_get_double() { anydsl_comm_not_available(); }
MPI_Datatype anydsl_comm_get_char() { anydsl_comm_not_available(); }
MPI_Datatype anydsl_comm_get_byte() { anydsl_comm_not_available(); }
MPI_Comm anydsl_comm_get_world() { anydsl_comm_not_available(); }
MPI_Status* anydsl_comm_get_status_ignore() { anydsl_comm_not_available(); }

int anydsl_comm_init() { anydsl_comm_not_available(); }
int anydsl_comm_initialized() { anydsl_comm_not_available(); }
int anydsl_comm_size() { anydsl_comm_not_available(); }
int anydsl_comm_rank() { anydsl_comm_not_available(); }
int anydsl_comm_allreduce() {
    anydsl_comm_not_available();
}
int anydsl_comm_send() {
    anydsl_comm_not_available();
}
int anydsl_comm_recv() {
    anydsl_comm_not_available();
}
int anydsl_comm_irecv() {
    anydsl_comm_not_available();
}
int anydsl_comm_wait() {
    anydsl_comm_not_available();
}
int anydsl_comm_probe() {
    anydsl_comm_not_available();
}
int anydsl_comm_get_count() {
    anydsl_comm_not_available();
}
int anydsl_comm_gather() {
    anydsl_comm_not_available();
}
int anydsl_comm_allgather() {
    anydsl_comm_not_available();
}
int anydsl_comm_barrier() {
    anydsl_comm_not_available();
}
double anydsl_comm_wtime() {
    anydsl_comm_not_available();
}
int anydsl_comm_finalize() {
    anydsl_comm_not_available();
}
int anydsl_comm_finalized() {
    anydsl_comm_not_available();
}
#endif

#ifdef __cplusplus
}
#include "anydsl_runtime.hpp"
#endif

#endif
