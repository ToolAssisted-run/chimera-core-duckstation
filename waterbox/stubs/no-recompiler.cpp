// The two functions the code cache asks its code generator for, in a build
// that has no code generator (-Drecompiler=false). Upstream assumes every
// platform it runs on has one; an interpreter needs neither a trampoline nor
// a jump, and never asks at run time.

#include "core/cpu_code_cache_private.h"

u32 CPU::CodeCache::EmitASMFunctions(void* code, u32 code_size)
{
  return 0;
}

u32 CPU::CodeCache::EmitJump(void* code, const void* dst, bool flush_icache)
{
  return 0;
}
