/* What both flavors of this core are compiled with, so that they agree.
 *
 * The guest (musl) and the native reference (glibc) do not offer the same C
 * library, and the differences are exactly the kind that would make an
 * equivalence gate compare two different machines. This header is force
 * included into every translation unit of both builds, so the answer to each
 * one is the same on both sides.
 */
#pragma once

/* resources.S is preprocessed too, and an assembler has no use for C */
#ifndef __ASSEMBLER__

#ifdef __cplusplus
extern "C" {
#endif

/* The kernel's thread id, which glibc has and musl does not. There is one
 * thread, it never changes, and DuckStation only uses the number to tell
 * whether it is on the thread it started on. The same number in both flavors:
 * a real one would be a different value every run, sitting in the heap. */
int chimera_gettid(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLER__ */
