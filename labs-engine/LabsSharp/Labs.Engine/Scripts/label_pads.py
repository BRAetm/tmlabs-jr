"""
label_pads.py — Interactive labeling tool for Got Next pad training data.

Usage:
    python label_pads.py <frames_folder>

Controls (in the OpenCV window):
    LEFT CLICK + DRAG  → draw a box around a Got Next pad
    RIGHT CLICK        → delete the last box on this frame
    SPACE              → save labels and go to next frame
    BACKSPACE          → previous frame
    S                  → skip frame (no pads visible)
    D                  → delete current frame (bad capture)
    Q                  → quit

Output:
    For each frame XXXX.png in the input folder, writes XXXX.txt with
    one line per labeled box in YOLO format:
        class_id  cx_norm  cy_norm  w_norm  h_norm

    class 0 = "pad"

Also crops every labeled pad as a separate image into a `pad_crops/`
subfolder, which can be used as templates for matching.
"""

import os
import sys
import cv2

CLASS_PAD = 0


class LabelTool:
    def __init__(self, folder: str):
        self.folder = folder
        self.frames = sorted(
            f for f in os.listdir(folder)
            if f.lower().endswith((".png", ".jpg", ".jpeg"))
        )
        if not self.frames:
            print(f"No image frames found in {folder}")
            sys.exit(1)

        self.crop_dir = os.path.join(folder, "pad_crops")
        os.makedirs(self.crop_dir, exist_ok=True)

        self.idx = 0
        self.boxes = []          # list of (x1, y1, x2, y2) in current frame
        self.drawing = False
        self.start_pt = None
        self.frame = None
        self.disp = None
        self.crop_count_total = 0

        self.window = "Label Pads — drag boxes around pads | SPACE=next | Q=quit"
        cv2.namedWindow(self.window, cv2.WINDOW_NORMAL)
        cv2.setMouseCallback(self.window, self._on_mouse)

    # ────────────────────────────────────────────────────────────────────

    def _label_path(self, frame_name: str) -> str:
        base = os.path.splitext(frame_name)[0]
        return os.path.join(self.folder, f"{base}.txt")

    def _load_existing_labels(self, frame_name: str):
        """Load existing YOLO label file (if any) into self.boxes."""
        path = self._label_path(frame_name)
        self.boxes = []
        if not os.path.isfile(path):
            return
        h, w = self.frame.shape[:2]
        try:
            with open(path) as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) != 5:
                        continue
                    _, cx, cy, bw, bh = parts
                    cx = float(cx); cy = float(cy); bw = float(bw); bh = float(bh)
                    x1 = int((cx - bw / 2) * w)
                    y1 = int((cy - bh / 2) * h)
                    x2 = int((cx + bw / 2) * w)
                    y2 = int((cy + bh / 2) * h)
                    self.boxes.append((x1, y1, x2, y2))
        except Exception as e:
            print(f"  warn: couldn't read existing labels for {frame_name}: {e}")

    def _save_labels(self, frame_name: str):
        """Write YOLO label file + save crops of each box."""
        if not self.boxes:
            # Empty label file = "no pads in this frame" (a valid negative)
            with open(self._label_path(frame_name), "w") as f:
                pass
            return

        h, w = self.frame.shape[:2]
        path = self._label_path(frame_name)
        base = os.path.splitext(frame_name)[0]

        with open(path, "w") as f:
            for i, (x1, y1, x2, y2) in enumerate(self.boxes):
                bx1 = max(0, min(x1, x2)); by1 = max(0, min(y1, y2))
                bx2 = min(w, max(x1, x2)); by2 = min(h, max(y1, y2))
                bw = bx2 - bx1; bh = by2 - by1
                if bw < 4 or bh < 4:
                    continue
                cx = (bx1 + bw / 2) / w
                cy = (by1 + bh / 2) / h
                wn = bw / w
                hn = bh / h
                f.write(f"{CLASS_PAD} {cx:.6f} {cy:.6f} {wn:.6f} {hn:.6f}\n")

                # Save crop for template library
                crop = self.frame[by1:by2, bx1:bx2]
                if crop.size > 0:
                    crop_path = os.path.join(self.crop_dir, f"{base}_box{i}.png")
                    cv2.imwrite(crop_path, crop)
                    self.crop_count_total += 1

    # ────────────────────────────────────────────────────────────────────

    def _on_mouse(self, event, x, y, flags, _param):
        if event == cv2.EVENT_LBUTTONDOWN:
            self.drawing = True
            self.start_pt = (x, y)
        elif event == cv2.EVENT_MOUSEMOVE and self.drawing:
            self._render(preview_box=(self.start_pt, (x, y)))
        elif event == cv2.EVENT_LBUTTONUP and self.drawing:
            self.drawing = False
            x1, y1 = self.start_pt
            x2, y2 = x, y
            if abs(x2 - x1) >= 4 and abs(y2 - y1) >= 4:
                self.boxes.append((x1, y1, x2, y2))
            self._render()
        elif event == cv2.EVENT_RBUTTONDOWN:
            if self.boxes:
                removed = self.boxes.pop()
                print(f"  removed box {removed}")
            self._render()

    # ────────────────────────────────────────────────────────────────────

    def _render(self, preview_box=None):
        if self.frame is None:
            return
        disp = self.frame.copy()

        # Draw all confirmed boxes
        for (x1, y1, x2, y2) in self.boxes:
            cv2.rectangle(disp, (x1, y1), (x2, y2), (0, 255, 0), 2)
            cv2.putText(disp, "pad", (x1, max(15, y1 - 4)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

        # Draw the in-progress box
        if preview_box is not None:
            (px1, py1), (px2, py2) = preview_box
            cv2.rectangle(disp, (px1, py1), (px2, py2), (0, 200, 255), 2)

        # HUD
        info = f"[{self.idx + 1}/{len(self.frames)}] {self.frames[self.idx]} — boxes: {len(self.boxes)}"
        cv2.rectangle(disp, (5, 5), (5 + 700, 32), (0, 0, 0), -1)
        cv2.putText(disp, info, (12, 25),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1)
        cv2.putText(disp, "drag=add  RClick=undo  SPACE=next  BSP=prev  S=skip  Q=quit",
                    (12, 55), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (180, 220, 255), 1)

        cv2.imshow(self.window, disp)

    # ────────────────────────────────────────────────────────────────────

    def _load_frame(self):
        name = self.frames[self.idx]
        path = os.path.join(self.folder, name)
        self.frame = cv2.imread(path)
        if self.frame is None:
            print(f"  WARN: couldn't read {path}")
            return False
        self._load_existing_labels(name)
        self._render()
        return True

    def run(self):
        print(f"Loaded {len(self.frames)} frames from {self.folder}")
        print(f"Crops will be saved to: {self.crop_dir}")
        print()

        if not self._load_frame():
            return

        while True:
            key = cv2.waitKey(0) & 0xFF

            if key == ord('q'):
                self._save_labels(self.frames[self.idx])
                break
            elif key == ord(' '):   # SPACE → save + next
                self._save_labels(self.frames[self.idx])
                self.idx += 1
                if self.idx >= len(self.frames):
                    print("Reached end of dataset.")
                    break
                self._load_frame()
            elif key == 8:          # BACKSPACE → previous
                self._save_labels(self.frames[self.idx])
                self.idx = max(0, self.idx - 1)
                self._load_frame()
            elif key == ord('s'):   # skip (mark as no pads)
                self.boxes = []
                self._save_labels(self.frames[self.idx])
                self.idx += 1
                if self.idx >= len(self.frames):
                    break
                self._load_frame()
            elif key == ord('d'):   # delete bad frame
                name = self.frames[self.idx]
                path = os.path.join(self.folder, name)
                lpath = self._label_path(name)
                try:
                    os.remove(path)
                    if os.path.isfile(lpath):
                        os.remove(lpath)
                    self.frames.pop(self.idx)
                    print(f"  deleted {name}")
                except Exception as e:
                    print(f"  delete failed: {e}")
                if not self.frames:
                    break
                if self.idx >= len(self.frames):
                    self.idx = len(self.frames) - 1
                self._load_frame()

        cv2.destroyAllWindows()
        print(f"\nFinished. Total pad crops saved: {self.crop_count_total}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python label_pads.py <frames_folder>")
        sys.exit(1)
    LabelTool(sys.argv[1]).run()


if __name__ == "__main__":
    main()
