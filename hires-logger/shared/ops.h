#ifndef SHARED_OPS_H
#define SHARED_OPS_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u32 uint32_t;
typedef u64 uint64_t;
#else
#include <stdint.h>
#include <time.h>
#endif

#ifdef __cplusplus
namespace HiResLogger {
namespace Ops {
#endif

static inline __attribute__((always_inline)) void cpu_serialize(void)
{
    asm volatile("xorl %%eax, %%eax\n\t"
		"cpuid" : : : "%rax", "%rbx", "%rcx", "%rdx");
}

static inline __attribute__((always_inline)) uint64_t __rdtsc(void)
{
	uint32_t a, d;
	asm volatile("rdtsc" : "=a" (a), "=d" (d));
	return ((uint64_t)a) | (((uint64_t)d) << 32);
}

static inline __attribute__((always_inline)) uint64_t __rdtscp(uint32_t *auxp)
{
	uint32_t a, d, c;
	asm volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
	if (auxp)
		*auxp = c;
	return ((uint64_t)a) | (((uint64_t)d) << 32);
}

#ifndef __KERNEL__
/* derived from DPDK (only for userspace program to use) */
static uint64_t __time_calibrate_tsc(void)
{
	uint64_t cycles_per_us = 0;

	/* TODO: New Intel CPUs report this value in CPUID */
	struct timespec sleeptime = {.tv_nsec = 5E8 }; /* 1/2 second */
	struct timespec t_start, t_end;

	cpu_serialize();
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) == 0) {
		uint64_t ns, end, start;
		double secs;

		start = __rdtsc();
		nanosleep(&sleeptime, NULL);
		clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
		end = __rdtscp(NULL);
		ns = ((t_end.tv_sec - t_start.tv_sec) * 1E9);
		ns += (t_end.tv_nsec - t_start.tv_nsec);

		secs = (double)ns / 1000;
		cycles_per_us = (uint64_t)((end - start) / secs);
		printf("time: detected %d ticks / us", cycles_per_us);
		return cycles_per_us;
	}

	return -1;
}
#endif

#ifdef __cplusplus
} // namespace Ops
} // namespace HResLogger
#endif

#endif // SHARED_OPS_H
