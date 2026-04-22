#include "XInputPlugin.h"

#include <QDateTime>
#include <QDebug>

#include <Windows.h>
#include <Xinput.h>

namespace Labs {

// ── XInputSource ────────────────────────────────────────────────────────────

XInputSource::XInputSource(QObject* parent) : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(8);  // ~125Hz polling
    connect(&m_timer, &QTimer::timeout, this, &XInputSource::poll);
}

XInputSource::~XInputSource() { stop(); }

bool XInputSource::start()
{
    if (m_running.load()) return true;
    m_activeSlot = -1;
    m_running.store(true);
    m_timer.start();
    return true;
}

void XInputSource::stop()
{
    if (!m_running.exchange(false)) return;
    m_timer.stop();
}

void XInputSource::poll()
{
    if (!m_sink) return;

    const int preferred = (m_activeSlot >= 0) ? m_activeSlot : 0;
    const int slotOrder[4] = { preferred,
                               (preferred + 1) & 3,
                               (preferred + 2) & 3,
                               (preferred + 3) & 3 };

    for (int slot : slotOrder) {
        if (m_skipMask & (1 << slot)) continue;
        XINPUT_STATE xs {};
        DWORD rc = XInputGetState(slot, &xs);
        if (rc == ERROR_SUCCESS) {
            if (m_activeSlot != slot) m_activeSlot = slot;

            ControllerState s;
            s.buttons      = xs.Gamepad.wButtons;
            s.leftTrigger  = xs.Gamepad.bLeftTrigger;
            s.rightTrigger = xs.Gamepad.bRightTrigger;
            s.leftThumbX   = xs.Gamepad.sThumbLX;
            s.leftThumbY   = xs.Gamepad.sThumbLY;
            s.rightThumbX  = xs.Gamepad.sThumbRX;
            s.rightThumbY  = xs.Gamepad.sThumbRY;
            s.timestampUs  = QDateTime::currentMSecsSinceEpoch() * 1000;
            s.slot         = slot;
            s.connected    = true;

            m_sink->pushState(s);
            return;
        }
    }

    if (m_activeSlot != -1) {
        m_activeSlot = -1;
        ControllerState s;
        s.connected   = false;
        s.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        m_sink->pushState(s);
    }
}

// ── XInputPlugin ────────────────────────────────────────────────────────────

XInputPlugin::XInputPlugin() : m_source(std::make_unique<XInputSource>(this)) {}
XInputPlugin::~XInputPlugin() = default;

} // namespace Labs

extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin()
{
    return new Labs::XInputPlugin();
}
