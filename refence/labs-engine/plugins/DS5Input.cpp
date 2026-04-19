// DS5Input.cpp — DualSense HID input plugin

#include "DS5Input.h"
#include "../helios_core/LoggingService.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace Helios {

// ── DS5InputWorker ────────────────────────────────────────────────────────────

DS5InputWorker::DS5InputWorker(QObject* parent) : QObject(parent) {}

DS5InputWorker::~DS5InputWorker() { stop(); }

void DS5InputWorker::start()
{
    m_running   = true;
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &DS5InputWorker::pollDevices);
    m_pollTimer->start(4); // ~250 Hz

    // Open DualSense HID device
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, nullptr, 0, &needed, nullptr);

        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(malloc(needed));
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        if (SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, detail, needed, nullptr, nullptr)) {
            HANDLE h = CreateFile(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attrs = {};
                attrs.Size = sizeof(attrs);
                HidD_GetAttributes(h, &attrs);

                // VID 054C = Sony, PID 0CE6 = DualSense
                if (attrs.VendorID == 0x054C && attrs.ProductID == 0x0CE6) {
                    m_deviceHandle = h;
                    free(detail);
                    SetupDiDestroyDeviceInfoList(devInfo);
                    emit deviceConnected();
                    return;
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
}

void DS5InputWorker::stop()
{
    m_running = false;
    if (m_pollTimer) { m_pollTimer->stop(); m_pollTimer = nullptr; }
    if (m_deviceHandle) {
        CloseHandle(reinterpret_cast<HANDLE>(m_deviceHandle));
        m_deviceHandle = nullptr;
        emit deviceDisconnected();
    }
}

void DS5InputWorker::pollDevices()
{
    if (!m_deviceHandle || !m_running) return;

    uint8_t buf[65] = {};
    DWORD bytesRead = 0;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (ReadFile(reinterpret_cast<HANDLE>(m_deviceHandle), buf, sizeof(buf), &bytesRead, &ov)) {
        if (bytesRead >= sizeof(DS5Report)) {
            DS5Report report;
            memcpy(&report, buf + 1, sizeof(DS5Report)); // skip report ID byte in buffer
            report.report_id = buf[0];
            emit reportReceived(report);
        }
    } else if (GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 4) == WAIT_OBJECT_0) {
            GetOverlappedResult(reinterpret_cast<HANDLE>(m_deviceHandle), &ov, &bytesRead, FALSE);
            if (bytesRead >= sizeof(DS5Report)) {
                DS5Report report;
                memcpy(&report, buf + 1, sizeof(DS5Report));
                report.report_id = buf[0];
                emit reportReceived(report);
            }
        }
    }

    CloseHandle(ov.hEvent);
}

// ── DS5InputPlugin ────────────────────────────────────────────────────────────

DS5InputPlugin::DS5InputPlugin(QObject* parent) : QObject(parent) {}

DS5InputPlugin::~DS5InputPlugin() { shutdown(); }

void DS5InputPlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new DS5InputWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,  m_worker, &DS5InputWorker::start);
    connect(m_thread, &QThread::finished, m_worker, &DS5InputWorker::stop);
    connect(m_worker, &DS5InputWorker::reportReceived,   this, &DS5InputPlugin::onReport);
    connect(m_worker, &DS5InputWorker::deviceConnected,  this, &DS5InputPlugin::deviceConnected);
    connect(m_worker, &DS5InputWorker::deviceDisconnected, this, &DS5InputPlugin::deviceDisconnected);

    m_thread->start();
}

void DS5InputPlugin::shutdown()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
        delete m_worker;
        m_worker = nullptr;
    }
}

void DS5InputPlugin::onReport(const DS5Report& report)
{
    emit controllerReport(report);
    writeToSharedMemory(report);
}

void DS5InputPlugin::writeToSharedMemory(const DS5Report& report)
{
    if (!m_shmWriter) return;

    SharedMemoryControllerState state = {};
    // Copy raw 32 bytes of DS5 report (excluding report_id byte)
    static_assert(sizeof(state.data) == 32, "state.data must be 32 bytes");
    memcpy(state.data, &report.left_stick_x, 32);
    state.timestamp_ms = GetTickCount();
    state.source       = 0; // DS5

    m_shmWriter->connect();
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::DS5InputPlugin();
}
