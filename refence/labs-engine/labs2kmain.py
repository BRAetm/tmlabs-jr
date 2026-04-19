"""
Labs 2K — TM Labs broadcast-HUD settings window.
Single scrollable page, asymmetric HUD grid, gated by Discord ID.
"""

import os
import sys
import json
from pathlib import Path

from PySide6 import QtCore, QtGui, QtWidgets

ROOT      = Path(__file__).resolve().parent
USERDATA  = Path(os.environ.get("LABS_SETTINGS_ROOT", ROOT / "userdata"))
SETTINGS  = USERDATA / "settings" / "nba2k_settings.current"
METERS    = USERDATA / "settings" / "meters"
BANNER    = USERDATA / "assets" / "tm_labs_banner.png"

# palette — pulled from the TM Labs banner: arena black + electric blue
BG         = "#02040A"
BG_HI      = "#070B16"
PANEL      = "#0A1226"
PANEL_HI   = "#0F1A33"
COURT      = "#0B2952"   # hardwood-floor cool blue tint
LINE       = "#1B2A4A"
TEXT       = "#FFFFFF"
DIM        = "#9DB1D4"
FAINT      = "#4D5E80"
ACCENT     = "#1FA0FF"   # electric core blue from the wordmark
ACCENT_HI  = "#7CCBFF"   # outer glow
ACCENT_DEEP= "#0A4F9C"   # dark stroke / shadow
ACCENT_DIM = "#1B5C99"
ACCENT_2   = ACCENT      # legacy alias — single accent system now
ACCENT_3   = ACCENT
OK         = "#3DD17F"
DANGER     = "#FF3B3B"

# ── auth ──────────────────────────────────────────────────────────────────────
# Single swap surface: replace the body of `validate()` with an HTTP call to
# the VPS. Callers only need (ok, duration) — no other auth knowledge leaks.

_LOCAL_KEYS: dict[str, str] = {"0000": "LIFETIME"}


def validate(discord_id: str) -> tuple[bool, str]:
    """Return (ok, duration) for a Discord ID. Local-stub for now."""
    did = (discord_id or "").strip()
    dur = _LOCAL_KEYS.get(did, "")
    return (bool(dur), dur)


def load_settings() -> dict:
    if SETTINGS.exists():
        try: return json.loads(SETTINGS.read_text())
        except Exception: pass
    return {}


def save_settings(data: dict) -> None:
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(json.dumps(data, indent=2))


# ── fonts ─────────────────────────────────────────────────────────────────────
def pick_font(candidates: list[str], fallback: str = "Segoe UI") -> str:
    fams = set(QtGui.QFontDatabase.families())
    for c in candidates:
        if c in fams: return c
    return fallback


def condensed(size: int, weight: int = 800, tracking: float = 4.0,
              caps: bool = True) -> QtGui.QFont:
    fam = pick_font(["Oswald", "Bebas Neue", "Impact", "Arial Narrow", "Segoe UI"])
    f = QtGui.QFont(fam)
    f.setPointSize(size)
    f.setWeight(QtGui.QFont.Weight(weight))
    if caps: f.setCapitalization(QtGui.QFont.Capitalization.AllUppercase)
    f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, tracking)
    return f


def display(size: int, weight: int = 900, tracking: float = -0.5) -> QtGui.QFont:
    fam = pick_font(["Inter Tight", "Inter", "Manrope", "Sora",
                     "Segoe UI Variable", "Segoe UI"])
    f = QtGui.QFont(fam)
    f.setPointSize(size)
    f.setWeight(QtGui.QFont.Weight(weight))
    f.setLetterSpacing(QtGui.QFont.SpacingType.AbsoluteSpacing, tracking)
    return f


def mono(size: int = 10, weight: int = 700) -> QtGui.QFont:
    fam = pick_font(["JetBrains Mono", "IBM Plex Mono", "Cascadia Mono",
                     "Consolas", "Courier New"])
    f = QtGui.QFont(fam)
    f.setPointSize(size)
    f.setWeight(QtGui.QFont.Weight(weight))
    try: f.setFeature(QtGui.QFont.Tag("tnum"), 1)
    except Exception: pass
    return f


STYLESHEET = f"""
* {{ outline: none; }}
QMainWindow, QWidget {{
    background: {BG};
    color: {TEXT};
    font-family: "Inter", "Segoe UI Variable", "Segoe UI", sans-serif;
    font-size: 10pt;
}}
QLabel {{ color: {TEXT}; background: transparent; }}

QSlider::groove:horizontal {{
    background: {LINE}; height: 4px; border-radius: 2px;
}}
QSlider::sub-page:horizontal {{
    background: {ACCENT}; border-radius: 2px;
}}
QSlider::add-page:horizontal {{ background: {LINE}; border-radius: 2px; }}
QSlider::handle:horizontal {{
    background: {TEXT};
    width: 14px; height: 14px; margin: -6px 0;
    border: 2px solid {ACCENT};
    border-radius: 8px;
}}
QSlider::handle:horizontal:hover {{ border: 2px solid {ACCENT_2}; }}

QCheckBox {{ color: {TEXT}; spacing: 10px; }}
QCheckBox::indicator {{
    width: 16px; height: 16px;
    border: 1px solid {LINE};
    background: {BG_HI};
    border-radius: 4px;
}}
QCheckBox::indicator:hover {{ border: 1px solid {ACCENT}; }}
QCheckBox::indicator:checked {{ background: {ACCENT}; border: 1px solid {ACCENT}; }}

QLineEdit {{
    background: {BG_HI}; color: {TEXT};
    border: 1px solid {LINE};
    border-radius: 8px; padding: 6px 12px;
    selection-background-color: {ACCENT};
}}
QLineEdit:focus {{ border: 1px solid {ACCENT}; }}
QLineEdit:hover {{ border: 1px solid {ACCENT_DIM}; }}

QScrollArea, QScrollArea > QWidget > QWidget {{ background: transparent; border: none; }}
QScrollBar:vertical {{ background: transparent; width: 8px; margin: 4px 2px; }}
QScrollBar::handle:vertical {{ background: {LINE}; min-height: 30px; border-radius: 4px; }}
QScrollBar::handle:vertical:hover {{ background: {ACCENT}; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
"""


# ── hero banner (frameless drag surface) ──────────────────────────────────────
class HeroBanner(QtWidgets.QWidget):
    def __init__(self, height: int = 170, y_bias: float = 0.38):
        super().__init__()
        self.setFixedHeight(height)
        self._pix = QtGui.QPixmap(str(BANNER)) if BANNER.exists() else QtGui.QPixmap()
        self._drag_offset: QtCore.QPoint | None = None
        self._y_bias = y_bias

    def mousePressEvent(self, e):
        if e.button() == QtCore.Qt.MouseButton.LeftButton:
            win = self.window()
            self._drag_offset = e.globalPosition().toPoint() - win.frameGeometry().topLeft()
            e.accept()

    def mouseMoveEvent(self, e):
        if self._drag_offset is not None and e.buttons() & QtCore.Qt.MouseButton.LeftButton:
            self.window().move(e.globalPosition().toPoint() - self._drag_offset)
            e.accept()

    def mouseReleaseEvent(self, e):
        self._drag_offset = None; e.accept()

    def mouseDoubleClickEvent(self, e):
        win = self.window()
        if win.isMaximized(): win.showNormal()
        else: win.showMaximized()

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.SmoothPixmapTransform, True)
        r = self.rect()
        if not self._pix.isNull():
            scaled = self._pix.scaled(r.size(),
                QtCore.Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                QtCore.Qt.TransformationMode.SmoothTransformation)
            ox = (scaled.width() - r.width()) // 2
            oy = int((scaled.height() - r.height()) * self._y_bias)
            p.drawPixmap(r, scaled, QtCore.QRect(ox, oy, r.width(), r.height()))
        else:
            p.fillRect(r, QtGui.QColor(PANEL))
        grad = QtGui.QLinearGradient(0, 0, 0, r.height())
        grad.setColorAt(0.0, QtGui.QColor(0, 0, 0, 0))
        grad.setColorAt(0.75, QtGui.QColor(5, 7, 12, 160))
        grad.setColorAt(1.0, QtGui.QColor(5, 7, 12, 255))
        p.fillRect(r, grad)


class WindowControl(QtWidgets.QAbstractButton):
    def __init__(self, kind: str, parent=None):
        super().__init__(parent)
        self._kind = kind
        self.setFixedSize(42, 30)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor)
        self._hover = False

    def enterEvent(self, _):  self._hover = True; self.update()
    def leaveEvent(self, _):  self._hover = False; self.update()

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, True)
        r = self.rect()
        if self._hover:
            p.fillRect(r, QtGui.QColor(DANGER if self._kind == "close" else ACCENT))
        pen = QtGui.QPen(QtGui.QColor("#000" if self._hover else TEXT))
        pen.setWidth(1); p.setPen(pen)
        cx, cy = r.width() // 2, r.height() // 2
        if self._kind == "min":
            p.drawLine(cx - 6, cy + 4, cx + 6, cy + 4)
        else:
            p.drawLine(cx - 5, cy - 5, cx + 5, cy + 5)
            p.drawLine(cx - 5, cy + 5, cx + 5, cy - 5)


# ── parallelogram broadcast nameplate panel ──────────────────────────────────
def _parallelogram(rect: QtCore.QRectF, slant: int = 18,
                   cuts: tuple[bool, bool, bool, bool] = (True, False, False, True)
                   ) -> QtGui.QPainterPath:
    """Polygon with optional 45-ish-deg cut at TL, TR, BR, BL corners."""
    s = slant
    l, t, r, b = rect.left(), rect.top(), rect.right(), rect.bottom()
    pts: list[QtCore.QPointF] = []
    cut_tl, cut_tr, cut_br, cut_bl = cuts
    # top-left
    if cut_tl: pts += [QtCore.QPointF(l + s, t)]
    else:      pts += [QtCore.QPointF(l, t)]
    # top-right
    if cut_tr: pts += [QtCore.QPointF(r, t), QtCore.QPointF(r - s, t + s if False else t)]
    pts += [QtCore.QPointF(r, t)]
    # bottom-right
    if cut_br: pts += [QtCore.QPointF(r, b - s), QtCore.QPointF(r - s, b)]
    else:      pts += [QtCore.QPointF(r, b)]
    # bottom-left
    if cut_bl: pts += [QtCore.QPointF(l + s, b), QtCore.QPointF(l, b - s)]
    else:      pts += [QtCore.QPointF(l, b)]
    if cut_tl: pts += [QtCore.QPointF(l, t + s)]
    path = QtGui.QPainterPath()
    path.moveTo(pts[0])
    for pt in pts[1:]: path.lineTo(pt)
    path.closeSubpath()
    return path


class HudPanel(QtWidgets.QFrame):
    """Broadcast nameplate: dark fill, electric blue diagonal cut accents top-left
    and bottom-right, thin glowing edge along the cut."""
    def __init__(self, *, accent: str = ACCENT, **_legacy):
        super().__init__()
        self._accent = accent
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_StyledBackground, False)
        self.setMinimumHeight(80)

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, True)
        r = QtCore.QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        slant = 22
        path = _parallelogram(r, slant=slant, cuts=(True, False, True, False))
        # main fill — vertical gradient, deep at top
        grad = QtGui.QLinearGradient(0, r.top(), 0, r.bottom())
        grad.setColorAt(0.0, QtGui.QColor(PANEL_HI))
        grad.setColorAt(0.6, QtGui.QColor(PANEL))
        grad.setColorAt(1.0, QtGui.QColor(BG_HI))
        p.fillPath(path, grad)
        # electric edge highlight on the diagonal cuts only
        glow = QtGui.QPen(QtGui.QColor(self._accent)); glow.setWidthF(2.0)
        p.setPen(glow)
        p.drawLine(QtCore.QPointF(r.left(), r.top() + slant),
                   QtCore.QPointF(r.left() + slant, r.top()))
        p.drawLine(QtCore.QPointF(r.right(), r.bottom() - slant),
                   QtCore.QPointF(r.right() - slant, r.bottom()))
        # hairline rest of border
        pen = QtGui.QPen(QtGui.QColor(LINE)); pen.setWidthF(1.0); p.setPen(pen)
        p.drawPath(path)


def _draw_varsity(p: QtGui.QPainter, text: str, baseline_y: int,
                  size: int = 28, x: int = 0, stroke: float = 3.5):
    """Paint chrome-blue gradient varsity text with a deep-blue outline
    (mimics the TM Labs wordmark on the brand banner)."""
    font = condensed(size, 900, 1.0, caps=True)
    path = QtGui.QPainterPath()
    path.addText(QtCore.QPointF(x, baseline_y), font, text.upper())
    outer = QtGui.QPen(QtGui.QColor(ACCENT_DEEP)); outer.setWidthF(stroke)
    outer.setJoinStyle(QtCore.Qt.PenJoinStyle.RoundJoin)
    p.setPen(outer); p.setBrush(QtCore.Qt.BrushStyle.NoBrush)
    p.drawPath(path)
    grad = QtGui.QLinearGradient(0, baseline_y - size, 0, baseline_y + 6)
    grad.setColorAt(0.0, QtGui.QColor("#FFFFFF"))
    grad.setColorAt(0.45, QtGui.QColor(ACCENT_HI))
    grad.setColorAt(0.55, QtGui.QColor(ACCENT))
    grad.setColorAt(1.0, QtGui.QColor(ACCENT_DEEP))
    p.setPen(QtCore.Qt.PenStyle.NoPen); p.setBrush(QtGui.QBrush(grad))
    p.drawPath(path)


class SectionHeader(QtWidgets.QWidget):
    """Subtitle tag (the tab strip already carries the H1)."""
    def __init__(self, number: str, title: str, tag: str = "",
                 accent: str = ACCENT):
        super().__init__()
        self.setFixedHeight(28)
        self._tag = tag.upper()

    def paintEvent(self, _):
        if not self._tag: return
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, True)
        r = self.rect()
        # accent caret + tag
        p.setBrush(QtGui.QColor(ACCENT)); p.setPen(QtCore.Qt.PenStyle.NoPen)
        tri = QtGui.QPolygonF([
            QtCore.QPointF(0, 8), QtCore.QPointF(8, 14), QtCore.QPointF(0, 20),
        ])
        p.drawPolygon(tri)
        p.setFont(mono(9, 800))
        p.setPen(QtGui.QColor(DIM))
        p.drawText(QtCore.QRect(14, 0, r.width() - 14, r.height()),
                   QtCore.Qt.AlignmentFlag.AlignLeft | QtCore.Qt.AlignmentFlag.AlignVCenter,
                   self._tag)
        # underline rule
        pen = QtGui.QPen(QtGui.QColor(LINE)); pen.setWidthF(1.0); p.setPen(pen)
        p.drawLine(0, r.height() - 1, r.width(), r.height() - 1)


# ── parallelogram tab (broadcast quarter-selector) ──────────────────────────
class TabPara(QtWidgets.QAbstractButton):
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        self.setText(text.upper())
        self.setCheckable(True)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor)
        self.setMinimumHeight(40)
        self.setFont(condensed(11, 900, 4.5))

    def sizeHint(self):
        fm = QtGui.QFontMetrics(self.font())
        return QtCore.QSize(fm.horizontalAdvance(self.text()) + 56, 40)

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, True)
        r = QtCore.QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        slant = 14
        # parallelogram: slanted left+right edges
        poly = QtGui.QPolygonF([
            QtCore.QPointF(r.left() + slant, r.top()),
            QtCore.QPointF(r.right(), r.top()),
            QtCore.QPointF(r.right() - slant, r.bottom()),
            QtCore.QPointF(r.left(), r.bottom()),
        ])
        path = QtGui.QPainterPath(); path.addPolygon(poly); path.closeSubpath()
        active = self.isChecked()
        hover = self.underMouse() and not active
        if active:
            grad = QtGui.QLinearGradient(0, r.top(), 0, r.bottom())
            grad.setColorAt(0.0, QtGui.QColor(ACCENT_HI))
            grad.setColorAt(1.0, QtGui.QColor(ACCENT))
            p.fillPath(path, grad)
            text_color = QtGui.QColor("#02040A")
        else:
            p.fillPath(path, QtGui.QColor(PANEL_HI if hover else PANEL))
            pen = QtGui.QPen(QtGui.QColor(ACCENT_DIM if hover else LINE)); pen.setWidthF(1.0)
            p.setPen(pen); p.drawPath(path)
            text_color = QtGui.QColor(ACCENT_HI if hover else DIM)
        p.setFont(self.font())
        p.setPen(text_color)
        p.drawText(QtCore.QRectF(r.left(), r.top(), r.width(), r.height()),
                   int(QtCore.Qt.AlignmentFlag.AlignCenter), self.text())


# ── reusable rows ─────────────────────────────────────────────────────────────

class SliderRow(QtWidgets.QWidget):
    """One compact row: label on left, slider in middle, tabular value + unit on right."""
    valueChanged = QtCore.Signal(float)

    def __init__(self, label: str, lo: float, hi: float, decimals: int = 1,
                 unit: str = "", step: float = 0.1):
        super().__init__()
        self._dec = decimals
        self._unit = unit
        self._step = step
        h = QtWidgets.QHBoxLayout(self)
        h.setContentsMargins(0, 2, 0, 2); h.setSpacing(10)

        self.lbl = QtWidgets.QLabel(label.upper())
        self.lbl.setFont(condensed(9, 800, 3.0))
        self.lbl.setStyleSheet(f"color: {DIM};")
        self.lbl.setFixedWidth(140)
        h.addWidget(self.lbl)

        self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.slider.setRange(int(lo * 10**decimals), int(hi * 10**decimals))
        self.slider.setSingleStep(max(1, int(step * 10**decimals)))
        self.slider.valueChanged.connect(self._on)
        h.addWidget(self.slider, 1)

        self.val = QtWidgets.QLabel("0")
        self.val.setFont(mono(13, 800))
        self.val.setStyleSheet(f"color: {TEXT};")
        self.val.setMinimumWidth(64)
        self.val.setAlignment(QtCore.Qt.AlignmentFlag.AlignRight | QtCore.Qt.AlignmentFlag.AlignVCenter)
        h.addWidget(self.val)

        self.u = QtWidgets.QLabel(unit)
        self.u.setFont(condensed(9, 700, 2.0))
        self.u.setStyleSheet(f"color: {FAINT};")
        self.u.setFixedWidth(28)
        h.addWidget(self.u)

    def setValue(self, v: float):
        self.slider.setValue(int(round(v * 10**self._dec)))

    def value(self) -> float:
        return self.slider.value() / 10**self._dec

    def _on(self, _):
        v = self.value()
        fmt = f"{{:.{self._dec}f}}"
        self.val.setText(fmt.format(v))
        self.valueChanged.emit(v)


class SwitchRow(QtWidgets.QWidget):
    """Label on left, indicator dot + checkbox on right."""
    toggled = QtCore.Signal(bool)

    def __init__(self, label: str):
        super().__init__()
        h = QtWidgets.QHBoxLayout(self)
        h.setContentsMargins(0, 2, 0, 2); h.setSpacing(10)

        self.lbl = QtWidgets.QLabel(label.upper())
        self.lbl.setFont(condensed(9, 800, 3.0))
        self.lbl.setStyleSheet(f"color: {DIM};")
        h.addWidget(self.lbl, 1)

        self.dot = QtWidgets.QLabel("●")
        self.dot.setFont(QtGui.QFont("Segoe UI", 10))
        self.dot.setStyleSheet(f"color: {FAINT};")
        h.addWidget(self.dot)

        self.box = QtWidgets.QCheckBox()
        self.box.toggled.connect(self._on)
        h.addWidget(self.box)

    def _on(self, v: bool):
        self.dot.setStyleSheet(f"color: {OK if v else FAINT};")
        self.toggled.emit(v)

    def setChecked(self, v: bool):
        self.box.setChecked(bool(v))


class ChipToggle(QtWidgets.QAbstractButton):
    """Flat rectangular chip-toggle for single-choice rows (PC TYPE)."""
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        self.setText(text)
        self.setCheckable(True)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor)
        self.setMinimumHeight(28)
        self.setFont(condensed(9, 800, 3.0))

    def sizeHint(self):
        fm = QtGui.QFontMetrics(self.font())
        return QtCore.QSize(fm.horizontalAdvance(self.text()) + 26, 28)

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, True)
        r = self.rect().adjusted(0, 0, -1, -1)
        if self.isChecked():
            p.fillRect(r, QtGui.QColor(ACCENT))
            p.setPen(QtGui.QColor("#000"))
        else:
            p.fillRect(r, QtGui.QColor(PANEL_HI))
            pen = QtGui.QPen(QtGui.QColor(LINE)); pen.setWidth(1); p.setPen(pen)
            p.drawRect(r)
            p.setPen(QtGui.QColor(DIM if not self.underMouse() else ACCENT_HI))
        p.setFont(self.font())
        p.drawText(r, QtCore.Qt.AlignmentFlag.AlignCenter, self.text())


class EngineButton(QtWidgets.QAbstractButton):
    """Large angular START/STOP button — notched right edge, diagonal accent."""
    def __init__(self):
        super().__init__()
        self._running = False
        self.setMinimumHeight(46)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor)

    def text(self) -> str:
        return "STOP ENGINE" if self._running else "START ENGINE"

    def setRunning(self, v: bool):
        self._running = v; self.update()

    def paintEvent(self, _):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, True)
        r = self.rect().adjusted(0, 0, -1, -1)
        n = 14
        poly = QtGui.QPolygon([
            QtCore.QPoint(r.left(), r.top()),
            QtCore.QPoint(r.right() - n, r.top()),
            QtCore.QPoint(r.right(), r.top() + n),
            QtCore.QPoint(r.right(), r.bottom()),
            QtCore.QPoint(r.left() + n, r.bottom()),
            QtCore.QPoint(r.left(), r.bottom() - n),
        ])
        hover = self.underMouse()
        base = QtGui.QColor(DANGER if self._running else ACCENT)
        if hover: base = base.lighter(115)
        path = QtGui.QPainterPath(); path.addPolygon(QtGui.QPolygonF(poly)); path.closeSubpath()
        p.fillPath(path, base)
        # inner dark stripe for depth
        p.setPen(QtGui.QPen(QtGui.QColor(0, 0, 0, 80), 2))
        p.drawLine(r.left() + 8, r.bottom() - 3, r.right() - 16, r.bottom() - 3)
        p.setPen(QtGui.QColor("#000"))
        p.setFont(condensed(12, 900, 6.0))
        p.drawText(r, QtCore.Qt.AlignmentFlag.AlignCenter, self.text())


class StatReadout(QtWidgets.QWidget):
    def __init__(self, label: str, value: str = "—"):
        super().__init__()
        v = QtWidgets.QVBoxLayout(self)
        v.setContentsMargins(0, 4, 0, 4); v.setSpacing(0)
        lbl = QtWidgets.QLabel(label.upper())
        lbl.setFont(condensed(8, 800, 3.0))
        lbl.setStyleSheet(f"color: {DIM};")
        v.addWidget(lbl)
        self.val = QtWidgets.QLabel(value)
        self.val.setFont(mono(20, 800))
        self.val.setStyleSheet(f"color: {TEXT};")
        v.addWidget(self.val)

    def setValue(self, s: str):
        self.val.setText(s)


def hrule() -> QtWidgets.QFrame:
    f = QtWidgets.QFrame()
    f.setFixedHeight(1)
    f.setStyleSheet(f"background: {LINE};")
    return f


# ── main window ───────────────────────────────────────────────────────────────
class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("TM LABS · 2K")
        self.resize(1040, 800)
        self.setMinimumSize(820, 620)
        self.setWindowFlags(QtCore.Qt.WindowType.FramelessWindowHint | QtCore.Qt.WindowType.Window)
        self.setStyleSheet(STYLESHEET)
        self._data = load_settings()

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        outer = QtWidgets.QVBoxLayout(central)
        outer.setContentsMargins(0, 0, 0, 0); outer.setSpacing(0)

        self._hero = HeroBanner(170, y_bias=0.38)
        outer.addWidget(self._hero)
        outer.addWidget(self._access_strip())

        self.body = QtWidgets.QStackedWidget()
        outer.addWidget(self.body, 1)

        self._locked = self._build_locked_page()
        self.body.addWidget(self._locked)
        self._content = self._build_content()
        self.body.addWidget(self._content)

        # window controls overlay
        self._controls = QtWidgets.QWidget(self)
        ch = QtWidgets.QHBoxLayout(self._controls)
        ch.setContentsMargins(0, 0, 0, 0); ch.setSpacing(0)
        bmin = WindowControl("min"); bmin.clicked.connect(self.showMinimized)
        bcls = WindowControl("close"); bcls.clicked.connect(self.close)
        ch.addWidget(bmin); ch.addWidget(bcls)

        self._apply_lock()
        self._position_controls()

    def resizeEvent(self, e):
        super().resizeEvent(e)
        if hasattr(self, "_controls"): self._position_controls()

    def _position_controls(self):
        self._controls.adjustSize()
        self._controls.move(self.width() - self._controls.width(), 0)
        self._controls.raise_()

    # ---- access strip ----
    def _access_strip(self) -> QtWidgets.QWidget:
        w = QtWidgets.QWidget()
        w.setFixedHeight(36)
        w.setAutoFillBackground(True)
        pal = w.palette()
        pal.setColor(QtGui.QPalette.ColorRole.Window, QtGui.QColor(PANEL))
        w.setPalette(pal)

        lay = QtWidgets.QHBoxLayout(w)
        lay.setContentsMargins(24, 0, 24, 0); lay.setSpacing(12)

        chip = QtWidgets.QLabel("● ACCESS")
        chip.setFont(condensed(8, 900, 3.5))
        chip.setStyleSheet(f"color: #000; background: {ACCENT}; padding: 4px 10px;")
        lay.addWidget(chip)

        lbl = QtWidgets.QLabel("DISCORD ID")
        lbl.setFont(condensed(8, 800, 3.5))
        lbl.setStyleSheet(f"color: {DIM};")
        lay.addWidget(lbl)

        edit = QtWidgets.QLineEdit()
        edit.setPlaceholderText("Enter Discord ID")
        edit.setText(str(self._data.get("discord_id", "")))
        edit.setFont(mono(10))
        edit.setFixedHeight(24)
        edit.setMinimumWidth(220)
        def on_edit(v: str):
            self._set("discord_id", v); self._apply_lock()
        edit.textEdited.connect(on_edit)
        self._discord_edit = edit
        lay.addWidget(edit)

        lay.addStretch(1)

        kl = QtWidgets.QLabel("KEY")
        kl.setFont(condensed(8, 800, 3.5)); kl.setStyleSheet(f"color: {DIM};")
        lay.addWidget(kl)
        self._key_label = QtWidgets.QLabel("—")
        self._key_label.setFont(condensed(10, 900, 5.0))
        self._key_label.setStyleSheet(f"color: {FAINT};")
        lay.addWidget(self._key_label)

        container = QtWidgets.QWidget()
        cl = QtWidgets.QVBoxLayout(container)
        cl.setContentsMargins(0, 0, 0, 0); cl.setSpacing(0)
        cl.addWidget(w); cl.addWidget(hrule())
        return container

    # ---- locked page ----
    def _build_locked_page(self) -> QtWidgets.QWidget:
        w = QtWidgets.QWidget()
        v = QtWidgets.QVBoxLayout(w)
        v.addStretch(1)
        lock = QtWidgets.QLabel("LOCKED")
        lock.setFont(condensed(34, 900, 14.0))
        lock.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        v.addWidget(lock)
        bar = QtWidgets.QFrame(); bar.setFixedSize(96, 3)
        bar.setStyleSheet(f"background: {ACCENT};")
        row = QtWidgets.QHBoxLayout(); row.addStretch(1); row.addWidget(bar); row.addStretch(1)
        v.addLayout(row)
        sub = QtWidgets.QLabel("ENTER DISCORD ID TO UNLOCK")
        sub.setFont(condensed(11, 800, 5.0))
        sub.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        sub.setStyleSheet(f"color: {DIM};")
        v.addWidget(sub); v.addStretch(1)
        return w

    def _auth(self) -> tuple[bool, str]:
        return validate(str(self._data.get("discord_id", "")))

    def _apply_lock(self):
        unlocked, dur = self._auth()
        self.body.setCurrentIndex(1 if unlocked else 0)
        if hasattr(self, "_discord_edit"):
            ok_color = ACCENT if unlocked else LINE
            self._discord_edit.setStyleSheet(
                f"QLineEdit {{ background: {BG}; color: {TEXT}; "
                f"border: 1px solid {ok_color}; border-radius: 0; padding: 4px 10px; }}"
                f"QLineEdit:focus {{ border: 1px solid {ACCENT}; }}"
                f"QLineEdit:hover {{ border: 1px solid {ACCENT_DIM}; }}"
            )
        if hasattr(self, "_key_label"):
            self._key_label.setText(dur or "—")
            self._key_label.setStyleSheet(f"color: {ACCENT_HI if unlocked else FAINT};")

    # ---- content ----
    def _build_content(self) -> QtWidgets.QWidget:
        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)

        inner = QtWidgets.QWidget()
        col = QtWidgets.QVBoxLayout(inner)
        col.setContentsMargins(36, 22, 36, 28); col.setSpacing(16)

        # parallelogram tab strip — broadcast quarter-selector
        tab_row = QtWidgets.QHBoxLayout()
        tab_row.setContentsMargins(0, 0, 0, 0); tab_row.setSpacing(-6)
        labels = ["SHOOTING", "DEFENSE", "FEATURES", "ENGINE", "LIVE STATS"]
        builders = [self._panel_shooting, self._panel_defense,
                    self._panel_features, self._panel_engine, self._panel_stats]
        self._tabs = QtWidgets.QButtonGroup(inner); self._tabs.setExclusive(True)
        self._stack = QtWidgets.QStackedWidget()
        for i, (lbl, build) in enumerate(zip(labels, builders)):
            tab = TabPara(lbl)
            self._tabs.addButton(tab, i); tab_row.addWidget(tab)
            self._stack.addWidget(build())
        tab_row.addStretch(1)
        self._tabs.idClicked.connect(self._stack.setCurrentIndex)
        self._tabs.button(0).setChecked(True)

        col.addLayout(tab_row)
        # accent rule under the tabs
        rule = QtWidgets.QFrame(); rule.setFixedHeight(2)
        rule.setStyleSheet(
            f"background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            f"stop:0 {ACCENT}, stop:0.5 {ACCENT_DEEP}, stop:1 transparent);"
        )
        col.addWidget(rule)
        col.addWidget(self._stack, 1)

        scroll.setWidget(inner)
        return scroll

    def _panel_shooting(self) -> QtWidgets.QWidget:
        panel = HudPanel(accent=ACCENT)
        v = QtWidgets.QVBoxLayout(panel)
        v.setContentsMargins(38, 26, 38, 32); v.setSpacing(10)
        v.addWidget(SectionHeader("01", "SHOOTING", "CALIBRATION", accent=ACCENT))
        v.addSpacing(4)

        rt = SliderRow("FADE TIMING", 0.0, 100.0, decimals=1, unit="%", step=0.1)
        rt.setValue(float(self._data.get("timing_value", 50)))
        rt.valueChanged.connect(lambda x: self._set("timing_value", float(x)))
        v.addWidget(rt)
        cap1 = QtWidgets.QLabel("TIMING FOR FADES")
        cap1.setFont(condensed(8, 700, 2.5)); cap1.setStyleSheet(f"color: {FAINT};")
        v.addWidget(cap1)
        v.addWidget(hrule())

        l2 = SliderRow("NO DIP TIMING", 0.0, 100.0, decimals=1, unit="%", step=0.1)
        l2.setValue(float(self._data.get("shot_confidence", 70)))
        l2.valueChanged.connect(lambda x: self._set("shot_confidence", float(x)))
        v.addWidget(l2)
        cap2 = QtWidgets.QLabel("TIMING FOR NO DIPS")
        cap2.setFont(condensed(8, 700, 2.5)); cap2.setStyleSheet(f"color: {FAINT};")
        v.addWidget(cap2)
        v.addWidget(hrule())

        st = SliderRow("STICK TEMPO", 0.0, 500.0, decimals=0, unit="MS", step=1)
        st.setValue(float(self._data.get("rhythm_tempo_ms", 29)))
        st.valueChanged.connect(lambda x: self._set("rhythm_tempo_ms", float(x)))
        v.addWidget(st)
        tog = SwitchRow("ENABLE TEMPO")
        tog.setChecked(bool(self._data.get("rhythm_enabled", False)))
        tog.toggled.connect(lambda v_: self._set("rhythm_enabled", v_))
        v.addWidget(tog)
        return panel

    def _panel_defense(self) -> QtWidgets.QWidget:
        panel = HudPanel(accent=ACCENT_2)
        v = QtWidgets.QVBoxLayout(panel)
        v.setContentsMargins(38, 26, 38, 32); v.setSpacing(10)
        v.addWidget(SectionHeader("02", "DEFENSE", "D-PAD UP", accent=ACCENT_2))
        v.addSpacing(4)
        tog = SwitchRow("DEFENSE ENABLED")
        tog.setChecked(bool(self._data.get("defense_enabled", False)))
        self._defense_status = QtWidgets.QLabel()
        self._defense_status.setFont(mono(12, 800))
        self._defense_status.setStyleSheet(f"color: {FAINT};")
        self._defense_status.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        def df(val: bool):
            self._set("defense_enabled", val)
            self._defense_status.setText("DEFENSE ON" if val else "DEFENSE OFF")
            self._defense_status.setStyleSheet(f"color: {OK if val else FAINT};")
        tog.toggled.connect(df)
        df(bool(self._data.get("defense_enabled", False)))
        v.addWidget(tog)
        v.addWidget(hrule())
        v.addWidget(self._defense_status)
        hint = QtWidgets.QLabel("PRESS L2 + UP TO TOGGLE IN-GAME")
        hint.setFont(condensed(8, 700, 2.5)); hint.setStyleSheet(f"color: {FAINT};")
        hint.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        v.addWidget(hint)
        return panel

    def _panel_features(self) -> QtWidgets.QWidget:
        panel = HudPanel(accent=ACCENT_3)
        v = QtWidgets.QVBoxLayout(panel)
        v.setContentsMargins(38, 26, 38, 32); v.setSpacing(10)
        v.addWidget(SectionHeader("03", "FEATURES", "TOGGLES", accent=ACCENT_3))
        v.addSpacing(4)

        stam = SwitchRow("INFINITE STAMINA")
        stam.setChecked(bool(self._data.get("infinite_stamina", False)))
        stam.toggled.connect(lambda v_: self._set("infinite_stamina", v_))
        v.addWidget(stam)
        v.addWidget(hrule())

        lat = SwitchRow("LOW LATENCY")
        lat.setChecked(bool(self._data.get("low_latency", True)))
        lat.toggled.connect(lambda v_: self._set("low_latency", v_))
        v.addWidget(lat)
        sub = QtWidgets.QLabel("7 OPTIMIZATIONS ACTIVE")
        sub.setFont(condensed(8, 700, 2.5)); sub.setStyleSheet(f"color: {ACCENT_HI};")
        v.addWidget(sub)
        return panel

    def _panel_engine(self) -> QtWidgets.QWidget:
        panel = HudPanel(accent=ACCENT)
        v = QtWidgets.QVBoxLayout(panel)
        v.setContentsMargins(38, 26, 38, 32); v.setSpacing(12)
        v.addWidget(SectionHeader("04", "ENGINE", "CONTROL", accent=ACCENT))
        v.addSpacing(4)

        # PC type chip row
        row = QtWidgets.QHBoxLayout(); row.setSpacing(8)
        pc_lbl = QtWidgets.QLabel("PC TYPE")
        pc_lbl.setFont(condensed(9, 800, 3.0)); pc_lbl.setStyleSheet(f"color: {DIM};")
        pc_lbl.setFixedWidth(100)
        row.addWidget(pc_lbl)
        lite = ChipToggle("LITE CPU")
        pro  = ChipToggle("PRO GPU")
        group = QtWidgets.QButtonGroup(panel); group.setExclusive(True)
        group.addButton(lite, 0); group.addButton(pro, 1)
        pc = str(self._data.get("pc_type", "lite")).lower()
        (lite if pc != "pro" else pro).setChecked(True)
        lite.toggled.connect(lambda on: on and self._set("pc_type", "lite"))
        pro.toggled.connect(lambda on: on and self._set("pc_type", "pro"))
        row.addWidget(lite); row.addWidget(pro); row.addStretch(1)
        v.addLayout(row)

        # status + button row
        st_row = QtWidgets.QHBoxLayout(); st_row.setSpacing(10)
        st_lbl = QtWidgets.QLabel("STATUS")
        st_lbl.setFont(condensed(9, 800, 3.0)); st_lbl.setStyleSheet(f"color: {DIM};")
        st_lbl.setFixedWidth(100)
        st_row.addWidget(st_lbl)
        self._engine_state = QtWidgets.QLabel("READY")
        self._engine_state.setFont(mono(12, 800))
        self._engine_state.setStyleSheet(f"color: {DANGER};")
        st_row.addWidget(self._engine_state); st_row.addStretch(1)
        v.addLayout(st_row)

        self._engine_btn = EngineButton()
        self._engine_btn.clicked.connect(self._toggle_engine)
        v.addWidget(self._engine_btn)
        return panel

    def _toggle_engine(self):
        self._engine_btn._running = not self._engine_btn._running
        running = self._engine_btn._running
        self._engine_btn.update()
        self._engine_state.setText("RUNNING" if running else "READY")
        self._engine_state.setStyleSheet(f"color: {OK if running else DANGER};")

    def _panel_stats(self) -> QtWidgets.QWidget:
        panel = HudPanel(accent=ACCENT_2)
        v = QtWidgets.QVBoxLayout(panel)
        v.setContentsMargins(38, 26, 38, 32); v.setSpacing(10)
        v.addWidget(SectionHeader("05", "LIVE STATS", "READ-ONLY", accent=ACCENT_2))
        v.addSpacing(6)

        grid = QtWidgets.QGridLayout()
        grid.setHorizontalSpacing(20); grid.setVerticalSpacing(10)
        self._stat_fps = StatReadout("FPS", "--")
        self._stat_rel = StatReadout("RELEASES", "0")
        self._stat_ses = StatReadout("SESSION", "00:00")
        self._stat_key = StatReadout("KEY", validate(str(self._data.get("discord_id", "")))[1] or "—")
        grid.addWidget(self._stat_fps, 0, 0)
        grid.addWidget(self._stat_rel, 0, 1)
        grid.addWidget(self._stat_ses, 1, 0)
        grid.addWidget(self._stat_key, 1, 1)
        v.addLayout(grid)
        return panel

    # ---- save helper ----
    def _set(self, key: str, value):
        self._data[key] = value
        save_settings(self._data)
        if key == "discord_id" and hasattr(self, "_stat_key"):
            self._stat_key.setValue(validate(str(value))[1] or "—")


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
