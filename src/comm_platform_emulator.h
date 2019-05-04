#ifndef COMM_PLATFORM_EMULATOR_H

#define COMM_PLATFORM_EMULATOR_H

#include <unordered_map>

typedef int MPI_Op;
typedef int MPI_Datatype;
typedef int MPI_Comm;
typedef int MPI_Status;
typedef void* MPI_Request;
typedef const void* MPI_Buf;
typedef void* MPI_MutBuf;

#define MPI_COMM_WORLD 0

#define MPI_MAX 100
#define MPI_SUM 101
#define MPI_OP_FIRST 100
#define MPI_OP_LAST 101

#define MPI_DOUBLE 103
#define MPI_INT 104
#define MPI_CHAR 105
#define MPI_BYTE 106

#define WAIT_LOOKUP 200
#define RAND_MAX_NUM 10000

typedef struct {
    int numBytes;
    void* data;
} DataItem;

typedef struct {
    void* buffer;
    int count;
    int datatype;
    int tag;
} iRecvItem;

//maps from tag to data
std::unordered_map<int, DataItem*> tagLookup;
std::unordered_map<uintptr_t, iRecvItem*> iRecvLookup;
std::unordered_map<int, int> datatypeSizeLookup;

bool initialized = false;
bool finalized = false;

void checkRank(int rank);
void copyBuffer(void* destination, const void* source, int size);
int datatypeSize(int datatype, std::string method);

#endif //COMM_PLATFORM_EMULATOR_H
