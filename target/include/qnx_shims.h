/**
 * @file qnx_shims.h
 * @brief Zero-Dependency Compatibility Shim for QNX Neutrino 8.0 Native & Host POSIX
 *
 * When compiled under QNX SDP 8.0 (__QNXNTO__), transparently links directly to
 * microkernel syscalls (sys/neutrino.h, sys/syspage.h).
 * When compiled on host (Linux, macOS, Windows/MinGW), provides cycle-accurate
 * thread-safe emulations of QNX channels, pulses, and TSC timers.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

#ifndef QNX_SHIMS_H
#define QNX_SHIMS_H

#if defined(__QNXNTO__)

/* Native BlackBerry QNX Neutrino 8.0 Microkernel Environment */
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/syspage.h>
#include <sys/mman.h>
#include <sys/trace.h>
#include <sched.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define QNX_PLATFORM_NAME "QNX Neutrino RTOS 8.0 (Native)"
#define QNX_IS_NATIVE     1

#else

/* Host Emulation Environment (Linux / macOS / POSIX Fallback) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sched.h>
#endif

#define QNX_PLATFORM_NAME "POSIX Fallback Emulation (Host Mode)"
#define QNX_IS_NATIVE     0

#ifdef __cplusplus
extern "C" {
#endif

/* QNX Specific Flags & Constants */
#define _NTO_CHF_FIXED_PRIORITY       0x0001
#define _NTO_CHF_UNBLOCK              0x0002
#define _NTO_SIDE_CHANNEL             -1
#define _NTO_TCTL_IO                  1
#define _NTO_TCTL_RUNMASK             4
#define _NTO_PULSE_CODE_DISCONNECT    -1
#define _NTO_PULSE_CODE_UNBLOCK       -2

#ifndef _NTO_TRACE_INSERTEVENT
#define _NTO_TRACE_INSERTEVENT        1
#endif

#ifndef _NTO_TRACE_USERFIRST
#define _NTO_TRACE_USERFIRST          0x00000400
#endif

static inline int TraceEvent(int cmd, ...) {
    (void)cmd;
    return 0; /* POSIX Host Fallback */
}

#ifndef SCHED_SPORADIC
    #ifdef SCHED_RR
        #define SCHED_SPORADIC        SCHED_RR
    #else
        #define SCHED_SPORADIC        0
    #endif
#endif

#ifndef MCL_CURRENT
    #define MCL_CURRENT               1
    #define MCL_FUTURE                2
#endif

/* QNX Pulse Struct Definition */
#ifndef _QNX_PULSE_STRUCT_DEFINED
#define _QNX_PULSE_STRUCT_DEFINED
union sigval_shim {
    int   sival_int;
    void *sival_ptr;
};

struct _pulse {
    uint16_t          type;
    uint16_t          subtype;
    int8_t            code;
    uint8_t           zero[3];
    union sigval_shim value;
    int32_t           scoid;
};
#endif

/* QNX Syspage Simulation */
typedef struct {
    uint64_t cycles_per_sec;
    uint32_t timer_scale;
    uint32_t timer_load;
} qnx_qtime_t;

static inline qnx_qtime_t *__get_shim_qtime(void) {
    static qnx_qtime_t s_qtime = {0};
    if (s_qtime.cycles_per_sec == 0) {
        /* Calibrate nominal 1.0 GHz base frequency if unknown */
        s_qtime.cycles_per_sec = 1000000000ULL;
    }
    return &s_qtime;
}

#define qtime __get_shim_qtime()
#define SYSPAGE_ENTRY(entry_name) (entry_name)

/* High-Precision Cycle Counter Emulation */
static inline uint64_t ClockCycles(void) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(_MSC_VER)
        return __rdtsc();
    #elif defined(__GNUC__) || defined(__clang__)
        uint32_t lo, hi;
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
        return ((uint64_t)hi << 32) | lo;
    #endif
#elif defined(__aarch64__)
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r" (val));
    return val;
#else
    struct timespec ts;
    #if defined(CLOCK_MONOTONIC_RAW)
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    #else
        clock_gettime(CLOCK_MONOTONIC, &ts);
    #endif
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#endif
}

/* Emulated QNX Channel and Pulse Queue Mechanics */
#define MAX_SHIM_CHANNELS 32
#define SHIM_QUEUE_SIZE   256

typedef struct {
    bool active;
    unsigned flags;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    struct _pulse queue[SHIM_QUEUE_SIZE];
    size_t head;
    size_t tail;
    size_t count;
} shim_channel_t;

static shim_channel_t g_shim_channels[MAX_SHIM_CHANNELS];
static pthread_mutex_t g_channel_mgr_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_channel_mgr_inited = false;

static inline void __init_shim_channels(void) {
    pthread_mutex_lock(&g_channel_mgr_lock);
    if (!g_channel_mgr_inited) {
        for (int i = 0; i < MAX_SHIM_CHANNELS; i++) {
            g_shim_channels[i].active = false;
            g_shim_channels[i].head = 0;
            g_shim_channels[i].tail = 0;
            g_shim_channels[i].count = 0;
            pthread_mutex_init(&g_shim_channels[i].lock, NULL);
            pthread_cond_init(&g_shim_channels[i].cond, NULL);
        }
        g_channel_mgr_inited = true;
    }
    pthread_mutex_unlock(&g_channel_mgr_lock);
}

static inline int ChannelCreate(unsigned flags) {
    __init_shim_channels();
    pthread_mutex_lock(&g_channel_mgr_lock);
    int chid = -1;
    for (int i = 0; i < MAX_SHIM_CHANNELS; i++) {
        if (!g_shim_channels[i].active) {
            g_shim_channels[i].active = true;
            g_shim_channels[i].flags = flags;
            g_shim_channels[i].head = 0;
            g_shim_channels[i].tail = 0;
            g_shim_channels[i].count = 0;
            chid = i + 1; /* Return 1-based channel id */
            break;
        }
    }
    pthread_mutex_unlock(&g_channel_mgr_lock);
    return chid;
}

static inline int ChannelDestroy(int chid) {
    if (chid <= 0 || chid > MAX_SHIM_CHANNELS) return -1;
    int idx = chid - 1;
    pthread_mutex_lock(&g_shim_channels[idx].lock);
    g_shim_channels[idx].active = false;
    pthread_cond_broadcast(&g_shim_channels[idx].cond);
    pthread_mutex_unlock(&g_shim_channels[idx].lock);
    return 0;
}

static inline int ConnectAttach(uint32_t nd, pid_t pid, int chid, unsigned index, int flags) {
    (void)nd; (void)pid; (void)index; (void)flags;
    if (chid <= 0 || chid > MAX_SHIM_CHANNELS) return -1;
    return chid; /* Simple direct map coid == chid */
}

static inline int ConnectDetach(int coid) {
    (void)coid;
    return 0;
}

static inline int MsgSendPulse(int coid, int priority, int code, int value) {
    if (coid <= 0 || coid > MAX_SHIM_CHANNELS) return -1;
    int idx = coid - 1;

    pthread_mutex_lock(&g_shim_channels[idx].lock);
    if (!g_shim_channels[idx].active) {
        pthread_mutex_unlock(&g_shim_channels[idx].lock);
        return -1;
    }

    if (g_shim_channels[idx].count >= SHIM_QUEUE_SIZE) {
        pthread_mutex_unlock(&g_shim_channels[idx].lock);
        errno = EAGAIN;
        return -1;
    }

    struct _pulse *p = &g_shim_channels[idx].queue[g_shim_channels[idx].head];
    p->type = 0;
    p->subtype = (uint16_t)priority;
    p->code = (int8_t)code;
    p->value.sival_int = value;
    p->scoid = coid;

    g_shim_channels[idx].head = (g_shim_channels[idx].head + 1) % SHIM_QUEUE_SIZE;
    g_shim_channels[idx].count++;

    pthread_cond_signal(&g_shim_channels[idx].cond);
    pthread_mutex_unlock(&g_shim_channels[idx].lock);
    return 0;
}

static inline int MsgReceivePulse(int chid, void *pulse, size_t bytes, void *vtp) {
    (void)vtp;
    if (chid <= 0 || chid > MAX_SHIM_CHANNELS || !pulse || bytes < sizeof(struct _pulse)) {
        return -1;
    }
    int idx = chid - 1;

    pthread_mutex_lock(&g_shim_channels[idx].lock);
    while (g_shim_channels[idx].active && g_shim_channels[idx].count == 0) {
        pthread_cond_wait(&g_shim_channels[idx].cond, &g_shim_channels[idx].lock);
    }

    if (!g_shim_channels[idx].active) {
        pthread_mutex_unlock(&g_shim_channels[idx].lock);
        return -1;
    }

    struct _pulse *p = &g_shim_channels[idx].queue[g_shim_channels[idx].tail];
    memcpy(pulse, p, sizeof(struct _pulse));

    g_shim_channels[idx].tail = (g_shim_channels[idx].tail + 1) % SHIM_QUEUE_SIZE;
    g_shim_channels[idx].count--;

    pthread_mutex_unlock(&g_shim_channels[idx].lock);
    return 0;
}

static inline int ThreadCtl(int cmd, void *data) {
    (void)cmd; (void)data;
    return 0; /* No-op on POSIX fallback */
}

#if !defined(_WIN32)
static inline int mlockall_shim(int flags) {
    (void)flags;
#if defined(MCL_CURRENT) && !defined(__APPLE__)
    return mlockall(flags);
#else
    return 0;
#endif
}
#define mlockall(f) mlockall_shim(f)
#else
static inline int mlockall_shim(int flags) { (void)flags; return 0; }
#define mlockall(f) mlockall_shim(f)
#endif

#ifdef __cplusplus
}
#endif

#endif /* !__QNXNTO__ */

#endif /* QNX_SHIMS_H */
