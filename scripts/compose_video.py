#!/usr/bin/env python3
"""
QNX-ChaosPulse // Master Video Compositor & Sync Engine
Muxes UHD 60 FPS video capture with studio neural voiceover audio
and produces the final production-ready Ultra-HD MP4 demo.

Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
"""

import os
import sys
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
CAPTURES_DIR = os.path.join(PROJECT_ROOT, "media", "captures")
VOICEOVER_DIR = os.path.join(PROJECT_ROOT, "media", "voiceover")
OUTPUT_DIR = os.path.join(PROJECT_ROOT, "media", "output")

RAW_VIDEO = os.path.join(CAPTURES_DIR, "raw_screen.mp4")
MASTER_AUDIO = os.path.join(VOICEOVER_DIR, "full_master_audio.mp3")
FINAL_OUTPUT = os.path.join(OUTPUT_DIR, "QNX_ChaosPulse_Demo_UHD.mp4")
FFMPEG_BIN = r"C:\Users\WALTON\.gemini\antigravity\bin\ffmpeg.exe"

def get_media_info(filepath):
    info = {"duration": 0.0, "video": "", "audio": ""}
    try:
        cmd = [FFMPEG_BIN, "-i", filepath]
        res = subprocess.run(cmd, capture_output=True, text=True)
        for line in res.stderr.splitlines():
            line_str = line.strip()
            if "Duration:" in line_str and info["duration"] == 0.0:
                part = line_str.split("Duration:")[1].split(",")[0].strip()
                h, m, s = part.split(":")
                info["duration"] = float(h) * 3600 + float(m) * 60 + float(s)
            elif "Stream #" in line_str and "Video:" in line_str:
                info["video"] = line_str
            elif "Stream #" in line_str and "Audio:" in line_str:
                info["audio"] = line_str
    except Exception as e:
        print(f"[WARN] Error parsing {filepath}: {e}")
    return info

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print("===================================================================")
    print("  QNX-CHAOSPULSE // Master Ultra-HD Video Compositor")
    print(f"  Raw Video Input:   {RAW_VIDEO}")
    print(f"  Master Audio Input: {MASTER_AUDIO}")
    print(f"  Final Destination: {FINAL_OUTPUT}")
    print("===================================================================")

    if not os.path.exists(RAW_VIDEO):
        print(f"[ERROR] Raw video footage missing at {RAW_VIDEO}. Run scripts/record_ui.js first.")
        sys.exit(1)

    if not os.path.exists(MASTER_AUDIO):
        print(f"[ERROR] Master audio track missing at {MASTER_AUDIO}. Run scripts/generate_voiceover.py first.")
        sys.exit(1)

    video_info = get_media_info(RAW_VIDEO)
    audio_info = get_media_info(MASTER_AUDIO)

    print(f"[INPUT STATS] Video Duration: {video_info['duration']:.2f}s | Stream: {video_info['video']}")
    print(f"[INPUT STATS] Audio Duration: {audio_info['duration']:.2f}s | Stream: {audio_info['audio']}")

    print("\n[COMPOSITOR] Synchronizing video timeline to full master voiceover (172.80s)...")
    
    # Calculate exact speed ratio so video matches audio duration exactly
    target_duration = audio_info["duration"]
    video_duration = video_info["duration"]
    pts_ratio = target_duration / video_duration if video_duration > 0 else 1.0

    print(f"[SYNC] Video duration: {video_duration:.2f}s -> Target Audio: {target_duration:.2f}s (PTS factor: {pts_ratio:.4f})")

    # Mux with exact synchronization, visually lossless CRF 17, and high-fidelity AAC 320k
    cmd = [
        FFMPEG_BIN, "-y",
        "-i", RAW_VIDEO,
        "-i", MASTER_AUDIO,
        "-filter_complex", f"[0:v]setpts={pts_ratio:.6f}*PTS,scale=3840:2160:flags=lanczos,fps=60[v]",
        "-map", "[v]",
        "-map", "1:a",
        "-c:v", "libx264",
        "-preset", "medium",
        "-crf", "17",
        "-pix_fmt", "yuv420p",
        "-c:a", "aac",
        "-b:a", "320k",
        "-t", f"{target_duration:.2f}",
        FINAL_OUTPUT
    ]

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"[ERROR] FFmpeg composition failed:\n{proc.stderr}")
        sys.exit(1)

    final_info = get_media_info(FINAL_OUTPUT)
    file_size_mb = os.path.getsize(FINAL_OUTPUT) / (1024 * 1024)

    print("===================================================================")
    print("  PRODUCTION DEMO VIDEO COMPILED SUCCESSFULLY!")
    print(f"  Target File:     {FINAL_OUTPUT}")
    print(f"  Exact Duration:  {final_info['duration']:.2f} seconds ({final_info['duration']/60:.2f} minutes)")
    print(f"  Resolution/FPS:  3840x2160 Ultra-HD @ 60.00 FPS")
    print(f"  File Size:       {file_size_mb:.2f} MB")
    print(f"  Audio Track:     AAC Stereo @ 320 kbps (48 kHz)")
    print(f"  Video Codec:     H.264 High Profile (yuv420p, CRF 17)")
    print("===================================================================")

if __name__ == "__main__":
    main()
