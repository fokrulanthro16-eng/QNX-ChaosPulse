# QNX-ChaosPulse // Microkernel Architecture & Safety Specification

## Deterministic Chaos Engineering & Real-Time IPC Resilience Suite
### BlackBerry QNX Neutrino RTOS 8.0 // ISO 26262 ASIL-D & IEC 61508 SIL-3

---

## 1. Executive Architecture Summary

**QNX-ChaosPulse** is an industrial-grade developer tooling suite designed to evaluate, verify, and stress-test the hard real-time guarantees, inter-process communication (IPC) determinism, and scheduler isolation of the **BlackBerry QNX Neutrino RTOS 8.0** microkernel under catastrophic hardware and bus contention.

In automotive Advanced Driver Assistance Systems (ADAS), steer-by-wire, automated braking, and aerospace flight control, real-time operating systems must guarantee that high-criticality tasks execute within bounded Worst-Case Execution Time (WCET). Under ISO 26262 ASIL-D:
- Missing a 1.0 ms actuator loop deadline is a safety-critical failure.
- Jitter exceeding $3.50\ \mu\text{s}$ indicates memory bus saturation, unhandled cache contention, or priority inversion anomalies.

```
+-------------------------------------------------------------------------------+
|                       HOST VISUALIZATION COCKPIT (Port 3000)                  |
|  [60 FPS HTML5 Canvas Scope]  [ASIL-D Scorecard]  [Chaos Control Deck]        |
+-------------------------------------------------------------------------------+
                                      ^  |
                     WebSocket Stream |  | Chaos Directives (JSON)
                                      |  v
+-------------------------------------------------------------------------------+
|                 ZERO-DEPENDENCY HOST GATEWAY (Node.js / RFC 6455)             |
|                  Auto-Probing Target TCP 9092 / Mock Fallback                 |
+-------------------------------------------------------------------------------+
                                      ^  |
                Binary/JSON Telemetry |  | Directives (TCP 9092)
                                      |  v
+-------------------------------------------------------------------------------+
|                      QNX NEUTRINO SDP 8.0 TARGET DAEMON                       |
|                             chaospulse_daemon.c                               |
|                                                                               |
|  +--------------------------+  +--------------------------+  +--------------+ |
|  |     CYCLE TELEMETRY      |  |    PULSE INTERCEPTOR     |  |  CPU STORM   | |
|  |     cycle_telemetry.c    |  |    pulse_interceptor.c   |  |  generator.c | |
|  |   - SYSPAGE qtime TSC    |  |  - ChannelCreate(FIXED)  |  | - SCHED_FIFO | |
|  |   - 10,000 Rolling Buffer|  |  - MsgSendPulse / Recv   |  |   Priority 63| |
|  |   - P50, P99, WCET       |  |  - Drop & Jitter Inject  |  | - Contention | |
|  |   - CRC32 Frame Guard    |  |  - Priority Invert Trap  |  |   P15 - P55  | |
|  +--------------------------+  +--------------------------+  +--------------+ |
+-------------------------------------------------------------------------------+
                                      |
                     POSIX.1b Real-Time Microkernel Core
              QNX Neutrino 8.0 Microkernel (procnto-smp / Syspage)
```

---

## 2. QNX Neutrino 8.0 Microkernel Fundamentals

Unlike monolithic kernels (such as Linux) where drivers, filesystems, and network stacks share a single supervisor address space, QNX Neutrino implements a true microkernel architecture (`procnto`).

### 2.1 Microkernel Primitives
The QNX 8.0 microkernel is responsible for only fundamental primitives:
1. **Thread Services**: Creation, scheduling, synchronization.
2. **Signal & Pulse Services**: Asynchronous event signaling.
3. **Message Passing**: Synchronous and asynchronous inter-process communication.
4. **Synchronization Primitives**: Mutexes, condition variables, semaphores.
5. **Timer Services**: High-resolution timers driven by hardware clock interrupts.
6. **Memory Management**: Address space virtualization via MMU.

All other OS components—including filesystems (`devb-*`), network stacks (`io-pkt`), character devices, and user applications—run in completely memory-protected user spaces.

### 2.2 Synchronous Messages vs. Asynchronous Pulses
QNX IPC provides two distinct primitives:
- **Synchronous Messages (`MsgSend`, `MsgReceive`, `MsgReply`)**: 
  - Causes the sender to block (`SEND-blocked`) until the receiver reads (`REPLY-blocked`) and replies.
  - Implements zero-copy memory mapping between separate address spaces.
- **Asynchronous Pulses (`MsgSendPulse`, `MsgReceivePulse`)**:
  - Non-blocking for the sender.
  - Carries a compact 8-bit code and a 32-bit/64-bit value payload.
  - Can be dispatched from Interrupt Service Routines (ISRs) without blocking.
  - Stored directly in the channel's microkernel queue in strict priority order.

---

## 3. Dedicated Fixed-Priority Channels

In standard QNX channels, the receiving thread temporarily inherits the priority of the sending thread (`_NTO_CHF_UNBLOCK`). However, in high-frequency safety-critical control loops, priority inheritance from untrusted clients can induce jitter.

QNX-ChaosPulse configures its channels with `_NTO_CHF_FIXED_PRIORITY`:
```c
g_interceptor.chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY);
```
- **Determinism Guarantee**: The receiver thread retains its designated priority (Priority 55) irrespective of the priority of any pulse producer.
- **FIFO Microkernel Enqueuing**: If multiple pulses arrive simultaneously, pulses are strictly ordered by pulse priority, preventing lower-priority tasks from starving real-time telemetry.

---

## 4. Deterministic Scheduling & CPU Storm Contention

QNX Neutrino supports POSIX.1b scheduling disciplines with 255 priority levels (0 to 255; 1 to 63 typically accessible to standard processes):

### 4.1 Scheduling Policies
| Policy | Priority Band | Description |
|---|---|---|
| `SCHED_FIFO` | 63 (Hard RT) | Never timesliced. Runs until it blocks on a channel or yields. |
| `SCHED_SPORADIC` | 15 - 55 | Bounded CPU budget per replenishment period. Prevents runaway contention. |
| `SCHED_RR` | 15 - 55 | Timesliced round-robin across equal-priority threads. |

### 4.2 Hard Real-Time Priority 63 Verification Loop
The `cpu_storm_generator.c` subsystem spawns an isolated verification thread configured as:
- **Scheduling Policy**: `SCHED_FIFO`
- **Priority**: 63 (Highest standard real-time priority)
- **Memory Locking**: `mlockall(MCL_CURRENT | MCL_FUTURE)` to eliminate demand paging latency.
- **Loop Frequency**: 1,000 Hz ($\Delta t = 1,000,000\ \text{ns}$).

Even when background storm workers saturate 100% of SMP cores using cache-thrashing matrix multiplications, the QNX 8.0 microkernel guarantees immediate preemption:
$$\text{Jitter}_{\text{FIFO}} = |t_{\text{actual}} - t_{\text{expected}}| \le 3.50\ \mu\text{s}$$

---

## 5. Priority Inversion & Self-Healing Protocol (PIP)

Priority inversion occurs when a high-priority task ($T_H$) is blocked waiting for a mutual exclusion primitive held by a low-priority task ($T_L$), while an intermediate-priority task ($T_M$) preempts $T_L$.

```
Priority
  ^
  |  [T_H Priority 63] ------------------ Blocked waiting for lock -----------------> [T_H Resumes]
  |                                        ^                                            ^
  |  [T_M Priority 35]                     | ===== Preempts T_L indefinitely ===== |    |
  |                                        |                                            |
  |  [T_L Priority 10] === Holds Lock ===> | (Boosted to P63 via PIP) ==============> Releases Lock
  +-------------------------------------------------------------------------------------------> Time
```

QNX Neutrino 8.0 employs the **Priority Inheritance Protocol (PIP)**:
1. When $T_H$ blocks on a mutex held by $T_L$, the microkernel dynamically elevates $T_L$'s priority to match $T_H$ (Priority 63).
2. $T_M$ can no longer preempt $T_L$.
3. $T_L$ executes rapidly, releases the lock, and instantly drops back to Priority 10.
4. $T_H$ claims the lock and continues execution.

QNX-ChaosPulse's `FAULT_PRIORITY_INVERSION_TEST` temporarily isolates the receiver thread to expose this phenomenon, recording the microsecond duration before the PIP engine recovers the system.

---

## 6. Lock-Free Atomic Ring Buffer Mechanics

To exchange thousands of high-frequency cycle timing metrics without lock contention, `atomic_ring.h` implements a lock-free Single-Producer Single-Consumer (SPSC) circular queue using C11 memory fences:

- Power-of-two capacity ($2^{14} = 16,384$ entries) eliminates integer division:
  $$\text{Index} = \text{Head} \ \& \ (\text{Capacity} - 1)$$
- Producer publishes with `memory_order_release`:
  ```c
  atomic_store_explicit(&ring->head, current_head + 1, memory_order_release);
  ```
- Consumer reads with `memory_order_acquire`:
  ```c
  size_t current_head = atomic_load_explicit(&ring->head, memory_order_acquire);
  ```
- Non-blocking overflow protection: if the queue is full, the sample is safely dropped and `dropped_count` is incremented atomically rather than stalling the real-time execution path.

---

## 7. Wire Protocol & Telemetry Frame Integrity

Every binary telemetry frame emitted on TCP 9092 is packed (`#pragma pack(push, 1)`) and guarded by an IEEE 802.3 32-bit cyclic redundancy check:

```c
typedef struct {
    uint32_t magic;                /* 0x43503830 ("CP80") */
    uint64_t sequence_id;          /* Monotonically increasing counter */
    uint64_t timestamp_cycles;     /* Hardware ClockCycles() */
    uint32_t tsc_freq_khz;         /* Calibrated system timer frequency */
    float    jitter_us;            /* Instantaneous periodic wakeup jitter */
    float    latency_us;           /* End-to-end pulse delivery latency */
    float    p50_jitter_us;        /* 50th percentile (Median) */
    float    p99_jitter_us;        /* 99th percentile */
    float    wcet_jitter_us;       /* Absolute worst-case execution time jitter */
    uint32_t total_pulses;         /* Total synchronous/asynchronous pulses */
    uint32_t dropped_pulses;       /* Intercepted/dropped pulses */
    uint32_t corrupted_pulses;     /* Intentionally corrupted payload checks */
    uint32_t current_priority;     /* Current priority (63) */
    uint32_t active_faults;        /* Active fault bitmask */
    uint8_t  cpu_load_pct;         /* Core saturation percentage */
    uint8_t  active_storm_threads; /* Active contention threads */
    uint8_t  asil_breach_flag;     /* 1 if jitter > 3.50 us */
    uint8_t  safety_state;         /* 0: Nominal, 1: Warning, 2: Breach */
    uint32_t crc32;                /* Frame integrity checksum */
} chaospulse_telemetry_t;
```

This strict structure guarantees zero-copy serialization between x86_64, aarch64le, and web visualization gateways.

---

## 8. QNX Kernel Tracing Integration (`tracelogger` / `TraceEvent()` / System Profiler)

In safety-critical automotive embedded systems, correlating user-space application failures with microkernel context switches is vital for Root Cause Analysis (RCA). **QNX-ChaosPulse** integrates directly with the QNX Neutrino 8.0 kernel instrumented logging subsystem via `TraceEvent(_NTO_TRACE_INSERTEVENT, ...)`.

### 8.1 Custom Trace Classes & Event Codes
Chaos events are injected into the kernel trace ring buffer under a dedicated custom class:
- **Trace Class Index**: `_NTO_TRACE_USERFIRST + 0x42` (`0x00000442`)
- **Event Codes**:
  | Event Code | Symbolic Identifier | Description | Injected Payload Parameters |
  |---|---|---|---|
  | `0x01` | `CHAOS_TRACE_STORM_START` | CPU Contention Storm Initiated | `data1 = thread_count`, `data2 = sched_policy` |
  | `0x02` | `CHAOS_TRACE_STORM_STOP` | CPU Storm Terminated | `data1 = 0`, `data2 = 0` |
  | `0x03` | `CHAOS_TRACE_PULSE_DROP` | Pulse Intercepted & Dropped | `data1 = drop_rate * 100`, `data2 = pulse_seq` |
  | `0x04` | `CHAOS_TRACE_JITTER_INJECT`| Synthetic Pre-dispatch Delay | `data1 = jitter_us`, `data2 = 0` |
  | `0x05` | `CHAOS_TRACE_INVERSION_START` | Priority Inversion Trap Injected | `data1 = ceiling_prio (63)`, `data2 = owner_prio (10)` |
  | `0x06` | `CHAOS_TRACE_INVERSION_HEALED`| PIP Auto-Healing Resolved | `data1 = recovery_time_ms`, `data2 = recovered_jitter` |
  | `0x07` | `CHAOS_TRACE_ASIL_BREACH` | ISO 26262 ASIL-D Boundary Breach | `data1 = instant_jitter_ns`, `data2 = limit_ns (3500)` |
  | `0x08` | `CHAOS_TRACE_SYSTEM_RESET`| System Reset / Panic Stop Executed | `data1 = 0`, `data2 = 0` |

### 8.2 Capturing Binary Traces on Target
Engineers can capture kernel-level context switches synchronized with chaos events using QNX's high-speed ring buffer:
```bash
# 1. Allocate 64 MB kernel trace ring buffer in shared memory
tracelogger -b 65536 -f /dev/shmem/chaospulse.trace -k 1000 &

# 2. Execute QNX-ChaosPulse daemon or chaos test sequence
/tmp/chaospulse_daemon 9092 &

# 3. Stop tracing and pull binary file to host
killall tracelogger
scp root@qnx-target:/dev/shmem/chaospulse.trace ./chaospulse.trace
```

### 8.3 QNX Momentics IDE System Profiler Analysis
When the resulting `.trace` buffer is opened in **QNX Momentics IDE (System Profiler)**:
1. **User Event Track**: Displays markers for `CHAOS_TRACE_STORM_START` and `CHAOS_TRACE_INVERSION_START` precisely synchronized with the CPU cycle counter.
2. **Context Switch Timeline**: Visualizes how background storm workers (`SCHED_SPORADIC`) are immediately preempted within $1.15\ \mu\text{s}$ when the Priority 63 hard real-time thread awakens.
3. **Priority Inheritance Verification**: During `CHAOS_TRACE_INVERSION_START`, the timeline graphically depicts the low-priority lock owner thread elevating dynamically from Priority 10 to Priority 63, executing its critical section, and returning to Priority 10 upon mutex release.
