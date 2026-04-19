"""
video_to_frames.py — Extract frames from a gameplay video for CV training.

Usage:
    python video_to_frames.py <video_file> [--every N] [--out folder]

Examples:
    python video_to_frames.py court_walk.mp4
    python video_to_frames.py court_walk.mp4 --every 15 --out my_dataset

Defaults:
    --every 30   (one frame every 30 video frames = ~1 per second at 30fps)
    --out frames_<videoname>

Drops extracted frames into the output folder, numbered 0001.png, 0002.png, ...
Each frame can then be labeled (with the labeling tool) to build training data.
"""

import os
import sys
import cv2
import argparse


def extract_frames(video_path: str, out_dir: str, every_n: int = 30) -> int:
    if not os.path.isfile(video_path):
        print(f"ERROR: Video file not found: {video_path}")
        return 0

    os.makedirs(out_dir, exist_ok=True)

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"ERROR: Could not open video: {video_path}")
        return 0

    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    print(f"Video: {video_path}")
    print(f"  Resolution: {width}x{height}")
    print(f"  FPS: {fps:.1f}")
    print(f"  Total frames: {total_frames}")
    print(f"  Duration: {total_frames / max(fps, 1):.1f}s")
    print(f"  Extracting every {every_n}th frame")
    print(f"  Output: {out_dir}")
    print()

    frame_idx = 0
    saved = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        if frame_idx % every_n == 0:
            saved += 1
            out_path = os.path.join(out_dir, f"{saved:04d}.png")
            cv2.imwrite(out_path, frame)
            if saved <= 5 or saved % 25 == 0:
                print(f"  saved {out_path} (from frame {frame_idx})")
        frame_idx += 1

    cap.release()
    print(f"\nDone. Extracted {saved} frames from {frame_idx} total video frames.")
    return saved


def main():
    parser = argparse.ArgumentParser(description="Extract frames from a video for CV training")
    parser.add_argument("video", help="Path to video file (.mp4, .mkv, .mov, ...)")
    parser.add_argument("--every", type=int, default=30,
                        help="Extract every Nth frame (default: 30)")
    parser.add_argument("--out", default=None,
                        help="Output folder (default: frames_<videoname>)")
    args = parser.parse_args()

    out_dir = args.out
    if out_dir is None:
        base = os.path.splitext(os.path.basename(args.video))[0]
        out_dir = os.path.join(os.path.dirname(os.path.abspath(args.video)),
                               f"frames_{base}")

    extract_frames(args.video, out_dir, every_n=args.every)


if __name__ == "__main__":
    main()
