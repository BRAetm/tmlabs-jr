// DeviceMonitor.cpp — Real-time controller input/output display plugin

#include "DeviceMonitor.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QGroupBox>
#include <QPainter>
#include <QPixmap>
#include <cmath>

namespace Helios {

DeviceMonitorPlugin::DeviceMonitorPlugin(QObject* parent) : QObject(parent) {}

DeviceMonitorPlugin::~DeviceMonitorPlugin() { shutdown(); }

void DeviceMonitorPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &DeviceMonitorPlugin::poll);
    m_pollTimer->start(16); // ~60 Hz UI update
}

void DeviceMonitorPlugin::shutdown()
{
    if (m_pollTimer) m_pollTimer->stop();
}

QWidget* DeviceMonitorPlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QHBoxLayout(w);

    auto makeGroup = [&](const QString& title) {
        auto* grp = new QGroupBox(title, w);
        auto* gl  = new QGridLayout(grp);

        auto* lblLStick = new QLabel("L:", grp); lblLStick->setFixedSize(80, 80);
        auto* lblRStick = new QLabel("R:", grp); lblRStick->setFixedSize(80, 80);
        auto* lblBtns   = new QLabel("Buttons: ---", grp);
        auto* lblTrigs  = new QLabel("L2:0  R2:0", grp);

        gl->addWidget(lblLStick, 0, 0);
        gl->addWidget(lblRStick, 0, 1);
        gl->addWidget(lblBtns,   1, 0, 1, 2);
        gl->addWidget(lblTrigs,  2, 0, 1, 2);

        return std::make_tuple(grp, lblLStick, lblRStick, lblBtns, lblTrigs);
    };

    auto [inGrp, inLS, inRS, inBtns, inTrigs]   = makeGroup("Input");
    auto [outGrp, outLS, outRS, outBtns, outTrigs] = makeGroup("Output");

    m_lblInputStickL   = inLS;
    m_lblInputStickR   = inRS;
    m_lblInputButtons  = inBtns;
    m_lblInputTriggers = inTrigs;
    m_lblOutputStickL  = outLS;
    m_lblOutputStickR  = outRS;
    m_lblOutputButtons = outBtns;
    m_lblOutputTriggers = outTrigs;

    lay->addWidget(inGrp);
    lay->addWidget(outGrp);

    return w;
}

void DeviceMonitorPlugin::poll()
{
    if (m_inputReader && m_inputReader->isConnected()) {
        SharedMemoryControllerState state = {};
        size_t read = 0;
        if (m_inputReader->readData(&state, sizeof(state), read) && read == sizeof(state))
            updateUI(state, true);
    }
    if (m_outputReader && m_outputReader->isConnected()) {
        SharedMemoryControllerState state = {};
        size_t read = 0;
        if (m_outputReader->readData(&state, sizeof(state), read) && read == sizeof(state))
            updateUI(state, false);
    }
}

void DeviceMonitorPlugin::updateUI(const SharedMemoryControllerState& state, bool isInput)
{
    // DS5 raw layout: [0]=LX [1]=LY [2]=RX [3]=RY [4]=L2 [5]=R2 [6..7]=buttons
    uint8_t lx = state.data[0], ly = state.data[1];
    uint8_t rx = state.data[2], ry = state.data[3];
    uint8_t l2 = state.data[4], r2 = state.data[5];
    uint16_t btns = (state.data[6]) | (state.data[7] << 8);

    auto* lblLS   = isInput ? m_lblInputStickL  : m_lblOutputStickL;
    auto* lblRS   = isInput ? m_lblInputStickR  : m_lblOutputStickR;
    auto* lblBtns = isInput ? m_lblInputButtons  : m_lblOutputButtons;
    auto* lblTrigs = isInput ? m_lblInputTriggers : m_lblOutputTriggers;

    if (lblLS) renderStick(lblLS, lx - 128, -(ly - 128), "L");
    if (lblRS) renderStick(lblRS, rx - 128, -(ry - 128), "R");
    if (lblBtns)  lblBtns->setText(QString("Btns: 0x%1").arg(btns, 4, 16, QChar('0')));
    if (lblTrigs) lblTrigs->setText(QString("L2:%1  R2:%2").arg(l2).arg(r2));
}

void DeviceMonitorPlugin::renderStick(QLabel* label, int x, int y, const QString& name)
{
    QPixmap pix(80, 80);
    pix.fill(Qt::black);
    QPainter p(&pix);
    p.setPen(Qt::darkGray);
    p.drawEllipse(4, 4, 72, 72);
    p.setPen(Qt::white);
    p.drawText(2, 12, name);

    // Dot position: map -128..127 → 4..76
    int px = 4 + static_cast<int>((x + 128) / 256.0 * 72);
    int py = 4 + static_cast<int>((127 - y) / 256.0 * 72);
    px = qBound(4, px, 76);
    py = qBound(4, py, 76);

    p.setBrush(Qt::cyan);
    p.setPen(Qt::NoPen);
    p.drawEllipse(px - 4, py - 4, 8, 8);
    p.end();

    label->setPixmap(pix);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::DeviceMonitorPlugin();
}
