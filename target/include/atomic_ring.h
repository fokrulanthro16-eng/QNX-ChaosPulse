/**
 * @file atomic_ring.h
 * @brief Lock-Free Atomic Circular Ring Buffer for High-Frequency Telemetry
 *
 * Utilizes atomic head/tail synchronization with acquire-release memory order semantics.
 * Designed for deterministic zero-jitter sample passing between QNX 8.0 real-time
 * threads and the network serialization daemon.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

#ifndef ATOMIC_RING_H
#define ATOMIC_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__STDC_NO_ATOMICS__)
    #error "C11 atomics support required for lock-free atomic_ring.h"
#else
    #include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buffer;
    size_t element_size;
    size_t capacity;       /* Must be power of 2 */
    size_t mask;           /* capacity - 1 */
    _Atomic size_t head;   /* Write index */
    _Atomic size_t tail;   /* Read index */
    _Atomic uint64_t dropped_count; /* Buffer overrun count */
} atomic_ring_t;

/**
 * @brief Initialize lock-free atomic ring buffer
 * @param ring Pointer to ring struct
 * @param capacity Number of elements (must be power of two)
 * @param element_size Byte size of each element
 * @return true on success, false on allocation error or invalid capacity
 */
static inline bool atomic_ring_init(atomic_ring_t *ring, size_t capacity, size_t element_size) {
    if (!ring || capacity < 2 || (capacity & (capacity - 1)) != 0 || element_size == 0) {
        return false;
    }
    ring->buffer = (uint8_t *)malloc(capacity * element_size);
    if (!ring->buffer) {
        return false;
    }
    ring->element_size = element_size;
    ring->capacity = capacity;
    ring->mask = capacity - 1;
    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
    atomic_init(&ring->dropped_count, 0);
    return true;
}

/**
 * @brief Free memory allocated by the ring buffer
 */
static inline void atomic_ring_destroy(atomic_ring_t *ring) {
    if (ring) {
        if (ring->buffer) {
            free(ring->buffer);
            ring->buffer = NULL;
        }
        ring->capacity = 0;
        ring->mask = 0;
    }
}

/**
 * @brief Current count of readable elements in ring
 */
static inline size_t atomic_ring_count(const atomic_ring_t *ring) {
    if (!ring) return 0;
    size_t h = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    return (h >= t) ? (h - t) : (ring->capacity - (t - h));
}

/**
 * @brief Check if ring is empty
 */
static inline bool atomic_ring_is_empty(const atomic_ring_t *ring) {
    if (!ring) return true;
    return atomic_load_explicit(&ring->head, memory_order_relaxed) ==
           atomic_load_explicit(&ring->tail, memory_order_relaxed);
}

/**
 * @brief Check if ring is full
 */
static inline bool atomic_ring_is_full(const atomic_ring_t *ring) {
    if (!ring) return false;
    size_t h = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    return ((h + 1) & ring->mask) == (t & ring->mask);
}

/**
 * @brief Push element into ring buffer (Single/Multi-Producer safe with spin or CAS)
 * @param ring Pointer to ring struct
 * @param item Pointer to item to copy
 * @return true if successfully enqueued, false if full (dropped_count incremented)
 */
static inline bool atomic_ring_push(atomic_ring_t *ring, const void *item) {
    if (!ring || !ring->buffer || !item) return false;

    size_t current_head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t current_tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    if ((current_head - current_tail) >= ring->capacity) {
        /* Buffer is saturated; drop sample to avoid stalling real-time path */
        atomic_fetch_add_explicit(&ring->dropped_count, 1, memory_order_relaxed);
        return false;
    }

    size_t offset = (current_head & ring->mask) * ring->element_size;
    memcpy(ring->buffer + offset, item, ring->element_size);

    /* Release barrier ensures data is written before head updates */
    atomic_store_explicit(&ring->head, current_head + 1, memory_order_release);
    return true;
}

/**
 * @brief Pop element from ring buffer
 * @param ring Pointer to ring struct
 * @param out_item Destination buffer to copy element into
 * @return true if element popped, false if ring empty
 */
static inline bool atomic_ring_pop(atomic_ring_t *ring, void *out_item) {
    if (!ring || !ring->buffer || !out_item) return false;

    size_t current_tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    size_t current_head = atomic_load_explicit(&ring->head, memory_order_acquire);

    if (current_tail == current_head) {
        return false; /* Queue empty */
    }

    size_t offset = (current_tail & ring->mask) * ring->element_size;
    memcpy(out_item, ring->buffer + offset, ring->element_size);

    /* Release barrier so producer can reuse slot */
    atomic_store_explicit(&ring->tail, current_tail + 1, memory_order_release);
    return true;
}

/**
 * @brief Get total dropped element count due to buffer overrun
 */
static inline uint64_t atomic_ring_get_dropped(const atomic_ring_t *ring) {
    if (!ring) return 0;
    return atomic_load_explicit(&ring->dropped_count, memory_order_relaxed);
}

#ifdef __cplusplus
}
#endif

#endif /* ATOMIC_RING_H */
