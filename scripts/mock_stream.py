#!/usr/bin/env python3
"""
QNX-ChaosPulse // Standalone Python 3 Telemetry Streamer & Mock Daemon
Acts as a TCP 9092 daemon server for verifying host visualization cockpits
independently of local C toolchains or QEMU instances.

Standard library only - zero external pip dependencies.
Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
"""

import sys
import os
import time
import socket
import select
import struct
import random
import json
import zlib
import signal
import threading

PORT = 9092
MAGIC = 0x43503830  # "CP80"
ASIL_D_MAX_JITTER_US = 3.50

class QnxChaosSimulator:
    def __init__(self):
        self.seq = 0
        self.start_time = time.time()
        self.tsc_khz = 2400000  # 2.4 GHz
        self.total_pulses = 0
        self.dropped_pulses = 0
        self.corrupted_pulses = 0
        
        # State & Faults
        self.storm_active = False
        self.storm_threads = 0
        self.cpu_load = 6
        self.drop_rate = 0.0
        self.inversion_active = False
        self.inversion_remaining_ticks = 0
        self.memory_corrupt_active = False
        self.wcet_jitter = 0.85
        
        self.rolling_jitter = []
        self.lock = threading.Lock()

    def handle_directive(self, cmd_json):
        action = cmd_json.get("action", "")
        with self.lock:
            if action == "start_storm":
                self.storm_active = True
                self.storm_threads = int(cmd_json.get("threads", 4))
                self.cpu_load = min(98, 75 + self.storm_threads * 3)
                if "drop_rate" in cmd_json:
                    self.drop_rate = float(cmd_json["drop_rate"])
            elif action == "stop_storm":
                self.storm_active = False
                self.storm_threads = 0
                self.cpu_load = 6
                self.drop_rate = 0.0
            elif action == "inject_drop":
                self.drop_rate = float(cmd_json.get("rate", 0.15))
            elif action == "priority_inversion":
                self.inversion_active = True
                self.inversion_remaining_ticks = 25
            elif action == "memory_fault":
                self.memory_corrupt_active = True
                self.corrupted_pulses += 15
            elif action == "reset":
                self.storm_active = False
                self.storm_threads = 0
                self.cpu_load = 6
                self.drop_rate = 0.0
                self.inversion_active = False
                self.inversion_remaining_ticks = 0
                self.memory_corrupt_active = False
                self.wcet_jitter = 0.85
                self.rolling_jitter.clear()

    def step(self):
        with self.lock:
            self.seq += 1
            self.total_pulses += 33

            # Gaussian baseline jitter
            base_jitter = random.gauss(0.82, 0.14)
            base_latency = 1.15 + random.uniform(0.0, 0.35)

            if self.storm_active:
                contention = (self.storm_threads / 16.0) * 1.8
                base_jitter += contention + random.uniform(0.0, 0.3)
                base_latency += contention * 0.7

            if self.drop_rate > 0:
                self.dropped_pulses += int(round(33 * self.drop_rate))
                if random.random() < self.drop_rate:
                    base_latency += 8.5 + random.uniform(0.0, 5.0)

            if self.inversion_active:
                if self.inversion_remaining_ticks > 0:
                    self.inversion_remaining_ticks -= 1
                    base_jitter += 24.0 + random.uniform(0.0, 16.0)
                    base_latency += 38.0 + random.uniform(0.0, 14.0)
                else:
                    self.inversion_active = False

            jitter = max(0.1, base_jitter)
            latency = max(0.2, base_latency)

            self.rolling_jitter.append(jitter)
            if len(self.rolling_jitter) > 300:
                self.rolling_jitter.pop(0)

            if jitter > self.wcet_jitter:
                self.wcet_jitter = jitter

            s = sorted(self.rolling_jitter)
            p50 = s[int(len(s) * 0.50)] if s else jitter
            p99 = s[min(len(s) - 1, int(len(s) * 0.99))] if s else jitter

            breach = 1 if (jitter > ASIL_D_MAX_JITTER_US or self.wcet_jitter > ASIL_D_MAX_JITTER_US) else 0
            safety_state = 2 if breach else (1 if p99 > 2.5 else 0)

            active_faults = 0
            if self.drop_rate > 0: active_faults |= 1
            if self.inversion_active: active_faults |= 4
            if self.storm_active: active_faults |= 8
            if self.memory_corrupt_active: active_faults |= 16

            cycles = int((time.time() - self.start_time) * self.tsc_khz * 1000)

            # JSON payload
            json_frame = {
                "timestamp": cycles,
                "jitter_us": round(jitter, 3),
                "latency_us": round(latency, 3),
                "p50": round(p50, 3),
                "p99": round(p99, 3),
                "wcet": round(self.wcet_jitter, 3),
                "dropped": self.dropped_pulses,
                "total": self.total_pulses,
                "cpu_load": self.cpu_load,
                "threads": self.storm_threads,
                "breach": breach,
                "state": safety_state,
                "priority": 63,
                "active_faults": active_faults,
                "source": "PYTHON_STREAM_MOCK_8_0"
            }

            # Binary frame format: <I Q Q I f f f f f I I I I I B B B B I
            bin_prefix = struct.pack(
                "<IQQIfffffIIIIIBBBB",
                MAGIC,
                self.seq,
                cycles,
                self.tsc_khz,
                jitter,
                latency,
                p50,
                p99,
                self.wcet_jitter,
                self.total_pulses,
                self.dropped_pulses,
                self.corrupted_pulses,
                63,  # priority
                active_faults,
                self.cpu_load,
                self.storm_threads,
                breach,
                safety_state
            )
            crc = zlib.crc32(bin_prefix) & 0xFFFFFFFF
            binary_frame = bin_prefix + struct.pack("<I", crc)

            return json_frame, binary_frame

def main():
    sim = QnxChaosSimulator()
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", PORT))
    server.listen(5)
    server.setblocking(False)

    print("===================================================================")
    print(f"  QNX-CHAOSPULSE // Python 3 Telemetry Mock Server Running")
    print(f"  Listening on: TCP 0.0.0.0:{PORT}")
    print(f"  ASIL-D Threshold: {ASIL_D_MAX_JITTER_US} us")
    print("===================================================================")

    clients = []
    running = True

    def sig_handler(sig, frame):
        nonlocal running
        print("\nShutting down mock streamer...")
        running = False

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    try:
        while running:
            # Check for new connections or incoming commands
            readable = [server] + clients
            r, _, _ = select.select(readable, [], [], 0.033)

            for s in r:
                if s is server:
                    client_sock, addr = server.accept()
                    client_sock.setblocking(False)
                    clients.append(client_sock)
                    print(f"[STREAMER] Connected client from {addr}")
                else:
                    try:
                        data = s.recv(1024)
                        if data:
                            cmd_str = data.decode("utf-8", errors="ignore").strip()
                            print(f"[DIRECTIVE] Received: {cmd_str}")
                            try:
                                cmd_obj = json.loads(cmd_str)
                                sim.handle_directive(cmd_obj)
                            except Exception:
                                pass
                            s.sendall(b'{"status":"ACK"}\n')
                        else:
                            clients.remove(s)
                            s.close()
                    except Exception:
                        if s in clients:
                            clients.remove(s)
                        s.close()

            # Broadcast frame to all active clients
            json_frame, binary_frame = sim.step()
            json_payload = (json.dumps(json_frame) + "\n").encode("utf-8")

            dead_clients = []
            for c in clients:
                try:
                    c.sendall(json_payload)
                except Exception:
                    dead_clients.append(c)

            for d in dead_clients:
                if d in clients:
                    clients.remove(d)
                try:
                    d.close()
                except Exception:
                    pass

            time.sleep(0.033)

    finally:
        for c in clients:
            try: c.close()
            except Exception: pass
        server.close()
        print("Server closed cleanly.")

if __name__ == "__main__":
    main()
