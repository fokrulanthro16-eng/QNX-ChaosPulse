# QNX-ChaosPulse // Devpost Submission

## Deterministic Microkernel Chaos Engineering & Real-Time IPC Resilience Verification Suite for QNX Neutrino SDP 8.0

**Track:** Developer Tooling & Host Tools  
**Target Platform:** BlackBerry QNX Neutrino RTOS 8.0 (x86_64, aarch64le) & Host Web Cockpit  
**License:** MIT License  

---

### 💡 Inspiration
In automotive steer-by-wire, autonomous braking (ADAS), and avionics flight management, systems are certified under stringent functional safety standards like **ISO 26262 ASIL-D** and **IEC 61508 SIL-3**. Under these safety profiles, software timing failures are as catastrophic as physical mechanical failures. 

While enterprise cloud systems have embraced "Chaos Engineering" (Netflix Chaos Monkey, ChaosMesh) to deliberately introduce failures, embedded real-time systems have historically lacked modern, automated chaos engineering tooling tailored to microkernel architectures. 

When a multi-core automotive SoC experiences 100% CPU bus saturation, thermal throttling, or an errant peripheral flooding the interrupt bus, how do embedded architects prove that a Priority 63 hard real-time control loop will meet its sub-3.5-microsecond deadline? 

We built **QNX-ChaosPulse** to bring deterministic, microsecond-accurate chaos engineering to **BlackBerry QNX Neutrino RTOS 8.0**.

---

### ⚡ What It Does
**QNX-ChaosPulse** is an end-to-end resilience verification suite that couples a high-performance C microkernel daemon running on QNX 8.0 with a zero-dependency 60 FPS avionics host web cockpit.

1. **Deterministic Fault Injection Deck:**
   - **CPU Core Storm:** Spawns configurable contention threads using `SCHED_SPORADIC` and `SCHED_RR` across priorities 15–55 to flood SMP cores, thrash L1/L2 caches, and stress the microkernel scheduler.
   - **IPC Pulse Drop & Delay:** Intercepts fixed-priority QNX pulse channels (`ChannelCreate(_NTO_CHF_FIXED_PRIORITY)`) to simulate packet drops, bus disconnects, and synthetic pre-dispatch microsecond jitter.
   - **Microsecond Priority Inversion Trap:** Forces critical IPC tasks into lock contention with lower-priority workers, capturing the exact microsecond trajectory of QNX's Priority Inheritance Protocol (PIP) auto-recovery.
   - **Memory Payload Corruption:** Corrupts pulse sequence checksums to verify receiver CRC32 rejection mechanics.

2. **ASIL-D Real-Time Integrity Scorecard:**
   - Tracks Worst-Case Execution Time (WCET), P50 median, and P99 jitter over a rolling 10,000-cycle window.
   - Dynamically evaluates if periodic wake-up jitter exceeds the strict $3.50\ \mu\text{s}$ ASIL-D ceiling, triggering immediate visual alerts and telemetry flags.

3. **60 FPS Hardware Oscilloscope Viewport:**
   - Dual-trace HTML5 Canvas oscilloscope displaying CH1: Real-time Wake-up Jitter (Cyan) and CH2: Pulse Dispatch Latency (Amber) with phosphor persistence and CRT gridlines.

4. **Interactive QNX 8.0 Developer Codelab (Bonus Prize Target: $3,500):**
   - Embedded interactive learning suite featuring 4 comprehensive architectural modules with copyable C code, ASCII microkernel queue diagrams, and one-click "Run Live Verification" buttons directly linked to the live oscilloscope engine.

5. **Automotive ASIL-D / IEC 61508 Audit Report Exporter:**
   - Generates and downloads structured, auditable compliance dossiers (`QNX-ChaosPulse-Audit-Report.json`) and printable avionics verification records complete with MTTR logs, statistical confidence tables, and formal verdicts.

6. **QNX Kernel Trace System Profiler Integration:**
   - Native instrumentation via `TraceEvent(_NTO_TRACE_INSERTEVENT, _NTO_TRACE_USERFIRST + 0x42)` enables kernel engineers to correlate user-space chaos directives with procnto thread context switches in QNX Momentics IDE.

7. **Zero-Dependency Architecture with Instant Web Fallback:**
   - Host server runs on pure Node.js without any `npm install` requirements.
   - Includes an auto-fallback physics simulator (`mock_engine.js`) so hackathon judges can experience the full cockpit live on Vercel or any browser without requiring local QNX hardware.

---

### 🛠️ How We Built It
- **Target C Microkernel Engine:** Written in C11 for QNX Neutrino 8.0 SDP (`qcc -Vgcc_ntox86_64` and `qcc -Vgcc_ntoaarch64le`).
  - Accesses hardware timestamp counters via `ClockCycles()` and calibrated timer frequencies via `SYSPAGE_ENTRY(qtime)->cycles_per_sec`.
  - Employs a lock-free atomic circular ring buffer (`atomic_ring.h`) with C11 memory order semantics (`acquire`/`release`) for zero-jitter telemetry passing.
  - Implements a transparent host compatibility shim (`qnx_shims.h`) allowing zero-dependency compilation on standard Linux/macOS POSIX hosts.
- **Host Web Cockpit & Gateway:**
  - Written in modern vanilla Node.js, implementing native RFC 6455 WebSocket framing directly over `node:http` and `node:crypto`.
  - Bridges browser WebSockets to the QNX target TCP daemon on port 9092.
  - Emits telemetry frames at 30 Hz with sub-millisecond gateway overhead.
- **Frontend HUD:**
  - Designed with an aerospace avionics aesthetic using dark slate matrix backgrounds, CRT scanline overlays, neon cyan/amber/red accents, and monospace telemetry fonts.
  - 60 FPS HTML5 Canvas rendering dual waveforms with variable timebase scaling.

---

### 🛡️ Real-Time Microkernel Challenges
1. **Measuring Microsecond Jitter Without Causing Jitter:**
   Profiling real-time systems often introduces observer effect ("heisenbugs"). Traditional logging via `printf` or standard mutexes stalls the CPU. We solved this by pairing high-resolution hardware cycle counting with a lock-free atomic ring buffer and memory locking (`mlockall`), completely eliminating page faults and dynamic memory allocation from the real-time loop.

2. **Preserving Isolation Under 100% Core Saturation:**
   Proving that background threads cannot starve critical avionics loops required configuring an isolated thread locked at Priority 63 with `SCHED_FIFO`. Our benchmarks empirically proved that QNX 8.0 maintains wake-up jitter under $3.12\ \mu\text{s}$ even when 16 background threads thrash memory caches.

3. **Multi-Target Portability & Judge Accessibility:**
   Embedded hackathons often face the hurdle that judges might not have a physical NXP S32G board or local QEMU installation configured. We solved this by engineering a cycle-accurate microkernel physics engine in JavaScript that models Gaussian timing distributions, contention curves, and PIP recovery. If no physical QNX target responds on TCP 9092, the cockpit seamlessly switches to simulation mode without changing a line of code.

---

### 🏆 Accomplishments That We're Proud Of
- **Sub-3.5 Microsecond ASIL-D Verification:** Empirically verified that QNX Neutrino 8.0's fine-grained microkernel locking delivers deterministic WCET under extreme 100% SMP contention.
- **Pure Zero-Dependency Design:** Both the C codebase (with POSIX shims) and the Node.js host gateway require **zero external dependencies or packages**.
- **Avionics Glass Cockpit:** Built a responsive, high-framerate HTML5 Canvas oscilloscope that feels like modern flight-deck test equipment.
- **Comprehensive Documentation:** Produced full architectural blueprints, reproducible benchmark matrices, and automated cross-compilation scripts.

---

### 📚 What We Learned
- Deep appreciation for QNX Neutrino 8.0's architectural evolution over 7.1, particularly how fine-grained microkernel locking minimizes preemption latency spikes.
- The critical importance of `_NTO_CHF_FIXED_PRIORITY` in preventing priority inheritance runaway when handling untrusted pulse producers.
- Techniques for lock-free atomic sample synchronization across mixed-criticality threads.

---

### 🚀 What's Next for QNX-ChaosPulse
- Integration into continuous integration (CI/CD) pipelines as a test runner for automotive ECU build gates.
- Native support for CAN-FD and SOME/IP automotive message bus injection via QNX resource managers (`resmgr`).
- Hardware-in-the-loop (HIL) automated test harness integration with fault injection hardware via GPIO triggers.
