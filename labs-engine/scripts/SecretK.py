"""
Labs 2K — TM Labs shot-timing tool.
"""

import os, sys, json, threading, time
from pathlib import Path

from PySide6 import QtCore, QtGui, QtWidgets

ROOT     = Path(__file__).resolve().parent
USERDATA = Path(os.environ.get("LABS_SETTINGS_ROOT", ROOT / "userdata"))
SETTINGS = USERDATA / "settings" / "nba2k_settings.current"
BANNER   = USERDATA / "assets" / "tm_labs_banner.png"

# ── palette ───────────────────────────────────────────────────────────────────
BG      = "#0C0E14"
SURFACE = "#12151F"
SURF2   = "#181C29"
BORDER  = "#1F2537"
BORD_HI = "#2C3654"
TEXT    = "#E8EDF5"
DIM     = "#6B7A9B"
FAINT   = "#343D55"
ACCENT  = "#1FA0FF"
ACCDIM  = "#0D3A6B"
OK      = "#22C55E"
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
* {{ outline: none; box-sizing: border-box; }}

QMainWindow, QDialog {{ background: {BG}; color: {TEXT}; }}
QWidget {{ background: transparent; color: {TEXT}; }}

QLabel {{ background: transparent; color: {TEXT}; }}

QScrollArea {{ background: {BG}; border: none; }}
QScrollArea > QWidget > QWidget {{ background: {BG}; }}
QScrollBar:vertical {{
    background: transparent; width: 6px; margin: 0;
}}
QScrollBar::handle:vertical {{
    background: {FAINT}; border-radius: 3px; min-height: 24px;
}}
QScrollBar::handle:vertical:hover {{ background: {BORD_HI}; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}

QLineEdit {{
    background: {SURF2};
    color: {TEXT};
    border: 1px solid {BORDER};
    border-radius: 4px;
    padding: 4px 10px;
    font-size: 10pt;
    selection-background-color: {ACCENT};
}}
QLineEdit:hover {{ border-color: {BORD_HI}; }}
QLineEdit:focus {{ border-color: {ACCENT}; }}
QLineEdit::placeholder {{ color: {DIM}; }}

QSlider::groove:horizontal {{
    background: {FAINT}; height: 3px; border-radius: 2px;
}}
QSlider::sub-page:horizontal {{
    background: {ACCENT}; border-radius: 2px;
}}
QSlider::add-page:horizontal {{ background: {FAINT}; border-radius: 2px; }}
QSlider::handle:horizontal {{
    background: {TEXT};
    width: 12px; height: 12px;
    margin: -5px 0;
    border-radius: 6px;
    border: 2px solid {ACCENT};
}}
QSlider::handle:horizontal:hover {{ background: {ACCENT}; border-color: {TEXT}; }}

QCheckBox {{ spacing: 8px; color: {DIM}; }}
QCheckBox::indicator {{
    width: 15px; height: 15px;
    border: 1px solid {BORDER};
    border-radius: 3px;
    background: {SURF2};
}}
QCheckBox::indicator:hover {{ border-color: {ACCENT}; }}
QCheckBox::indicator:checked {{
    background: {ACCENT};
    border-color: {ACCENT};
    image: url(none);
}}

QPushButton {{
    background: {ACCENT};
    color: #000;
    border: none;
    border-radius: 4px;
    padding: 8px 20px;
    font-size: 10pt;
    font-weight: 600;
}}
QPushButton:hover {{ background: #3fb3ff; }}
QPushButton:pressed {{ background: #0d8ae8; }}
QPushButton:disabled {{ background: {FAINT}; color: {DIM}; }}
"""


# ── engine backend ─────────────────────────────────────────────────────────────
try:
    import sys as _sys
    _sys.path.insert(0, str(ROOT.parent / "cv-scripts"))
    from shot_meter import ShotMeterDetector, find_window
    from features import PSControllerBridge
    import cv2 as _cv2, numpy as _np, ctypes as _ct
    try:
        import bettercam as _bc
        _BC_OK = True
    except ImportError:
        import mss as _mss
        _BC_OK = False
    try:
        from network_optimizer import apply as _net_apply, restore as _net_restore
        _NETOPT_OK = True
    except ImportError:
        _NETOPT_OK = False
    ENGINE_OK = True
except Exception as _e:
    ENGINE_OK = False
    _BC_OK    = False
    _NETOPT_OK = False

if ENGINE_OK:
    class _XGP(_ct.Structure):
        _fields_ = [("wButtons",_ct.c_ushort),("bLeftTrigger",_ct.c_ubyte),
                    ("bRightTrigger",_ct.c_ubyte),("sThumbLX",_ct.c_short),
                    ("sThumbLY",_ct.c_short),("sThumbRX",_ct.c_short),
                    ("sThumbRY",_ct.c_short)]
    class _XS(_ct.Structure):
        _fields_ = [("dwPacketNumber",_ct.c_ulong),("Gamepad",_XGP)]

    def _read_xi(idx=0):
        try:
            s = _XS()
            return s.Gamepad if _ct.windll.xinput1_4.XInputGetState(idx, _ct.byref(s)) == 0 else None
        except Exception: return None
else:
    def _read_xi(idx=0): return None


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
        region   = find_window()
        detector = ShotMeterDetector(self.threshold_normal, self.threshold_l2)
        bridge   = PSControllerBridge()
        fc, tw   = 0, time.perf_counter()

        # XInput passthrough at 500 Hz on side thread
        _pt_run = [True]
        def _passthrough():
            while _pt_run[0]:
                bridge.defense_enabled          = self.defense_enabled
                bridge.infinite_stamina         = self.infinite_stamina
                bridge.anti_blowby              = self.defense_anti_blowby
                bridge.auto_hands_up            = self.defense_auto_hands_up
                bridge.contest_assist           = self.defense_contest_assist
                bridge.lateral_boost            = self.defense_lateral_boost
                bridge.sensitivity_boost        = self.defense_sensitivity_boost
                bridge.stick_tempo_enabled      = self.stick_tempo_enabled
                bridge.quickstop_enabled        = self.quickstop_enabled
                bridge.passthrough(_read_xi())
                if bridge._qs_toggle_requested:
                    self.quickstop_enabled = not self.quickstop_enabled
                    bridge._qs_toggle_requested = False
                time.sleep(1 / 500)
        threading.Thread(target=_passthrough, daemon=True).start()

        reg = (region["left"], region["top"],
               region["left"] + region["width"],
               region["top"]  + region["height"])

        if _BC_OK:
            camera = _bc.create(device_idx=self.gpu_index, output_color="BGR")
            camera.start(region=reg, target_fps=240, video_mode=True)
            try:
                while not self._stop.is_set():
                    bgr = camera.get_latest_frame()
                    if bgr is None:
                        continue
                    gp = _read_xi()
                    l2 = bool(gp and gp.bLeftTrigger > 128)
                    detector.threshold_normal = self.threshold_normal
                    detector.threshold_l2     = self.threshold_l2
                    if detector.check(bgr, l2=l2):
                        if self.defense_enabled:
                            bridge.contest_flick()
                        else:
                            bridge.fire_shot(l2=l2, tempo=self.tempo, tempo_ms=self.tempo_ms)
                        self.shots = detector.shots_fired
                    fc += 1
                    if (t := time.perf_counter()) - tw >= 1.0:
                        self.fps_cur = fc / (t - tw); fc = 0; tw = t
            finally:
                _pt_run[0] = False
                camera.stop()
                camera.release()
        else:
            import mss as _mss
            with _mss.mss() as sct:
                while not self._stop.is_set():
                    t0  = time.perf_counter()
                    bgr = _cv2.cvtColor(_np.asarray(sct.grab(region)), _cv2.COLOR_BGRA2BGR)
                    gp  = _read_xi()
                    l2  = bool(gp and gp.bLeftTrigger > 128)
                    detector.threshold_normal = self.threshold_normal
                    detector.threshold_l2     = self.threshold_l2
                    if detector.check(bgr, l2=l2):
                        if self.defense_enabled:
                            bridge.contest_flick()
                        else:
                            bridge.fire_shot(l2=l2, tempo=self.tempo, tempo_ms=self.tempo_ms)
                        self.shots = detector.shots_fired
                    fc += 1
                    if (t := time.perf_counter()) - tw >= 1.0:
                        self.fps_cur = fc / (t - tw); fc = 0; tw = t
                    wait = 1.0 / self.fps_cap - (time.perf_counter() - t0)
                    if wait > 0: time.sleep(wait)
            _pt_run[0] = False


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
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        self.setText(text)
        self.setCheckable(True)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor)
        self.setFont(label_font(9))
        self.setFixedHeight(36)

    def sizeHint(self):
        fm = QtGui.QFontMetrics(self.font())
        return QtCore.QSize(fm.horizontalAdvance(self.text()) + 32, 36)

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        r = self.rect()
        active = self.isChecked()
        hover  = self.underMouse() and not active
        color  = QtGui.QColor(TEXT if active else (DIM if not hover else "#A0B4D0"))
        p.setFont(self.font())
        p.setPen(color)
        p.drawText(r, QtCore.Qt.AlignmentFlag.AlignCenter, self.text())
        if active:
            p.fillRect(QtCore.QRect(0, r.height()-2, r.width(), 2), QtGui.QColor(ACCENT))

    def enterEvent(self, e): self.update(); super().enterEvent(e)
    def leaveEvent(self, e): self.update(); super().leaveEvent(e)


class Card(QtWidgets.QFrame):
    """Clean bordered surface card."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")
        self.setStyleSheet(f"""
            QFrame#Card {{
                background: {SURFACE};
                border: 1px solid {BORDER};
                border-radius: 6px;
            }}
        """)


class SectionLabel(QtWidgets.QWidget):
    """Left-bar section label."""
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        self.setFixedHeight(22)
        self._text = text.upper()

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        # left accent bar
        p.fillRect(0, 4, 2, 14, QtGui.QColor(ACCENT))
        p.setFont(label_font(8))
        p.setPen(QtGui.QColor(DIM))
        p.drawText(QtCore.QRect(10, 0, self.width()-10, self.height()),
                   QtCore.Qt.AlignmentFlag.AlignLeft | QtCore.Qt.AlignmentFlag.AlignVCenter,
                   self._text)


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


class StatBox(QtWidgets.QWidget):
    def __init__(self, label: str, value: str = "—"):
        super().__init__()
        self.setStyleSheet(f"""
            background: {SURF2};
            border: 1px solid {BORDER};
            border-radius: 4px;
        """)
        v = QtWidgets.QVBoxLayout(self)
        v.setContentsMargins(12, 10, 12, 10); v.setSpacing(2)

        self._lbl = QtWidgets.QLabel(label.upper())
        self._lbl.setFont(label_font(7))
        self._lbl.setStyleSheet(f"background: transparent; color: {DIM}; border: none;")
        v.addWidget(self._lbl)

        self._val = QtWidgets.QLabel(value)
        self._val.setFont(mono_font(18, 700))
        self._val.setStyleSheet(f"background: transparent; color: {TEXT}; border: none;")
        v.addWidget(self._val)

    def setValue(self, s: str): self._val.setText(s)


class EngBtn(QtWidgets.QPushButton):
    def __init__(self):
        super().__init__("START ENGINE")
        self._running = False
        self.setFixedHeight(42)
        self.setFont(label_font(10))
        self._update_style()

    def setRunning(self, v: bool):
        self._running = v
        self.setText("STOP ENGINE" if v else "START ENGINE")
        self._update_style()

    def _update_style(self):
        if self._running:
            self.setStyleSheet(f"""
                QPushButton {{
                    background: {DANGER}; color: #fff;
                    border: none; border-radius: 4px;
                    font-size: 10pt; font-weight: 700; letter-spacing: 1.5px;
                }}
                QPushButton:hover {{ background: #f87171; }}
                QPushButton:pressed {{ background: #dc2626; }}
            """)
        else:
            self.setStyleSheet(f"""
                QPushButton {{
                    background: {ACCENT}; color: #000;
                    border: none; border-radius: 4px;
                    font-size: 10pt; font-weight: 700; letter-spacing: 1.5px;
                }}
                QPushButton:hover {{ background: #3fb3ff; }}
                QPushButton:pressed {{ background: #0d8ae8; }}
                QPushButton:disabled {{ background: {FAINT}; color: {DIM}; }}
            """)


def _divider() -> QtWidgets.QFrame:
    f = QtWidgets.QFrame()
    f.setFixedHeight(1)
    f.setStyleSheet(f"background: {BORDER};")
    return f


# ── main window ───────────────────────────────────────────────────────────────
class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("TM LABS · 2K")
        self.resize(760, 620)
        self.setMinimumSize(700, 540)
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
        bar.setFixedHeight(44)
        bar.setStyleSheet(f"background: {SURFACE}; border-bottom: 1px solid {BORDER};")
        h = QtWidgets.QHBoxLayout(bar)
        h.setContentsMargins(20, 0, 120, 0); h.setSpacing(16)

        id_lbl = QtWidgets.QLabel("DISCORD ID")
        id_lbl.setFont(label_font(8))
        id_lbl.setStyleSheet(f"color: {DIM};")
        h.addWidget(id_lbl)

        self._id_field = QtWidgets.QLineEdit()
        self._id_field.setPlaceholderText("Enter your Discord ID")
        self._id_field.setText(str(self._data.get("discord_id", "")))
        self._id_field.setFixedHeight(28)
        self._id_field.setMinimumWidth(200)
        self._id_field.setMaximumWidth(280)
        self._id_field.textEdited.connect(lambda v: (self._set("discord_id", v), self._apply_lock()))
        h.addWidget(self._id_field)

        h.addStretch(1)

        # key block — "KEY" label stacked above the value
        key_block = QtWidgets.QWidget()
        key_block.setStyleSheet("background: transparent;")
        kv = QtWidgets.QVBoxLayout(key_block)
        kv.setContentsMargins(0, 4, 0, 4); kv.setSpacing(0)

        key_lbl = QtWidgets.QLabel("KEY")
        key_lbl.setFont(label_font(7))
        key_lbl.setStyleSheet(f"color: {DIM};")
        kv.addWidget(key_lbl)

        self._key_val = QtWidgets.QLabel("—")
        self._key_val.setFont(label_font(11))
        self._key_val.setStyleSheet(f"color: {FAINT};")
        kv.addWidget(self._key_val)

        h.addWidget(key_block)

        return bar

    # ── body (tabs + pages) ───────────────────────────────────────────────────
    def _build_body(self) -> QtWidgets.QWidget:
        w = QtWidgets.QWidget()
        w.setStyleSheet(f"background: {BG};")
        v = QtWidgets.QVBoxLayout(w)
        v.setContentsMargins(0, 0, 0, 0); v.setSpacing(0)

        # tab row
        tab_bar = QtWidgets.QWidget()
        tab_bar.setFixedHeight(36)
        tab_bar.setStyleSheet(f"background: {SURFACE}; border-bottom: 1px solid {BORDER};")
        th = QtWidgets.QHBoxLayout(tab_bar)
        th.setContentsMargins(20, 0, 0, 0); th.setSpacing(0)

        pages    = ["Shooting", "Defense", "Features", "Engine", "Live Stats"]
        builders = [self._pg_shooting, self._pg_defense,
                    self._pg_features, self._pg_engine, self._pg_stats]

        self._tabs  = QtWidgets.QButtonGroup(w); self._tabs.setExclusive(True)
        self._stack = QtWidgets.QStackedWidget()
        self._stack.setStyleSheet(f"background: {BG};")

        for i, (lbl, build) in enumerate(zip(pages, builders)):
            t = TabBtn(lbl); self._tabs.addButton(t, i); th.addWidget(t)
            self._stack.addWidget(build())
        th.addStretch(1)
        self._tabs.idClicked.connect(self._stack.setCurrentIndex)
        self._tabs.button(0).setChecked(True)

        v.addWidget(tab_bar)
        v.addWidget(self._stack, 1)

        # stack the whole body in a stacked widget for locked/unlocked
        self._body_stack = QtWidgets.QStackedWidget()
        self._body_stack.setStyleSheet(f"background: {BG};")
        self._body_stack.addWidget(self._build_locked())
        self._body_stack.addWidget(w)
        return self._body_stack

    # ── locked screen ──────────────────────────────────────────────────────────
    def _build_locked(self) -> QtWidgets.QWidget:
        w = QtWidgets.QWidget()
        w.setStyleSheet(f"background: {BG};")
        v = QtWidgets.QVBoxLayout(w)
        v.addStretch(1)

        icon = QtWidgets.QLabel("⬡")
        icon.setFont(ui_font(28)); icon.setStyleSheet(f"color: {FAINT};")
        icon.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        v.addWidget(icon)

        t = QtWidgets.QLabel("Enter your Discord ID to unlock")
        t.setFont(ui_font(11)); t.setStyleSheet(f"color: {DIM};")
        t.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        v.addWidget(t)

        v.addStretch(1)
        return w

    # ── pages ──────────────────────────────────────────────────────────────────
    def _scroll_page(self) -> tuple[QtWidgets.QScrollArea, QtWidgets.QVBoxLayout]:
        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)
        scroll.setStyleSheet(f"background: {BG};")
        inner = QtWidgets.QWidget()
        inner.setStyleSheet(f"background: {BG};")
        lay = QtWidgets.QVBoxLayout(inner)
        lay.setContentsMargins(16, 14, 16, 16); lay.setSpacing(12)
        scroll.setWidget(inner)
        return scroll, lay

    def _pg_shooting(self) -> QtWidgets.QWidget:
        scroll, lay = self._scroll_page()

        card = Card()
        cv = QtWidgets.QVBoxLayout(card)
        cv.setContentsMargins(20, 18, 20, 20); cv.setSpacing(14)

        cv.addWidget(SectionLabel("Shot Timing"))
        cv.addSpacing(2)

        # threshold_normal
        self._sl_normal = SliderRow("Fade timing", 50.0, 100.0, decimals=1, unit="%", step=0.5)
        self._sl_normal.setValue(float(self._data.get("timing_value", 95.0)))
        def _sn(x): self._set("timing_value", x); _engine.threshold_normal = x / 100.0
        self._sl_normal.valueChanged.connect(_sn)
        _engine.threshold_normal = self._sl_normal.value() / 100.0
        cv.addWidget(self._sl_normal)

        hint1 = QtWidgets.QLabel("Release point for fades — meter fill % before R2 fires")
        hint1.setFont(ui_font(8)); hint1.setStyleSheet(f"color: {DIM};")
        cv.addWidget(hint1)
        cv.addWidget(_divider())

        # threshold_l2
        self._sl_l2 = SliderRow("No-dip timing", 50.0, 100.0, decimals=1, unit="%", step=0.5)
        self._sl_l2.setValue(float(self._data.get("shot_confidence", 75.0)))
        def _sl(x): self._set("shot_confidence", x); _engine.threshold_l2 = x / 100.0
        self._sl_l2.valueChanged.connect(_sl)
        _engine.threshold_l2 = self._sl_l2.value() / 100.0
        cv.addWidget(self._sl_l2)

        hint2 = QtWidgets.QLabel("Release point while L2 is held (no-dip shots)")
        hint2.setFont(ui_font(8)); hint2.setStyleSheet(f"color: {DIM};")
        cv.addWidget(hint2)
        cv.addWidget(_divider())

        # tempo
        self._sl_tempo = SliderRow("Tempo delay", 0.0, 200.0, decimals=0, unit="ms", step=1)
        self._sl_tempo.setValue(float(self._data.get("rhythm_tempo_ms", 39)))
        def _stm(x): self._set("rhythm_tempo_ms", x); _engine.tempo_ms = int(x)
        self._sl_tempo.valueChanged.connect(_stm)
        _engine.tempo_ms = int(self._sl_tempo.value())
        cv.addWidget(self._sl_tempo)

        self._tog_tempo = ToggleRow("Enable tempo delay", "Adds fixed delay before releasing on fades")
        self._tog_tempo.setChecked(bool(self._data.get("rhythm_enabled", False)))
        def _tt(v): self._set("rhythm_enabled", v); _engine.tempo = v
        self._tog_tempo.toggled.connect(_tt)
        _engine.tempo = bool(self._data.get("rhythm_enabled", False))
        cv.addWidget(self._tog_tempo)

        cv.addWidget(_divider())

        # stick tempo
        self._tog_stick_tempo = ToggleRow(
            "Stick tempo",
            f"RS DOWN flick for tempo_ms ({_engine.tempo_ms}ms) then R2 — exact ZP stick_tempo sequence"
        )
        self._tog_stick_tempo.setChecked(bool(self._data.get("stick_tempo_enabled", False)))
        def _stk(v): self._set("stick_tempo_enabled", v); _engine.stick_tempo_enabled = v
        self._tog_stick_tempo.toggled.connect(_stk)
        _engine.stick_tempo_enabled = bool(self._data.get("stick_tempo_enabled", False))
        cv.addWidget(self._tog_stick_tempo)

        cv.addWidget(_divider())

        # quickstop
        self._tog_quickstop = ToggleRow(
            "Quickstop assist",
            "RS pull-down (20ms) + pause (15ms) before R2 — times quickstop footwork release"
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
        cv.setContentsMargins(20, 18, 20, 20); cv.setSpacing(14)
        cv.addWidget(SectionLabel("Defense"))

        tog = ToggleRow("Defense mode", "L2 + D-pad up to toggle in-game")
        tog.setChecked(bool(self._data.get("defense_enabled", False)))
        def _def(v):
            self._set("defense_enabled", v)
            _engine.defense_enabled = v
        tog.toggled.connect(_def)
        _engine.defense_enabled = bool(self._data.get("defense_enabled", False))
        cv.addWidget(tog)

        cv.addWidget(_divider())
        cv.addWidget(SectionLabel("Sub-features"))
        cv.addSpacing(2)

        _sub_map = [
            ("defense_anti_blowby",        "Anti-blowby",        "Caps sprint R2 to 0.40 -- prevents over-committing",        "defense_anti_blowby"),
            ("defense_auto_hands_up",      "Auto hands up",      "RS-up flick when you toggle into defense mode",              "defense_auto_hands_up"),
            ("defense_contest_assist",     "Contest assist",     "Auto RS-up (80ms) when shot meter fires on defense",         "defense_contest_assist"),
            ("defense_lateral_boost",      "Lateral boost",      "Lateral RS x1.20 for tighter ballhandler tracking",          "defense_lateral_boost"),
            ("defense_sensitivity_boost",  "Sensitivity boost",  "Vertical RS x1.30 for shot contest / hands-up response",     "defense_sensitivity_boost"),
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
        cv.setContentsMargins(20, 18, 20, 20); cv.setSpacing(14)
        cv.addWidget(SectionLabel("Toggles"))

        stam = ToggleRow("Infinite stamina", "Scales R2 to 0.70× — prevents drain without breaking shots")
        stam.setChecked(bool(self._data.get("infinite_stamina", False)))
        def _stam(v):
            self._set("infinite_stamina", v)
            _engine.infinite_stamina = v
        stam.toggled.connect(_stam)
        _engine.infinite_stamina = bool(self._data.get("infinite_stamina", False))
        cv.addWidget(stam)
        cv.addWidget(_divider())

        lat = ToggleRow("Low latency mode", "7 network optimizations")
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

    def _pg_engine(self) -> QtWidgets.QWidget:
        scroll, lay = self._scroll_page()

        # status card
        s_card = Card()
        sc = QtWidgets.QHBoxLayout(s_card)
        sc.setContentsMargins(20, 16, 20, 16); sc.setSpacing(20)

        dot_col = QtWidgets.QVBoxLayout(); dot_col.setSpacing(2)
        self._eng_dot = QtWidgets.QLabel("●")
        self._eng_dot.setFont(ui_font(14))
        self._eng_dot.setStyleSheet(f"color: {FAINT};")
        dot_col.addWidget(self._eng_dot)
        sc.addLayout(dot_col)

        txt_col = QtWidgets.QVBoxLayout(); txt_col.setSpacing(2)
        self._eng_status = QtWidgets.QLabel("Ready")
        self._eng_status.setFont(ui_font(13, 600))
        self._eng_status.setStyleSheet(f"color: {TEXT};")
        txt_col.addWidget(self._eng_status)
        self._eng_sub = QtWidgets.QLabel("Engine idle — click Start Engine to begin")
        self._eng_sub.setFont(ui_font(9))
        self._eng_sub.setStyleSheet(f"color: {DIM};")
        txt_col.addWidget(self._eng_sub)
        sc.addLayout(txt_col, 1)

        lay.addWidget(s_card)

        # controls card
        c_card = Card()
        cc = QtWidgets.QVBoxLayout(c_card)
        cc.setContentsMargins(20, 18, 20, 20); cc.setSpacing(14)
        cc.addWidget(SectionLabel("Control"))

        # PC type row
        pc_row = QtWidgets.QHBoxLayout(); pc_row.setSpacing(8)
        pc_lbl = QtWidgets.QLabel("PC type")
        pc_lbl.setFont(label_font(8)); pc_lbl.setStyleSheet(f"color: {DIM};")
        pc_row.addWidget(pc_lbl)
        pc_row.addStretch(1)

        self._pc_lite = QtWidgets.QRadioButton("Lite  CPU")
        self._pc_pro  = QtWidgets.QRadioButton("Pro  GPU")
        for rb in (self._pc_lite, self._pc_pro):
            rb.setFont(label_font(8))
            rb.setStyleSheet(f"color: {DIM}; spacing: 6px;")
            pc_row.addWidget(rb)
        pc = str(self._data.get("pc_type", "lite")).lower()
        (self._pc_lite if pc != "pro" else self._pc_pro).setChecked(True)
        self._pc_lite.toggled.connect(lambda on: on and self._set("pc_type", "lite"))
        self._pc_pro.toggled.connect(lambda on: on and self._set("pc_type", "pro"))
        cc.addLayout(pc_row)

        # GPU index (BetterCam device_idx)
        gpu_row = QtWidgets.QHBoxLayout(); gpu_row.setSpacing(8)
        gpu_lbl = QtWidgets.QLabel("Capture GPU")
        gpu_lbl.setFont(label_font(8)); gpu_lbl.setStyleSheet(f"color: {DIM};")
        gpu_row.addWidget(gpu_lbl)
        gpu_row.addStretch(1)
        self._gpu_spin = QtWidgets.QSpinBox()
        self._gpu_spin.setRange(0, 3)
        self._gpu_spin.setValue(int(self._data.get("gpu_index", 0)))
        self._gpu_spin.setFixedWidth(52)
        self._gpu_spin.setStyleSheet(f"""
            QSpinBox {{
                background: {SURF2}; color: {TEXT};
                border: 1px solid {BORDER}; border-radius: 4px;
                padding: 2px 6px; font-size: 10pt;
            }}
            QSpinBox::up-button, QSpinBox::down-button {{ width: 16px; }}
        """)
        def _gpu(v): self._set("gpu_index", v); _engine.gpu_index = v
        self._gpu_spin.valueChanged.connect(_gpu)
        _engine.gpu_index = int(self._data.get("gpu_index", 0))
        gpu_row.addWidget(self._gpu_spin)
        cc.addLayout(gpu_row)
        cc.addWidget(_divider())

        self._eng_btn = EngBtn()
        self._eng_btn.setEnabled(ENGINE_OK)
        self._eng_btn.clicked.connect(self._toggle_engine)
        cc.addWidget(self._eng_btn)

        if not ENGINE_OK:
            w = QtWidgets.QLabel("opencv-python, mss, or vgamepad not installed")
            w.setFont(ui_font(8)); w.setStyleSheet(f"color: {WARN};")
            cc.addWidget(w)

        lay.addWidget(c_card)
        lay.addStretch(1)
        return scroll

    def _pg_stats(self) -> QtWidgets.QWidget:
        scroll, lay = self._scroll_page()
        card = Card()
        cv = QtWidgets.QVBoxLayout(card)
        cv.setContentsMargins(20, 18, 20, 20); cv.setSpacing(14)
        cv.addWidget(SectionLabel("Live stats"))

        grid = QtWidgets.QGridLayout(); grid.setSpacing(8)
        self._st_fps = StatBox("FPS", "--")
        self._st_rel = StatBox("Releases", "0")
        self._st_ses = StatBox("Time Left", self._time_left_val())
        self._st_key = StatBox("License", validate(str(self._data.get("discord_id","")))[1] or "—")
        grid.addWidget(self._st_fps, 0, 0)
        grid.addWidget(self._st_rel, 0, 1)
        grid.addWidget(self._st_ses, 1, 0)
        grid.addWidget(self._st_key, 1, 1)
        cv.addLayout(grid)

        lay.addWidget(card)
        lay.addStretch(1)
        return scroll

    # ── engine toggle ─────────────────────────────────────────────────────────
    def _toggle_engine(self):
        if not ENGINE_OK: return
        if _engine.is_running():
            _engine.stop()
            self._set_eng_ui(False)
        else:
            _engine.start()
            self._set_eng_ui(True)

    def _set_eng_ui(self, running: bool):
        self._eng_btn.setRunning(running)
        self._eng_dot.setStyleSheet(f"color: {OK if running else FAINT};")
        self._eng_status.setText("Running" if running else "Ready")
        self._eng_sub.setText(
            "Capturing screen — engine active" if running
            else "Engine idle — click Start Engine to begin"
        )

    # ── stats tick ────────────────────────────────────────────────────────────
    def _tick(self):
        if not hasattr(self, "_st_fps"): return
        if _engine.is_running():
            self._st_fps.setValue(f"{_engine.fps_cur:.0f}")
            self._st_rel.setValue(str(_engine.shots))
            s = int(time.perf_counter() - _engine.start_t)
            self._st_ses.setValue(self._time_left_val())
            if not self._eng_btn._running:
                self._set_eng_ui(True)
        elif self._eng_btn._running:
            self._set_eng_ui(False)

    # ── auth ──────────────────────────────────────────────────────────────────
    def _apply_lock(self):
        ok, dur = validate(str(self._data.get("discord_id", "")))
        if hasattr(self, "_body_stack"):
            self._body_stack.setCurrentIndex(1 if ok else 0)
        if hasattr(self, "_key_val"):
            self._key_val.setText(dur if dur else "—")
            self._key_val.setStyleSheet(
                f"color: {ACCENT}; letter-spacing: 2px;" if ok
                else f"color: {FAINT};"
            )
        if hasattr(self, "_id_field"):
            self._id_field.setStyleSheet(f"""
                QLineEdit {{
                    background: {SURF2}; color: {TEXT};
                    border: 1px solid {"rgba(31,160,255,0.4)" if ok else BORDER};
                    border-radius: 4px; padding: 4px 10px;
                }}
                QLineEdit:focus {{ border-color: {ACCENT}; }}
            """)
        if hasattr(self, "_st_key"):
            self._st_key.setValue(dur or "—")
        if hasattr(self, "_st_ses"):
            val = self._time_left_val()
            self._st_ses.setValue(val)
            is_life = val == "LIFETIME"
            self._st_ses._val.setFont(label_font(10) if is_life else mono_font(18, 700))
            self._st_ses._val.setStyleSheet(
                f"background: transparent; border: none; color: {ACCENT}; letter-spacing: 2px;"
                if is_life else
                f"background: transparent; border: none; color: {TEXT};"
            )

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
