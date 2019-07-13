#ifdef USING_MPI
#include <mpi.h>
#else
#include "comm_platform_emulator.h"
#include <sys/time.h>
#include <iostream>
#include <cstring>
#include <chrono>
#endif

#include <thread>

#ifdef __cplusplus
extern "C" {
#endif

int get_num_threads() {
    int num_threads = std::thread::hardware_concurrency();
    //num_threads may be 0 (see anydsl runtime.cpp)
    return (num_threads > 0) ? num_threads : 1;
}

MPI_Op anydsl_comm_get_max() { return MPI_MAX; }
MPI_Op anydsl_comm_get_sum() { return MPI_SUM; }
MPI_Datatype anydsl_comm_get_int() { return MPI_INT; }
MPI_Datatype anydsl_comm_get_double() { return MPI_DOUBLE; }
MPI_Datatype anydsl_comm_get_char() { return MPI_CHAR; }
MPI_Datatype anydsl_comm_get_byte() { return MPI_BYTE; }
MPI_Comm anydsl_comm_get_world() { return MPI_COMM_WORLD; }

#ifdef USING_MPI
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
int anydsl_comm_get_count(int source, int tag, MPI_Datatype datatype, int* count, MPI_Comm comm) {
    MPI_Status status;
    MPI_Probe(source, tag, comm, &status);
    return MPI_Get_count(&status, datatype, count);
}
int anydsl_comm_gather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    return MPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm);
}
int anydsl_comm_allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    return MPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
}
int anydsl_comm_bcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
    return MPI_Bcast(buffer, count, datatype, root, comm);
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
MPI_Datatype anydsl_create_datatype(int size) {
    MPI_Datatype newtype;
    MPI_Type_contiguous(size, MPI_BYTE, &newtype);
    MPI_Type_commit(&newtype);
    return newtype;
}
MPI_Status* anydsl_comm_get_status_ignore() { return MPI_STATUS_IGNORE; }
#else
//emulation on 1 node
int anydsl_comm_init() {
    initialized = true;
    datatypeSizeLookup.clear();
    datatypeSizeLookup.insert({MPI_INT, sizeof(int)});
    datatypeSizeLookup.insert({MPI_DOUBLE, sizeof(double)});
    datatypeSizeLookup.insert({MPI_CHAR, sizeof(char)});
    datatypeSizeLookup.insert({MPI_BYTE, 1});
    return 0;
}
int anydsl_comm_initialized(int* flag) {
    *flag = initialized;
    return 0;
}
int anydsl_comm_size(MPI_Comm comm, int* size) {
    *size = 1;
    return comm == 0;
}
int anydsl_comm_rank(MPI_Comm comm, int* rank) {
    *rank = 0;
    return comm == 0;
}
int anydsl_comm_send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
    checkRank(dest);
    DataItem* dt = (DataItem*) malloc(sizeof(DataItem));
    int dataSize = datatypeSize(datatype, std::string("send()"));
    dt->numBytes = count * dataSize;
    void* data = malloc(count * dataSize);
    copyBuffer(data, buf, count * dataSize);
    dt->data = data;

    //wait until there is space for the current tag
    while(tagLookup.count(tag) == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_LOOKUP));
    }

    tagLookup[tag] = dt;
    return comm==0;
}
int anydsl_comm_recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status) {
    checkRank(source);
    //wait until tag is saved
    while(tagLookup.count(tag) == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_LOOKUP));
    }

    DataItem* dt = tagLookup[tag];

    int dataSize = datatypeSize(datatype, std::string("recv()"));
    if(dt->numBytes != count * dataSize) {
      std::cerr << "Invalid number of elements in recv()! ABORT!" << std::endl;
      exit(1);
    }
    copyBuffer(buf, dt->data, count * dataSize);

    tagLookup.erase(tag);
    free(dt->data);
    free(dt);
    //TODO status
    return (comm==0) && (status!=0);
}
int anydsl_comm_irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request) {
    checkRank(source);
    uintptr_t lookup = reinterpret_cast<uintptr_t>(request);
    iRecvItem* cur_item = (iRecvItem*) malloc(sizeof(iRecvItem));
    cur_item->buffer = buf;
    cur_item->count = count;
    cur_item->datatype = datatype;
    cur_item->tag = tag;

    iRecvLookup[lookup] = cur_item;
    return comm==0;
}

int anydsl_comm_wait(MPI_Request *request, MPI_Status *status) {
    uintptr_t lookup = reinterpret_cast<uintptr_t>(request);

    while(iRecvLookup.count(lookup) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_LOOKUP));
    }

    iRecvItem* cur_item = iRecvLookup[lookup];
    anydsl_comm_recv(cur_item->buffer, cur_item->count, cur_item->datatype, 0, cur_item->tag, MPI_COMM_WORLD, status);
    return 0;
}

int anydsl_comm_get_count(int source, int tag, MPI_Datatype datatype, int* count, MPI_Comm comm) {
    checkRank(source);
    //wait until data for tag has been saved
    while(tagLookup.count(tag) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_LOOKUP));
    }
    *count = (tagLookup[tag]->numBytes / datatypeSize(datatype, std::string("get_count()")));
    return comm==0;
}

int anydsl_comm_gather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
    checkRank(root);
    if(datatypeSize(sendtype,std::string("gather():sendtype")) != datatypeSize(recvtype,std::string("gather():recvtype"))) {
        std::cerr << "Send and recv types must have the same size! ABORT!" << std::endl;
        exit(1);
    }
    if(sendcount != recvcount) {
        std::cerr << "sendcount and recvcount must be equal!" << std::endl;
        exit(1);
    }
    copyBuffer(recvbuf, sendbuf, sendcount * datatypeSize(sendtype,std::string("gather()")));
    return comm==0;
}
int anydsl_comm_allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    return anydsl_comm_gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, 0, comm);
}
int anydsl_comm_allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
      if(op < MPI_OP_FIRST || op > MPI_OP_LAST) {
          std::cerr << "Invalid operator in allreduce()! ABORT!" << std::endl;
          exit(1);
      }
      copyBuffer(recvbuf, sendbuf, count * datatypeSize(datatype, std::string("allreduce()")));
      return comm==0;
}
int anydsl_comm_barrier(MPI_Comm comm) {
    return comm==0;
}
int anydsl_comm_bcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
    if(root != 0) {
        std::cerr << "Invalid root rank! ABORT!" << std::endl;
        exit(1);
    }
    //nothing to do
    return comm==0;
}
double anydsl_comm_wtime() {
    timeval timer;
    gettimeofday(&timer, 0);
    return timer.tv_sec + (timer.tv_usec / 1000000.0);
}
int anydsl_comm_finalize() {
    finalized = true;
    return 0;
}
int anydsl_comm_finalized(int* flag) {
    *flag = finalized;
    return 0;
}
MPI_Datatype anydsl_create_datatype(int size) {
    if(datatypeSizeLookup.size() > RAND_MAX_NUM/4) {
        std::cerr << "RAND_MAX_NUM should be increased for better performance!" << std::endl;
    }
    bool foundKey = false;
    int key;
    while(!foundKey) {
        key = rand() % RAND_MAX_NUM + 1;
        if(datatypeSizeLookup.find(key) == datatypeSizeLookup.end()) {
            foundKey = true;
        }
    }
    datatypeSizeLookup.insert({key, size});
    return key;
}
MPI_Status* anydsl_comm_get_status_ignore() {
    return NULL;
}
#endif

#ifdef __cplusplus
}
#endif

#ifndef USING_MPI
void checkRank(int rank) {
    if(rank != 0) {
        std::cerr << "Invalid destination rank " << rank << "! ABORT!" << std::endl;
        exit(1);
    }
}
void copyBuffer(void* destination, const void* source, int size) {
    memcpy(destination, source, size);
}

int datatypeSize(int datatype, std::string method) {
    if(datatypeSizeLookup.find(datatype) == datatypeSizeLookup.end()) {
        std::cerr << "Invalid datatype in " << method << "! ABORT!" << std::endl;
        exit(1);
    }
    else {
        return datatypeSizeLookup[datatype];
    }
}
#endif
