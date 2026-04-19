"""
smart_label.py — One-click labeling with self-improving template matching.

How it works:
  1. Click on a pad in the frame.
  2. Tool extracts a small template around your click.
  3. Tool runs template matching on the WHOLE frame to find every similar
     thing, auto-labels them.
  4. Right-click to remove any wrong boxes.
  5. Press SPACE to save and go to next frame.
  6. On the next frame, the tool RE-RUNS all templates you've collected so
     far — each new frame is pre-labeled automatically.
  7. The library of templates grows with every click. By frame 50, most
     pads are auto-detected and you barely need to click.

Output:
  - YOLO-format label files (compatible with train_pad_yolo.py)
  - Templates saved to _templates/<class>/template_NNN.png
  - The same template library can be used at runtime by boost_court.py

Usage:
  python smart_label.py <frames_folder>

Controls:
  LEFT CLICK on object   → auto-find all similar things in frame
  RIGHT CLICK on box     → remove that box
  1-5                    → switch active class
  A                      → re-run auto-find with current templates
  C                      → clear all boxes on this frame
  R                      → reset (delete templates for current class)
  SPACE                  → save labels and go to next frame
  BACKSPACE              → previous frame
  S                      → skip frame (save empty)
  Q                      → quit
"""

import os
import sys
import cv2
import numpy as np

# ─────────────────────────────────────────────────────────────────
# Classes — same as label_objects.py
# ─────────────────────────────────────────────────────────────────
CLASSES = [
    (0, "pad",        (0, 255, 0),   ord('1')),
    (1, "nameplate",  (0, 0, 255),   ord('2')),
    (2, "own_player", (0, 165, 255), ord('3')),
    (3, "prompt",     (0, 255, 255), ord('4')),
    (4, "wall",       (200, 0, 200), ord('5')),
]
KEY_TO_CLASS = {c[3]: c for c in CLASSES}
ID_TO_CLASS  = {c[0]: c for c in CLASSES}

# Template extraction config
TEMPLATE_HALF_W = 30           # half-width of template patch (60x60 total)
TEMPLATE_HALF_H = 30
MATCH_THRESHOLD = 0.65         # min similarity to count as a match (0-1)
NMS_OVERLAP_THRESHOLD = 0.30   # IoU threshold for non-max suppression


class SmartLabeler:
    def __init__(self, folder: str):
        self.folder = folder
        self.frames = sorted(
            f for f in os.listdir(folder)
            if f.lower().endswith((".png", ".jpg", ".jpeg"))
        )
        if not self.frames:
            print(f"No image frames in {folder}")
            sys.exit(1)

        # Template library: {class_id: [template_image, ...]}
        self.templates_dir = os.path.join(folder, "_templates")
        os.makedirs(self.templates_dir, exist_ok=True)
        self.templates = {c[0]: [] for c in CLASSES}
        self._load_templates()

        self.idx = 0
        self.current_class = CLASSES[0]
        self.boxes = []  # list of (cid, x1, y1, x2, y2)
        self.frame = None
        self.cursor = (0, 0)

        self.window = "Smart Labeler — click=auto-find | RClick=remove | 1-5=class | SPACE=next | Q=quit"
        cv2.namedWindow(self.window, cv2.WINDOW_NORMAL)
        cv2.setMouseCallback(self.window, self._on_mouse)

    # ─────────────────────────────────────────────────────────────
    # Template library persistence
    # ─────────────────────────────────────────────────────────────

    def _class_template_dir(self, cid: int) -> str:
        cls = ID_TO_CLASS.get(cid)
        name = cls[1] if cls else f"class_{cid}"
        d = os.path.join(self.templates_dir, name)
        os.makedirs(d, exist_ok=True)
        return d

    def _load_templates(self):
        """Read all saved template patches from _templates/<class>/."""
        if not os.path.isdir(self.templates_dir):
            return
        total = 0
        for cid, name, _color, _key in CLASSES:
            cdir = os.path.join(self.templates_dir, name)
            if not os.path.isdir(cdir):
                continue
            for f in sorted(os.listdir(cdir)):
                if f.lower().endswith((".png", ".jpg", ".jpeg")):
                    img = cv2.imread(os.path.join(cdir, f))
                    if img is not None:
                        self.templates[cid].append(img)
                        total += 1
        print(f"  loaded {total} existing templates from {self.templates_dir}")
        for cid in self.templates:
            n = len(self.templates[cid])
            if n > 0:
                print(f"    {ID_TO_CLASS[cid][1]}: {n} templates")

    def _save_template(self, cid: int, patch: np.ndarray):
        """Save a new template patch to disk + memory."""
        self.templates[cid].append(patch)
        cdir = self._class_template_dir(cid)
        idx = len(self.templates[cid])
        path = os.path.join(cdir, f"template_{idx:03d}.png")
        cv2.imwrite(path, patch)
        print(f"  saved {ID_TO_CLASS[cid][1]} template #{idx}")

    def _reset_class_templates(self, cid: int):
        """Wipe templates for a class."""
        self.templates[cid] = []
        cdir = self._class_template_dir(cid)
        for f in os.listdir(cdir):
            try:
                os.remove(os.path.join(cdir, f))
            except Exception:
                pass
        print(f"  reset {ID_TO_CLASS[cid][1]} templates")

    # ─────────────────────────────────────────────────────────────
    # Template matching
    # ─────────────────────────────────────────────────────────────

    def _extract_template(self, frame: np.ndarray, x: int, y: int) -> np.ndarray:
        """Crop a fixed-size patch around (x, y)."""
        h, w = frame.shape[:2]
        x1 = max(0, x - TEMPLATE_HALF_W)
        y1 = max(0, y - TEMPLATE_HALF_H)
        x2 = min(w, x + TEMPLATE_HALF_W)
        y2 = min(h, y + TEMPLATE_HALF_H)
        return frame[y1:y2, x1:x2].copy()

    def _match_template(self, frame: np.ndarray, template: np.ndarray, cid: int) -> list:
        """Run cv2.matchTemplate, return list of (cid, x1, y1, x2, y2) above threshold."""
        if template is None or template.size == 0:
            return []
        th, tw = template.shape[:2]
        if frame.shape[0] < th or frame.shape[1] < tw:
            return []
        try:
            result = cv2.matchTemplate(frame, template, cv2.TM_CCOEFF_NORMED)
        except Exception:
            return []
        ys, xs = np.where(result >= MATCH_THRESHOLD)
        out = []
        for x, y in zip(xs, ys):
            out.append((cid, int(x), int(y), int(x + tw), int(y + th)))
        return out

    def _nms(self, boxes: list) -> list:
        """Non-max suppression — drop overlapping boxes, keep the unique ones."""
        if not boxes:
            return []
        # Sort by area descending (bigger boxes first)
        sorted_boxes = sorted(boxes, key=lambda b: -((b[3] - b[1]) * (b[4] - b[2])))
        kept = []
        for b in sorted_boxes:
            cid, x1, y1, x2, y2 = b
            is_dup = False
            for k in kept:
                _, kx1, ky1, kx2, ky2 = k
                # Compute IoU
                ix1 = max(x1, kx1); iy1 = max(y1, ky1)
                ix2 = min(x2, kx2); iy2 = min(y2, ky2)
                iw = max(0, ix2 - ix1); ih = max(0, iy2 - iy1)
                inter = iw * ih
                if inter > 0:
                    area1 = (x2 - x1) * (y2 - y1)
                    area2 = (kx2 - kx1) * (ky2 - ky1)
                    iou = inter / (area1 + area2 - inter)
                    if iou > NMS_OVERLAP_THRESHOLD:
                        is_dup = True
                        break
            if not is_dup:
                kept.append(b)
        return kept

    def _auto_find_all(self):
        """Run every template across the current frame, dedupe, add boxes."""
        if self.frame is None:
            return
        all_matches = []
        for cid in self.templates:
            for tpl in self.templates[cid]:
                all_matches.extend(self._match_template(self.frame, tpl, cid))
        deduped = self._nms(all_matches)

        # Add only ones not already in self.boxes (avoid double-adding)
        added = 0
        for b in deduped:
            cid, x1, y1, x2, y2 = b
            cx = (x1 + x2) // 2
            cy = (y1 + y2) // 2
            already = False
            for eb in self.boxes:
                ex1, ey1, ex2, ey2 = eb[1], eb[2], eb[3], eb[4]
                if ex1 <= cx <= ex2 and ey1 <= cy <= ey2:
                    already = True
                    break
            if not already:
                self.boxes.append(b)
                added += 1
        if added > 0:
            print(f"  auto-found {added} matches")
        return added

    # ─────────────────────────────────────────────────────────────
    # Mouse handler
    # ─────────────────────────────────────────────────────────────

    def _on_mouse(self, event, x, y, flags, _param):
        self.cursor = (x, y)

        if event == cv2.EVENT_LBUTTONDOWN:
            # Click → extract template at click point + auto-find similar
            patch = self._extract_template(self.frame, x, y)
            if patch.size == 0:
                return
            cid = self.current_class[0]
            self._save_template(cid, patch)
            print(f"  click at ({x},{y}) → extracted {ID_TO_CLASS[cid][1]} template")
            self._auto_find_all()
            self._render()

        elif event == cv2.EVENT_RBUTTONDOWN:
            # Right click → remove the box that contains this point
            removed = False
            for i in range(len(self.boxes) - 1, -1, -1):
                cid, x1, y1, x2, y2 = self.boxes[i]
                if x1 <= x <= x2 and y1 <= y <= y2:
                    self.boxes.pop(i)
                    cls = ID_TO_CLASS.get(cid, (0, '?'))[1]
                    print(f"  removed {cls} box")
                    removed = True
                    break
            if not removed and self.boxes:
                # No box under cursor, just remove the last one
                removed_box = self.boxes.pop()
                cls = ID_TO_CLASS.get(removed_box[0], (0, '?'))[1]
                print(f"  removed last {cls} box")
            self._render()

        elif event == cv2.EVENT_MOUSEMOVE:
            self._render()

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
            print(f"  warn: load labels failed: {e}")

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
    # Render
    # ─────────────────────────────────────────────────────────────

    def _render(self):
        if self.frame is None:
            return
        disp = self.frame.copy()
        h, w = disp.shape[:2]

        # Draw all boxes
        class_counts = {c[0]: 0 for c in CLASSES}
        for (cid, x1, y1, x2, y2) in self.boxes:
            cls = ID_TO_CLASS.get(cid)
            if cls is None:
                continue
            color = cls[2]
            class_counts[cid] += 1
            cv2.rectangle(disp, (x1, y1), (x2, y2), color, 2)
            cv2.putText(disp, cls[1], (x1, max(15, y1 - 4)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1)

        # Crosshair at cursor
        cx, cy = self.cursor
        ccolor = self.current_class[2]
        cv2.line(disp, (cx - 20, cy), (cx + 20, cy), ccolor, 1)
        cv2.line(disp, (cx, cy - 20), (cx, cy + 20), ccolor, 1)
        # Show template region preview
        cv2.rectangle(disp,
                      (cx - TEMPLATE_HALF_W, cy - TEMPLATE_HALF_H),
                      (cx + TEMPLATE_HALF_W, cy + TEMPLATE_HALF_H),
                      ccolor, 1)

        # Top bar
        info = f"[{self.idx + 1}/{len(self.frames)}] {self.frames[self.idx]}  boxes: {len(self.boxes)}"
        cv2.rectangle(disp, (5, 5), (min(w - 5, 5 + 700), 30), (0, 0, 0), -1)
        cv2.putText(disp, info, (10, 23),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

        # Active class chip top-right
        cw, ch = 200, 32
        cx1 = w - cw - 5
        cv2.rectangle(disp, (cx1, 5), (cx1 + cw, 5 + ch), self.current_class[2], -1)
        chip = f"[{chr(self.current_class[3])}] {self.current_class[1]} ({len(self.templates[self.current_class[0]])})"
        cv2.putText(disp, chip, (cx1 + 8, 27),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 2)

        # Help bar
        help_text = "click:auto-find  RClick:remove  1-5:class  A:rerun  C:clear  R:reset class  SPACE:next  Q:quit"
        cv2.rectangle(disp, (5, h - 28), (w - 5, h - 5), (0, 0, 0), -1)
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
        # Auto-run all templates on the new frame
        if any(len(t) > 0 for t in self.templates.values()):
            self._auto_find_all()
        self._render()
        return True

    def run(self):
        print(f"Loaded {len(self.frames)} frames from {self.folder}")
        print(f"Templates dir: {self.templates_dir}")
        print()
        print("CLASSES:")
        for cid, name, _color, key in CLASSES:
            print(f"  [{chr(key)}] {cid} = {name}")
        print()
        print("Click on a pad to teach the system. The next frame will be pre-labeled.")
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
                    print("End of dataset.")
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
            elif key == ord('a'):
                # Re-run all templates on this frame
                self._auto_find_all()
                self._render()
            elif key == ord('c'):
                # Clear all boxes
                self.boxes = []
                self._render()
            elif key == ord('r'):
                # Reset templates for current class
                self._reset_class_templates(self.current_class[0])
                self._render()
            elif key in KEY_TO_CLASS:
                self.current_class = KEY_TO_CLASS[key]
                print(f"  active: {self.current_class[1]}")
                self._render()

        cv2.destroyAllWindows()
        print(f"\nDone. Templates saved to: {self.templates_dir}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python smart_label.py <frames_folder>")
        sys.exit(1)
    SmartLabeler(sys.argv[1]).run()


if __name__ == "__main__":
    main()
