#ifndef ANYDSL_JIT_H
#define ANYDSL_JIT_H

#include <stdint.h>

#include "anydsl_runtime_config.h"

class Runtime;

AnyDSL_runtime_API Runtime& runtime();

#ifdef AnyDSL_runtime_HAS_JIT_SUPPORT
AnyDSL_runtime_jit_API void anydsl_link(const char*);
AnyDSL_runtime_jit_API int32_t anydsl_compile(const char*, uint32_t, uint32_t);
AnyDSL_runtime_jit_API void *anydsl_lookup_function(int32_t, const char*);
#endif

#endif
