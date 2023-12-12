#ifndef ANYDSL_JIT_H
#define ANYDSL_JIT_H

#include <stdint.h>

#include "anydsl_runtime_config.h"

class Runtime;

AnyDSL_runtime_API Runtime& runtime();

#ifdef AnyDSL_runtime_HAS_JIT_SUPPORT
AnyDSL_runtime_jit_API void anydsl_set_cache_directory(const char*);
AnyDSL_runtime_jit_API const char* anydsl_get_cache_directory();
AnyDSL_runtime_jit_API void anydsl_link(const char*);
AnyDSL_runtime_jit_API int32_t anydsl_compile(const char*, uint32_t, uint32_t);
AnyDSL_runtime_jit_API void *anydsl_lookup_function(int32_t, const char*);
AnyDSL_runtime_jit_API void anydsl_set_log_level(uint32_t /* log level (4=error only, 3=warn, 2=info, 1=verbose, 0=debug) */);
#if THORIN_ENABLE_JSON
AnyDSL_runtime_jit_API int32_t anyopt_compile(const char*, uint32_t, uint32_t);
#endif
#endif

#endif
