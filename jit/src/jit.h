#ifndef JIT_H
#define JIT_H

extern "C" {
    void  anydsl_link(const char*);
    void* anydsl_compile(const char*, uint32_t, const char*, uint32_t);
}

#endif
