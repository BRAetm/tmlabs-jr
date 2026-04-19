// XInputInput.cpp — XInput gamepad polling plugin

#include "XInputInput.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#pragma comment(lib, "xinput.lib")

namespace Helios {

XInputWorker::XInputWorker(QObject* parent) : QObject(parent) {}

void XInputWorker::start()
{
    m_running = true;
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &XInputWorker::poll);
    m_timer->start(8); // ~125 Hz
}

void XInputWorker::stop()
{
    m_running = false;
    if (m_timer) { m_timer->stop(); m_timer = nullptr; }
}

void XInputWorker::poll()
{
    if (!m_running) return;

    for (int i = 0; i < 4; ++i) {
        XINPUT_STATE xs = {};
        DWORD result = XInputGetState(i, &xs);

        if (result == ERROR_SUCCESS) {
            if (!m_connected[i]) {
                m_connected[i] = true;
                emit deviceConnected(i);
            }
            if (xs.dwPacketNumber != m_lastPacket[i]) {
                m_lastPacket[i] = xs.dwPacketNumber;
                XInputState state = {};
                state.packet_number = xs.dwPacketNumber;
                state.buttons       = xs.Gamepad.wButtons;
                state.left_trigger  = xs.Gamepad.bLeftTrigger;
                state.right_trigger = xs.Gamepad.bRightTrigger;
                state.thumb_lx      = xs.Gamepad.sThumbLX;
                state.thumb_ly      = xs.Gamepad.sThumbLY;
                state.thumb_rx      = xs.Gamepad.sThumbRX;
                state.thumb_ry      = xs.Gamepad.sThumbRY;
                emit stateChanged(i, state);
            }
        } else {
            if (m_connected[i]) {
                m_connected[i] = false;
                emit deviceDisconnected(i);
            }
        }
    }
}

// ── XInputInputPlugin ─────────────────────────────────────────────────────────

XInputInputPlugin::XInputInputPlugin(QObject* parent) : QObject(parent) {}
XInputInputPlugin::~XInputInputPlugin() { shutdown(); }

void XInputInputPlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new XInputWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,  m_worker, &XInputWorker::start);
    connect(m_thread, &QThread::finished, m_worker, &XInputWorker::stop);
    connect(m_worker, &XInputWorker::stateChanged,    this, &XInputInputPlugin::stateChanged);
    connect(m_worker, &XInputWorker::deviceConnected,  this, &XInputInputPlugin::deviceConnected);
    connect(m_worker, &XInputWorker::deviceDisconnected, this, &XInputInputPlugin::deviceDisconnected);

    m_thread->start();
}

void XInputInputPlugin::shutdown()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
        delete m_worker;
        m_worker = nullptr;
    }
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::XInputInputPlugin();
}
