/* Unified Random Number Generator for Tests
 *
 * Fast 32-bit RNG based on rapidhash variant.
 * Reference: github.com/Nicoshev/rapidhash
 *
 * This provides a consistent, fast, and deterministic RNG across all test
 * files in the rbtree project.
 */

#ifndef RNG_H
#define RNG_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/**
 * Advance state and return a new 32-bit pseudo-random value
 *
 * @state : Pointer to RNG state
 * Return New pseudo-random 32-bit value
 *
 * Usage:
 *   uint32_t rng_state = 42;  // Fixed seed for reproducible tests
 *   uint32_t random_value = test_rand_u32(&rng_state);
 */
static inline uint32_t test_rand_u32(uint32_t *state)
{
    *state += 0xe120fc15u;
    uint64_t tmp = (uint64_t) (*state) * 0x4a39b70d;
    uint32_t mix = (uint32_t) ((tmp >> 32) ^ tmp);
    tmp = (uint64_t) mix * 0x12fad5c9;
    return (uint32_t) ((tmp >> 32) ^ tmp);
}

/**
 * Initialize RNG state with a seed
 *
 * @state : Pointer to RNG state to initialize
 * @seed  : Seed value (use 0 for non-deterministic based on time)
 */
static inline void test_rand_init(uint32_t *state, uint32_t seed)
{
    if (seed == 0) {
        /* Non-deterministic: use current time as seed */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        *state = (uint32_t) (ts.tv_sec ^ ts.tv_nsec);
    } else {
        /* Deterministic: use provided seed */
        *state = seed;
    }
}

/**
 * Generate random value in range [0, max)
 *
 * @state : Pointer to RNG state
 * @max   : Upper bound (exclusive)
 * Return Random value in [0, max)
 */
static inline uint32_t test_rand_range(uint32_t *state, uint32_t max)
{
    return test_rand_u32(state) % max;
}

/**
 * Fisher-Yates shuffle for array of uint32_t values
 *
 * @state : Pointer to RNG state
 * @array : Array to shuffle
 * @count : Number of elements
 */
static inline void test_shuffle_u32(uint32_t *state,
                                    uint32_t *array,
                                    size_t count)
{
    for (size_t i = count - 1; i > 0; i--) {
        size_t j = test_rand_range(state, i + 1);
        uint32_t temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}

#endif /* RNG_H */
