// #pragma once

// #include <stdint.h>

// #ifdef __cplusplus
// namespace HResLogger {
// namespace Ops {

// extern "C" {
// #endif

// static inline __attribute__((always_inline)) void cpu_serialize(void)
// {
//     asm volatile("xorl %%eax, %%eax\n\t"
// 		"cpuid" : : : "%rax", "%rbx", "%rcx", "%rdx");
// }

// static inline __attribute__((always_inline)) uint64_t rdtsc(void)
// {
// 	uint32_t a, d;
// 	asm volatile("rdtsc" : "=a" (a), "=d" (d));
// 	return ((uint64_t)a) | (((uint64_t)d) << 32);
// }

// static inline __attribute__((always_inline)) uint64_t rdtscp(uint32_t *auxp)
// {
// 	uint32_t a, d, c;
// 	asm volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
// 	if (auxp)
// 		*auxp = c;
// 	return ((uint64_t)a) | (((uint64_t)d) << 32);
// }
// } // namespace Ops
// } // namespace HResLogger

