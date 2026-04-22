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
    setMinimumSize(700, 420);
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

// ── paintEvent ───────────────────────────────────────────────────────────────

void ControllerMonitorWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Palette
    const QColor bg      {14,  16,  22};
    const QColor surface {22,  26,  36};
    const QColor border  {38,  46,  64};
    const QColor text    {200, 210, 230};
    const QColor muted   {90,  100, 118};
    const QColor accent  = qApp->palette().color(QPalette::Highlight);

    const int W = width();
    const int H = height();

    p.fillRect(rect(), bg);

    // Layout
    const int hdrH  = 28;
    const int plotH = qMax(90, H * 28 / 100);
    const int listH = H - hdrH - plotH;
    const int listW = W * 58 / 100;
    const int statX = listW + 1;
    const int statW = W - statX;

    // ── Header ───────────────────────────────────────────────────────────────
    p.fillRect(0, 0, W, hdrH, surface);
    p.fillRect(0, hdrH - 1, W, 1, border);

    QFont mono("Cascadia Mono");
    mono.setPixelSize(11);
    mono.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    p.setFont(mono);
    p.setPen(accent);
    p.drawText(QRect(10, 0, 200, hdrH), Qt::AlignVCenter, "XINPUT MONITOR");

    const bool conn = m_hasState && m_state.connected;
    const QString statusStr = !m_hasState
        ? QStringLiteral("WAITING FOR CONTROLLER")
        : (conn
            ? QStringLiteral("SLOT %1  ●  CONNECTED").arg(m_state.slot)
            : QStringLiteral("SLOT %1  ○  DISCONNECTED").arg(m_state.slot));
    p.setPen(conn ? QColor(30, 210, 90) : muted);
    p.drawText(QRect(0, 0, W - 10, hdrH), Qt::AlignVCenter | Qt::AlignRight, statusStr);

    // ── Input list ───────────────────────────────────────────────────────────
    struct Row { const char* name; int value; int range; };  // range: 255, 32767, or 1

    const Row rows[] = {
        {"LT",     m_state.leftTrigger,               255},
        {"RT",     m_state.rightTrigger,              255},
        {"LX",     m_state.leftThumbX,                32767},
        {"LY",     m_state.leftThumbY,                32767},
        {"RX",     m_state.rightThumbX,               32767},
        {"RY",     m_state.rightThumbY,               32767},
        {"A",      !!(m_state.buttons & ButtonA),     1},
        {"B",      !!(m_state.buttons & ButtonB),     1},
        {"X",      !!(m_state.buttons & ButtonX),     1},
        {"Y",      !!(m_state.buttons & ButtonY),     1},
        {"LB",     !!(m_state.buttons & ButtonLeftShoulder),  1},
        {"RB",     !!(m_state.buttons & ButtonRightShoulder), 1},
        {"UP",     !!(m_state.buttons & ButtonDpadUp),    1},
        {"DOWN",   !!(m_state.buttons & ButtonDpadDown),  1},
        {"LEFT",   !!(m_state.buttons & ButtonDpadLeft),  1},
        {"RIGHT",  !!(m_state.buttons & ButtonDpadRight), 1},
        {"START",  !!(m_state.buttons & ButtonStart),     1},
        {"BACK",   !!(m_state.buttons & ButtonBack),      1},
        {"GUIDE",  !!(m_state.buttons & ButtonGuide),     1},
        {"LS",     !!(m_state.buttons & ButtonLeftThumb),  1},
        {"RS",     !!(m_state.buttons & ButtonRightThumb), 1},
    };
    constexpr int kRows = (int)(sizeof(rows) / sizeof(rows[0]));

    const int rowH  = qMax(14, listH / kRows);
    const int idxW  = 28;
    const int nameW = 52;
    const int valW  = 58;
    const int barX  = idxW + nameW + valW + 6;
    const int barW  = listW - barX - 8;

    mono.setPixelSize(10);
    mono.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    p.setFont(mono);

    for (int i = 0; i < kRows; ++i) {
        const Row& r   = rows[i];
        const int  y   = hdrH + i * rowH;
        const bool hot = r.value != 0;

        // Row background
        if (hot)
            p.fillRect(0, y, listW, rowH,
                       QColor(accent.red(), accent.green(), accent.blue(), 28));
        else if (i & 1)
            p.fillRect(0, y, listW, rowH, QColor(18, 22, 30));

        // Index
        p.setPen(muted);
        p.drawText(QRect(4, y, idxW - 4, rowH), Qt::AlignVCenter | Qt::AlignRight,
                   QString::number(i));

        // Name
        p.setPen(hot ? text : muted);
        p.drawText(QRect(idxW + 4, y, nameW - 4, rowH), Qt::AlignVCenter, r.name);

        // Value
        const QString valStr = (r.range == 1)
            ? (r.value ? "+1" : "+0")
            : ((r.value >= 0 ? "+" : "") + QString::number(r.value));
        p.setPen(hot ? accent : muted);
        p.drawText(QRect(idxW + nameW, y, valW, rowH),
                   Qt::AlignVCenter | Qt::AlignRight, valStr);

        // Bar track
        if (barW > 10) {
            const int bcy = y + rowH / 2 - 2;
            p.fillRect(barX, bcy, barW, 4, QColor(28, 34, 48));

            if (hot) {
                int bx = barX, bw = 0;
                if (r.range == 255) {
                    bw = int(barW * r.value / 255.0f);
                    bx = barX;
                } else if (r.range == 32767) {
                    const float n = r.value / 32767.0f;
                    if (n >= 0) { bx = barX + barW/2; bw = int(barW/2 * n); }
                    else        { bw = int(barW/2 * (-n)); bx = barX + barW/2 - bw; }
                } else {
                    bx = barX; bw = barW;
                }
                p.fillRect(bx, bcy, bw, 4,
                           QColor(accent.red(), accent.green(), accent.blue(), 190));
            }
        }

        // Row divider
        p.fillRect(0, y + rowH - 1, listW, 1, border);
    }

    // Column divider
    p.fillRect(listW, hdrH, 1, listH + plotH, border);

    // ── Status / button grid panel ────────────────────────────────────────────
    {
        const int sx = statX + 10;
        const int sw = statW - 14;
        int sy = hdrH + 12;
        const int lh = 17;

        mono.setPixelSize(10);
        mono.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        p.setFont(mono);

        auto statRow = [&](const QString& label, const QString& val, const QColor& vc) {
            p.setPen(muted);
            p.drawText(QRect(sx, sy, sw / 2, lh), Qt::AlignVCenter, label);
            p.setPen(vc);
            p.drawText(QRect(sx + sw / 2, sy, sw / 2, lh),
                       Qt::AlignVCenter | Qt::AlignRight, val);
            sy += lh;
        };

        statRow("SLOT",
                m_hasState ? QString::number(m_state.slot) : "—", text);
        statRow("STATUS",
                !m_hasState ? "WAITING" : (conn ? "CONNECTED" : "DISCONNECTED"),
                conn ? QColor(30,210,90) : QColor(210,70,70));

        sy += 6;
        p.fillRect(sx, sy, sw, 1, border);
        sy += 8;

        // Trigger bars
        auto trigBar = [&](const QString& lbl, int val) {
            p.setPen(muted);
            p.drawText(QRect(sx, sy, 24, lh), Qt::AlignVCenter, lbl);
            const int bx = sx + 26, bw = sw - 46;
            p.fillRect(bx, sy + lh/2 - 3, bw, 6, QColor(26, 32, 46));
            if (val > 0)
                p.fillRect(bx, sy + lh/2 - 3, int(bw * val / 255.0f), 6,
                           QColor(accent.red(), accent.green(), accent.blue(), 200));
            p.setPen(val ? accent : muted);
            p.drawText(QRect(sx + sw - 20, sy, 20, lh), Qt::AlignVCenter | Qt::AlignRight,
                       QString::number(val));
            sy += lh;
        };
        trigBar("LT", m_state.leftTrigger);
        trigBar("RT", m_state.rightTrigger);

        sy += 6;
        p.fillRect(sx, sy, sw, 1, border);
        sy += 8;

        // Button grid (4 columns)
        struct Btn { const char* name; bool pressed; };
        const Btn btns[] = {
            {"A",     !!(m_state.buttons & ButtonA)},
            {"B",     !!(m_state.buttons & ButtonB)},
            {"X",     !!(m_state.buttons & ButtonX)},
            {"Y",     !!(m_state.buttons & ButtonY)},
            {"LB",    !!(m_state.buttons & ButtonLeftShoulder)},
            {"RB",    !!(m_state.buttons & ButtonRightShoulder)},
            {"UP",    !!(m_state.buttons & ButtonDpadUp)},
            {"DOWN",  !!(m_state.buttons & ButtonDpadDown)},
            {"LEFT",  !!(m_state.buttons & ButtonDpadLeft)},
            {"RIGHT", !!(m_state.buttons & ButtonDpadRight)},
            {"START", !!(m_state.buttons & ButtonStart)},
            {"BACK",  !!(m_state.buttons & ButtonBack)},
            {"GUIDE", !!(m_state.buttons & ButtonGuide)},
            {"LS",    !!(m_state.buttons & ButtonLeftThumb)},
            {"RS",    !!(m_state.buttons & ButtonRightThumb)},
        };
        constexpr int kBtns = (int)(sizeof(btns) / sizeof(btns[0]));
        const int cols   = 3;
        const int cellW  = sw / cols;
        const int cellH  = 20;

        for (int i = 0; i < kBtns; ++i) {
            const int bx = sx + (i % cols) * cellW;
            const int by = sy + (i / cols) * cellH;
            const bool pr = btns[i].pressed;
            if (pr)
                p.fillRect(bx, by, cellW - 2, cellH - 2,
                           QColor(accent.red(), accent.green(), accent.blue(), 55));
            p.setPen(pr ? accent : muted);
            p.drawText(QRect(bx, by, cellW - 2, cellH), Qt::AlignCenter, btns[i].name);
        }
    }

    // ── Oscilloscope ─────────────────────────────────────────────────────────
    const int plotY = H - plotH;
    p.fillRect(0, plotY, W, plotH, QColor(10, 12, 18));
    p.fillRect(0, plotY, W, 1, border);

    // Grid
    p.setPen(QPen(QColor(28, 34, 50), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i)
        p.drawLine(0, plotY + plotH * i / 4, W, plotY + plotH * i / 4);
    p.setPen(QPen(QColor(42, 50, 70), 1));
    p.drawLine(0, plotY + plotH / 2, W, plotY + plotH / 2);

    // Y labels
    mono.setPixelSize(8);
    mono.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    p.setFont(mono);
    p.setPen(muted);
    p.drawText(QRect(2, plotY + 1, 32, 11), Qt::AlignLeft, "+100");
    p.drawText(QRect(2, plotY + plotH/2 - 6, 18, 11), Qt::AlignLeft, "0");
    p.drawText(QRect(2, plotY + plotH - 12, 32, 11), Qt::AlignLeft, "-100");

    // Traces
    if (!m_history.empty()) {
        struct Trace { float Sample::*field; float scale; QColor color; };
        const Trace traces[] = {
            {&Sample::lx, 1.0f, QColor(50,  130, 255)},   // LX blue
            {&Sample::ly, 1.0f, QColor(0,   200, 220)},   // LY cyan
            {&Sample::rx, 1.0f, QColor(255, 140,  30)},   // RX orange
            {&Sample::ry, 1.0f, QColor(255, 220,   0)},   // RY yellow
            {&Sample::lt, 0.5f, QColor(220,  50,  50)},   // LT red
            {&Sample::rt, 0.5f, QColor(50,  200,  80)},   // RT green
        };

        p.setRenderHint(QPainter::Antialiasing, true);
        const int n = (int)m_history.size();
        const qreal halfH = plotH / 2.0 - 4.0;

        for (const Trace& tr : traces) {
            QPolygonF poly;
            poly.reserve(n);
            for (int i = 0; i < n; ++i) {
                const float v = m_history[i].*tr.field * tr.scale;
                poly << QPointF(qreal(W) * i / kHistLen,
                                plotY + plotH / 2.0 - v * halfH);
            }
            p.setPen(QPen(tr.color, 1.5));
            p.drawPolyline(poly);
        }

        // Legend
        const char* names[] = {"LX","LY","RX","RY","LT","RT"};
        mono.setPixelSize(8);
        p.setFont(mono);
        int lx = W - 38 * 6 - 4;
        for (int i = 0; i < 6; ++i) {
            p.setPen(QPen(traces[i].color, 2));
            p.drawLine(lx, plotY + 8, lx + 12, plotY + 8);
            p.setPen(traces[i].color);
            p.drawText(lx + 14, plotY + 12, names[i]);
            lx += 38;
        }
    }
}

} // namespace Labs
