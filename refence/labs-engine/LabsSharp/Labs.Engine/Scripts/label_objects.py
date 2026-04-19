"""
label_objects.py — Multi-class labeling tool with drag boxes + custom labels.

Usage:
    python label_objects.py <frames_folder>

Controls:
    LEFT CLICK + DRAG    → draw a box for the CURRENT class
    RIGHT CLICK          → delete the last box on this frame
    1-5                  → switch to preset class
    6                    → create a NEW custom class (prompts for name in terminal)
    7-9, 0               → switch to custom classes 1-4 (after you've created them)
    SPACE                → save labels and go to next frame
    BACKSPACE            → previous frame
    S                    → skip frame (save as empty)
    D                    → delete current frame from disk
    Q                    → quit and save

Preset classes:
    0 = pad           1 = nameplate    2 = own_player
    3 = prompt        4 = wall

Custom classes:
    Press 6 to create one (e.g. "billboard", "scoreboard", "signature")
    Custom names persist in _class_names.txt in the dataset folder, so the
    trainer (train_pad_yolo.py) can pick them up automatically.
"""

import os
import sys
import cv2

# ─────────────────────────────────────────────────────────────────
# Preset classes (always available)
# ─────────────────────────────────────────────────────────────────
PRESET_CLASSES = [
    # (id, name, BGR color, key)
    (0, "pad",        (0, 255, 0),    ord('1')),    # green
    (1, "nameplate",  (0, 0, 255),    ord('2')),    # red
    (2, "own_player", (0, 165, 255),  ord('3')),    # orange
    (3, "prompt",     (0, 255, 255),  ord('4')),    # yellow
    (4, "wall",       (200, 0, 200),  ord('5')),    # purple
]

# Colors used for auto-assigned custom classes (cycle through these)
CUSTOM_COLORS = [
    (255, 200, 0),    # cyan
    (255, 100, 200),  # pink
    (100, 255, 200),  # mint
    (200, 200, 255),  # peach
    (255, 255, 100),  # light blue
]

# Keys 7, 8, 9, 0 map to custom class slots 0, 1, 2, 3
CUSTOM_KEYS = [ord('7'), ord('8'), ord('9'), ord('0')]


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

        # Build the active class list — start with presets, load custom from disk
        self.classes = list(PRESET_CLASSES)
        self._load_custom_classes()

        self.idx = 0
        self.current_class = self.classes[0]  # default: pad
        self.boxes = []  # list of (class_id, x1, y1, x2, y2)
        self.drawing = False
        self.start_pt = None
        self.frame = None

        self.window = "Label Objects"
        cv2.namedWindow(self.window, cv2.WINDOW_NORMAL)
        cv2.setMouseCallback(self.window, self._on_mouse)

    # ─────────────────────────────────────────────────────────────
    # Custom class persistence
    # ─────────────────────────────────────────────────────────────

    def _custom_path(self) -> str:
        return os.path.join(self.folder, "_class_names.txt")

    def _load_custom_classes(self):
        """Read custom class names saved from previous sessions."""
        path = self._custom_path()
        if not os.path.isfile(path):
            return
        try:
            with open(path) as f:
                for line in f:
                    parts = line.strip().split(":", 1)
                    if len(parts) != 2:
                        continue
                    cid_str, name = parts
                    cid = int(cid_str)
                    if cid >= len(PRESET_CLASSES):
                        # Custom class — assign a color from cycle
                        slot = cid - len(PRESET_CLASSES)
                        color = CUSTOM_COLORS[slot % len(CUSTOM_COLORS)]
                        key = CUSTOM_KEYS[slot] if slot < len(CUSTOM_KEYS) else 0
                        self.classes.append((cid, name, color, key))
        except Exception as e:
            print(f"  warn: couldn't read custom classes: {e}")

    def _save_custom_classes(self):
        """Persist custom class names to disk so they reload next session."""
        try:
            with open(self._custom_path(), "w") as f:
                for c in self.classes:
                    cid, name = c[0], c[1]
                    if cid >= len(PRESET_CLASSES):
                        f.write(f"{cid}:{name}\n")
        except Exception as e:
            print(f"  warn: couldn't save custom classes: {e}")

    def _add_custom_class(self):
        """Prompt user in the terminal for a custom class name."""
        slot = len(self.classes) - len(PRESET_CLASSES)
        if slot >= len(CUSTOM_KEYS):
            print(f"  max custom classes reached ({len(CUSTOM_KEYS)})")
            return
        try:
            print()
            name = input(f"  Enter name for new custom class (e.g. 'billboard'): ").strip()
            if not name:
                print("  cancelled")
                return
            cid = len(self.classes)
            color = CUSTOM_COLORS[slot % len(CUSTOM_COLORS)]
            key = CUSTOM_KEYS[slot]
            new_class = (cid, name, color, key)
            self.classes.append(new_class)
            self.current_class = new_class
            self._save_custom_classes()
            print(f"  created class [{chr(key)}] {name} (id={cid})")
            print()
        except Exception as e:
            print(f"  error creating custom class: {e}")

    def _id_to_class(self, cid):
        for c in self.classes:
            if c[0] == cid:
                return c
        return None

    # ─────────────────────────────────────────────────────────────
    # Label file I/O
    # ─────────────────────────────────────────────────────────────

    def _label_path(self, frame_name: str) -> str:
        base = os.path.splitext(frame_name)[0]
        return os.path.join(self.folder, f"{base}.txt")

    def _load_existing_labels(self, frame_name: str):
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
                    cid_str, cx, cy, bw, bh = parts
                    cid = int(cid_str)
                    cx, cy, bw, bh = map(float, (cx, cy, bw, bh))
                    x1 = int((cx - bw / 2) * w)
                    y1 = int((cy - bh / 2) * h)
                    x2 = int((cx + bw / 2) * w)
                    y2 = int((cy + bh / 2) * h)
                    self.boxes.append((cid, x1, y1, x2, y2))
        except Exception as e:
            print(f"  warn: couldn't read labels for {frame_name}: {e}")

    def _save_labels(self, frame_name: str):
        h, w = self.frame.shape[:2]
        path = self._label_path(frame_name)
        with open(path, "w") as f:
            for (cid, x1, y1, x2, y2) in self.boxes:
                bx1 = max(0, min(x1, x2)); by1 = max(0, min(y1, y2))
                bx2 = min(w, max(x1, x2)); by2 = min(h, max(y1, y2))
                bw = bx2 - bx1; bh = by2 - by1
                if bw < 2 or bh < 2:
                    continue
                cx = (bx1 + bw / 2) / w
                cy = (by1 + bh / 2) / h
                wn = bw / w
                hn = bh / h
                f.write(f"{cid} {cx:.6f} {cy:.6f} {wn:.6f} {hn:.6f}\n")

    # ─────────────────────────────────────────────────────────────
    # Mouse handler — DRAG mode (the version that worked first time)
    # ─────────────────────────────────────────────────────────────

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
                cid = self.current_class[0]
                self.boxes.append((cid, x1, y1, x2, y2))
                print(f"  added {self.current_class[1]} box ({len(self.boxes)} total)")
            self._render()
        elif event == cv2.EVENT_RBUTTONDOWN:
            if self.boxes:
                removed = self.boxes.pop()
                cls = self._id_to_class(removed[0])
                name = cls[1] if cls else "?"
                print(f"  removed {name} box ({len(self.boxes)} remaining)")
            self._render()

    # ─────────────────────────────────────────────────────────────
    # Render
    # ─────────────────────────────────────────────────────────────

    def _render(self, preview_box=None):
        if self.frame is None:
            return
        disp = self.frame.copy()

        # Draw all confirmed boxes
        for (cid, x1, y1, x2, y2) in self.boxes:
            cls = self._id_to_class(cid)
            if cls is None:
                continue
            color = cls[2]
            name = cls[1]
            cv2.rectangle(disp, (x1, y1), (x2, y2), color, 2)
            cv2.putText(disp, name, (x1, max(15, y1 - 4)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

        # Draw the in-progress box
        if preview_box is not None:
            (px1, py1), (px2, py2) = preview_box
            cv2.rectangle(disp, (px1, py1), (px2, py2), self.current_class[2], 2)

        # Top bar — frame counter + active class + box count
        info = f"[{self.idx + 1}/{len(self.frames)}] {self.frames[self.idx]}  boxes: {len(self.boxes)}"
        cv2.rectangle(disp, (5, 5), (min(disp.shape[1] - 5, 5 + 700), 30), (0, 0, 0), -1)
        cv2.putText(disp, info, (10, 23),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

        # Active class chip in top-right corner
        cw, ch = 200, 32
        cx1 = disp.shape[1] - cw - 5
        cy1 = 5
        cv2.rectangle(disp, (cx1, cy1), (cx1 + cw, cy1 + ch), self.current_class[2], -1)
        chip_text = f"[{chr(self.current_class[3])}] {self.current_class[1]}"
        cv2.putText(disp, chip_text, (cx1 + 8, cy1 + 22),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 0), 2)

        # Help line at bottom
        h = disp.shape[0]
        help_text = "1-6:class  drag:add  RClick:undo  SPACE:next  BSP:prev  S:skip  Q:quit"
        cv2.rectangle(disp, (5, h - 28), (disp.shape[1] - 5, h - 5), (0, 0, 0), -1)
        cv2.putText(disp, help_text, (10, h - 12),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.42, (200, 200, 200), 1)

        cv2.imshow(self.window, disp)

    # ─────────────────────────────────────────────────────────────
    # Main loop
    # ─────────────────────────────────────────────────────────────

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
        print()
        print("CLASSES:")
        for cid, name, _color, key in self.classes:
            print(f"  [{chr(key)}] {cid} = {name}")
        print()
        print("Press 6 to create a new custom class.")
        print()

        if not self._load_frame():
            return

        while True:
            key = cv2.waitKey(0) & 0xFF

            if key == ord('q'):
                self._save_labels(self.frames[self.idx])
                break
            elif key == ord(' '):
                self._save_labels(self.frames[self.idx])
                self.idx += 1
                if self.idx >= len(self.frames):
                    print("Reached end of dataset.")
                    break
                self._load_frame()
            elif key == 8:  # BACKSPACE
                self._save_labels(self.frames[self.idx])
                self.idx = max(0, self.idx - 1)
                self._load_frame()
            elif key == ord('s'):
                self.boxes = []
                self._save_labels(self.frames[self.idx])
                self.idx += 1
                if self.idx >= len(self.frames):
                    break
                self._load_frame()
            elif key == ord('d'):
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
            elif key == ord('6'):
                # Create a new custom class via terminal prompt
                self._add_custom_class()
                self._render()
            else:
                # Class switch via 1-5 (preset) or 7-9, 0 (custom)
                for c in self.classes:
                    if c[3] == key:
                        self.current_class = c
                        print(f"  active: {c[1]}")
                        self._render()
                        break

        cv2.destroyAllWindows()
        print(f"\nDone.")


def main():
    if len(sys.argv) < 2:
        print("Usage: python label_objects.py <frames_folder>")
        sys.exit(1)
    LabelTool(sys.argv[1]).run()


if __name__ == "__main__":
    main()
