/* cpuinfo.h - what the processor is, for a machine that must not know.
 *
 * Upstream asks the cpuinfo library three things at startup: whether the host
 * has SSE 4.1 (it must: the build uses those instructions unconditionally),
 * how long its cache lines are (to warn about a build made for another), and
 * what it is called (for the log). A core answers the same on every host, so
 * the answers here are constants and the library is not linked: nothing about
 * the processor a run happened on may reach the run.
 */
#ifndef CHIMERA_CPUINFO_H
#define CHIMERA_CPUINFO_H

#include <stdbool.h>
#include <stdint.h>

struct cpuinfo_cache
{
	uint32_t line_size;
};

struct cpuinfo_package
{
	char name[48];
	uint32_t processor_count;
	uint32_t core_count;
	uint32_t cluster_count;
};

/* "could not be determined", which upstream already handles */
static inline bool cpuinfo_initialize(void) { return false; }
static inline bool cpuinfo_has_x86_sse4_1(void) { return true; }
static inline uint32_t cpuinfo_get_l1i_caches_count(void) { return 0; }
static inline uint32_t cpuinfo_get_l1d_caches_count(void) { return 0; }
static inline uint32_t cpuinfo_get_l2_caches_count(void) { return 0; }
static inline const struct cpuinfo_cache *cpuinfo_get_l1i_cache(uint32_t i) { (void)i; return 0; }
static inline const struct cpuinfo_cache *cpuinfo_get_l1d_cache(uint32_t i) { (void)i; return 0; }
static inline const struct cpuinfo_cache *cpuinfo_get_l2_cache(uint32_t i) { (void)i; return 0; }
static inline const struct cpuinfo_package *cpuinfo_get_package(uint32_t i) { (void)i; return 0; }

#endif
