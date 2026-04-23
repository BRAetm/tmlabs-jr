#include "LabsTheme.h"
#include "SettingsManager.h"

#include <QApplication>
#include <QPalette>
#include <QWidget>

namespace Labs {

static QString hex(const QColor& c)
{
    return c.name(QColor::HexRgb);
}

static QColor tweak(const QColor& c, int dL)  // lighten/darken by HSL lightness
{
    int h, s, l, a; c.getHsl(&h, &s, &l, &a);
    l = qBound(0, l + dL, 255);
    return QColor::fromHsl(h, s, l, a);
}

void labsThemeSave(const LabsThemeData& t, SettingsManager* settings)
{
    if (!settings) return;
    settings->setValue("theme/preset",   t.preset);
    settings->setValue("theme/bg",       hex(t.bg));
    settings->setValue("theme/bgSubtle", hex(t.bgSubtle));
    settings->setValue("theme/surface",  hex(t.surface));
    settings->setValue("theme/accent",   hex(t.accent));
    settings->setValue("theme/text",     hex(t.text));
    settings->setValue("theme/imagePath",    t.imagePath);
    settings->setValue("theme/imageOpacity", t.imageOpacity);
    settings->sync();
}

LabsThemeData labsThemeLoad(SettingsManager* settings)
{
    LabsThemeData t;
    if (!settings) return t;
    t.preset = settings->value("theme/preset", t.preset).toString();

    // Seed from the named preset, then override with any user-customized slots.
    t = labsThemeFromPreset(t.preset);

    const auto readColor = [&](const char* key, QColor& out) {
        const QString s = settings->value(QString::fromUtf8(key)).toString();
        if (!s.isEmpty()) out = QColor(s);
    };
    readColor("theme/bg",       t.bg);
    readColor("theme/bgSubtle", t.bgSubtle);
    readColor("theme/surface",  t.surface);
    readColor("theme/accent",   t.accent);
    readColor("theme/text",     t.text);
    t.imagePath    = settings->value("theme/imagePath").toString();
    t.imageOpacity = settings->value("theme/imageOpacity", 1.0).toDouble();
    return t;
}

QString labsStylesheet(const LabsThemeData& t)
{
    // Also update QApplication palette so palette-driven widgets (like the
    // logo) retint when theme changes.
    if (qApp) {
        QPalette p = qApp->palette();
        p.setColor(QPalette::Window,          t.bg);
        p.setColor(QPalette::WindowText,      t.text);
        p.setColor(QPalette::Base,            t.bgSubtle);
        p.setColor(QPalette::AlternateBase,   t.surface);
        p.setColor(QPalette::Text,            t.text);
        p.setColor(QPalette::Button,          t.surface);
        p.setColor(QPalette::ButtonText,      t.text);
        p.setColor(QPalette::Highlight,       t.accent);
        p.setColor(QPalette::HighlightedText, t.text);
        qApp->setPalette(p);
        // Force re-paint on already-visible widgets (logo, etc.).
        for (QWidget* w : qApp->allWidgets()) w->update();
    }

    // In image mode, use rgba() for the panel colors so the background image
    // bleeds through rails and cards. Color helpers return rgba strings.
    auto colorCss = [&](const QColor& c, int alpha = 255) -> QString {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue())
            .arg(QString::number(alpha / 255.0, 'f', 3));
    };

    const bool imageMode = t.imageActive();
    const QString bg        = imageMode ? colorCss(t.bg,       0)    : hex(t.bg);
    const QString bgSubtle  = imageMode ? colorCss(t.bgSubtle, 0xC8) : hex(t.bgSubtle);
    const QString surface   = imageMode ? colorCss(t.surface,  0xDC) : hex(t.surface);
    const QString surfaceH  = imageMode ? colorCss(tweak(t.surface, +12), 0xEC) : hex(tweak(t.surface, +12));
    const QString accent    = hex(t.accent);
    const QString accentH   = hex(tweak(t.accent,  -20));
    const QString accentLt  = hex(tweak(t.accent,  +22));
    const QString text      = hex(t.text);
    const QString textMuted = hex(tweak(t.text, -60));
    const QString textDim   = hex(tweak(t.text, -90));
    const QString border    = hex(tweak(t.surface, +20));
    const QString borderBr  = hex(tweak(t.surface, +40));

    // Build the stylesheet. Kept compact; same rules as the hardcoded version.
    return QString(R"(
* { font-family: "Segoe UI Variable Text","Segoe UI",Arial; font-size: 13px; color: %TEXT%; }
QMainWindow, QWidget { background: %BG%; color: %TEXT%; }
QMainWindow::separator { background: %BORDER%; width: 1px; height: 1px; }

QToolBar { background: %BGSUB%; border: none; border-bottom: 1px solid %BORDER%; spacing: 6px; padding: 6px 10px; }
QToolBar QLabel { color: %TEXTMUTED%; padding: 0 4px; }
QToolBar::separator { background: %BORDER%; width: 1px; margin: 4px 6px; }

QToolButton, QPushButton {
    background: %SURFACE%; color: %TEXT%;
    border: 1px solid %BORDER%; border-radius: 6px;
    padding: 6px 14px; min-height: 22px; font-weight: 500;
}
QToolButton:hover, QPushButton:hover { background: %SURFACEH%; border-color: %BORDERBR%; }
QToolButton:pressed, QPushButton:pressed { background: %BGSUB%; }
QToolButton:disabled, QPushButton:disabled { color: %TEXTDIM%; background: %BGSUB%; border-color: %SURFACE%; }
QToolButton:checked { background: %ACCENTH%; border-color: %ACCENT%; color: %TEXT%; }

/* Hero accent — the one button that owns the screen (Start Engine) */
QPushButton[accent="true"] {
    background: %ACCENT%; color: white;
    border: none; border-radius: 8px;
    padding: 0 28px; min-height: 38px; font-weight: 700;
    font-size: 13px; letter-spacing: 0.6px;
}
QPushButton[accent="true"]:hover    { background: %ACCENTLT%; }
QPushButton[accent="true"]:pressed  { background: %ACCENTH%; }
QPushButton[accent="true"]:disabled { background: %BGSUB%; color: %TEXTDIM%; }

/* Danger — for stop button when something is running */
QPushButton[danger="true"] {
    background: #B91C1C; color: white;
    border: none; border-radius: 8px;
    padding: 0 28px; min-height: 38px; font-weight: 700;
    font-size: 13px; letter-spacing: 0.6px;
}
QPushButton[danger="true"]:hover   { background: #DC2626; }
QPushButton[danger="true"]:pressed { background: #991B1B; }

/* Ghost — secondary actions (browse, theme, settings) */
QPushButton[ghost="true"] {
    background: transparent; color: %TEXTMUTED%;
    border: 1px solid %BORDER%; border-radius: 6px;
    padding: 6px 14px; min-height: 26px; font-weight: 500;
}
QPushButton[ghost="true"]:hover { color: %TEXT%; border-color: %BORDERBR%; }

/* Segmented control halves (Lite | Pro) */
QPushButton[segLeft="true"], QPushButton[segRight="true"] {
    background: %BGSUB%; color: %TEXTMUTED%;
    border: 1px solid %BORDER%;
    padding: 0 8px; font-weight: 600; font-size: 11px;
    letter-spacing: 0.4px;
}
QPushButton[segLeft="true"]  { border-top-left-radius: 6px; border-bottom-left-radius: 6px;  border-top-right-radius: 0; border-bottom-right-radius: 0; border-right: none; }
QPushButton[segRight="true"] { border-top-right-radius: 6px; border-bottom-right-radius: 6px; border-top-left-radius: 0;  border-bottom-left-radius: 0; }
QPushButton[segLeft="true"]:hover, QPushButton[segRight="true"]:hover { color: %TEXT%; }
QPushButton[segLeft="true"]:checked, QPushButton[segRight="true"]:checked {
    background: %ACCENT%; color: white; border-color: %ACCENT%;
}

QComboBox {
    background: %SURFACE%; color: %TEXT%;
    border: 1px solid %BORDER%; border-radius: 6px; padding: 5px 10px; min-width: 160px;
}
QComboBox:hover  { border-color: %BORDERBR%; }
QComboBox:focus  { border-color: %ACCENT%; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: right center; width: 22px; border-left: 1px solid %BORDER%; }
QComboBox QAbstractItemView {
    background: %SURFACE%; color: %TEXT%;
    border: 1px solid %BORDERBR%;
    selection-background-color: %ACCENTH%; selection-color: %TEXT%; padding: 4px;
}

QLineEdit {
    background: %BGSUB%; color: %TEXT%;
    border: 1px solid %BORDER%; border-radius: 6px; padding: 6px 10px;
    selection-background-color: %ACCENTH%; selection-color: %TEXT%;
}
QLineEdit:focus { border-color: %ACCENT%; }
QLineEdit:disabled { color: %TEXTDIM%; background: %BG%; }

QCheckBox { color: %TEXT%; spacing: 8px; padding: 4px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid %BORDERBR%; border-radius: 3px; background: %BGSUB%; }
QCheckBox::indicator:checked { background: %ACCENT%; border-color: %ACCENTLT%; }

QStatusBar { background: %BGSUB%; border-top: 1px solid %BORDER%; color: %TEXTMUTED%; font-size: 12px; padding: 0 10px; }
QStatusBar QLabel { color: %TEXTMUTED%; }

QDialog { background: %BGSUB%; border: 1px solid %BORDER%; }
QDialog QLabel { color: %TEXTMUTED%; }
QDialog QLabel[role="title"] { font-family: "Segoe UI Variable Display","Segoe UI"; font-size: 18px; font-weight: 600; color: %TEXT%; }

QScrollBar:vertical { background: %BG%; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %BORDER%; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: %BORDERBR%; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: %BG%; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: %BORDER%; border-radius: 4px; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background: %BORDERBR%; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

/* ── custom labs-engine widgets ─────────────────────────────────────────── */

QWidget#topBar {
    background: %BGSUB%;
    border-bottom: 1px solid %ACCENT%;
}
QWidget#leftRail, QWidget#rightRail {
    background: %BGSUB%;
}
QWidget#leftRail  { border-right: 1px solid %BORDER%; }
QWidget#rightRail { border-left:  1px solid %BORDER%; }
QWidget#stage     { background: %BG%; }
QWidget#stageTabs {
    background: %BGSUB%;
    border-bottom: 1px solid %BORDER%;
}

QLabel#wordmark {
    color: %TEXT%;
    font-family: "Segoe UI Variable Display","Segoe UI";
    font-size: 18px;
    font-weight: 700;
    letter-spacing: -0.3px;
    background: transparent;
}
QLabel#versionTag {
    color: %TEXTDIM%;
    font-family: "Segoe UI Variable Text","Segoe UI";
    font-size: 11px;
    font-weight: 500;
    background: transparent;
}
QLabel#eyebrow {
    color: %TEXTDIM%;
    font-family: "Segoe UI Variable Text","Segoe UI";
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1.0px;
    background: transparent;
}

/* Engine state pill — the big visible RUNNING / READY indicator */
QLabel#statePill {
    background: %BGSUB%;
    border: 1px solid %BORDER%;
    border-radius: 14px;
    padding: 4px 14px;
    font-family: "Segoe UI Variable Text","Segoe UI";
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1.2px;
    color: %TEXTDIM%;
}
QLabel#statePill[state="running"] {
    background: rgba(34,197,94,0.12);
    border-color: rgba(34,197,94,0.45);
    color: #4ADE80;
}
QLabel#statePill[state="ready"] {
    background: %BGSUB%;
    border-color: %BORDER%;
    color: %TEXTMUTED%;
}
QLabel#statePill[state="error"] {
    background: rgba(239,68,68,0.12);
    border-color: rgba(239,68,68,0.45);
    color: #FCA5A5;
}

/* Big stat numbers (FPS, shots fired) */
QLabel#bigStat {
    color: %TEXT%;
    font-family: "Segoe UI Variable Display","Segoe UI";
    font-size: 28px;
    font-weight: 700;
    background: transparent;
    letter-spacing: -0.5px;
}
QLabel#bigStatLabel {
    color: %TEXTDIM%;
    font-family: "Segoe UI Variable Text","Segoe UI";
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1.0px;
    background: transparent;
}
QLabel#sectionTitle {
    color: %TEXT%;
    font-family: "Segoe UI Variable Display","Segoe UI";
    font-size: 13px;
    font-weight: 600;
    background: transparent;
}
QLabel#pathBlue {
    color: %ACCENTLT%;
    font-family: "Cascadia Mono","Consolas";
    font-size: 10px;
    background: transparent;
}
QLabel#tabActive {
    color: %TEXT%;
    font-weight: 600;
    padding: 10px 16px;
    border-bottom: 2px solid %ACCENT%;
    background: transparent;
}
QLabel#tabInactive {
    color: %TEXTDIM%;
    padding: 10px 16px;
    background: transparent;
}
QLabel#targetText {
    color: %TEXTMUTED%;
    font-family: "Cascadia Mono","Consolas";
    font-size: 11px;
    background: transparent;
}
QLabel#fpsPill {
    color: %ACCENTLT%;
    font-family: "Cascadia Mono","Consolas";
    font-size: 11px;
    padding: 0 12px;
    background: transparent;
}
QLabel#statusMono {
    color: %TEXTMUTED%;
    font-family: "Cascadia Mono","Consolas";
    font-size: 12px;
    background: transparent;
}
QLabel#deviceList {
    color: %TEXTMUTED%;
    font-family: "Cascadia Mono","Consolas";
    font-size: 11px;
    background: transparent;
}
QLabel#modeLabel, QLabel#logLabel {
    color: %TEXTMUTED%;
    background: transparent;
}
QPlainTextEdit#logStrip {
    background: #0A0D14;
    border-top: 1px solid %BORDER%;
    color: #C8D1E0;
    font-family: "Cascadia Mono","Consolas";
    font-size: 12px;
    padding: 8px 14px;
    selection-background-color: %ACCENTH%;
    selection-color: %TEXT%;
}
QPlainTextEdit#logStrip:focus { border-top-color: %ACCENT%; }
QFrame#hrSep {
    color: %BORDER%;
    background: %BORDER%;
}
)")
        .replace("%BG%",        bg)
        .replace("%BGSUB%",     bgSubtle)
        .replace("%SURFACEH%",  surfaceH)
        .replace("%SURFACE%",   surface)
        .replace("%ACCENTLT%",  accentLt)
        .replace("%ACCENTH%",   accentH)
        .replace("%ACCENT%",    accent)
        .replace("%TEXTMUTED%", textMuted)
        .replace("%TEXTDIM%",   textDim)
        .replace("%TEXT%",      text)
        .replace("%BORDERBR%",  borderBr)
        .replace("%BORDER%",    border);
}

} // namespace Labs
