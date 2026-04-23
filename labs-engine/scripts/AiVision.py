"""
AiVision.py — TM Labs AI-Vision shot-timing tool.

Replica of SecretK.py with self-tuning thresholds: tracks the outcome (green /
normal / miss) of every shot, and after enough samples suggests a better
threshold. The script gets smarter the more you play.
"""

import os, sys, json, threading, time
from collections import defaultdict
from pathlib import Path

from PySide6 import QtCore, QtGui, QtWidgets

ROOT     = Path(__file__).resolve().parent
USERDATA = Path(os.environ.get("LABS_SETTINGS_ROOT", ROOT / "userdata"))
SETTINGS = USERDATA / "settings" / "ai_vision_settings.current"   # separate from SecretK
BANNER   = USERDATA / "assets" / "tm_labs_banner.png"
OUTCOMES_FILE = USERDATA / "settings" / "ai_vision_outcomes.json"

# ── palette — deep neutrals + single confident accent ────────────────────────
BG      = "#06080F"   # near-black canvas
SURFACE = "#0C0F18"   # primary panels
SURF2   = "#11151F"   # nested panels / inputs
BORDER  = "#1A2030"   # subtle hair-line
BORD_HI = "#2A3450"   # focus / hover border
TEXT    = "#F0F4FA"   # primary text
DIM     = "#7E8AA6"   # secondary text
FAINT   = "#3A4360"   # disabled / placeholder
ACCENT  = "#3B82F6"   # confident electric blue (one strong accent)
ACCDIM  = "#1E3A8A"
OK      = "#34D399"
DANGER  = "#EF4444"
WARN    = "#F59E0B"

# ── auth ──────────────────────────────────────────────────────────────────────
_LOCAL_KEYS: dict[str, str] = {"0000": "LIFETIME"}

def validate(discord_id: str) -> tuple[bool, str]:
    did = (discord_id or "").strip()
    dur = _LOCAL_KEYS.get(did, "")
    return bool(dur), dur

def load_settings() -> dict:
    if SETTINGS.exists():
        try: return json.loads(SETTINGS.read_text())
        except Exception: pass
    return {}

def save_settings(data: dict) -> None:
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(json.dumps(data, indent=2))


# ── fonts ─────────────────────────────────────────────────────────────────────
def _fam(candidates: list[str], fallback="Segoe UI") -> str:
    db = set(QtGui.QFontDatabase.families())
    return next((c for c in candidates if c in db), fallback)

def ui_font(size=10, weight=400) -> QtGui.QFont:
    f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable", "Segoe UI"]))
    f.setPointSize(size); f.setWeight(QtGui.QFont.Weight(weight))
    return f

def mono_font(size=11, weight=600) -> QtGui.QFont:
    f = QtGui.QFont(_fam(["JetBrains Mono", "Cascadia Mono", "Consolas"]))
    f.setPointSize(size); f.setWeight(QtGui.QFont.Weight(weight))
    try: f.setFeature(QtGui.QFont.Tag("tnum"), 1)
    except Exception: pass
    return f

def label_font(size=8) -> QtGui.QFont:
    f = ui_font(size, 600)
    f.setCapitalization(QtGui.QFont.Capitalization.AllUppercase)
    f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 1.2)
    return f


STYLESHEET = f"""
* {{ outline: none; }}

QMainWindow, QDialog {{ background: {BG}; color: {TEXT}; }}
QWidget {{ background: transparent; color: {TEXT}; font-family: "Segoe UI Variable Text","Segoe UI"; }}

QLabel {{ background: transparent; color: {TEXT}; }}

QScrollArea {{ background: {BG}; border: none; }}
QScrollArea > QWidget > QWidget {{ background: {BG}; }}
QScrollBar:vertical {{
    background: transparent; width: 8px; margin: 0;
}}
QScrollBar::handle:vertical {{
    background: {FAINT}; border-radius: 4px; min-height: 28px;
}}
QScrollBar::handle:vertical:hover {{ background: {BORD_HI}; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}

QLineEdit {{
    background: {SURF2};
    color: {TEXT};
    border: 1px solid {BORDER};
    border-radius: 6px;
    padding: 6px 12px;
    font-size: 10pt;
    selection-background-color: {ACCENT};
}}
QLineEdit:hover {{ border-color: {BORD_HI}; }}
QLineEdit:focus {{ border-color: {ACCENT}; }}
QLineEdit::placeholder {{ color: {DIM}; }}

QSlider::groove:horizontal {{
    background: {FAINT}; height: 4px; border-radius: 2px;
}}
QSlider::sub-page:horizontal {{
    background: {ACCENT}; border-radius: 2px;
}}
QSlider::add-page:horizontal {{ background: {FAINT}; border-radius: 2px; }}
QSlider::handle:horizontal {{
    background: {TEXT};
    width: 14px; height: 14px;
    margin: -6px 0;
    border-radius: 7px;
    border: 2px solid {ACCENT};
}}
QSlider::handle:horizontal:hover {{ background: {ACCENT}; border-color: {TEXT}; }}

QCheckBox {{ spacing: 10px; color: {DIM}; }}
QCheckBox::indicator {{
    width: 18px; height: 18px;
    border: 1.5px solid {BORDER};
    border-radius: 4px;
    background: {SURF2};
}}
QCheckBox::indicator:hover {{ border-color: {ACCENT}; }}
QCheckBox::indicator:checked {{
    background: {ACCENT};
    border-color: {ACCENT};
    image: url(none);
}}

/* Default button = quiet ghost. Hero comes via [accent="true"] */
QPushButton {{
    background: {SURF2};
    color: {TEXT};
    border: 1px solid {BORDER};
    border-radius: 6px;
    padding: 7px 16px;
    font-size: 10pt;
    font-weight: 500;
}}
QPushButton:hover    {{ background: {SURFACE}; border-color: {BORD_HI}; }}
QPushButton:pressed  {{ background: {BG}; }}
QPushButton:disabled {{ background: {SURFACE}; color: {FAINT}; border-color: {BORDER}; }}

QPushButton[accent="true"] {{
    background: {ACCENT};
    color: #001523;
    border: none;
    border-radius: 8px;
    padding: 0 24px;
    font-size: 11pt;
    font-weight: 700;
    letter-spacing: 0.6px;
    min-height: 38px;
}}
QPushButton[accent="true"]:hover    {{ background: #3FB3FF; }}
QPushButton[accent="true"]:pressed  {{ background: #0D8AE8; }}
QPushButton[accent="true"]:disabled {{ background: {SURFACE}; color: {FAINT}; }}

QPushButton[danger="true"] {{
    background: {DANGER};
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 0 24px;
    font-size: 11pt;
    font-weight: 700;
    letter-spacing: 0.6px;
    min-height: 38px;
}}
QPushButton[danger="true"]:hover    {{ background: #F87171; }}
QPushButton[danger="true"]:pressed  {{ background: #DC2626; }}

/* License chip — top-bar pill that flips color on unlock */
QLabel#licenseChip {{
    background: {SURF2};
    border: 1px solid {BORDER};
    border-radius: 14px;
    padding: 4px 14px;
    color: {DIM};
}}
QLabel#licenseChip[state="locked"] {{
    background: {SURF2};
    border: 1px solid {BORDER};
    color: {DIM};
}}
QLabel#licenseChip[state="active"] {{
    background: rgba(52,211,153,0.10);
    border: 1px solid rgba(52,211,153,0.45);
    color: {OK};
}}
"""


# ── engine backend ─────────────────────────────────────────────────────────────
# All capture / detection / gamepad logic lives in labs2kmain.run().
# This file owns only the UI; it builds a settings dict from UI state
# and hands it to the backend on Start.
try:
    import labs2kmain
    try:
        from network_optimizer import apply as _net_apply, restore as _net_restore
        _NETOPT_OK = True
    except ImportError:
        _NETOPT_OK = False
    ENGINE_OK = True
except Exception:
    ENGINE_OK = False
    _NETOPT_OK = False


# ── AI Vision outcome tracker ─────────────────────────────────────────────────
# Records the outcome (green / normal / miss) of every fired shot bucketed by
# the threshold value used. After enough samples, suggests a better threshold.
class OutcomeTracker:
    """Thread-safe outcome history with persistence + suggestion logic."""

    BUCKET_SIZE = 0.5         # group thresholds in 0.5% buckets (e.g., 94.5 → "94.5")
    MIN_SAMPLES_PER_BUCKET = 10
    MIN_DELTA_TO_SUGGEST   = 0.05   # only suggest if best beats current by 5%+

    def __init__(self):
        self._lock = threading.Lock()
        # data["94.5"] = {"green": 12, "normal": 5, "miss": 3}
        self._data = defaultdict(lambda: {"green": 0, "normal": 0, "miss": 0})
        self.shots_total = 0
        self.greens_total = 0
        self.session_started_at = time.time()
        self._load()

    @staticmethod
    def _bucket(threshold_pct: float) -> str:
        # threshold comes in as 0–1 float; convert to % then bucket
        pct = threshold_pct * 100.0 if threshold_pct <= 1.0 else threshold_pct
        b = round(pct / OutcomeTracker.BUCKET_SIZE) * OutcomeTracker.BUCKET_SIZE
        return f"{b:.1f}"

    def _load(self):
        if not OUTCOMES_FILE.exists(): return
        try:
            with self._lock:
                raw = json.loads(OUTCOMES_FILE.read_text())
                for k, v in raw.get("buckets", {}).items():
                    self._data[k] = {"green":  int(v.get("green", 0)),
                                     "normal": int(v.get("normal", 0)),
                                     "miss":   int(v.get("miss", 0))}
                self.shots_total  = int(raw.get("shots_total", 0))
                self.greens_total = int(raw.get("greens_total", 0))
        except Exception:
            pass

    def _save(self):
        try:
            OUTCOMES_FILE.parent.mkdir(parents=True, exist_ok=True)
            payload = {
                "buckets": {k: dict(v) for k, v in self._data.items()},
                "shots_total":  self.shots_total,
                "greens_total": self.greens_total,
            }
            OUTCOMES_FILE.write_text(json.dumps(payload, indent=2))
        except Exception:
            pass

    def record(self, threshold: float, outcome: str, l2: bool = False):
        with self._lock:
            bucket = self._bucket(threshold)
            if outcome not in ("green", "normal", "miss"): outcome = "miss"
            self._data[bucket][outcome] += 1
            self.shots_total += 1
            if outcome == "green":
                self.greens_total += 1
            self._save()

    def green_rate(self) -> float:
        if self.shots_total == 0: return 0.0
        return self.greens_total / self.shots_total

    def suggested_threshold(self, current: float) -> tuple[float | None, float, float]:
        """
        Returns (suggested_threshold_or_None, current_bucket_rate, suggested_rate).
        Suggested is None if no bucket beats current by MIN_DELTA_TO_SUGGEST.
        """
        with self._lock:
            current_bucket = self._bucket(current)
            best_pct  = None
            best_rate = -1.0
            cur_rate  = 0.0
            for bucket, counts in self._data.items():
                total = counts["green"] + counts["normal"] + counts["miss"]
                if total < self.MIN_SAMPLES_PER_BUCKET:
                    continue
                rate = counts["green"] / total
                if bucket == current_bucket:
                    cur_rate = rate
                if rate > best_rate:
                    best_rate = rate
                    best_pct  = float(bucket)
            if best_pct is None or best_rate < cur_rate + self.MIN_DELTA_TO_SUGGEST:
                return (None, cur_rate, best_rate if best_rate >= 0 else 0.0)
            return (best_pct, cur_rate, best_rate)


_tracker = OutcomeTracker()


class EngineRunner:
    def __init__(self):
        self._thread: threading.Thread | None = None
        self._stop   = threading.Event()
        self.shots   = 0
        self.fps_cur = 0.0
        self.start_t = 0.0
        self.threshold_normal = 0.95
        self.threshold_l2     = 0.75
        self.tempo            = False
        self.tempo_ms         = 39
        self.fps_cap          = 120
        self.defense_enabled          = False
        self.infinite_stamina         = False
        self.defense_anti_blowby      = True
        self.defense_auto_hands_up    = True
        self.defense_contest_assist   = True
        self.defense_lateral_boost    = True
        self.defense_sensitivity_boost= True
        self.stick_tempo_enabled      = False
        self.quickstop_enabled        = False
        self.gpu_index                = 0
        self.lite_mode                = False  # True = mss + 60fps + every-2; False = BetterCam + 120fps

    def is_running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self):
        if self.is_running() or not ENGINE_OK: return
        self._stop.clear()
        self.shots = 0
        self.start_t = time.perf_counter()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()

    def _loop(self):
        """Build a settings dict from UI state and hand off to labs2kmain.run()."""
        cfg = {
            "threshold":      self.threshold_normal,
            "threshold_l2":   self.threshold_l2,
            "tempo":          self.tempo,
            "tempo_ms":       self.tempo_ms,
            "stick_tempo":    self.stick_tempo_enabled,
            "quickstop":      self.quickstop_enabled,
            "defense":        self.defense_enabled,
            "stamina":        self.infinite_stamina,
            "no_hands_up":    not self.defense_auto_hands_up,
            "gpu":            self.gpu_index,
            "low_end":        self.lite_mode,
            "capture":        "mss" if self.lite_mode else "bettercam",
        }

        def _on_shot(n: int, l2: bool):
            self.shots = n

        def _on_outcome(threshold: float, outcome: str, l2: bool):
            _tracker.record(threshold, outcome, l2)
            print(f"AiVision outcome: {outcome} @ {threshold*100:.1f}% "
                  f"(rate {_tracker.green_rate()*100:.0f}% over {_tracker.shots_total} shots)",
                  flush=True)

        try:
            labs2kmain.run(cfg, stop_event=self._stop,
                           on_shot=_on_shot,
                           on_shot_outcome=_on_outcome)
        except Exception as ex:
            print(f"[ENGINE] backend error: {ex}", flush=True)


_engine = EngineRunner()


# ── shared widgets ─────────────────────────────────────────────────────────────

class _Drag(QtWidgets.QWidget):
    """Mixin: makes a widget drag the parent window."""
    def __init__(self):
        super().__init__()
        self._off: QtCore.QPoint | None = None

    def mousePressEvent(self, e):
        if e.button() == QtCore.Qt.MouseButton.LeftButton:
            self._off = e.globalPosition().toPoint() - self.window().frameGeometry().topLeft()

    def mouseMoveEvent(self, e):
        if self._off and e.buttons() & QtCore.Qt.MouseButton.LeftButton:
            self.window().move(e.globalPosition().toPoint() - self._off)

    def mouseReleaseEvent(self, e):
        self._off = None


class HeroBanner(_Drag):
    def __init__(self):
        super().__init__()
        self.setFixedHeight(170)
        self._pix = QtGui.QPixmap(str(BANNER)) if BANNER.exists() else QtGui.QPixmap()

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        r = self.rect()
        p.fillRect(r, QtGui.QColor(SURFACE))
        if not self._pix.isNull():
            p.setRenderHint(QtGui.QPainter.RenderHint.SmoothPixmapTransform)
            scaled = self._pix.scaled(r.size(),
                QtCore.Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                QtCore.Qt.TransformationMode.SmoothTransformation)
            ox = (scaled.width()  - r.width())  // 2
            oy = int((scaled.height() - r.height()) * 0.38)
            p.drawPixmap(r, scaled, QtCore.QRect(ox, oy, r.width(), r.height()))
        # bottom fade
        g = QtGui.QLinearGradient(0, 0, 0, r.height())
        g.setColorAt(0.4, QtGui.QColor(0, 0, 0, 0))
        g.setColorAt(1.0, QtGui.QColor(BG))
        p.fillRect(r, g)
        # bottom border
        p.setPen(QtGui.QPen(QtGui.QColor(BORDER)))
        p.drawLine(0, r.height()-1, r.width(), r.height()-1)


class WinBtn(QtWidgets.QAbstractButton):
    def __init__(self, kind: str, parent=None):
        super().__init__(parent)
        self._kind = kind
        self.setFixedSize(36, 28)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor)

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        hover = self.underMouse()
        if hover:
            bg = QtGui.QColor(DANGER if self._kind == "close" else BORD_HI)
            p.fillRect(self.rect(), bg)
        pen = QtGui.QPen(QtGui.QColor(TEXT if hover else DIM), 1)
        p.setPen(pen)
        cx, cy = self.width()//2, self.height()//2
        if self._kind == "min":
            p.drawLine(cx-5, cy+2, cx+5, cy+2)
        else:
            p.drawLine(cx-4, cy-4, cx+4, cy+4)
            p.drawLine(cx-4, cy+4, cx+4, cy-4)

    def enterEvent(self, e): self.update(); super().enterEvent(e)
    def leaveEvent(self, e): self.update(); super().leaveEvent(e)


class TabBtn(QtWidgets.QAbstractButton):
    """Pill-style tab — solid block when active, ghost when not."""
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        self.setText(text)
        self.setCheckable(True)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor)
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
        f.setPointSize(10)
        f.setWeight(QtGui.QFont.Weight(600))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 0.4)
        self.setFont(f)
        self.setFixedHeight(34)

    def sizeHint(self):
        fm = QtGui.QFontMetrics(self.font())
        return QtCore.QSize(fm.horizontalAdvance(self.text()) + 32, 34)

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        r = self.rect().adjusted(2, 4, -2, -4)
        active = self.isChecked()
        hover  = self.underMouse() and not active

        # Background
        if active:
            p.setBrush(QtGui.QColor(ACCENT))
            p.setPen(QtCore.Qt.PenStyle.NoPen)
            p.drawRoundedRect(r, r.height()/2, r.height()/2)
            text_color = QtGui.QColor("#001023")
        elif hover:
            p.setBrush(QtGui.QColor(SURF2))
            p.setPen(QtCore.Qt.PenStyle.NoPen)
            p.drawRoundedRect(r, r.height()/2, r.height()/2)
            text_color = QtGui.QColor(TEXT)
        else:
            text_color = QtGui.QColor(DIM)

        p.setFont(self.font())
        p.setPen(text_color)
        p.drawText(r, QtCore.Qt.AlignmentFlag.AlignCenter, self.text())

    def enterEvent(self, e): self.update(); super().enterEvent(e)
    def leaveEvent(self, e): self.update(); super().leaveEvent(e)


class Card(QtWidgets.QFrame):
    """Subtle elevated surface — no visible border, depth from background only."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")
        self.setStyleSheet(f"""
            QFrame#Card {{
                background: {SURFACE};
                border: none;
                border-radius: 10px;
            }}
        """)


class SectionLabel(QtWidgets.QWidget):
    """Confident section header — caps eyebrow + thin underline."""
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        self.setFixedHeight(24)
        self._text = text.upper()

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f.setPointSize(8); f.setWeight(QtGui.QFont.Weight(700))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 2.0)
        p.setFont(f)
        p.setPen(QtGui.QColor(DIM))
        fm = QtGui.QFontMetrics(f)
        text_w = fm.horizontalAdvance(self._text)
        p.drawText(QtCore.QRect(0, 0, text_w + 4, self.height()),
                   QtCore.Qt.AlignmentFlag.AlignLeft | QtCore.Qt.AlignmentFlag.AlignVCenter,
                   self._text)
        # tiny accent dash to the right of the label
        y = self.height() // 2
        p.fillRect(text_w + 12, y - 1, 24, 2, QtGui.QColor(ACCENT))


class SliderRow(QtWidgets.QWidget):
    valueChanged = QtCore.Signal(float)

    def __init__(self, label: str, lo: float, hi: float,
                 decimals: int = 1, unit: str = "", step: float = 0.1):
        super().__init__()
        self._dec = decimals
        h = QtWidgets.QHBoxLayout(self)
        h.setContentsMargins(0, 0, 0, 0); h.setSpacing(12)

        lbl = QtWidgets.QLabel(label.upper())
        lbl.setFont(label_font(8))
        lbl.setStyleSheet(f"color: {DIM};")
        lbl.setFixedWidth(130)
        h.addWidget(lbl)

        self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.slider.setRange(int(lo * 10**decimals), int(hi * 10**decimals))
        self.slider.setSingleStep(max(1, int(step * 10**decimals)))
        self.slider.valueChanged.connect(self._on)
        h.addWidget(self.slider, 1)

        self.val = QtWidgets.QLabel("0")
        self.val.setFont(mono_font(11, 600))
        self.val.setStyleSheet(f"color: {TEXT};")
        self.val.setFixedWidth(52)
        self.val.setAlignment(QtCore.Qt.AlignmentFlag.AlignRight | QtCore.Qt.AlignmentFlag.AlignVCenter)
        h.addWidget(self.val)

        if unit:
            u = QtWidgets.QLabel(unit)
            u.setFont(label_font(7))
            u.setStyleSheet(f"color: {FAINT};")
            u.setFixedWidth(24)
            h.addWidget(u)

    def setValue(self, v: float):
        self.slider.setValue(int(round(v * 10**self._dec)))

    def value(self) -> float:
        return self.slider.value() / 10**self._dec

    def _on(self, _):
        v = self.value()
        self.val.setText(f"{v:.{self._dec}f}")
        self.valueChanged.emit(v)


class ToggleRow(QtWidgets.QWidget):
    toggled = QtCore.Signal(bool)

    def __init__(self, label: str, hint: str = ""):
        super().__init__()
        h = QtWidgets.QHBoxLayout(self)
        h.setContentsMargins(0, 0, 0, 0); h.setSpacing(12)

        col = QtWidgets.QVBoxLayout(); col.setSpacing(1)
        lbl = QtWidgets.QLabel(label.upper())
        lbl.setFont(label_font(8)); lbl.setStyleSheet(f"color: {TEXT};")
        col.addWidget(lbl)
        if hint:
            sub = QtWidgets.QLabel(hint)
            sub.setFont(ui_font(8)); sub.setStyleSheet(f"color: {DIM};")
            col.addWidget(sub)
        h.addLayout(col, 1)

        self._box = QtWidgets.QCheckBox()
        self._box.toggled.connect(self.toggled)
        h.addWidget(self._box)

    def setChecked(self, v: bool): self._box.setChecked(bool(v))
    def isChecked(self) -> bool:   return self._box.isChecked()


# ── Column-layout widgets — control-panel style ───────────────────────────────

class ColumnHeader(QtWidgets.QLabel):
    """Big centered cyan column header (SHOOTING / DEFENSE / BOOSTS)."""
    def __init__(self, text: str):
        super().__init__(text.upper())
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f.setPointSize(13); f.setWeight(QtGui.QFont.Weight(800))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 4.5)
        self.setFont(f)
        self.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self.setStyleSheet(f"color: {ACCENT}; padding: 4px 0 2px 0;")


class FieldLabel(QtWidgets.QLabel):
    """Small caps label that sits above a control."""
    def __init__(self, text: str):
        super().__init__(text.upper())
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
        f.setPointSize(8); f.setWeight(QtGui.QFont.Weight(700))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 2.0)
        self.setFont(f)
        self.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self.setStyleSheet(f"color: {DIM};")


class StepBtn(QtWidgets.QPushButton):
    """Small step button used flanking a slider (-2 / -.5 / +.5 / +2)."""
    def __init__(self, text: str):
        super().__init__(text)
        self.setFixedSize(34, 26)
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
        f.setPointSize(9); f.setWeight(QtGui.QFont.Weight(600))
        self.setFont(f)
        self.setStyleSheet(f"""
            QPushButton {{
                background: {SURF2}; color: {DIM};
                border: 1px solid {BORDER}; border-radius: 5px;
                padding: 0;
            }}
            QPushButton:hover  {{ color: {TEXT}; border-color: {BORD_HI}; }}
            QPushButton:pressed{{ background: {BG}; }}
        """)


class BigStepSlider(QtWidgets.QWidget):
    """Centered field label + huge value + step buttons + slider."""
    valueChanged = QtCore.Signal(float)

    def __init__(self, label: str, lo: float, hi: float,
                 unit: str = "%", decimals: int = 1, default: float = 0.0,
                 small_step: float = 0.5, big_step: float = 2.0):
        super().__init__()
        self._dec = decimals
        self._unit = unit

        col = QtWidgets.QVBoxLayout(self)
        col.setContentsMargins(0, 0, 0, 0); col.setSpacing(6)

        col.addWidget(FieldLabel(label))

        # huge centered value
        self.val = QtWidgets.QLabel(f"{default:.{decimals}f}{unit}")
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f.setPointSize(30); f.setWeight(QtGui.QFont.Weight(800))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, -1.0)
        self.val.setFont(f)
        self.val.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self.val.setStyleSheet(f"color: {ACCENT};")
        col.addWidget(self.val)

        # step buttons + slider
        row = QtWidgets.QHBoxLayout(); row.setSpacing(4)
        bm2  = StepBtn(f"-{big_step:g}");   bms = StepBtn(f"-{small_step:g}")
        bps  = StepBtn(f"+{small_step:g}"); bp2 = StepBtn(f"+{big_step:g}")
        self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.slider.setRange(int(lo * 10**decimals), int(hi * 10**decimals))
        self.slider.setSingleStep(max(1, int(small_step * 10**decimals)))
        self.slider.setPageStep(max(1, int(big_step * 10**decimals)))
        self.slider.setValue(int(round(default * 10**decimals)))
        self.slider.valueChanged.connect(self._on_change)
        bm2.clicked.connect(lambda: self._step(-big_step))
        bms.clicked.connect(lambda: self._step(-small_step))
        bps.clicked.connect(lambda: self._step(+small_step))
        bp2.clicked.connect(lambda: self._step(+big_step))
        row.addWidget(bm2); row.addWidget(bms)
        row.addWidget(self.slider, 1)
        row.addWidget(bps); row.addWidget(bp2)
        col.addLayout(row)

    def _step(self, delta: float):
        self.setValue(self.value() + delta)

    def _on_change(self, _):
        v = self.value()
        self.val.setText(f"{v:.{self._dec}f}{self._unit}")
        self.valueChanged.emit(v)

    def setValue(self, v: float):
        self.slider.setValue(int(round(v * 10**self._dec)))

    def value(self) -> float:
        return self.slider.value() / 10**self._dec


class StateToggle(QtWidgets.QWidget):
    """Checkbox + LABEL + ON/OFF state text. Like ZP's [✓] ENABLED ON."""
    toggled = QtCore.Signal(bool)

    def __init__(self, label: str):
        super().__init__()
        h = QtWidgets.QHBoxLayout(self)
        h.setContentsMargins(0, 0, 0, 0); h.setSpacing(10)

        self._box = QtWidgets.QCheckBox()
        self._box.toggled.connect(self._on_toggle)
        h.addWidget(self._box)

        self._lbl = QtWidgets.QLabel(label.upper())
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
        f.setPointSize(9); f.setWeight(QtGui.QFont.Weight(700))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 1.4)
        self._lbl.setFont(f)
        self._lbl.setStyleSheet(f"color: {TEXT};")
        h.addWidget(self._lbl)

        h.addStretch(1)

        self._state = QtWidgets.QLabel("OFF")
        self._state.setFont(f)
        self._state.setStyleSheet(f"color: {DIM};")
        h.addWidget(self._state)

    def _on_toggle(self, v: bool):
        self._state.setText("ON" if v else "OFF")
        self._state.setStyleSheet(f"color: {ACCENT if v else DIM};")
        self.toggled.emit(v)

    def setChecked(self, v: bool):
        self._box.setChecked(bool(v))
        self._on_toggle(bool(v))

    def isChecked(self) -> bool:
        return self._box.isChecked()


class StatLine(QtWidgets.QWidget):
    """Right-aligned big number with caps label on the left — bottom-of-column stat."""
    def __init__(self, label: str, value: str = "—"):
        super().__init__()
        h = QtWidgets.QHBoxLayout(self)
        h.setContentsMargins(0, 0, 0, 0); h.setSpacing(12)
        self._lbl = QtWidgets.QLabel(label.upper())
        f1 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
        f1.setPointSize(9); f1.setWeight(QtGui.QFont.Weight(700))
        f1.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 1.6)
        self._lbl.setFont(f1)
        self._lbl.setStyleSheet(f"color: {DIM};")
        h.addWidget(self._lbl)
        h.addStretch(1)
        self._val = QtWidgets.QLabel(value)
        f2 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f2.setPointSize(20); f2.setWeight(QtGui.QFont.Weight(800))
        self._val.setFont(f2)
        self._val.setStyleSheet(f"color: {ACCENT};")
        self._val.setAlignment(QtCore.Qt.AlignmentFlag.AlignRight | QtCore.Qt.AlignmentFlag.AlignVCenter)
        h.addWidget(self._val)

    def setValue(self, s: str): self._val.setText(s)


def _hr(parent=None):
    f = QtWidgets.QFrame(parent)
    f.setFixedHeight(1)
    f.setStyleSheet(f"background: {BORDER};")
    return f


def _vsep(parent=None):
    f = QtWidgets.QFrame(parent)
    f.setFixedWidth(1)
    f.setStyleSheet(f"background: {BORDER};")
    return f


class StatBox(QtWidgets.QWidget):
    """Big jersey-number stat. Dim caps label up top, huge bold number below."""
    def __init__(self, label: str, value: str = "—"):
        super().__init__()
        self.setStyleSheet(f"""
            background: {SURF2};
            border: none;
            border-radius: 10px;
        """)
        v = QtWidgets.QVBoxLayout(self)
        v.setContentsMargins(20, 16, 20, 18); v.setSpacing(6)

        self._lbl = QtWidgets.QLabel(label.upper())
        f1 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
        f1.setPointSize(8); f1.setWeight(QtGui.QFont.Weight(700))
        f1.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 2.0)
        self._lbl.setFont(f1)
        self._lbl.setStyleSheet(f"background: transparent; color: {DIM}; border: none;")
        v.addWidget(self._lbl)

        self._val = QtWidgets.QLabel(value)
        f2 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f2.setPointSize(28); f2.setWeight(QtGui.QFont.Weight(800))
        f2.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, -0.8)
        self._val.setFont(f2)
        self._val.setStyleSheet(f"background: transparent; color: {TEXT}; border: none;")
        v.addWidget(self._val)

    def setValue(self, s: str): self._val.setText(s)


class EngBtn(QtWidgets.QPushButton):
    """The hero Start/Stop button. Picks accent vs danger via property."""
    def __init__(self):
        super().__init__("START ENGINE")
        self._running = False
        self.setMinimumHeight(48)
        self.setProperty("accent", True)
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f.setPointSize(11); f.setWeight(QtGui.QFont.Weight(800))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 1.0)
        self.setFont(f)

    def setRunning(self, v: bool):
        self._running = v
        self.setText("STOP ENGINE" if v else "START ENGINE")
        self.setProperty("accent", not v)
        self.setProperty("danger", v)
        # Re-apply stylesheet so the property change takes effect
        self.style().unpolish(self); self.style().polish(self)


def _divider() -> QtWidgets.QFrame:
    f = QtWidgets.QFrame()
    f.setFixedHeight(1)
    f.setStyleSheet(f"background: {BORDER};")
    return f


# ── main window ───────────────────────────────────────────────────────────────
class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("TM LABS · AI VISION")
        self.resize(1180, 720)
        self.setMinimumSize(1080, 640)
        self.setWindowFlags(QtCore.Qt.WindowType.FramelessWindowHint)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_TranslucentBackground, False)
        self.setStyleSheet(STYLESHEET + f"QMainWindow {{ background: {BG}; }}")
        self._data = load_settings()

        root = QtWidgets.QWidget()
        root.setStyleSheet(f"background: {BG};")
        self.setCentralWidget(root)
        vbox = QtWidgets.QVBoxLayout(root)
        vbox.setContentsMargins(0, 0, 0, 0); vbox.setSpacing(0)

        # window controls overlay
        self._ctrl_bar = QtWidgets.QWidget(self)
        cb = QtWidgets.QHBoxLayout(self._ctrl_bar)
        cb.setContentsMargins(0, 0, 0, 0); cb.setSpacing(0)
        bmin = WinBtn("min");   bmin.clicked.connect(self.showMinimized)
        bcls = WinBtn("close"); bcls.clicked.connect(self._on_close)
        cb.addWidget(bmin); cb.addWidget(bcls)

        vbox.addWidget(HeroBanner())
        vbox.addWidget(self._build_topbar())
        vbox.addWidget(_divider())
        vbox.addWidget(self._build_body(), 1)

        self._apply_lock()
        self._position_ctrls()

        self._ticker = QtCore.QTimer(self)
        self._ticker.timeout.connect(self._tick)
        self._ticker.start(500)

    def _on_close(self):
        _engine.stop(); self.close()

    def resizeEvent(self, e):
        super().resizeEvent(e)
        if hasattr(self, "_ctrl_bar"): self._position_ctrls()

    def _position_ctrls(self):
        self._ctrl_bar.adjustSize()
        self._ctrl_bar.move(self.width() - self._ctrl_bar.width(), 0)
        self._ctrl_bar.raise_()

    # ── top bar ───────────────────────────────────────────────────────────────
    def _build_topbar(self) -> QtWidgets.QWidget:
        bar = QtWidgets.QWidget()
        bar.setFixedHeight(56)
        bar.setStyleSheet(f"background: {SURFACE}; border-bottom: 1px solid {BORDER};")
        h = QtWidgets.QHBoxLayout(bar)
        h.setContentsMargins(28, 0, 120, 0); h.setSpacing(14)

        # Left: Discord ID input — labeled inline, no eyebrow
        self._id_field = QtWidgets.QLineEdit()
        self._id_field.setPlaceholderText("Discord ID")
        self._id_field.setText(str(self._data.get("discord_id", "")))
        self._id_field.setFixedHeight(34)
        self._id_field.setMinimumWidth(220)
        self._id_field.setMaximumWidth(280)
        self._id_field.textEdited.connect(lambda v: (self._set("discord_id", v), self._apply_lock()))
        h.addWidget(self._id_field)

        h.addStretch(1)

        # Right: license chip — one pill, color-coded
        self._key_val = QtWidgets.QLabel("LOCKED")
        self._key_val.setObjectName("licenseChip")
        self._key_val.setProperty("state", "locked")
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f.setPointSize(9); f.setWeight(QtGui.QFont.Weight(700))
        f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 1.2)
        self._key_val.setFont(f)
        self._key_val.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self._key_val.setMinimumHeight(30)
        self._key_val.setMinimumWidth(110)
        h.addWidget(self._key_val)

        return bar

    # ── body — scoreboard at top + 2 columns below (single screen, no tabs) ──
    def _build_body(self) -> QtWidgets.QWidget:
        w = QtWidgets.QWidget()
        w.setStyleSheet(f"background: {BG};")
        v = QtWidgets.QVBoxLayout(w)
        v.setContentsMargins(0, 0, 0, 0); v.setSpacing(0)

        v.addWidget(self._build_scoreboard())
        v.addWidget(_hr())

        body = QtWidgets.QWidget()
        body.setStyleSheet(f"background: {BG};")
        h = QtWidgets.QHBoxLayout(body)
        h.setContentsMargins(0, 0, 0, 0); h.setSpacing(0)
        h.addWidget(self._col_shooting(),  3)
        h.addWidget(_vsep())
        h.addWidget(self._col_defense(),   2)
        v.addWidget(body, 1)

        # locked / unlocked switcher
        self._body_stack = QtWidgets.QStackedWidget()
        self._body_stack.setStyleSheet(f"background: {BG};")
        self._body_stack.addWidget(self._build_locked())
        self._body_stack.addWidget(w)
        return self._body_stack

    # ── scoreboard strip — live stats + reduce lag at the top ─────────────────
    def _build_scoreboard(self) -> QtWidgets.QWidget:
        bar = QtWidgets.QWidget()
        bar.setStyleSheet(f"background: {SURFACE};")
        bar.setFixedHeight(96)
        h = QtWidgets.QHBoxLayout(bar)
        h.setContentsMargins(28, 16, 28, 16); h.setSpacing(28)

        def stat(label: str, value: str):
            box = QtWidgets.QWidget()
            v = QtWidgets.QVBoxLayout(box)
            v.setContentsMargins(0, 0, 0, 0); v.setSpacing(2)
            l = QtWidgets.QLabel(label.upper())
            f1 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
            f1.setPointSize(8); f1.setWeight(QtGui.QFont.Weight(700))
            f1.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 2.0)
            l.setFont(f1); l.setStyleSheet(f"color: {DIM};")
            v.addWidget(l)
            n = QtWidgets.QLabel(value)
            f2 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
            f2.setPointSize(28); f2.setWeight(QtGui.QFont.Weight(800))
            f2.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, -0.8)
            n.setFont(f2); n.setStyleSheet(f"color: {TEXT};")
            v.addWidget(n)
            return box, n

        sh_box, self._st_rel = stat("Shots Made", "0")
        gr_box, self._st_green = stat("Green Rate", "—")
        st_box, self._st_status = stat("Status", "READY")
        h.addWidget(sh_box)
        h.addWidget(gr_box)
        h.addWidget(st_box)
        h.addStretch(1)
        # legacy refs kept None so any stale code paths don't NameError
        self._st_ses = None
        self._st_key = None

        # Reduce Lag toggle lives here (connection-y, not a player boost)
        lat_wrap = QtWidgets.QVBoxLayout(); lat_wrap.setSpacing(2)
        lat_wrap.addStretch(1)
        self._tog_lat = StateToggle("Reduce Lag")
        self._tog_lat.setChecked(bool(self._data.get("low_latency", True)))
        def _lat(v):
            self._set("low_latency", v)
            if _NETOPT_OK: (_net_apply if v else _net_restore)()
        self._tog_lat.toggled.connect(_lat)
        if bool(self._data.get("low_latency", True)) and _NETOPT_OK:
            _net_apply()
        lat_wrap.addWidget(self._tog_lat)
        lat_wrap.addStretch(1)
        h.addLayout(lat_wrap)
        return bar

    # ── locked screen ──────────────────────────────────────────────────────────
    def _build_locked(self) -> QtWidgets.QWidget:
        w = QtWidgets.QWidget()
        w.setStyleSheet(f"background: {BG};")
        v = QtWidgets.QVBoxLayout(w)
        v.setContentsMargins(0, 0, 0, 0)
        v.addStretch(2)

        # Big eyebrow label, then headline, then sub
        eyebrow = QtWidgets.QLabel("LICENSE REQUIRED")
        f1 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f1.setPointSize(9); f1.setWeight(QtGui.QFont.Weight(700))
        f1.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, 2.5)
        eyebrow.setFont(f1)
        eyebrow.setStyleSheet(f"color: {ACCENT};")
        eyebrow.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        v.addWidget(eyebrow)

        v.addSpacing(18)

        headline = QtWidgets.QLabel("Enter your Discord ID")
        f2 = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Display", "Segoe UI"]))
        f2.setPointSize(26); f2.setWeight(QtGui.QFont.Weight(700))
        f2.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, -0.5)
        headline.setFont(f2)
        headline.setStyleSheet(f"color: {TEXT};")
        headline.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        v.addWidget(headline)

        v.addSpacing(8)

        sub = QtWidgets.QLabel("to access shooting tools")
        sub.setFont(ui_font(11))
        sub.setStyleSheet(f"color: {DIM};")
        sub.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        v.addWidget(sub)

        v.addStretch(3)
        return w

    # ── columns ────────────────────────────────────────────────────────────────
    def _column_scroll(self):
        """Returns (scroll_area, vbox_layout) for one of the three side columns."""
        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)
        scroll.setStyleSheet(f"background: {BG};")
        scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        inner = QtWidgets.QWidget()
        inner.setStyleSheet(f"background: {BG};")
        lay = QtWidgets.QVBoxLayout(inner)
        lay.setContentsMargins(28, 22, 28, 22); lay.setSpacing(18)
        scroll.setWidget(inner)
        return scroll, lay

    # ── COL 1 — Shooting ──────────────────────────────────────────────────────
    def _col_shooting(self) -> QtWidgets.QWidget:
        scroll, lay = self._column_scroll()
        lay.addWidget(ColumnHeader("Shooting"))
        lay.addSpacing(2)

        # Open shot timing
        self._sl_normal = BigStepSlider("Open Shot Timing", 50, 100, "%", 1,
                                        default=float(self._data.get("timing_value", 95.0)))
        self._sl_normal.valueChanged.connect(
            lambda x: (self._set("timing_value", x), setattr(_engine, "threshold_normal", x / 100.0)))
        _engine.threshold_normal = self._sl_normal.value() / 100.0
        lay.addWidget(self._sl_normal)

        # AI suggestion row — shows recommended threshold, one tap to apply
        suggest_row = QtWidgets.QHBoxLayout()
        suggest_row.setSpacing(8)
        self._suggest_lbl = QtWidgets.QLabel("AI: collecting data…")
        f = QtGui.QFont(_fam(["Inter", "Segoe UI Variable Text", "Segoe UI"]))
        f.setPointSize(9); f.setWeight(QtGui.QFont.Weight(600))
        self._suggest_lbl.setFont(f)
        self._suggest_lbl.setStyleSheet(f"color: {DIM};")
        self._suggest_apply = QtWidgets.QPushButton("Apply")
        self._suggest_apply.setProperty("ghost", True)
        self._suggest_apply.setVisible(False)
        self._suggest_apply.setStyleSheet(f"""
            QPushButton {{
                background: {SURF2}; color: {ACCENT};
                border: 1px solid {ACCENT};
                border-radius: 6px; padding: 4px 12px;
                font-size: 9pt; font-weight: 600;
            }}
            QPushButton:hover {{ background: {ACCENT}; color: white; }}
        """)
        def _apply_suggestion():
            v = self._suggest_apply.property("target_value")
            if v is not None:
                self._sl_normal.setValue(float(v))
        self._suggest_apply.clicked.connect(_apply_suggestion)
        suggest_row.addWidget(self._suggest_lbl, 1)
        suggest_row.addWidget(self._suggest_apply)
        lay.addLayout(suggest_row)

        lay.addWidget(_hr())

        # Contested
        self._sl_l2 = BigStepSlider("Contested Shot", 50, 100, "%", 1,
                                    default=float(self._data.get("shot_confidence", 75.0)))
        self._sl_l2.valueChanged.connect(
            lambda x: (self._set("shot_confidence", x), setattr(_engine, "threshold_l2", x / 100.0)))
        _engine.threshold_l2 = self._sl_l2.value() / 100.0
        lay.addWidget(self._sl_l2)

        lay.addWidget(_hr())

        # Release Delay (ms)
        self._sl_tempo = BigStepSlider("Release Delay", 0, 200, "ms", 0,
                                       default=float(self._data.get("rhythm_tempo_ms", 39)),
                                       small_step=1, big_step=5)
        self._sl_tempo.valueChanged.connect(
            lambda x: (self._set("rhythm_tempo_ms", x), setattr(_engine, "tempo_ms", int(x))))
        _engine.tempo_ms = int(self._sl_tempo.value())
        lay.addWidget(self._sl_tempo)

        # Toggles below the sliders
        self._tog_tempo = StateToggle("Use Release Delay")
        self._tog_tempo.setChecked(bool(self._data.get("rhythm_enabled", False)))
        self._tog_tempo.toggled.connect(
            lambda v: (self._set("rhythm_enabled", v), setattr(_engine, "tempo", v)))
        _engine.tempo = bool(self._data.get("rhythm_enabled", False))
        lay.addWidget(self._tog_tempo)

        self._tog_stick_tempo = StateToggle("Smart Stick Tempo")
        self._tog_stick_tempo.setChecked(bool(self._data.get("stick_tempo_enabled", False)))
        self._tog_stick_tempo.toggled.connect(
            lambda v: (self._set("stick_tempo_enabled", v), setattr(_engine, "stick_tempo_enabled", v)))
        _engine.stick_tempo_enabled = bool(self._data.get("stick_tempo_enabled", False))
        lay.addWidget(self._tog_stick_tempo)

        self._tog_quickstop = StateToggle("Quickstop")
        self._tog_quickstop.setChecked(bool(self._data.get("quickstop_enabled", False)))
        self._tog_quickstop.toggled.connect(
            lambda v: (self._set("quickstop_enabled", v), setattr(_engine, "quickstop_enabled", v)))
        _engine.quickstop_enabled = bool(self._data.get("quickstop_enabled", False))
        lay.addWidget(self._tog_quickstop)

        lay.addStretch(1)
        return scroll

    # ── COL 2 — Defense + Boost (Stamina) ─────────────────────────────────────
    def _col_defense(self) -> QtWidgets.QWidget:
        scroll, lay = self._column_scroll()
        lay.addWidget(ColumnHeader("Defense"))
        lay.addSpacing(2)

        master = StateToggle("Smart Defense")
        master.setChecked(bool(self._data.get("defense_enabled", False)))
        master.toggled.connect(
            lambda v: (self._set("defense_enabled", v), setattr(_engine, "defense_enabled", v)))
        _engine.defense_enabled = bool(self._data.get("defense_enabled", False))
        lay.addWidget(master)

        hint = QtWidgets.QLabel(
            "Auto-tracks ballhandlers, contests shots, raises hands. "
            "Press D-Pad Up while playing to toggle on / off."
        )
        hint.setFont(ui_font(8)); hint.setStyleSheet(f"color: {DIM};")
        hint.setAlignment(QtCore.Qt.AlignmentFlag.AlignHCenter)
        hint.setWordWrap(True)
        lay.addWidget(hint)

        # All the defense helpers stay on by default — that's what "smart" means.
        # No UI knobs needed; the engine just runs them.
        for attr in ("defense_anti_blowby", "defense_auto_hands_up",
                     "defense_contest_assist", "defense_lateral_boost",
                     "defense_sensitivity_boost"):
            setattr(_engine, attr, True)

        lay.addSpacing(8)
        lay.addWidget(_hr())
        lay.addSpacing(8)

        lay.addWidget(ColumnHeader("Boosts"))
        lay.addSpacing(2)

        stam = StateToggle("Never Run Out of Stamina")
        stam.setChecked(bool(self._data.get("infinite_stamina", False)))
        stam.toggled.connect(
            lambda v: (self._set("infinite_stamina", v), setattr(_engine, "infinite_stamina", v)))
        _engine.infinite_stamina = bool(self._data.get("infinite_stamina", False))
        lay.addWidget(stam)

        lay.addStretch(1)
        return scroll

    # ── COL 3 — Boosts + Live Stats ───────────────────────────────────────────
    def _col_boosts_stats(self) -> QtWidgets.QWidget:
        scroll, lay = self._column_scroll()
        lay.addWidget(ColumnHeader("Boosts"))
        lay.addSpacing(2)

        stam = StateToggle("Never Run Out of Stamina")
        stam.setChecked(bool(self._data.get("infinite_stamina", False)))
        stam.toggled.connect(
            lambda v: (self._set("infinite_stamina", v), setattr(_engine, "infinite_stamina", v)))
        _engine.infinite_stamina = bool(self._data.get("infinite_stamina", False))
        lay.addWidget(stam)

        lat = StateToggle("Reduce Lag")
        lat.setChecked(bool(self._data.get("low_latency", True)))
        def _lat(v):
            self._set("low_latency", v)
            if _NETOPT_OK: (_net_apply if v else _net_restore)()
        lat.toggled.connect(_lat)
        if bool(self._data.get("low_latency", True)) and _NETOPT_OK:
            _net_apply()
        lay.addWidget(lat)

        lay.addSpacing(8)
        lay.addWidget(_hr())
        lay.addSpacing(8)
        lay.addWidget(ColumnHeader("Live Stats"))
        lay.addSpacing(2)

        self._st_rel = StatLine("Shots Made", "0")
        self._st_ses = StatLine("Time Left", self._time_left_val())
        self._st_key = StatLine("Plan", validate(str(self._data.get("discord_id","")))[1] or "—")
        lay.addWidget(self._st_rel)
        lay.addWidget(self._st_ses)
        lay.addWidget(self._st_key)

        lay.addStretch(1)
        return scroll

    # ── DEPRECATED tab pages (kept as no-ops; not wired into the new 3-col body)
    def _pg_shooting(self) -> QtWidgets.QWidget:
        scroll, lay = self._scroll_page()

        card = Card()
        cv = QtWidgets.QVBoxLayout(card)
        cv.setContentsMargins(28, 26, 28, 28); cv.setSpacing(18)

        cv.addWidget(SectionLabel("Shooting"))
        cv.addSpacing(2)

        # threshold_normal — open shots
        self._sl_normal = SliderRow("Open Shot Timing", 50.0, 100.0, decimals=1, unit="%", step=0.5)
        self._sl_normal.setValue(float(self._data.get("timing_value", 95.0)))
        def _sn(x): self._set("timing_value", x); _engine.threshold_normal = x / 100.0
        self._sl_normal.valueChanged.connect(_sn)
        _engine.threshold_normal = self._sl_normal.value() / 100.0
        cv.addWidget(self._sl_normal)

        hint1 = QtWidgets.QLabel("How full the shot meter has to be before the script releases")
        hint1.setFont(ui_font(8)); hint1.setStyleSheet(f"color: {DIM};")
        cv.addWidget(hint1)
        cv.addWidget(_divider())

        # threshold_l2 — L2/contested shots
        self._sl_l2 = SliderRow("Contested Shot Timing", 50.0, 100.0, decimals=1, unit="%", step=0.5)
        self._sl_l2.setValue(float(self._data.get("shot_confidence", 75.0)))
        def _sl(x): self._set("shot_confidence", x); _engine.threshold_l2 = x / 100.0
        self._sl_l2.valueChanged.connect(_sl)
        _engine.threshold_l2 = self._sl_l2.value() / 100.0
        cv.addWidget(self._sl_l2)

        hint2 = QtWidgets.QLabel("Used when you hold L2 (hesitation / no-dip shots)")
        hint2.setFont(ui_font(8)); hint2.setStyleSheet(f"color: {DIM};")
        cv.addWidget(hint2)
        cv.addWidget(_divider())

        # tempo
        self._sl_tempo = SliderRow("Release Delay", 0.0, 200.0, decimals=0, unit="ms", step=1)
        self._sl_tempo.setValue(float(self._data.get("rhythm_tempo_ms", 39)))
        def _stm(x): self._set("rhythm_tempo_ms", x); _engine.tempo_ms = int(x)
        self._sl_tempo.valueChanged.connect(_stm)
        _engine.tempo_ms = int(self._sl_tempo.value())
        cv.addWidget(self._sl_tempo)

        self._tog_tempo = ToggleRow("Use Release Delay", "Pause briefly before each shot fires")
        self._tog_tempo.setChecked(bool(self._data.get("rhythm_enabled", False)))
        def _tt(v): self._set("rhythm_enabled", v); _engine.tempo = v
        self._tog_tempo.toggled.connect(_tt)
        _engine.tempo = bool(self._data.get("rhythm_enabled", False))
        cv.addWidget(self._tog_tempo)

        cv.addWidget(_divider())

        # stick tempo
        self._tog_stick_tempo = ToggleRow(
            "Smart Stick Tempo",
            "Pulls the right stick down before each shot for tempo timing"
        )
        self._tog_stick_tempo.setChecked(bool(self._data.get("stick_tempo_enabled", False)))
        def _stk(v): self._set("stick_tempo_enabled", v); _engine.stick_tempo_enabled = v
        self._tog_stick_tempo.toggled.connect(_stk)
        _engine.stick_tempo_enabled = bool(self._data.get("stick_tempo_enabled", False))
        cv.addWidget(self._tog_stick_tempo)

        cv.addWidget(_divider())

        # quickstop
        self._tog_quickstop = ToggleRow(
            "Quickstop",
            "Times the quickstop footwork before releasing"
        )
        self._tog_quickstop.setChecked(bool(self._data.get("quickstop_enabled", False)))
        def _qs(v): self._set("quickstop_enabled", v); _engine.quickstop_enabled = v
        self._tog_quickstop.toggled.connect(_qs)
        _engine.quickstop_enabled = bool(self._data.get("quickstop_enabled", False))
        cv.addWidget(self._tog_quickstop)

        lay.addWidget(card)
        lay.addStretch(1)
        return scroll

    def _pg_defense(self) -> QtWidgets.QWidget:
        scroll, lay = self._scroll_page()
        card = Card()
        cv = QtWidgets.QVBoxLayout(card)
        cv.setContentsMargins(28, 26, 28, 28); cv.setSpacing(18)
        cv.addWidget(SectionLabel("Defense"))

        tog = ToggleRow("Smart Defense", "Press D-Pad Up while playing to toggle on / off")
        tog.setChecked(bool(self._data.get("defense_enabled", False)))
        def _def(v):
            self._set("defense_enabled", v)
            _engine.defense_enabled = v
        tog.toggled.connect(_def)
        _engine.defense_enabled = bool(self._data.get("defense_enabled", False))
        cv.addWidget(tog)

        cv.addWidget(_divider())
        cv.addWidget(SectionLabel("Smart Defense Helpers"))
        cv.addSpacing(2)

        _sub_map = [
            ("defense_anti_blowby",        "Stay Tight",            "Stops you from over-committing on sprints",                       "defense_anti_blowby"),
            ("defense_auto_hands_up",      "Auto Hands Up",         "Raises your hands the moment Smart Defense kicks in",            "defense_auto_hands_up"),
            ("defense_contest_assist",     "Auto Contest Shots",    "Auto-contests when an opponent shoots near you",                  "defense_contest_assist"),
            ("defense_lateral_boost",      "Quicker Side Steps",    "Sharper side movement to track ballhandlers",                     "defense_lateral_boost"),
            ("defense_sensitivity_boost",  "Sharper Stick Response","More responsive stick for contests and hands-up",                  "defense_sensitivity_boost"),
        ]
        for key, label, hint, attr in _sub_map:
            sub = ToggleRow(label, hint)
            sub.setChecked(bool(self._data.get(key, True)))
            def _on_sub(v, k=key, a=attr):
                self._set(k, v)
                setattr(_engine, a, v)
            sub.toggled.connect(_on_sub)
            setattr(_engine, attr, bool(self._data.get(key, True)))
            cv.addWidget(sub)

        lay.addWidget(card)
        lay.addStretch(1)
        return scroll

    def _pg_features(self) -> QtWidgets.QWidget:
        scroll, lay = self._scroll_page()
        card = Card()
        cv = QtWidgets.QVBoxLayout(card)
        cv.setContentsMargins(28, 26, 28, 28); cv.setSpacing(18)
        cv.addWidget(SectionLabel("Boosts"))

        stam = ToggleRow("Never Run Out of Stamina", "Keeps your stamina from draining out during shots")
        stam.setChecked(bool(self._data.get("infinite_stamina", False)))
        def _stam(v):
            self._set("infinite_stamina", v)
            _engine.infinite_stamina = v
        stam.toggled.connect(_stam)
        _engine.infinite_stamina = bool(self._data.get("infinite_stamina", False))
        cv.addWidget(stam)
        cv.addWidget(_divider())

        lat = ToggleRow("Reduce Lag", "Optimizes your connection for smoother online play")
        lat.setChecked(bool(self._data.get("low_latency", True)))
        def _lat(v):
            self._set("low_latency", v)
            if _NETOPT_OK:
                (_net_apply if v else _net_restore)()
        lat.toggled.connect(_lat)
        if bool(self._data.get("low_latency", True)) and _NETOPT_OK:
            _net_apply()
        cv.addWidget(lat)

        lay.addWidget(card)
        lay.addStretch(1)
        return scroll

    def _pg_stats(self) -> QtWidgets.QWidget:
        scroll, lay = self._scroll_page()
        card = Card()
        cv = QtWidgets.QVBoxLayout(card)
        cv.setContentsMargins(28, 26, 28, 28); cv.setSpacing(18)
        cv.addWidget(SectionLabel("Live stats"))

        grid = QtWidgets.QGridLayout(); grid.setSpacing(12)
        self._st_rel = StatBox("Shots Made", "0")
        self._st_ses = StatBox("Time Left", self._time_left_val())
        self._st_key = StatBox("Plan", validate(str(self._data.get("discord_id","")))[1] or "—")
        grid.addWidget(self._st_rel, 0, 0)
        grid.addWidget(self._st_ses, 0, 1)
        grid.addWidget(self._st_key, 1, 0, 1, 2)
        cv.addLayout(grid)

        lay.addWidget(card)
        lay.addStretch(1)
        return scroll

    # ── stats tick ────────────────────────────────────────────────────────────
    def _tick(self):
        """Refresh scoreboard + AI suggestion."""
        if not hasattr(self, "_st_rel"): return
        running = _engine.is_running() if ENGINE_OK else False
        self._st_rel.setText(str(_engine.shots if running else 0))
        if hasattr(self, "_st_status") and self._st_status:
            self._st_status.setText("RUNNING" if running else "READY")
            self._st_status.setStyleSheet(f"color: {OK if running else TEXT};")

        # AI Vision: green rate + suggestion
        if hasattr(self, "_st_green") and self._st_green:
            if _tracker.shots_total > 0:
                rate = _tracker.green_rate() * 100
                self._st_green.setText(f"{rate:.0f}%")
                self._st_green.setStyleSheet(f"color: {OK};")
            else:
                self._st_green.setText("—")
                self._st_green.setStyleSheet(f"color: {TEXT};")

        if hasattr(self, "_suggest_lbl"):
            current = self._sl_normal.value() if hasattr(self, "_sl_normal") else 95.0
            best, cur_rate, best_rate = _tracker.suggested_threshold(current)
            if _tracker.shots_total < 10:
                self._suggest_lbl.setText(
                    f"AI: collecting data… ({_tracker.shots_total} shots so far)")
                self._suggest_lbl.setStyleSheet(f"color: {DIM};")
                self._suggest_apply.setVisible(False)
            elif best is None:
                self._suggest_lbl.setText(
                    f"AI: this timing is your best ({cur_rate*100:.0f}% green)")
                self._suggest_lbl.setStyleSheet(f"color: {OK};")
                self._suggest_apply.setVisible(False)
            else:
                self._suggest_lbl.setText(
                    f"AI suggests {best:.1f}% — {best_rate*100:.0f}% green vs your {cur_rate*100:.0f}%")
                self._suggest_lbl.setStyleSheet(f"color: {ACCENT};")
                self._suggest_apply.setProperty("target_value", best)
                self._suggest_apply.setVisible(True)

    # ── auth ──────────────────────────────────────────────────────────────────
    def _apply_lock(self):
        ok, dur = validate(str(self._data.get("discord_id", "")))
        if hasattr(self, "_body_stack"):
            self._body_stack.setCurrentIndex(1 if ok else 0)
        # When unlocked, the engine runs continuously while this window is open.
        # LabsEngine controls the lifecycle via spawn / kill; no Start button here.
        if ok and ENGINE_OK and not _engine.is_running():
            _engine.lite_mode = (str(self._data.get("pc_type", "high")).lower() in ("low", "lite"))
            _engine.gpu_index = int(self._data.get("gpu_index", 0))
            _engine.start()
        if hasattr(self, "_key_val"):
            self._key_val.setText(dur if dur else "LOCKED")
            self._key_val.setProperty("state", "active" if ok else "locked")
            self._key_val.style().unpolish(self._key_val)
            self._key_val.style().polish(self._key_val)
        if hasattr(self, "_id_field"):
            border = "rgba(52,211,153,0.45)" if ok else BORDER
            self._id_field.setStyleSheet(f"""
                QLineEdit {{
                    background: {SURF2}; color: {TEXT};
                    border: 1px solid {border};
                    border-radius: 6px; padding: 6px 12px;
                }}
                QLineEdit:focus {{ border-color: {ACCENT}; }}
            """)
        # _st_ses / _st_key are deprecated — license info now lives in the
        # top-bar chip only. Keep this method idempotent.

    # ── settings ──────────────────────────────────────────────────────────────
    def _time_left_val(self) -> str:
        ok, dur = validate(str(self._data.get("discord_id", "")))
        if not ok:
            return "—"
        if dur.upper() == "LIFETIME":
            return "LIFETIME"
        # future: parse expiry timestamp from dur and return days/hours
        return dur

    def _set(self, key: str, value):
        self._data[key] = value
        save_settings(self._data)
        if key == "discord_id":
            self._apply_lock()


def main():
    app = QtWidgets.QApplication(sys.argv)
    app.setStyle("Fusion")
    icon = QtGui.QIcon(str(BANNER)) if BANNER.exists() else QtGui.QIcon()
    app.setWindowIcon(icon)
    win = MainWindow()
    win.setWindowIcon(icon)
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
