/**
 * @file chaospulse_types.h
 * @brief Binary IPC Wire Types & Chaos Fault Injection Definitions for QNX Neutrino 8.0
 *
 * Designed for deterministic real-time telemetry streaming and microkernel fault verification
 * compliant with ISO 26262 ASIL-D and IEC 61508 SIL-3 execution bounds.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

#ifndef CHAOSPULSE_TYPES_H
#define CHAOSPULSE_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic frame synchronizer ("CP80" -> 0x43503830) */
#define CHAOSPULSE_WIRE_MAGIC         0x43503830U
#define CHAOSPULSE_DEFAULT_PORT       9092
#define CHAOSPULSE_UNIX_SOCKET_PATH   "/tmp/chaospulse.sock"

/* Real-Time Hard Bounds (ASIL-D Profile) */
#define ASIL_D_MAX_JITTER_US          3.50f   /* Microkernel scheduling wake-up jitter limit */
#define ASIL_D_MAX_LATENCY_US         25.0f   /* Hard IPC deadline for priority 63 message dispatch */
#define NOMINAL_PERIOD_NS             1000000ULL /* 1 kHz Periodic Task -> 1,000,000 ns */
#define NOMINAL_PRIORITY_REALTIME     63      /* QNX Maximum Hard Real-Time FIFO Priority */
#define NOMINAL_PRIORITY_INTERCEPTOR  55      /* Pulse Interceptor Dispatch Priority */
#define ROLLING_BUFFER_CAPACITY       10000   /* 10,000 samples for high-precision P50/P99/WCET */

/* QNX Neutrino 8.0 Kernel Trace Event Definitions */
#define CHAOS_TRACE_CLASS_USER        0x42    /* Trace class index: _NTO_TRACE_USERFIRST + 0x42 */
#define CHAOS_TRACE_STORM_START       0x01    /* CPU contention storm engaged */
#define CHAOS_TRACE_STORM_STOP        0x02    /* CPU contention storm disengaged */
#define CHAOS_TRACE_PULSE_DROP        0x03    /* Synthetic pulse drop injected */
#define CHAOS_TRACE_JITTER_INJECT     0x04    /* Synthetic microsecond jitter delay injected */
#define CHAOS_TRACE_INVERSION_START   0x05    /* Priority inversion trap triggered */
#define CHAOS_TRACE_INVERSION_HEALED  0x06    /* Priority Inheritance Protocol auto-healed */
#define CHAOS_TRACE_ASIL_BREACH       0x07    /* ISO 26262 ASIL-D timing breach (> 3.50 us) */
#define CHAOS_TRACE_SYSTEM_RESET      0x08    /* System reset / panic stop executed */

/**
 * @enum fault_type_t
 * @brief Discrete microkernel injection fault bitmask and identifiers
 */
typedef enum {
    FAULT_NONE                    = 0,
    FAULT_PULSE_DROP              = (1 << 0), /* Drop 1 out of N or rate-based IPC pulses */
    FAULT_PULSE_JITTER            = (1 << 1), /* Inject microsecond pre-dispatch bus delay */
    FAULT_PRIORITY_INVERSION_TEST = (1 << 2), /* Clamp receiver to lower priority without PIP */
    FAULT_CPU_CORE_SATURATION     = (1 << 3), /* Flood SMP cores with SCHED_SPORADIC/RR contention */
    FAULT_MEMORY_CORRUPT          = (1 << 4)  /* Inject deterministic checksum corruption on pulse payload */
} fault_type_t;

/**
 * @enum chaospulse_cmd_id_t
 * @brief Wire protocol command identifiers
 */
typedef enum {
    CMD_GET_STATUS        = 0x01,
    CMD_START_CPU_STORM   = 0x02,
    CMD_STOP_CPU_STORM    = 0x03,
    CMD_INJECT_PULSE_DROP = 0x04,
    CMD_INJECT_JITTER     = 0x05,
    CMD_PRIORITY_INVERT   = 0x06,
    CMD_CORRUPT_PAYLOAD   = 0x07,
    CMD_RESET_ALL_FAULTS  = 0x08,
    CMD_PANIC_KILL        = 0xFF
} chaospulse_cmd_id_t;

#pragma pack(push, 1)

/**
 * @struct chaospulse_telemetry_t
 * @brief Strict binary telemetry frame streamed across TCP 9092 / Unix domain socket.
 *
 * Wire payload is byte-aligned without compiler padding to guarantee zero-copy
 * deserialization across x86_64 and aarch64le architectures.
 */
typedef struct {
    uint32_t magic;                /* 0x43503830 ("CP80") */
    uint64_t sequence_id;          /* Monotonically increasing frame sequence counter */
    uint64_t timestamp_cycles;     /* Raw CPU ClockCycles() counter at frame capture */
    uint32_t tsc_freq_khz;         /* Calibrated system timer frequency (kHz) */
    
    /* Real-Time Metrics (in Microseconds) */
    float    jitter_us;            /* Instantaneous periodic wakeup jitter (delta from 1.000 ms) */
    float    latency_us;           /* End-to-end pulse delivery latency */
    float    p50_jitter_us;        /* 50th percentile (Median) jitter across rolling window */
    float    p99_jitter_us;        /* 99th percentile jitter across rolling window */
    float    wcet_jitter_us;       /* Absolute Worst-Case Execution Time jitter recorded */

    /* Pulse IPC Statistics */
    uint32_t total_pulses;         /* Total synchronous/asynchronous pulses generated */
    uint32_t dropped_pulses;       /* Count of intercepted/dropped pulses */
    uint32_t corrupted_pulses;     /* Count of intentionally corrupted payload checks */
    
    /* System & Microkernel State */
    uint32_t current_priority;     /* Current priority of the critical real-time thread */
    uint32_t active_faults;        /* Bitmask of currently active fault injection flags */
    uint8_t  cpu_load_pct;         /* Aggregated core saturation percentage (0-100) */
    uint8_t  active_storm_threads; /* Number of active background contention threads */
    uint8_t  asil_breach_flag;     /* 1 if jitter_us > ASIL_D_MAX_JITTER_US or pulse loss breached */
    uint8_t  safety_state;         /* 0: Nominal, 1: Degradation, 2: ASIL-D Violation, 3: Safe Stop */
    uint32_t crc32;                /* Frame integrity checksum */
} chaospulse_telemetry_t;

/**
 * @struct chaospulse_directive_t
 * @brief Inbound binary control directive sent from host cockpit
 */
typedef struct {
    uint32_t magic;                /* 0x43503830 */
    uint8_t  command_id;           /* chaospulse_cmd_id_t */
    uint8_t  storm_threads;        /* Number of contention threads to spawn (0-32) */
    uint16_t duration_ms;          /* Duration of fault in milliseconds (0 = indefinite) */
    float    param_float;          /* e.g., drop_rate (0.0 to 1.0) or jitter_us */
    uint32_t param_int;            /* Auxiliary integer parameter */
    uint32_t crc32;                /* Directive verification checksum */
} chaospulse_directive_t;

/**
 * @struct chaospulse_pulse_payload_t
 * @brief Payload transported inside QNX 8.0 pulse (value field / sidechannel)
 */
typedef struct {
    uint32_t sequence;             /* IPC pulse sequence number */
    uint64_t tx_cycles;            /* Time pulse was scheduled/sent */
    uint16_t subsystem_id;         /* Subsystem identifier (e.g. Steer-by-wire: 0xA1) */
    uint16_t checksum;             /* Payload integrity verification check */
} chaospulse_pulse_payload_t;

#pragma pack(pop)

/* Subsystem Interface Declarations */

/* 1. Cycle Telemetry Subsystem */
int      telemetry_init(void);
void     telemetry_record_sample(float jitter_us, float latency_us);
void     telemetry_get_snapshot(chaospulse_telemetry_t *out);
void     telemetry_reset(void);
uint64_t telemetry_get_cycles(void);
float    telemetry_cycles_to_us(uint64_t delta_cycles);
uint32_t telemetry_calculate_crc32(const void *data, size_t length);

/* 2. Pulse Interceptor Subsystem */
int      pulse_interceptor_init(void);
int      pulse_interceptor_start(void);
void     pulse_interceptor_stop(void);
void     pulse_interceptor_set_fault(fault_type_t fault, bool enable, float param);
void     pulse_interceptor_get_stats(uint32_t *total, uint32_t *dropped, uint32_t *corrupted);
void     pulse_interceptor_reset_stats(void);
uint32_t pulse_interceptor_get_active_faults(void);

/* 3. CPU Storm & Hard Real-Time Generator Subsystem */
int      cpu_storm_init(void);
int      cpu_storm_start(uint8_t thread_count);
void     cpu_storm_stop(void);
bool     cpu_storm_is_active(void);
uint8_t  cpu_storm_get_active_threads(void);
uint8_t  cpu_storm_get_cpu_load(void);
void     cpu_storm_cleanup(void);

/* 4. QNX Kernel Instrumentation Hook (TraceEvent) */
void     qnx_trace_chaos_event(uint32_t event_code, uint32_t data1, uint32_t data2);

#ifdef __cplusplus
}
#endif

#endif /* CHAOSPULSE_TYPES_H */
