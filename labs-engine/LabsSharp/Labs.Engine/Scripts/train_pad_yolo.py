r"""
train_pad_yolo.py — Train a YOLOv8 model to detect Got Next pads.

Workflow:
  1. Run label_pads.py on your extracted frames folder
  2. Run this script:  python train_pad_yolo.py <frames_folder>
  3. It splits the labeled frames into train/val, writes data.yaml,
     and trains a small YOLOv8 model
  4. The trained model is saved as `pad_detector.pt` in the script folder

Usage:
    python train_pad_yolo.py "C:\Users\kesha\Videos\frames_2026-04-06 13-46-52_cropped"
    python train_pad_yolo.py <folder> --epochs 50 --imgsz 640
"""

import os
import sys
import shutil
import random
import argparse


def collect_labeled_frames(folder: str):
    """Find all (image, label) pairs in the folder where the label file exists."""
    pairs = []
    for f in sorted(os.listdir(folder)):
        if not f.lower().endswith((".png", ".jpg", ".jpeg")):
            continue
        label_path = os.path.join(folder, os.path.splitext(f)[0] + ".txt")
        if os.path.isfile(label_path):
            pairs.append((os.path.join(folder, f), label_path))
    return pairs


def make_yolo_dataset(pairs, dataset_dir: str, val_split: float = 0.2):
    """Lay out frames in YOLO's expected directory structure."""
    train_img = os.path.join(dataset_dir, "images", "train")
    val_img   = os.path.join(dataset_dir, "images", "val")
    train_lbl = os.path.join(dataset_dir, "labels", "train")
    val_lbl   = os.path.join(dataset_dir, "labels", "val")
    for d in (train_img, val_img, train_lbl, val_lbl):
        os.makedirs(d, exist_ok=True)

    random.seed(42)
    random.shuffle(pairs)
    n_val = max(1, int(len(pairs) * val_split))
    val_pairs = pairs[:n_val]
    train_pairs = pairs[n_val:]

    for src_img, src_lbl in train_pairs:
        shutil.copy2(src_img, os.path.join(train_img, os.path.basename(src_img)))
        shutil.copy2(src_lbl, os.path.join(train_lbl, os.path.basename(src_lbl)))
    for src_img, src_lbl in val_pairs:
        shutil.copy2(src_img, os.path.join(val_img, os.path.basename(src_img)))
        shutil.copy2(src_lbl, os.path.join(val_lbl, os.path.basename(src_lbl)))

    print(f"  train: {len(train_pairs)}  val: {len(val_pairs)}")
    return train_pairs, val_pairs


def write_data_yaml(dataset_dir: str, source_folder: str = None):
    """Auto-detects how many classes are in the labels and writes data.yaml.
    Reads custom class names from <source_folder>/_class_names.txt if it exists.
    """
    # Scan all label files to find max class id
    max_cid = 0
    train_lbl = os.path.join(dataset_dir, "labels", "train")
    if os.path.isdir(train_lbl):
        for f_name in os.listdir(train_lbl):
            if not f_name.endswith(".txt"):
                continue
            try:
                with open(os.path.join(train_lbl, f_name)) as fp:
                    for line in fp:
                        parts = line.strip().split()
                        if parts:
                            cid = int(parts[0])
                            if cid > max_cid:
                                max_cid = cid
            except Exception:
                pass

    nc = max_cid + 1
    # Preset class names from label_objects.py
    DEFAULT_NAMES = ["pad", "nameplate", "own_player", "prompt", "wall"]
    names = list(DEFAULT_NAMES[:nc]) if nc <= len(DEFAULT_NAMES) else list(DEFAULT_NAMES)

    # Pad with placeholders if we need more
    while len(names) < nc:
        names.append(f"class_{len(names)}")

    # Read custom class names from _class_names.txt (saved by label_objects.py)
    if source_folder is None:
        source_folder = dataset_dir.replace("_yolo_dataset", "")
    custom_path = os.path.join(source_folder, "_class_names.txt")
    if os.path.isfile(custom_path):
        try:
            with open(custom_path) as f:
                for line in f:
                    parts = line.strip().split(":", 1)
                    if len(parts) != 2:
                        continue
                    cid = int(parts[0])
                    name = parts[1]
                    if cid < nc:
                        names[cid] = name
        except Exception as e:
            print(f"  warn: couldn't read custom class names: {e}")

    yaml_path = os.path.join(dataset_dir, "data.yaml")
    with open(yaml_path, "w") as f:
        f.write(f"path: {os.path.abspath(dataset_dir)}\n")
        f.write("train: images/train\n")
        f.write("val:   images/val\n")
        f.write(f"nc: {nc}\n")
        f.write(f"names: {names}\n")
    print(f"  detected nc={nc}, names={names}")
    return yaml_path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("folder", help="Folder of labeled frames (run label_pads.py first)")
    p.add_argument("--epochs", type=int, default=40)
    p.add_argument("--imgsz",  type=int, default=640)
    p.add_argument("--batch",  type=int, default=8)
    p.add_argument("--model",  default="yolov8n.pt",
                   help="base model (yolov8n.pt is smallest, fastest)")
    args = p.parse_args()

    folder = args.folder
    if not os.path.isdir(folder):
        print(f"ERROR: {folder} is not a folder")
        sys.exit(1)

    print(f"Scanning {folder} for labeled frames...")
    pairs = collect_labeled_frames(folder)
    if len(pairs) < 5:
        print(f"ERROR: Only {len(pairs)} labeled frames found. Need at least ~10 to train.")
        print("       Run label_pads.py first to create label .txt files.")
        sys.exit(1)

    print(f"Found {len(pairs)} labeled frames\n")

    dataset_dir = os.path.join(folder, "_yolo_dataset")
    if os.path.exists(dataset_dir):
        shutil.rmtree(dataset_dir)

    print(f"Building YOLO dataset at {dataset_dir}")
    make_yolo_dataset(pairs, dataset_dir)
    yaml_path = write_data_yaml(dataset_dir, source_folder=folder)
    print(f"Wrote {yaml_path}\n")

    print(f"Training YOLOv8 (base: {args.model}, epochs={args.epochs}, imgsz={args.imgsz})")
    print("This will download the base model (~6MB) on first run.\n")

    from ultralytics import YOLO

    model = YOLO(args.model)
    results = model.train(
        data=yaml_path,
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        project=os.path.join(folder, "_yolo_runs"),
        name="pad_train",
        exist_ok=True,
        verbose=True,
        plots=True,
    )

    # Find the best.pt file from the training run
    best_pt = None
    for root, _, files in os.walk(os.path.join(folder, "_yolo_runs")):
        for f in files:
            if f == "best.pt":
                best_pt = os.path.join(root, f)
                break

    if best_pt is None:
        print("\nWARN: best.pt not found in run output")
        return

    # Copy the trained model into the Scripts folder so boost_court.py can find it
    scripts_dir = os.path.dirname(os.path.abspath(__file__))
    target = os.path.join(scripts_dir, "pad_detector.pt")
    shutil.copy2(best_pt, target)

    print(f"\n✓ Trained model saved to:")
    print(f"    {target}")
    print(f"\nNow update boost_court.py to use it (USE_YOLO = True).")


if __name__ == "__main__":
    main()
