#include "ControllerMonitorWidget.h"

#include <QApplication>
#include <QFont>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>

namespace Labs {

ControllerMonitorWidget::ControllerMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    // Compact minimum so the widget shrinks gracefully when the window does.
    // Drawing math is purely fractional (W * 0.20 etc) so it scales to any size.
    setMinimumSize(420, 260);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void ControllerMonitorWidget::pushState(const ControllerState& state)
{
    QMetaObject::invokeMethod(this, [this, state]() {
        m_state    = state;
        m_hasState = true;

        Sample s;
        s.lx = state.leftThumbX  / 32767.f;
        s.ly = state.leftThumbY  / 32767.f;
        s.rx = state.rightThumbX / 32767.f;
        s.ry = state.rightThumbY / 32767.f;
        s.lt = state.leftTrigger  / 255.f;
        s.rt = state.rightTrigger / 255.f;
        m_history.push_back(s);
        while ((int)m_history.size() > kHistLen)
            m_history.pop_front();

        update();
    }, Qt::QueuedConnection);
}

// ── helpers ──────────────────────────────────────────────────────────────────

namespace {

QColor mix(QColor a, QColor b, qreal t)
{
    return QColor(
        int(a.red()   * (1 - t) + b.red()   * t),
        int(a.green() * (1 - t) + b.green() * t),
        int(a.blue()  * (1 - t) + b.blue()  * t));
}

void drawStickWell(QPainter& p, QRectF rect, float x, float y, float deadzone,
                   const QColor& trackBg, const QColor& trackBorder,
                   const QColor& accent, const QColor& dim, bool pressed)
{
    // Track (well) — circular
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(trackBg);
    p.setPen(QPen(trackBorder, 1.5));
    p.drawEllipse(rect);

    // Crosshairs
    p.setPen(QPen(trackBorder, 1, Qt::DotLine));
    p.drawLine(QPointF(rect.left() + 6, rect.center().y()),
               QPointF(rect.right() - 6, rect.center().y()));
    p.drawLine(QPointF(rect.center().x(), rect.top() + 6),
               QPointF(rect.center().x(), rect.bottom() - 6));

    // Dead-zone ring
    p.setPen(QPen(dim, 1, Qt::DotLine));
    const qreal dzR = (rect.width() / 2 - 4) * deadzone;
    p.drawEllipse(rect.center(), dzR, dzR);

    // Stick dot
    const qreal r = rect.width() / 2 - 6;
    const QPointF dot(rect.center().x() + x * r,
                      rect.center().y() - y * r);  // y inverted
    const qreal mag = qBound(0.0, qSqrt(qreal(x*x + y*y)), 1.0);
    QColor dotColor = mix(dim, accent, mag);
    if (pressed) dotColor = accent.lighter(140);

    p.setPen(QPen(dotColor.darker(150), 1.5));
    p.setBrush(dotColor);
    p.drawEllipse(dot, 8, 8);

    // Vector line center → dot when stick is moved
    if (mag > deadzone) {
        p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 120), 1.5));
        p.drawLine(rect.center(), dot);
    }
}

void drawTrigger(QPainter& p, QRectF rect, float v,
                 const QString& label,
                 const QColor& bg, const QColor& border,
                 const QColor& accent, const QColor& dim)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    // Track
    p.setBrush(bg);
    p.setPen(QPen(border, 1));
    p.drawRoundedRect(rect, 6, 6);
    // Fill from bottom up
    if (v > 0.001f) {
        QRectF fill = rect.adjusted(2, 2 + (rect.height() - 4) * (1.0f - v), -2, -2);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 200));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(fill, 4, 4);
    }
    // Label + value
    QFont f("Segoe UI Variable Text", 9, QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    p.setFont(f);
    p.setPen(v > 0.001f ? accent.lighter(140) : dim);
    p.drawText(QRectF(rect.left(), rect.top() - 16, rect.width(), 14),
               Qt::AlignHCenter, label);
    f.setPixelSize(10);
    f.setBold(false);
    p.setFont(f);
    p.setPen(v > 0.001f ? QColor(220, 230, 245) : dim);
    p.drawText(QRectF(rect.left(), rect.bottom() + 2, rect.width(), 14),
               Qt::AlignHCenter, QString::number(int(v * 255)));
}

void drawFaceButton(QPainter& p, QPointF center, qreal radius,
                    const QString& letter, const QColor& accent,
                    const QColor& dim, const QColor& border, bool pressed)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = pressed
        ? QColor(accent.red(), accent.green(), accent.blue(), 220)
        : QColor(20, 24, 34);
    p.setBrush(fill);
    p.setPen(QPen(pressed ? accent : border, pressed ? 2 : 1));
    p.drawEllipse(center, radius, radius);

    QFont f("Segoe UI Variable Display", int(radius), QFont::Bold);
    p.setFont(f);
    p.setPen(pressed ? Qt::white : dim);
    p.drawText(QRectF(center.x() - radius, center.y() - radius,
                      radius * 2, radius * 2),
               Qt::AlignCenter, letter);
}

void drawDpad(QPainter& p, QRectF rect,
              bool up, bool down, bool left, bool right,
              const QColor& accent, const QColor& dim, const QColor& border)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal arm = rect.width() / 3;
    // Vertical bar
    QRectF v(rect.center().x() - arm/2, rect.top(), arm, rect.height());
    QRectF h(rect.left(), rect.center().y() - arm/2, rect.width(), arm);

    auto drawArm = [&](QRectF r, bool pressed, Qt::Orientation orient, bool first) {
        QColor fill = pressed ? QColor(accent.red(), accent.green(), accent.blue(), 220) : QColor(20, 24, 34);
        p.setBrush(fill);
        p.setPen(QPen(pressed ? accent : border, pressed ? 2 : 1));
        if (orient == Qt::Vertical) {
            QRectF half = first
                ? QRectF(r.left(), r.top(), r.width(), r.height()/2 - 1)
                : QRectF(r.left(), r.center().y() + 1, r.width(), r.height()/2 - 1);
            p.drawRoundedRect(half, 4, 4);
        } else {
            QRectF half = first
                ? QRectF(r.left(), r.top(), r.width()/2 - 1, r.height())
                : QRectF(r.center().x() + 1, r.top(), r.width()/2 - 1, r.height());
            p.drawRoundedRect(half, 4, 4);
        }
    };
    drawArm(v, up,    Qt::Vertical,   true);
    drawArm(v, down,  Qt::Vertical,   false);
    drawArm(h, left,  Qt::Horizontal, true);
    drawArm(h, right, Qt::Horizontal, false);

    // Center dot
    p.setBrush(QColor(20, 24, 34));
    p.setPen(QPen(border, 1));
    p.drawEllipse(rect.center(), arm/3, arm/3);
}

void drawShoulderPill(QPainter& p, QRectF rect, const QString& label,
                      bool pressed, const QColor& accent,
                      const QColor& dim, const QColor& border)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = pressed ? QColor(accent.red(), accent.green(), accent.blue(), 220) : QColor(20, 24, 34);
    p.setBrush(fill);
    p.setPen(QPen(pressed ? accent : border, pressed ? 2 : 1));
    p.drawRoundedRect(rect, rect.height()/2, rect.height()/2);

    QFont f("Segoe UI Variable Text", 10, QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    p.setFont(f);
    p.setPen(pressed ? Qt::white : dim);
    p.drawText(rect, Qt::AlignCenter, label);
}

void drawCenterPill(QPainter& p, QRectF rect, const QString& label,
                    bool pressed, const QColor& accent,
                    const QColor& dim, const QColor& border)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = pressed ? QColor(accent.red(), accent.green(), accent.blue(), 220) : QColor(20, 24, 34);
    p.setBrush(fill);
    p.setPen(QPen(pressed ? accent : border, pressed ? 1.5 : 1));
    p.drawRoundedRect(rect, 4, 4);

    QFont f("Segoe UI Variable Text", 8, QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    p.setFont(f);
    p.setPen(pressed ? Qt::white : dim);
    p.drawText(rect, Qt::AlignCenter, label);
}

} // anonymous namespace

// ── paintEvent ───────────────────────────────────────────────────────────────

void ControllerMonitorWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Palette
    const QColor bg      {10,  13,  20};
    const QColor surface {16,  20,  30};
    const QColor border  {38,  46,  64};
    const QColor borderD {28,  34,  50};
    const QColor text    {220, 230, 245};
    const QColor muted   {110, 122, 145};
    const QColor accent  = qApp->palette().color(QPalette::Highlight);

    const int W = width();
    const int H = height();
    p.fillRect(rect(), bg);

    // ── Header strip ─────────────────────────────────────────────────────────
    const int hdrH = 36;
    p.fillRect(0, 0, W, hdrH, surface);
    p.fillRect(0, hdrH - 1, W, 1, border);

    QFont head("Segoe UI Variable Text", 9, QFont::DemiBold);
    head.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    p.setFont(head);
    p.setPen(muted);
    p.drawText(QRect(16, 0, 240, hdrH), Qt::AlignVCenter, "CONTROLLER MONITOR");

    const bool conn = m_hasState && m_state.connected;
    QString statusStr = !m_hasState ? QStringLiteral("WAITING")
                       : conn        ? QStringLiteral("SLOT %1  ●  CONNECTED").arg(m_state.slot)
                                     : QStringLiteral("SLOT %1  ○  DISCONNECTED").arg(m_state.slot);
    p.setPen(conn ? QColor(74, 222, 128) : muted);
    QFont stHead("Segoe UI Variable Text", 9, QFont::DemiBold);
    stHead.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    p.setFont(stHead);
    p.drawText(QRect(0, 0, W - 16, hdrH), Qt::AlignVCenter | Qt::AlignRight, statusStr);

    // ── Main controller diagram ──────────────────────────────────────────────
    // Oscilloscope shrinks for small windows so the controller diagram still fits.
    const int oscH    = qBound(60, H / 5, 100);
    const int diagY0  = hdrH + 10;
    const int diagY1  = H - oscH - 6;
    const int diagH   = diagY1 - diagY0;

    // Symmetric layout: two big stick wells flanking the diagram, triggers above,
    // shoulders above triggers, dpad bottom-left, face buttons bottom-right,
    // center pills (Back/Guide/Start) between sticks at the top middle.

    // Stick radius bounded by BOTH width and remaining vertical space (so dpad
    // and face buttons fit under the sticks without overflowing the frame).
    qreal stickR = qreal(diagH) * 0.22;
    stickR = qMin(stickR, qreal(W)     * 0.16);
    stickR = qMin(stickR, qreal(diagH) * 0.32 - 28);
    if (stickR < 24) stickR = 24;
    const qreal stickD = stickR * 2;

    // Left/right column anchors
    const qreal leftCenterX  = W * 0.22;
    const qreal rightCenterX = W * 0.78;
    const qreal sticksY      = diagY0 + stickR + 32;  // leave room for shoulders above

    QRectF leftStick (leftCenterX  - stickR, sticksY - stickR, stickD, stickD);
    QRectF rightStick(rightCenterX - stickR, sticksY - stickR, stickD, stickD);

    // Stick coordinates (raw float -1..1)
    float lx = m_state.leftThumbX  / 32767.f;
    float ly = m_state.leftThumbY  / 32767.f;
    float rx = m_state.rightThumbX / 32767.f;
    float ry = m_state.rightThumbY / 32767.f;
    drawStickWell(p, leftStick,  lx, ly, 0.10f, surface, borderD, accent, muted,
                  m_state.buttons & ButtonLeftThumb);
    drawStickWell(p, rightStick, rx, ry, 0.10f, surface, borderD, accent, muted,
                  m_state.buttons & ButtonRightThumb);

    // Stick labels under each
    QFont lbl("Segoe UI Variable Text", 8, QFont::DemiBold);
    lbl.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    p.setFont(lbl);
    p.setPen(muted);
    p.drawText(QRectF(leftStick.left(),  leftStick.bottom() + 6, leftStick.width(), 14),
               Qt::AlignHCenter, "LEFT STICK");
    p.drawText(QRectF(rightStick.left(), rightStick.bottom() + 6, rightStick.width(), 14),
               Qt::AlignHCenter, "RIGHT STICK");

    // Triggers — vertical bars on the inside of each stick
    const qreal trigW = 22;
    const qreal trigH = stickD - 14;
    QRectF lt(leftStick.right() + 14, leftStick.top() + 7, trigW, trigH);
    QRectF rt(rightStick.left() - 14 - trigW, rightStick.top() + 7, trigW, trigH);
    drawTrigger(p, lt, m_state.leftTrigger / 255.f,  "LT", surface, borderD, accent, muted);
    drawTrigger(p, rt, m_state.rightTrigger / 255.f, "RT", surface, borderD, accent, muted);

    // Shoulders LB / RB above each trigger
    const qreal shW = stickD * 0.55;
    const qreal shH = 24;
    QRectF lb(leftStick.center().x()  - shW/2, leftStick.top() - shH - 18, shW, shH);
    QRectF rb(rightStick.center().x() - shW/2, rightStick.top() - shH - 18, shW, shH);
    drawShoulderPill(p, lb, "LB", m_state.buttons & ButtonLeftShoulder,  accent, muted, border);
    drawShoulderPill(p, rb, "RB", m_state.buttons & ButtonRightShoulder, accent, muted, border);

    // D-pad — under-left
    const qreal dpadSize = stickD * 0.70;
    QRectF dpad(leftStick.center().x()  - dpadSize/2,
                leftStick.bottom() + 28,
                dpadSize, dpadSize);
    drawDpad(p, dpad,
             m_state.buttons & ButtonDpadUp,
             m_state.buttons & ButtonDpadDown,
             m_state.buttons & ButtonDpadLeft,
             m_state.buttons & ButtonDpadRight,
             accent, muted, border);

    // Face buttons (A/B/X/Y) — under-right, in diamond layout
    const qreal faceR = stickR * 0.34;
    const qreal faceCx = rightStick.center().x();
    const qreal faceCy = rightStick.bottom() + 28 + dpadSize / 2;
    const qreal faceSpread = faceR * 2.4;
    drawFaceButton(p, QPointF(faceCx, faceCy - faceSpread), faceR, "Y",
                   QColor(255, 220, 0), muted, border, m_state.buttons & ButtonY);
    drawFaceButton(p, QPointF(faceCx + faceSpread, faceCy), faceR, "B",
                   QColor(220, 60, 60), muted, border, m_state.buttons & ButtonB);
    drawFaceButton(p, QPointF(faceCx, faceCy + faceSpread), faceR, "A",
                   QColor(50, 200, 80), muted, border, m_state.buttons & ButtonA);
    drawFaceButton(p, QPointF(faceCx - faceSpread, faceCy), faceR, "X",
                   QColor(50, 130, 255), muted, border, m_state.buttons & ButtonX);

    // Center pills: BACK | GUIDE | START
    const qreal pillW = 56, pillH = 22, pillSpacing = 8;
    const qreal centerX = W * 0.50;
    const qreal pillsY  = sticksY - pillH / 2;
    const qreal pillsLeft = centerX - (pillW * 3 + pillSpacing * 2) / 2;
    drawCenterPill(p, QRectF(pillsLeft,                              pillsY, pillW, pillH),
                   "BACK",  m_state.buttons & ButtonBack,  accent, muted, border);
    drawCenterPill(p, QRectF(pillsLeft + pillW + pillSpacing,        pillsY, pillW, pillH),
                   "GUIDE", m_state.buttons & ButtonGuide, accent, muted, border);
    drawCenterPill(p, QRectF(pillsLeft + (pillW + pillSpacing) * 2,  pillsY, pillW, pillH),
                   "START", m_state.buttons & ButtonStart, accent, muted, border);

    // Live coordinate readout under center
    QFont mono("Cascadia Mono", 9);
    p.setFont(mono);
    p.setPen(muted);
    const QString coords = QStringLiteral("LX %1  LY %2     RX %3  RY %4")
        .arg(m_state.leftThumbX,  6).arg(m_state.leftThumbY, 6)
        .arg(m_state.rightThumbX, 6).arg(m_state.rightThumbY, 6);
    p.drawText(QRectF(0, pillsY + pillH + 18, W, 14), Qt::AlignHCenter, coords);

    // ── Oscilloscope at the bottom ───────────────────────────────────────────
    const int plotY = H - oscH;
    p.fillRect(0, plotY, W, oscH, QColor(7, 9, 14));
    p.fillRect(0, plotY, W, 1, border);

    // Grid
    p.setPen(QPen(QColor(22, 28, 42), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i)
        p.drawLine(0, plotY + oscH * i / 4, W, plotY + oscH * i / 4);
    p.setPen(QPen(QColor(38, 48, 70), 1));
    p.drawLine(0, plotY + oscH / 2, W, plotY + oscH / 2);

    // Eyebrow label inside the plot
    QFont eyebrow("Segoe UI Variable Text", 8, QFont::DemiBold);
    eyebrow.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    p.setFont(eyebrow);
    p.setPen(QColor(70, 80, 100));
    p.drawText(QRect(12, plotY + 6, 120, 12), Qt::AlignLeft, "STICK / TRIGGER TRACE");

    // Y labels
    QFont yLbl("Cascadia Mono", 8);
    p.setFont(yLbl);
    p.setPen(QColor(80, 90, 110));
    p.drawText(QRect(2, plotY + 1, 30, 11), Qt::AlignLeft, "+1.0");
    p.drawText(QRect(2, plotY + oscH/2 - 6, 18, 11), Qt::AlignLeft, "0");
    p.drawText(QRect(2, plotY + oscH - 12, 30, 11), Qt::AlignLeft, "-1.0");

    if (!m_history.empty()) {
        struct Trace { float Sample::*field; QColor color; const char* name; };
        const Trace traces[] = {
            {&Sample::lx, QColor(50,  130, 255), "LX"},
            {&Sample::ly, QColor(80,  200, 220), "LY"},
            {&Sample::rx, QColor(255, 140,  30), "RX"},
            {&Sample::ry, QColor(255, 220,   0), "RY"},
            {&Sample::lt, QColor(220,  70,  70), "LT"},
            {&Sample::rt, QColor(74,  222, 128), "RT"},
        };

        const int n = (int)m_history.size();
        const qreal halfH = oscH / 2.0 - 6.0;

        for (const Trace& tr : traces) {
            QPolygonF poly;
            poly.reserve(n);
            for (int i = 0; i < n; ++i) {
                const float v = m_history[i].*tr.field;
                poly << QPointF(qreal(W) * i / kHistLen,
                                plotY + oscH / 2.0 - v * halfH);
            }
            p.setPen(QPen(tr.color, 1.5));
            p.drawPolyline(poly);
        }

        // Tight legend top-right
        QFont leg("Segoe UI Variable Text", 8, QFont::DemiBold);
        p.setFont(leg);
        int lx2 = W - 30 * 6 - 12;
        for (int i = 0; i < 6; ++i) {
            p.fillRect(lx2, plotY + 7, 8, 8, traces[i].color);
            p.setPen(traces[i].color);
            p.drawText(lx2 + 12, plotY + 14, traces[i].name);
            lx2 += 30;
        }
    }
}

} // namespace Labs
