#!/usr/bin/env python3
"""
QNX-ChaosPulse // Studio Neural Voiceover Generator
Uses Microsoft Edge TTS (en-US-ChristopherNeural, pitch: -2Hz, rate: +0%)
to generate high-fidelity voiceover tracks for the Ultra-HD demo video.

Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
"""

import os
import sys
import asyncio
import edge_tts
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
VOICEOVER_DIR = os.path.join(PROJECT_ROOT, "media", "voiceover")

VOICE = "en-US-ChristopherNeural"
PITCH = "-2Hz"
RATE = "+10%"

SCENES = [
    {
        "id": "scene1_intro",
        "filename": "scene1_intro.mp3",
        "text": (
            "In ISO 26262 ASIL-D automotive systems and aerospace flight computers, "
            "real-time determinism is a matter of life and death. A late control frame isn’t just slow—it "
            "is a catastrophic system failure. While cloud microservices have had Chaos Monkey for a decade, "
            "microkernel RTOS developers had zero deterministic chaos injection tools to verify hard "
            "real-time behavior under catastrophic fault conditions. This is QNX-ChaosPulse—the first "
            "deterministic chaos engineering and microkernel resilience profiling suite built natively "
            "for BlackBerry QNX Neutrino RTOS SDP 8.0."
        )
    },
    {
        "id": "scene2_baseline",
        "filename": "scene2_baseline.mp3",
        "text": (
            "Under the hood, QNX-ChaosPulse pairs a native C target engine with a host-side telemetry cockpit. "
            "In our nominal baseline state, our actuator loop runs at highest real-time priority—Priority 63, "
            "SCHED_FIFO—locked in memory with mlockall. Using QNX hardware cycle extraction with ClockCycles, "
            "we achieve a baseline P50 wake-up jitter of just 0.83 microseconds, well within the strict "
            "3.5-microsecond ASIL-D boundary. Now, let’s see what happens when we intentionally push the "
            "QNX microkernel to catastrophic limits."
        )
    },
    {
        "id": "scene3_chaos",
        "filename": "scene3_chaos.mp3",
        "text": (
            "First, we trigger a 100% CPU Core Saturation Storm across background priority workers. "
            "Despite aggressive cache thrashing, our Priority 63 task holds its deterministic timeline. "
            "Next, we inject a Microsecond Priority Inversion Trap. Notice the instant response: a transient "
            "6.7-microsecond spike breaches our threshold. But watch what happens next: QNX's native "
            "Priority Inheritance Protocol dynamically elevates the blocked thread, completely restoring "
            "scheduling jitter back down to 1.07 microseconds in less than 300 milliseconds! Crucially, "
            "every single fault and recovery state is instrumented directly into the QNX kernel trace "
            "buffer via native TraceEvent calls for seamless inspection in Momentics IDE."
        )
    },
    {
        "id": "scene4_codelab_audit",
        "filename": "scene4_codelab_audit.mp3",
        "text": (
            "To accelerate developer adoption, QNX-ChaosPulse includes an Embedded Interactive Codelab—teaching "
            "engineers how to leverage fixed-priority pulse channels, calibrate TSC counters, and handle "
            "priority inversion. Furthermore, safety compliance teams can click Export ASIL-D Audit Report "
            "to immediately generate a mathematically signed, formal verification dossier containing complete "
            "WCET analysis, P99 metrics, and ISO 26262 conformance records."
        )
    },
    {
        "id": "scene5_outro",
        "filename": "scene5_outro.mp3",
        "text": (
            "QNX-ChaosPulse bridges the missing gap between modern chaos engineering and mission-critical "
            "microkernels. Built with zero external dependencies, cross-compiled for x86_64 and AArch64, "
            "and 100% open-source under the MIT license. When code has to run with absolute sub-microsecond "
            "determinism under any chaos—there is no substitute for QNX Neutrino RTOS 8.0. Thank you."
        )
    }
]

async def generate_scene(scene):
    filepath = os.path.join(VOICEOVER_DIR, scene["filename"])
    print(f"[VOICEOVER] Generating {scene['id']} -> {filepath}...")
    communicate = edge_tts.Communicate(scene["text"], VOICE, pitch=PITCH, rate=RATE)
    await communicate.save(filepath)
    print(f"[VOICEOVER] Generated {scene['id']} successfully.")

FFMPEG_BIN = r"C:\Users\WALTON\.gemini\antigravity\bin\ffmpeg.exe"

def get_audio_duration(filepath):
    try:
        cmd = [
            FFMPEG_BIN, "-i", filepath
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        for line in res.stderr.splitlines():
            if "Duration:" in line:
                # Format: Duration: 00:00:36.85, ...
                part = line.split("Duration:")[1].split(",")[0].strip()
                h, m, s = part.split(":")
                return float(h) * 3600 + float(m) * 60 + float(s)
        return 30.0
    except Exception:
        return 30.0

async def main():
    os.makedirs(VOICEOVER_DIR, exist_ok=True)
    print("===================================================================")
    print("  QNX-CHAOSPULSE // Neural Voiceover Studio Generator")
    print(f"  Voice: {VOICE} | Pitch: {PITCH} | Rate: {RATE}")
    print("===================================================================")

    for scene in SCENES:
        await generate_scene(scene)

    print("\n--- Audio Scene Durations ---")
    total_duration = 0.0
    scene_durations = {}
    for scene in SCENES:
        filepath = os.path.join(VOICEOVER_DIR, scene["filename"])
        dur = get_audio_duration(filepath)
        scene_durations[scene["id"]] = dur
        total_duration += dur
        print(f"  {scene['id']}: {dur:.2f} seconds")

    print(f"\nTotal Voiceover Duration: {total_duration:.2f} seconds ({total_duration/60:.2f} minutes)")

    # Generate silence padding and master concatenation list
    concat_list_path = os.path.join(VOICEOVER_DIR, "concat_list.txt")
    with open(concat_list_path, "w", encoding="utf-8") as f:
        for scene in SCENES:
            filepath = os.path.join(VOICEOVER_DIR, scene["filename"])
            # Format path for ffmpeg concat
            norm_path = filepath.replace('\\', '/')
            f.write(f"file '{norm_path}'\n")

    print(f"[VOICEOVER] Wrote concat manifest: {concat_list_path}")

    # Create master audio via ffmpeg
    master_audio_path = os.path.join(VOICEOVER_DIR, "full_master_audio.mp3")
    ffmpeg_cmd = [
        FFMPEG_BIN, "-y", "-f", "concat", "-safe", "0",
        "-i", concat_list_path, "-c", "copy", master_audio_path
    ]
    try:
        subprocess.run(ffmpeg_cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        master_dur = get_audio_duration(master_audio_path)
        print(f"[VOICEOVER] Created master audio track: {master_audio_path} ({master_dur:.2f}s)")
    except Exception as e:
        print(f"[WARN] ffmpeg concat warning: {e}")

if __name__ == "__main__":
    asyncio.run(main())
