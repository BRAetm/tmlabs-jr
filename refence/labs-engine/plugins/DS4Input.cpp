// DS4Input.cpp — DualShock 4 HID input plugin

#include "DS4Input.h"
#include <QThread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace Helios {

DS4InputWorker::DS4InputWorker(QObject* parent) : QObject(parent) {}
DS4InputWorker::~DS4InputWorker() { stop(); }

void DS4InputWorker::start()
{
    m_running = true;

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
                // DS4 gen1: 054C:05C4  gen2: 054C:09CC
                if (attrs.VendorID == 0x054C &&
                    (attrs.ProductID == 0x05C4 || attrs.ProductID == 0x09CC)) {
                    m_deviceHandle = h;
                    free(detail);
                    SetupDiDestroyDeviceInfoList(devInfo);
                    emit deviceConnected();

                    m_pollTimer = new QTimer(this);
                    connect(m_pollTimer, &QTimer::timeout, this, &DS4InputWorker::pollDevices);
                    m_pollTimer->start(4);
                    return;
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
}

void DS4InputWorker::stop()
{
    m_running = false;
    if (m_pollTimer) { m_pollTimer->stop(); m_pollTimer = nullptr; }
    if (m_deviceHandle) {
        CloseHandle(reinterpret_cast<HANDLE>(m_deviceHandle));
        m_deviceHandle = nullptr;
        emit deviceDisconnected();
    }
}

void DS4InputWorker::pollDevices()
{
    if (!m_deviceHandle || !m_running) return;
    uint8_t buf[65] = {};
    DWORD bytesRead = 0;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (ReadFile(reinterpret_cast<HANDLE>(m_deviceHandle), buf, sizeof(buf), &bytesRead, &ov) ||
        (GetLastError() == ERROR_IO_PENDING &&
         WaitForSingleObject(ov.hEvent, 4) == WAIT_OBJECT_0 &&
         GetOverlappedResult(reinterpret_cast<HANDLE>(m_deviceHandle), &ov, &bytesRead, FALSE))) {
        if (bytesRead >= sizeof(DS4Report)) {
            DS4Report report;
            memcpy(&report, buf, sizeof(DS4Report));
            emit reportReceived(report);
        }
    }
    CloseHandle(ov.hEvent);
}

// ── DS4InputPlugin ────────────────────────────────────────────────────────────

DS4InputPlugin::DS4InputPlugin(QObject* parent) : QObject(parent) {}
DS4InputPlugin::~DS4InputPlugin() { shutdown(); }

void DS4InputPlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new DS4InputWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,  m_worker, &DS4InputWorker::start);
    connect(m_thread, &QThread::finished, m_worker, &DS4InputWorker::stop);
    connect(m_worker, &DS4InputWorker::reportReceived,    this, &DS4InputPlugin::controllerReport);
    connect(m_worker, &DS4InputWorker::deviceConnected,   this, &DS4InputPlugin::deviceConnected);
    connect(m_worker, &DS4InputWorker::deviceDisconnected, this, &DS4InputPlugin::deviceDisconnected);

    m_thread->start();
}

void DS4InputPlugin::shutdown()
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
    return new Helios::DS4InputPlugin();
}
