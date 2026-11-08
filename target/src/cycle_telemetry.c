/**
 * @file cycle_telemetry.c
 * @brief Nanosecond-Precision Hardware Cycle Counting & Percentile Engine
 *
 * Implements deterministic cycle-accurate timing analysis via QNX Neutrino 8.0
 * syspage timer entries (SYSPAGE_ENTRY(qtime)->cycles_per_sec) and TSC ClockCycles().
 * Computes P50, P99, and Absolute Worst-Case Execution Time (WCET) jitter across
 * a rolling 10,000-sample window for ISO 26262 ASIL-D verification.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

#include "chaospulse_types.h"
#include "qnx_shims.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* IEEE 802.3 Standard CRC32 Lookup Table */
static uint32_t s_crc32_table[256];
static bool s_crc32_initialized = false;

static void init_crc32_table(void) {
    if (s_crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = true;
}

uint32_t telemetry_calculate_crc32(const void *data, size_t length) {
    init_crc32_table();
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        crc = s_crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

/* Rolling Metric Buffer State */
typedef struct {
    pthread_mutex_t lock;
    uint64_t        cycles_per_sec;
    uint32_t        tsc_freq_khz;
    float           jitter_samples[ROLLING_BUFFER_CAPACITY];
    float           latency_samples[ROLLING_BUFFER_CAPACITY];
    size_t          sample_index;
    size_t          total_recorded;
    
    /* Cumulative Statistics */
    float           last_jitter_us;
    float           last_latency_us;
    float           wcet_jitter_us;
    uint64_t        sequence_id;
    uint32_t        breach_count;
} telemetry_state_t;

static telemetry_state_t g_telemetry = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cycles_per_sec = 0,
    .tsc_freq_khz = 0,
    .sample_index = 0,
    .total_recorded = 0,
    .last_jitter_us = 0.0f,
    .last_latency_us = 0.0f,
    .wcet_jitter_us = 0.0f,
    .sequence_id = 0,
    .breach_count = 0
};

static int float_comparator(const void *a, const void *b) {
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

int telemetry_init(void) {
    pthread_mutex_lock(&g_telemetry.lock);
    init_crc32_table();

#if defined(__QNXNTO__)
    /* Native QNX 8.0 Syspage Access */
    g_telemetry.cycles_per_sec = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
#else
    /* Host Shim Fallback: Calibrate CPU frequency against system monotonic clock */
    uint64_t c1 = ClockCycles();
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 10000000L; /* 10ms calibration window */
    nanosleep(&ts, NULL);
    uint64_t c2 = ClockCycles();
    uint64_t delta = (c2 > c1) ? (c2 - c1) : 10000000ULL;
    g_telemetry.cycles_per_sec = delta * 100ULL; /* Scale 10ms -> 1s */
    if (g_telemetry.cycles_per_sec < 500000000ULL || g_telemetry.cycles_per_sec > 10000000000ULL) {
        g_telemetry.cycles_per_sec = 2400000000ULL; /* 2.4 GHz default sanity fallback */
    }
#endif

    g_telemetry.tsc_freq_khz = (uint32_t)(g_telemetry.cycles_per_sec / 1000ULL);
    g_telemetry.sample_index = 0;
    g_telemetry.total_recorded = 0;
    g_telemetry.wcet_jitter_us = 0.0f;
    g_telemetry.sequence_id = 0;
    g_telemetry.breach_count = 0;

    pthread_mutex_unlock(&g_telemetry.lock);
    return 0;
}

uint64_t telemetry_get_cycles(void) {
    return ClockCycles();
}

float telemetry_cycles_to_us(uint64_t delta_cycles) {
    if (g_telemetry.cycles_per_sec == 0) return 0.0f;
    return (float)((double)delta_cycles * 1000000.0 / (double)g_telemetry.cycles_per_sec);
}

void telemetry_record_sample(float jitter_us, float latency_us) {
    pthread_mutex_lock(&g_telemetry.lock);

    g_telemetry.jitter_samples[g_telemetry.sample_index] = jitter_us;
    g_telemetry.latency_samples[g_telemetry.sample_index] = latency_us;
    
    g_telemetry.sample_index = (g_telemetry.sample_index + 1) % ROLLING_BUFFER_CAPACITY;
    if (g_telemetry.total_recorded < ROLLING_BUFFER_CAPACITY) {
        g_telemetry.total_recorded++;
    }

    g_telemetry.last_jitter_us = jitter_us;
    g_telemetry.last_latency_us = latency_us;

    if (jitter_us > g_telemetry.wcet_jitter_us) {
        g_telemetry.wcet_jitter_us = jitter_us;
    }

    if (jitter_us > ASIL_D_MAX_JITTER_US) {
        g_telemetry.breach_count++;
    }

    pthread_mutex_unlock(&g_telemetry.lock);
}

void telemetry_reset(void) {
    pthread_mutex_lock(&g_telemetry.lock);
    g_telemetry.sample_index = 0;
    g_telemetry.total_recorded = 0;
    g_telemetry.last_jitter_us = 0.0f;
    g_telemetry.last_latency_us = 0.0f;
    g_telemetry.wcet_jitter_us = 0.0f;
    g_telemetry.breach_count = 0;
    pthread_mutex_unlock(&g_telemetry.lock);
}

void telemetry_get_snapshot(chaospulse_telemetry_t *out) {
    if (!out) return;

    static float sort_workspace[ROLLING_BUFFER_CAPACITY];

    pthread_mutex_lock(&g_telemetry.lock);

    out->magic = CHAOSPULSE_WIRE_MAGIC;
    out->sequence_id = ++g_telemetry.sequence_id;
    out->timestamp_cycles = ClockCycles();
    out->tsc_freq_khz = g_telemetry.tsc_freq_khz;
    out->jitter_us = g_telemetry.last_jitter_us;
    out->latency_us = g_telemetry.last_latency_us;
    out->wcet_jitter_us = g_telemetry.wcet_jitter_us;

    /* Compute P50 and P99 percentiles over rolling window */
    size_t count = g_telemetry.total_recorded;
    if (count > 0) {
        memcpy(sort_workspace, g_telemetry.jitter_samples, count * sizeof(float));
        qsort(sort_workspace, count, sizeof(float), float_comparator);

        size_t p50_idx = count * 50 / 100;
        size_t p99_idx = count * 99 / 100;
        if (p99_idx >= count) p99_idx = count - 1;

        out->p50_jitter_us = sort_workspace[p50_idx];
        out->p99_jitter_us = sort_workspace[p99_idx];
    } else {
        out->p50_jitter_us = 0.0f;
        out->p99_jitter_us = 0.0f;
    }

    /* Safety status verification */
    if (out->jitter_us > ASIL_D_MAX_JITTER_US || out->wcet_jitter_us > ASIL_D_MAX_JITTER_US) {
        out->asil_breach_flag = 1;
        out->safety_state = 2; /* ASIL-D Violation */
    } else if (out->p99_jitter_us > 2.50f) {
        out->asil_breach_flag = 0;
        out->safety_state = 1; /* Degradation Warning */
    } else {
        out->asil_breach_flag = 0;
        out->safety_state = 0; /* Nominal */
    }

    /* Subsystem status integration */
    uint32_t total = 0, dropped = 0, corrupted = 0;
    pulse_interceptor_get_stats(&total, &dropped, &corrupted);
    out->total_pulses = total;
    out->dropped_pulses = dropped;
    out->corrupted_pulses = corrupted;

    out->current_priority = NOMINAL_PRIORITY_REALTIME;
    out->active_faults = pulse_interceptor_get_active_faults();
    if (cpu_storm_is_active()) {
        out->active_faults |= FAULT_CPU_CORE_SATURATION;
    }
    out->cpu_load_pct = cpu_storm_get_cpu_load();
    out->active_storm_threads = cpu_storm_get_active_threads();

    /* Compute CRC32 over entire struct except the crc32 field itself */
    out->crc32 = 0;
    out->crc32 = telemetry_calculate_crc32(out, sizeof(chaospulse_telemetry_t) - sizeof(uint32_t));

    pthread_mutex_unlock(&g_telemetry.lock);
}
