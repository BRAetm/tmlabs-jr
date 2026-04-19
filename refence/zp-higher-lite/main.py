"""
ZP HIGHER LITE – PySide6 UI  (fully interactive)
"""

import sys, os, json, random
# Ensure the project root is on sys.path so 'core' is always importable
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QLabel, QPushButton,
    QSlider, QHBoxLayout, QVBoxLayout, QFrame,
    QLineEdit, QRadioButton, QButtonGroup, QCheckBox,
)
from PySide6.QtGui import QFont, QFontDatabase, QIcon, QPainter, QColor
from PySide6.QtCore import Qt, QPoint, QTimer

ASSETS = os.path.join(os.path.dirname(__file__), "assets")
PROFILE_DIR = os.path.join(os.path.dirname(__file__), "profiles")
os.makedirs(PROFILE_DIR, exist_ok=True)

BG = "#070d14"
PANEL = "#0b1420"
ACCENT = "#00b8ff"
ACCENT2 = "#00d4ff"
TEXT_HI = "#ffffff"
TEXT_MED = "#a0c8e0"
TEXT_DIM = "#3a6080"
BTN_DARK = "#0d1e2e"
BTN_BORD = "#1a3a55"


def load_fonts():
    for f in [
        "Aquire.otf", "AquireBold.otf", "AquireLight.otf",
        "Poppins-Regular.ttf", "Poppins-Bold.ttf",
        "Poppins-Light.ttf", "Poppins-Medium.ttf", "Poppins-SemiBold.ttf",
    ]:
        QFontDatabase.addApplicationFont(os.path.join(ASSETS, f))


def aquire(size=10, bold=False):
    return QFont("AquireBold" if bold else "Aquire", size)


def poppins(size=9, weight=QFont.Weight.Normal):
    return QFont("Poppins", size, weight)


class HRule(QFrame):
    def __init__(self):
        super().__init__()
        self.setFrameShape(QFrame.Shape.HLine)
        self.setStyleSheet(f"color:{TEXT_DIM}; background:{TEXT_DIM};")
        self.setFixedHeight(1)


class VRule(QFrame):
    def __init__(self):
        super().__init__()
        self.setFrameShape(QFrame.Shape.VLine)
        self.setStyleSheet(f"color:{TEXT_DIM}; background:{TEXT_DIM};")
        self.setFixedWidth(1)


class SectionHeader(QLabel):
    def __init__(self, text):
        super().__init__(text)
        self.setFont(aquire(11, bold=True))
        self.setStyleSheet(f"color:{ACCENT2}; letter-spacing:3px;")
        self.setAlignment(Qt.AlignmentFlag.AlignHCenter)


class SmallLabel(QLabel):
    def __init__(self, text, color=TEXT_MED, size=8):
        super().__init__(text)
        self.setFont(poppins(size))
        self.setStyleSheet(f"color:{color}; letter-spacing:1.5px;")
        self.setAlignment(Qt.AlignmentFlag.AlignHCenter)


class BigValue(QLabel):
    def __init__(self, text):
        super().__init__(text)
        self.setFont(aquire(26, bold=True))
        self.setStyleSheet(f"color:{ACCENT}; letter-spacing:2px;")
        self.setAlignment(Qt.AlignmentFlag.AlignHCenter)


class StepBtn(QPushButton):
    def __init__(self, text):
        super().__init__(text)
        self.setFixedSize(34, 28)
        self.setFont(poppins(8))
        self.setStyleSheet(
            f"QPushButton{{background:{BTN_DARK};color:{TEXT_MED};"
            f"border:1px solid {BTN_BORD};border-radius:4px;}}"
            f"QPushButton:hover{{border-color:{ACCENT};color:{TEXT_HI};}}"
        )


class RecalBtn(QPushButton):
    def __init__(self):
        super().__init__("RECAL")
        self.setFixedHeight(24)
        self.setFont(poppins(7))
        self.setStyleSheet(
            f"QPushButton{{background:{BTN_DARK};color:{TEXT_MED};"
            f"border:1px solid {BTN_BORD};border-radius:3px;padding:0 6px;}}"
            f"QPushButton:hover{{border-color:{ACCENT};color:{TEXT_HI};}}"
        )


class CyanSlider(QSlider):
    def __init__(self, lo=0, hi=100, val=36):
        super().__init__(Qt.Orientation.Horizontal)
        self.setRange(lo, hi)
        self.setValue(val)
        self.setStyleSheet(
            f"QSlider::groove:horizontal{{height:3px;background:{TEXT_DIM};border-radius:1px;}}"
            f"QSlider::sub-page:horizontal{{background:{ACCENT};border-radius:1px;}}"
            f"QSlider::handle:horizontal{{background:{ACCENT};border:2px solid {BG};"
            "width:14px;height:14px;margin:-6px 0px;border-radius:7px;}}"
        )


class ToggleCheckBox(QCheckBox):
    def __init__(self, label="ENABLED"):
        super().__init__(label)
        self.setFont(poppins(9, QFont.Weight.Bold))
        self.setStyleSheet(
            f"QCheckBox{{color:{TEXT_MED};spacing:8px;}}"
            f"QCheckBox::indicator{{width:22px;height:22px;border:2px solid {ACCENT};"
            "border-radius:4px;background:transparent;}}"
            f"QCheckBox::indicator:checked{{background:{ACCENT};image:none;}}"
        )


class CyanButton(QPushButton):
    def __init__(self, text, height=34):
        super().__init__(text)
        self.setFixedHeight(height)
        self.setFont(poppins(9, QFont.Weight.Bold))
        self.set_active_style(True)

    def set_active_style(self, active):
        if active:
            self.setStyleSheet(
                f"QPushButton{{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                "stop:0 #0070cc,stop:1 #004a99);"
                f"color:{TEXT_HI};border:1px solid {ACCENT};border-radius:5px;}}"
                "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                "stop:0 #0090ee,stop:1 #0060bb);}"
            )
        else:
            self.setStyleSheet(
                "QPushButton{background:#1a0a0a;color:#ff6666;border:1px solid #772222;"
                "border-radius:5px;}"
                "QPushButton:hover{background:#2a0a0a;}"
            )


class ProfileBtn(QPushButton):
    def __init__(self, text):
        super().__init__(text)
        self.setFixedSize(38, 28)
        self.setFont(poppins(8, QFont.Weight.Bold))
        self.set_active(False)

    def set_active(self, active):
        if active:
            self.setStyleSheet(
                f"QPushButton{{background:#0d2a40;color:{ACCENT};"
                f"border:1px solid {ACCENT};border-radius:4px;}}"
            )
        else:
            self.setStyleSheet(
                f"QPushButton{{background:{BTN_DARK};color:{TEXT_MED};"
                f"border:1px solid {BTN_BORD};border-radius:4px;}}"
                f"QPushButton:hover{{border-color:{ACCENT};color:{TEXT_HI};}}"
            )


class DarkLineEdit(QLineEdit):
    def __init__(self, placeholder="", readonly=False):
        super().__init__()
        self.setPlaceholderText(placeholder)
        self.setFixedHeight(30)
        self.setFont(poppins(9))
        self.setReadOnly(readonly)
        self.setStyleSheet(
            f"QLineEdit{{background:#0a1620;color:{TEXT_MED};"
            f"border:1px solid {BTN_BORD};border-radius:4px;padding:0 8px;}}"
            f"QLineEdit:focus{{border-color:{ACCENT};}}"
        )


class DotBackground(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(0, 120, 180, 30))
        for x in range(0, self.width(), 22):
            for y in range(0, self.height(), 22):
                p.drawEllipse(QPoint(x, y), 1, 1)
        p.end()


class ShootingColumn(QWidget):
    DEFAULT_RT = 36
    DEFAULT_L2 = 36
    DEFAULT_TEMPO = 39

    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12, 16, 12, 16)
        lay.setSpacing(6)

        lay.addWidget(SectionHeader("SHOOTING"))
        lay.addSpacing(4)

        lay.addWidget(SmallLabel("RELEASE TRIGGER", TEXT_MED, 8))
        self.rt_val = BigValue("36.0%")
        lay.addWidget(self.rt_val)

        row1 = QHBoxLayout()
        row1.setSpacing(4)
        self.rt_minus2 = StepBtn("-2")
        self.rt_minus05 = StepBtn("-.5")
        self.rt_plus05 = StepBtn("+.5")
        self.rt_plus2 = StepBtn("+2")
        self.rt_slider = CyanSlider(0, 100, self.DEFAULT_RT)
        row1.addWidget(self.rt_minus2)
        row1.addWidget(self.rt_minus05)
        row1.addWidget(self.rt_slider, 1)
        row1.addWidget(self.rt_plus05)
        row1.addWidget(self.rt_plus2)
        lay.addLayout(row1)

        hint1 = QHBoxLayout()
        hint1.addWidget(SmallLabel("TAKE 4 SHOTS (HOLD, DON'T RELEASE)", TEXT_DIM, 7), 1)
        self.rt_recal = RecalBtn()
        hint1.addWidget(self.rt_recal)
        lay.addLayout(hint1)

        lay.addSpacing(10)
        lay.addWidget(HRule())
        lay.addSpacing(10)

        lay.addWidget(SmallLabel("L2 / CONTESTED", TEXT_MED, 8))
        self.l2_val = BigValue("36.0%")
        lay.addWidget(self.l2_val)

        row2 = QHBoxLayout()
        row2.setSpacing(4)
        self.l2_minus2 = StepBtn("-2")
        self.l2_minus05 = StepBtn("-.5")
        self.l2_plus05 = StepBtn("+.5")
        self.l2_plus2 = StepBtn("+2")
        self.l2_slider = CyanSlider(0, 100, self.DEFAULT_L2)
        row2.addWidget(self.l2_minus2)
        row2.addWidget(self.l2_minus05)
        row2.addWidget(self.l2_slider, 1)
        row2.addWidget(self.l2_plus05)
        row2.addWidget(self.l2_plus2)
        lay.addLayout(row2)

        hint2 = QHBoxLayout()
        hint2.addWidget(SmallLabel("TAKE 4 L2/LT SHOTS (HOLD, DON'T RELEASE)", TEXT_DIM, 7), 1)
        self.l2_recal = RecalBtn()
        hint2.addWidget(self.l2_recal)
        lay.addLayout(hint2)

        lay.addSpacing(10)
        lay.addWidget(HRule())
        lay.addSpacing(10)

        lay.addWidget(SmallLabel("STICK TEMPO", TEXT_MED, 8))
        trow = QHBoxLayout()
        self.tempo_chk = ToggleCheckBox()
        self.tempo_chk.setChecked(True)
        self.tempo_state = QLabel("ON")
        self.tempo_state.setFont(poppins(9, QFont.Weight.Bold))
        self.tempo_state.setStyleSheet(f"color:{ACCENT}; letter-spacing:3px;")
        trow.addWidget(self.tempo_chk)
        trow.addWidget(self.tempo_state)
        trow.addStretch()
        lay.addLayout(trow)

        self.tempo_val = BigValue("39ms")
        lay.addWidget(self.tempo_val)

        tslide = QHBoxLayout()
        tslide.setSpacing(4)
        self.tempo_minus = StepBtn("-")
        self.tempo_minus.setFixedWidth(26)
        self.tempo_slider = CyanSlider(10, 100, self.DEFAULT_TEMPO)
        self.tempo_plus = StepBtn("+")
        self.tempo_plus.setFixedWidth(26)
        tslide.addWidget(self.tempo_minus)
        tslide.addWidget(self.tempo_slider, 1)
        tslide.addWidget(self.tempo_plus)
        lay.addLayout(tslide)

        lay.addStretch()
        self._connect()

    def _connect(self):
        self.rt_slider.valueChanged.connect(lambda v: self.rt_val.setText(f"{v}.0%"))
        self.l2_slider.valueChanged.connect(lambda v: self.l2_val.setText(f"{v}.0%"))
        self.tempo_slider.valueChanged.connect(lambda v: self.tempo_val.setText(f"{v}ms"))

        self.rt_minus2.clicked.connect(lambda: self._step(self.rt_slider, -2))
        self.rt_minus05.clicked.connect(lambda: self._step(self.rt_slider, -1))
        self.rt_plus05.clicked.connect(lambda: self._step(self.rt_slider, 1))
        self.rt_plus2.clicked.connect(lambda: self._step(self.rt_slider, 2))
        self.rt_recal.clicked.connect(lambda: self.rt_slider.setValue(self.DEFAULT_RT))

        self.l2_minus2.clicked.connect(lambda: self._step(self.l2_slider, -2))
        self.l2_minus05.clicked.connect(lambda: self._step(self.l2_slider, -1))
        self.l2_plus05.clicked.connect(lambda: self._step(self.l2_slider, 1))
        self.l2_plus2.clicked.connect(lambda: self._step(self.l2_slider, 2))
        self.l2_recal.clicked.connect(lambda: self.l2_slider.setValue(self.DEFAULT_L2))

        self.tempo_minus.clicked.connect(lambda: self._step(self.tempo_slider, -1))
        self.tempo_plus.clicked.connect(lambda: self._step(self.tempo_slider, 1))
        self.tempo_chk.stateChanged.connect(self._tempo_changed)

    def _step(self, slider, delta):
        slider.setValue(max(slider.minimum(), min(slider.maximum(), slider.value() + delta)))

    def _tempo_changed(self, state):
        on = state == Qt.CheckState.Checked.value
        self.tempo_state.setText("ON" if on else "OFF")
        self.tempo_state.setStyleSheet(f"color:{ACCENT if on else '#666666'}; letter-spacing:3px;")
        self.tempo_slider.setEnabled(on)
        self.tempo_minus.setEnabled(on)
        self.tempo_plus.setEnabled(on)
        self.tempo_val.setStyleSheet(f"color:{ACCENT if on else '#666666'}; letter-spacing:2px;")

    def get_state(self):
        return {
            "rt": self.rt_slider.value(),
            "l2": self.l2_slider.value(),
            "tempo": self.tempo_slider.value(),
            "tempo_on": self.tempo_chk.isChecked(),
        }

    def set_state(self, data):
        self.rt_slider.setValue(data.get("rt", self.DEFAULT_RT))
        self.l2_slider.setValue(data.get("l2", self.DEFAULT_L2))
        self.tempo_slider.setValue(data.get("tempo", self.DEFAULT_TEMPO))
        self.tempo_chk.setChecked(data.get("tempo_on", True))


class FeaturesColumn(QWidget):
    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12, 16, 12, 16)
        lay.setSpacing(8)

        lay.addWidget(SectionHeader("FEATURES"))
        lay.addSpacing(8)

        lay.addWidget(SmallLabel("INFINITE STAMINA", TEXT_MED, 8))
        self.stamina_chk = ToggleCheckBox()
        lay.addWidget(self.stamina_chk, alignment=Qt.AlignmentFlag.AlignHCenter)

        lay.addSpacing(12)
        lay.addWidget(HRule())
        lay.addSpacing(12)

        lay.addWidget(SmallLabel("DEFENSE", TEXT_MED, 8))
        def_row = QHBoxLayout()
        self.def_chk = ToggleCheckBox()
        self.def_chk.setChecked(True)
        self.def_state = QLabel("ON")
        self.def_state.setFont(poppins(9, QFont.Weight.Bold))
        self.def_state.setStyleSheet(f"color:{ACCENT}; letter-spacing:3px;")
        def_row.addWidget(self.def_chk)
        def_row.addWidget(self.def_state)
        def_row.addStretch()
        lay.addLayout(def_row)

        self.def_btn = QPushButton("DEFENSE OFF")
        self.def_btn.setFixedHeight(36)
        self.def_btn.setFont(poppins(9, QFont.Weight.Bold))
        self._style_def_btn(True)
        lay.addWidget(self.def_btn)
        lay.addWidget(SmallLabel("D-PAD UP TO TOGGLE ON / OFF.", TEXT_DIM, 7))

        lay.addSpacing(12)
        lay.addWidget(HRule())
        lay.addSpacing(12)

        lay.addWidget(SmallLabel("LOW LATENCY", TEXT_MED, 8))
        ll_row = QHBoxLayout()
        self.ll_chk = ToggleCheckBox()
        self.ll_chk.setChecked(True)
        self.ll_state = QLabel("ON")
        self.ll_state.setFont(poppins(9, QFont.Weight.Bold))
        self.ll_state.setStyleSheet(f"color:{ACCENT}; letter-spacing:3px;")
        ll_row.addWidget(self.ll_chk)
        ll_row.addWidget(self.ll_state)
        ll_row.addStretch()
        lay.addLayout(ll_row)

        lay.addStretch()
        self._connect()

    def _style_def_btn(self, on):
        if on:
            self.def_btn.setStyleSheet(
                f"QPushButton{{background:#111c26;color:{TEXT_MED};border:1px solid {BTN_BORD};"
                "border-radius:4px;}}"
                f"QPushButton:hover{{border-color:{ACCENT};color:{TEXT_HI};}}"
            )
        else:
            self.def_btn.setStyleSheet(
                "QPushButton{background:#132410;color:#6fff7b;border:1px solid #2f6a35;"
                "border-radius:4px;}"
            )

    def _connect(self):
        self.def_chk.stateChanged.connect(self._def_changed)
        self.ll_chk.stateChanged.connect(self._ll_changed)
        self.def_btn.clicked.connect(lambda: self.def_chk.setChecked(not self.def_chk.isChecked()))

    def _def_changed(self, state):
        on = state == Qt.CheckState.Checked.value
        self.def_state.setText("ON" if on else "OFF")
        self.def_state.setStyleSheet(f"color:{ACCENT if on else '#666666'}; letter-spacing:3px;")
        self.def_btn.setText("DEFENSE OFF" if on else "DEFENSE ON")
        self._style_def_btn(on)

    def _ll_changed(self, state):
        on = state == Qt.CheckState.Checked.value
        self.ll_state.setText("ON" if on else "OFF")
        self.ll_state.setStyleSheet(f"color:{ACCENT if on else '#666666'}; letter-spacing:3px;")

    def get_state(self):
        return {
            "stamina": self.stamina_chk.isChecked(),
            "defense": self.def_chk.isChecked(),
            "latency": self.ll_chk.isChecked(),
        }

    def set_state(self, data):
        self.stamina_chk.setChecked(data.get("stamina", False))
        self.def_chk.setChecked(data.get("defense", True))
        self.ll_chk.setChecked(data.get("latency", True))


class SystemColumn(QWidget):
    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12, 16, 12, 16)
        lay.setSpacing(8)

        lay.addWidget(SectionHeader("SYSTEM"))
        lay.addSpacing(4)

        lay.addWidget(SmallLabel("CAPTURE SOURCE", TEXT_MED, 8))
        self.capture_edit = DarkLineEdit()
        lay.addWidget(self.capture_edit)

        gpu_row = QHBoxLayout()
        gpu_lbl = SmallLabel("GPU", TEXT_MED, 8)
        gpu_lbl.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        gpu_row.addWidget(gpu_lbl)
        self.gpu_edit = DarkLineEdit(readonly=True)
        self.gpu_edit.setText("NVIDIA GeForce RTX 5050")
        gpu_row.addWidget(self.gpu_edit, 1)

        self.scan_btn = QPushButton("SCAN")
        self.scan_btn.setFixedHeight(30)
        self.scan_btn.setFont(poppins(8, QFont.Weight.Bold))
        self.scan_btn.setStyleSheet(
            f"QPushButton{{background:{BTN_DARK};color:{ACCENT};border:1px solid {ACCENT};"
            "border-radius:4px;padding:0 10px;}}"
            "QPushButton:hover{background:#0d2033;}"
        )
        gpu_row.addWidget(self.scan_btn)
        lay.addLayout(gpu_row)

        lay.addSpacing(6)
        lay.addWidget(HRule())
        lay.addSpacing(8)

        lay.addWidget(SmallLabel("PC TYPE", TEXT_MED, 8))
        radio_style = (
            f"QRadioButton{{color:{TEXT_HI};spacing:8px;}}"
            f"QRadioButton::indicator{{width:16px;height:16px;border:2px solid {ACCENT};border-radius:8px;}}"
            f"QRadioButton::indicator:checked{{background:{ACCENT};border-color:{ACCENT};}}"
        )
        self.pc_group = QButtonGroup(self)
        self.lite_radio = QRadioButton("Lite Mode (CPU)")
        self.lite_radio.setFont(poppins(9))
        self.lite_radio.setChecked(True)
        self.lite_radio.setStyleSheet(radio_style)
        self.gpu_radio = QRadioButton("GPU Mode (NVIDIA)")
        self.gpu_radio.setFont(poppins(9))
        self.gpu_radio.setStyleSheet(radio_style)
        self.pc_group.addButton(self.lite_radio, 0)
        self.pc_group.addButton(self.gpu_radio, 1)
        lay.addWidget(self.lite_radio, alignment=Qt.AlignmentFlag.AlignHCenter)
        self.pc_hint = SmallLabel("Optimised for laptops & PCs without NVIDIA GPU", TEXT_DIM, 7)
        lay.addWidget(self.pc_hint)
        lay.addWidget(self.gpu_radio, alignment=Qt.AlignmentFlag.AlignHCenter)

        lay.addSpacing(8)
        lay.addWidget(HRule())
        lay.addSpacing(8)

        lay.addWidget(SmallLabel("ENGINE CONTROL", TEXT_MED, 8))
        self.status_lbl = QLabel("READY")
        self.status_lbl.setFont(aquire(10, bold=True))
        self.status_lbl.setStyleSheet(f"color:{ACCENT2}; letter-spacing:4px;")
        self.status_lbl.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        lay.addWidget(self.status_lbl)

        self.start_btn = CyanButton("START ENGINE", height=44)
        lay.addWidget(self.start_btn)

        lay.addSpacing(8)
        lay.addWidget(HRule())
        lay.addSpacing(8)

        lay.addWidget(SmallLabel("LIVE STATS", TEXT_MED, 8))
        fps_row = QHBoxLayout()
        fps_name = SmallLabel("FPS", TEXT_MED, 8)
        fps_name.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        self.fps_val = QLabel("--")
        self.fps_val.setFont(poppins(11))
        self.fps_val.setStyleSheet(f"color:{ACCENT};")
        fps_row.addWidget(fps_name)
        fps_row.addStretch()
        fps_row.addWidget(self.fps_val)
        lay.addLayout(fps_row)

        rel_row = QHBoxLayout()
        rel_name = SmallLabel("RELEASES", TEXT_MED, 8)
        rel_name.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        self.rel_val = QLabel("0")
        self.rel_val.setFont(poppins(11))
        self.rel_val.setStyleSheet(f"color:{TEXT_MED};")
        rel_row.addWidget(rel_name)
        rel_row.addStretch()
        rel_row.addWidget(self.rel_val)
        lay.addLayout(rel_row)

        lay.addStretch()

        self.running = False
        self.releases = 0
        self.current_engine = None
        self._get_shooting_state = None  # injected by MainWindow
        self.timer = QTimer(self)
        self.timer.setInterval(500)
        self.timer.timeout.connect(self._tick)
        self._connect()

    def _connect(self):
        self.scan_btn.clicked.connect(self._scan)
        self.start_btn.clicked.connect(self._toggle_engine)
        self.pc_group.idToggled.connect(self._pc_changed)

    def _scan(self):
        self.scan_btn.setText("...")
        self.scan_btn.setEnabled(False)
        QTimer.singleShot(800, self._finish_scan)

    def _finish_scan(self):
        self.scan_btn.setText("SCAN")
        self.scan_btn.setEnabled(True)
        self.gpu_edit.setText("NVIDIA GeForce RTX 5050")

    def _toggle_engine(self):
        """Toggles the AI engine and updates UI. Includes safe engine lifecycle management."""
        self.running = not self.running

        if self.running:
            # Local import avoids circular dependency at module load time
            from core.engine import Engine
            config = self._get_shooting_state() if callable(self._get_shooting_state) else {}
            self.current_engine = Engine(config)
            self.current_engine.start()

            # UI – running state
            self.start_btn.setText("STOP ENGINE")
            self.start_btn.set_active_style(False)
            self.status_lbl.setText("RUNNING")
            self.status_lbl.setStyleSheet("color:#44ff88; letter-spacing:4px;")
            self.releases = 0
            self.rel_val.setText("0")
            self.timer.start()
        else:
            # Safely shut down the engine
            if self.current_engine is not None:
                self.current_engine.stop()
                self.current_engine = None
            self.timer.stop()

            # UI – ready state
            self.start_btn.setText("START ENGINE")
            self.start_btn.set_active_style(True)
            self.status_lbl.setText("READY")
            self.status_lbl.setStyleSheet(f"color:{ACCENT2}; letter-spacing:4px;")
            self.fps_val.setText("--")

    def _tick(self):
        self.fps_val.setText(str(random.randint(55, 62)))
        self.releases += random.randint(0, 2)
        self.rel_val.setText(str(self.releases))
        self.rel_val.setStyleSheet(f"color:{ACCENT};")

    def _pc_changed(self, button_id, checked):
        if not checked:
            return
        if button_id == 0:
            self.pc_hint.setText("Optimised for laptops & PCs without NVIDIA GPU")
        else:
            self.pc_hint.setText("Full GPU acceleration — requires NVIDIA CUDA driver")

    def get_state(self):
        return {
            "capture": self.capture_edit.text(),
            "gpu": self.gpu_edit.text(),
            "pc_mode": self.pc_group.checkedId(),
        }

    def set_state(self, data):
        self.capture_edit.setText(data.get("capture", ""))
        self.gpu_edit.setText(data.get("gpu", "NVIDIA GeForce RTX 5050"))
        mode = data.get("pc_mode", 0)
        btn = self.pc_group.button(mode)
        if btn:
            btn.setChecked(True)


class TitleBar(QWidget):
    def __init__(self, parent, shooting, features, system):
        super().__init__(parent)
        self.parent_window = parent
        self.shooting = shooting
        self.features = features
        self.system = system
        self.drag_pos = None
        self.active_profile = 1
        self.setFixedHeight(50)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(16, 0, 16, 0)
        lay.setSpacing(0)

        title = QLabel("ZP HIGHER LITE")
        title.setFont(aquire(14, bold=True))
        title.setStyleSheet(f"color:{ACCENT2}; letter-spacing:4px;")
        pipe = QLabel(" | ")
        pipe.setFont(aquire(14))
        pipe.setStyleSheet(f"color:{TEXT_DIM};")
        subtitle = QLabel("PLAYSTATION")
        subtitle.setFont(poppins(9))
        subtitle.setStyleSheet(f"color:{TEXT_MED}; letter-spacing:4px;")
        lay.addWidget(title)
        lay.addWidget(pipe)
        lay.addWidget(subtitle)
        lay.addStretch()

        self.profile_buttons = {}
        for i in [1, 2, 3]:
            btn = ProfileBtn(f"P{i}")
            btn.clicked.connect(lambda _, idx=i: self.load_profile(idx))
            lay.addWidget(btn)
            lay.addSpacing(4)
            self.profile_buttons[i] = btn
        self.profile_buttons[1].set_active(True)

        lay.addSpacing(8)
        self.save_btn = CyanButton("SAVE", height=28)
        self.save_btn.setMinimumWidth(70)
        self.save_btn.clicked.connect(self.save_profile)
        lay.addWidget(self.save_btn)
        lay.addSpacing(16)

        for sym, fn in [("─", parent.showMinimized), ("✕", parent.close)]:
            btn = QPushButton(sym)
            btn.setFixedSize(28, 28)
            btn.setFont(poppins(9))
            btn.setStyleSheet(
                f"QPushButton{{background:transparent;color:{TEXT_MED};border:none;}}"
                f"QPushButton:hover{{color:{TEXT_HI};}}"
            )
            btn.clicked.connect(fn)
            lay.addWidget(btn)

    def profile_path(self, idx):
        return os.path.join(PROFILE_DIR, f"profile_{idx}.json")

    def save_profile(self):
        state = {
            "shooting": self.shooting.get_state(),
            "features": self.features.get_state(),
            "system": self.system.get_state(),
        }
        with open(self.profile_path(self.active_profile), "w", encoding="utf-8") as f:
            json.dump(state, f, indent=2)
        self.save_btn.setText("SAVED!")
        self.save_btn.setStyleSheet(
            "QPushButton{background:#103018;color:#44ff88;border:1px solid #44ff88;border-radius:5px;}"
        )
        QTimer.singleShot(900, self._restore_save_btn)

    def _restore_save_btn(self):
        self.save_btn.setText("SAVE")
        self.save_btn.set_active_style(True)

    def load_profile(self, idx):
        self.active_profile = idx
        for i, btn in self.profile_buttons.items():
            btn.set_active(i == idx)
        path = self.profile_path(idx)
        if not os.path.exists(path):
            return
        with open(path, "r", encoding="utf-8") as f:
            state = json.load(f)
        self.shooting.set_state(state.get("shooting", {}))
        self.features.set_state(state.get("features", {}))
        self.system.set_state(state.get("system", {}))

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.drag_pos = event.globalPosition().toPoint()

    def mouseMoveEvent(self, event):
        if self.drag_pos and event.buttons() == Qt.MouseButton.LeftButton:
            delta = event.globalPosition().toPoint() - self.drag_pos
            self.parent_window.move(self.parent_window.pos() + delta)
            self.drag_pos = event.globalPosition().toPoint()

    def mouseReleaseEvent(self, event):
        self.drag_pos = None


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint | Qt.WindowType.Window)
        self.setMinimumSize(1180, 700)
        self.resize(1180, 700)
        ico = os.path.join(ASSETS, "ZP.ico")
        if os.path.exists(ico):
            self.setWindowIcon(QIcon(ico))
        self.setWindowTitle("ZP HIGHER LITE")

        root = QWidget()
        root.setStyleSheet(f"QWidget{{background:{BG};}}")
        self.setCentralWidget(root)

        root_lay = QVBoxLayout(root)
        root_lay.setContentsMargins(0, 0, 0, 0)
        root_lay.setSpacing(0)

        self.dots = DotBackground(root)
        self.dots.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)

        self.shooting = ShootingColumn()
        self.features = FeaturesColumn()
        self.system = SystemColumn()
        # Give SystemColumn access to shooting state for Engine config
        self.system._get_shooting_state = self.shooting.get_state

        title_bar = TitleBar(self, self.shooting, self.features, self.system)
        title_bar.setStyleSheet(f"background:{PANEL}; border-bottom:1px solid {TEXT_DIM};")
        root_lay.addWidget(title_bar)

        body = QWidget()
        body_lay = QHBoxLayout(body)
        body_lay.setContentsMargins(0, 0, 0, 0)
        body_lay.setSpacing(0)

        body_lay.addWidget(self.shooting, 1)
        body_lay.addWidget(VRule())
        body_lay.addWidget(self.features, 1)
        body_lay.addWidget(VRule())
        body_lay.addWidget(self.system, 1)
        root_lay.addWidget(body, 1)

        self._sync_dots()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._sync_dots()

    def showEvent(self, event):
        super().showEvent(event)
        self._sync_dots()

    def _sync_dots(self):
        self.dots.setGeometry(0, 0, self.width(), self.height())
        self.dots.raise_()


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("ZP HIGHER LITE")
    load_fonts()
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
