// ViGEmOutput.cpp — ViGEm Bus virtual controller output plugin

#include "ViGEmOutput.h"
#include <QLibrary>
#include <QString>

// ViGEm function pointer typedefs (loaded dynamically from ViGEmClient.dll)
typedef PVIGEM_CLIENT (*pfn_vigem_alloc)();
typedef void         (*pfn_vigem_free)(PVIGEM_CLIENT);
typedef int          (*pfn_vigem_connect)(PVIGEM_CLIENT);
typedef void         (*pfn_vigem_disconnect)(PVIGEM_CLIENT);
typedef PVIGEM_TARGET (*pfn_vigem_target_ds4_alloc)();
typedef PVIGEM_TARGET (*pfn_vigem_target_x360_alloc)();
typedef void         (*pfn_vigem_target_free)(PVIGEM_TARGET);
typedef int          (*pfn_vigem_target_add)(PVIGEM_CLIENT, PVIGEM_TARGET);
typedef int          (*pfn_vigem_target_remove)(PVIGEM_CLIENT, PVIGEM_TARGET);
typedef int          (*pfn_vigem_target_ds4_update)(PVIGEM_CLIENT, PVIGEM_TARGET, void*);
typedef int          (*pfn_vigem_target_x360_update)(PVIGEM_CLIENT, PVIGEM_TARGET, void*);

namespace Helios {

ViGEmOutputWorker::ViGEmOutputWorker(QObject* parent) : QObject(parent) {}

ViGEmOutputWorker::~ViGEmOutputWorker() { stop(); }

void ViGEmOutputWorker::start(ViGEmTargetType type)
{
    m_type = type;

    QLibrary lib("ViGEmClient");
    if (!lib.load()) {
        emit error("ViGEmClient.dll not found — is ViGEm Bus driver installed?");
        return;
    }

    auto vigem_alloc      = (pfn_vigem_alloc)     lib.resolve("vigem_alloc");
    auto vigem_connect    = (pfn_vigem_connect)    lib.resolve("vigem_connect");
    auto vigem_target_ds4_alloc  = (pfn_vigem_target_ds4_alloc)  lib.resolve("vigem_target_ds4_alloc");
    auto vigem_target_x360_alloc = (pfn_vigem_target_x360_alloc) lib.resolve("vigem_target_x360_alloc");
    auto vigem_target_add = (pfn_vigem_target_add) lib.resolve("vigem_target_add");

    if (!vigem_alloc || !vigem_connect || !vigem_target_ds4_alloc || !vigem_target_add) {
        emit error("Failed to resolve ViGEmClient exports");
        return;
    }

    m_client = vigem_alloc();
    if (vigem_connect(m_client) != 0) {
        emit error("vigem_connect failed — ViGEm Bus driver not running");
        return;
    }

    m_target = (type == ViGEmTargetType::DS4)
               ? vigem_target_ds4_alloc()
               : vigem_target_x360_alloc();

    if (vigem_target_add(m_client, m_target) != 0) {
        emit error("vigem_target_add failed");
        return;
    }

    emit connected();
}

void ViGEmOutputWorker::stop()
{
    QLibrary lib("ViGEmClient");
    if (!lib.load()) return;

    auto vigem_target_remove = (pfn_vigem_target_remove) lib.resolve("vigem_target_remove");
    auto vigem_target_free   = (pfn_vigem_target_free)   lib.resolve("vigem_target_free");
    auto vigem_disconnect    = (pfn_vigem_disconnect)    lib.resolve("vigem_disconnect");
    auto vigem_free          = (pfn_vigem_free)          lib.resolve("vigem_free");

    if (m_target && vigem_target_remove && vigem_target_free) {
        vigem_target_remove(m_client, m_target);
        vigem_target_free(m_target);
        m_target = nullptr;
    }
    if (m_client && vigem_disconnect && vigem_free) {
        vigem_disconnect(m_client);
        vigem_free(m_client);
        m_client = nullptr;
    }
    emit disconnected();
}

void ViGEmOutputWorker::sendReport(const DS4OutputReport& report)
{
    if (!m_client || !m_target) return;

    QLibrary lib("ViGEmClient");
    if (!lib.load()) return;

    if (m_type == ViGEmTargetType::DS4) {
        // DS4_REPORT layout matches ViGEm API exactly
        struct DS4_REPORT {
            uint16_t wButtons;
            uint8_t  bSpecial;
            uint8_t  bThumbLX;
            uint8_t  bThumbLY;
            uint8_t  bThumbRX;
            uint8_t  bThumbRY;
            uint8_t  bTriggerL;
            uint8_t  bTriggerR;
        } ds4 = {};
        ds4.wButtons  = report.buttons;
        ds4.bSpecial  = report.special;
        ds4.bThumbLX  = report.thumb_lx;
        ds4.bThumbLY  = report.thumb_ly;
        ds4.bThumbRX  = report.thumb_rx;
        ds4.bThumbRY  = report.thumb_ry;
        ds4.bTriggerL = report.trigger_l;
        ds4.bTriggerR = report.trigger_r;

        auto fn = (pfn_vigem_target_ds4_update) lib.resolve("vigem_target_ds4_update");
        if (fn) fn(m_client, m_target, &ds4);
    } else {
        // XUSB_REPORT layout for Xbox 360
        struct XUSB_REPORT {
            uint16_t wButtons;
            uint8_t  bLeftTrigger;
            uint8_t  bRightTrigger;
            int16_t  sThumbLX;
            int16_t  sThumbLY;
            int16_t  sThumbRX;
            int16_t  sThumbRY;
        } xusb = {};
        xusb.wButtons      = report.buttons;
        xusb.bLeftTrigger  = report.trigger_l;
        xusb.bRightTrigger = report.trigger_r;
        // Convert 0-255 to -32768..32767
        xusb.sThumbLX = (int16_t)((report.thumb_lx - 128) * 257);
        xusb.sThumbLY = (int16_t)((128 - report.thumb_ly) * 257);
        xusb.sThumbRX = (int16_t)((report.thumb_rx - 128) * 257);
        xusb.sThumbRY = (int16_t)((128 - report.thumb_ry) * 257);

        auto fn = (pfn_vigem_target_x360_update) lib.resolve("vigem_target_x360_update");
        if (fn) fn(m_client, m_target, &xusb);
    }
}

// ── ViGEmOutputPlugin ─────────────────────────────────────────────────────────

ViGEmOutputPlugin::ViGEmOutputPlugin(QObject* parent) : QObject(parent) {}
ViGEmOutputPlugin::~ViGEmOutputPlugin() { shutdown(); }

void ViGEmOutputPlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new ViGEmOutputWorker;
    m_worker->moveToThread(m_thread);

    connect(this,     &ViGEmOutputPlugin::_start,    m_worker, &ViGEmOutputWorker::start);
    connect(this,     &ViGEmOutputPlugin::_stop,     m_worker, &ViGEmOutputWorker::stop);
    connect(this,     &ViGEmOutputPlugin::_sendReport, m_worker, &ViGEmOutputWorker::sendReport);
    connect(m_worker, &ViGEmOutputWorker::connected,   this, &ViGEmOutputPlugin::virtualControllerConnected);
    connect(m_worker, &ViGEmOutputWorker::disconnected, this, &ViGEmOutputPlugin::virtualControllerDisconnected);
    connect(m_worker, &ViGEmOutputWorker::error,       this, &ViGEmOutputPlugin::outputError);

    m_thread->start();
    emit _start(m_targetType);
}

void ViGEmOutputPlugin::shutdown()
{
    if (m_thread) {
        emit _stop();
        m_thread->quit();
        m_thread->wait(2000);
        delete m_worker;
        m_worker = nullptr;
    }
}

void ViGEmOutputPlugin::setTargetType(ViGEmTargetType type)
{
    m_targetType = type;
}

void ViGEmOutputPlugin::onControllerData(const SharedMemoryControllerState& state)
{
    DS4OutputReport report = {};
    // Raw 32-byte DS5 format → DS4 output report
    report.thumb_lx  = state.data[0];
    report.thumb_ly  = state.data[1];
    report.thumb_rx  = state.data[2];
    report.thumb_ry  = state.data[3];
    report.trigger_l = state.data[4];
    report.trigger_r = state.data[5];
    memcpy(&report.buttons, state.data + 6, 2);
    report.special   = state.data[8];

    emit _sendReport(report);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::ViGEmOutputPlugin();
}
