#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
