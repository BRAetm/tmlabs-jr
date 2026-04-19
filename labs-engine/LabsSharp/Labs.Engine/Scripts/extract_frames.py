"""
extract_frames.py — Extract frames from a video file for YOLO training.

Usage:
    python extract_frames.py <video_path> [output_folder] [--every N]

Defaults:
    output_folder = same folder as video, named <videoname>_frames
    every = 30  (extract 1 frame per 30, so at 60fps that's 2 fps of output)
"""

import os
import sys
import argparse
import cv2


def main():
    p = argparse.ArgumentParser()
    p.add_argument("video", help="Path to video file")
    p.add_argument("output", nargs="?", default=None, help="Output folder (default: <video>_frames)")
    p.add_argument("--every", type=int, default=30, help="Extract every Nth frame (default 30)")
    p.add_argument("--quality", type=int, default=95, help="JPEG quality 1-100 (default 95)")
    args = p.parse_args()

    if not os.path.isfile(args.video):
        print(f"ERROR: Video not found: {args.video}")
        sys.exit(1)

    # Default output: <video_basename>_frames in same dir
    if args.output is None:
        video_dir = os.path.dirname(os.path.abspath(args.video))
        video_name = os.path.splitext(os.path.basename(args.video))[0]
        args.output = os.path.join(video_dir, f"{video_name}_frames")

    os.makedirs(args.output, exist_ok=True)
    print(f"Output folder: {args.output}")

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        print(f"ERROR: Could not open video")
        sys.exit(1)

    fps = cap.get(cv2.CAP_PROP_FPS)
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    print(f"Video: {fps:.1f} fps, {total} frames, ~{total/fps:.1f}s")
    print(f"Extracting every {args.every} frames (= {fps/args.every:.1f} fps output)")
    print(f"Expected output: ~{total // args.every} frames\n")

    frame_idx = 0
    saved = 0
    while True:
        ret, frame = cap.read()
        if not ret:
            break

        if frame_idx % args.every == 0:
            out_path = os.path.join(args.output, f"frame_{saved:05d}.jpg")
            cv2.imwrite(out_path, frame, [cv2.IMWRITE_JPEG_QUALITY, args.quality])
            saved += 1
            if saved % 25 == 0 or saved == 1:
                pct = (frame_idx / total) * 100
                print(f"  [{pct:5.1f}%] saved frame_{saved-1:05d}.jpg")

        frame_idx += 1

    cap.release()
    print(f"\nDone. Saved {saved} frames to:")
    print(f"  {args.output}")


if __name__ == "__main__":
    main()
