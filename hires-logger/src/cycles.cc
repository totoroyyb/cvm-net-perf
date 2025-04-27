/* Copyright (c) 2011-2014 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 * Modified by Yibo Yan.
 */

#include <errno.h>
#include <time.h>

#include "cycles.hpp"

namespace HResLogger{

int Cycles::cycles_per_us __aligned(CACHE_LINE_SIZE) = 0;
uint64_t Cycles::start_tsc = 0;
uint64_t Cycles::mockTscValue = 0;
double Cycles::mockCyclesPerSec = 0;

/**
 * Perform once-only overall initialization for the Cycles class, such
 * as calibrating the clock frequency.  This method is invoked automatically
 * during initialization, but it may be invoked explicitly by other modules
 * to ensure that initialization occurs before those modules initialize
 * themselves.
 */
void
Cycles::init() {
    if (cycles_per_us != 0)
        return;
    
    /* TODO: New Intel CPUs report this value in CPUID */
	struct timespec sleeptime = {.tv_nsec = 5E8 }; /* 1/2 second */
	struct timespec t_start, t_end;

	Ops::cpu_serialize();
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) == 0) {
		uint64_t ns, end, start;
		double secs;

		start = rdtsc();
		nanosleep(&sleeptime, NULL);
		clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
		end = rdtscp(NULL);
		ns = ((t_end.tv_sec - t_start.tv_sec) * 1E9);
		ns += (t_end.tv_nsec - t_start.tv_nsec);

		secs = (double)ns / 1000;
		cycles_per_us = (uint64_t)((end - start) / secs);
		std::cout << "time: detected " << cycles_per_us << "ticks / us" << std::endl;

		/* record the start time of the binary */
		start_tsc = rdtsc();
		// return 0;
	}

	// return -1;
}

/**
 * Return the number of CPU cycles per second.
 */
double
Cycles::perSecond()
{
    return getCyclesPerSec();
}

/**
 * Given an elapsed time measured in cycles, return a floating-point number
 * giving the corresponding time in seconds.
 * \param cycles
 *      Difference between the results of two calls to rdtsc.
 * \param cyclesPerSec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The time in seconds corresponding to cycles.
 */
double
Cycles::toSeconds(uint64_t cycles, double cyclesPerSec)
{
    if (cyclesPerSec == 0)
        cyclesPerSec = getCyclesPerSec();
    return static_cast<double>(cycles)/cyclesPerSec;
}

/**
 * Given a time in seconds, return the number of cycles that it
 * corresponds to.
 * \param seconds
 *      Time in seconds.
 * \param cyclesPerSec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The approximate number of cycles corresponding to #seconds.
 */
uint64_t
Cycles::fromSeconds(double seconds, double cyclesPerSec)
{
    if (cyclesPerSec == 0)
        cyclesPerSec = getCyclesPerSec();
    return (uint64_t) (seconds*cyclesPerSec + 0.5);
}

/**
 * Given an elapsed time measured in cycles, return an integer
 * giving the corresponding time in microseconds. Note: toSeconds()
 * is faster than this method.
 * \param cycles
 *      Difference between the results of two calls to rdtsc.
 * \param cyclesPerSec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The time in microseconds corresponding to cycles (rounded).
 */
uint64_t
Cycles::toMicroseconds(uint64_t cycles, double cyclesPerSec)
{
    return toNanoseconds(cycles, cyclesPerSec) / 1000;
}

/**
 * Given a number of microseconds, return an approximate number of
 * cycles for an equivalent time length.
 * \param us
 *      Number of microseconds.
 * \param cyclesPerSec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The approximate number of cycles for the same time length.
 */
uint64_t
Cycles::fromMicroseconds(uint64_t us, double cyclesPerSec)
{
    return fromNanoseconds(1000 * us, cyclesPerSec);
}

/**
 * Given an elapsed time measured in cycles, return an integer
 * giving the corresponding time in nanoseconds. Note: toSeconds()
 * is faster than this method.
 * \param cycles
 *      Difference between the results of two calls to rdtsc.
 * \param cyclesPerSec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The time in nanoseconds corresponding to cycles (rounded).
 */
uint64_t
Cycles::toNanoseconds(uint64_t cycles, double cyclesPerSec)
{
    if (cyclesPerSec == 0)
        cyclesPerSec = getCyclesPerSec();
    return (uint64_t) (1e09*static_cast<double>(cycles)/cyclesPerSec + 0.5);
}

/**
 * Given a number of nanoseconds, return an approximate number of
 * cycles for an equivalent time length.
 * \param ns
 *      Number of nanoseconds.
 * \param cyclesPerSec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The approximate number of cycles for the same time length.
 */
uint64_t
Cycles::fromNanoseconds(uint64_t ns, double cyclesPerSec)
{
    if (cyclesPerSec == 0)
        cyclesPerSec = getCyclesPerSec();
    return (uint64_t) (static_cast<double>(ns)*cyclesPerSec/1e09 + 0.5);
}

/**
 * Busy wait for a given number of microseconds.
 * Callers should use this method in most reasonable cases as opposed to
 * usleep for accurate measurements. Calling usleep may put the the processor
 * in a low power mode/sleep state which reduces the clock frequency.
 * So, each time the process/thread wakes up from usleep, it takes some time
 * to ramp up to maximum frequency. Thus meausrements often incur higher
 * latencies.
 * \param us
 *      Number of microseconds.
 */
void
Cycles::sleep(uint64_t us)
{
    uint64_t stop = Cycles::rdtsc() + Cycles::fromNanoseconds(1000*us);
    while (Cycles::rdtsc() < stop);
}
} // end RAMCloud
