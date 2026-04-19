// TitanBridge.cpp — Titan Two USB bridge plugin

#include "TitanBridge.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QFileDialog>
#include <QFile>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace Helios {

// ── TitanBridgeWorker ─────────────────────────────────────────────────────────

TitanBridgeWorker::TitanBridgeWorker(QObject* parent) : QObject(parent) {}

TitanBridgeWorker::~TitanBridgeWorker() { stop(); }

bool TitanBridgeWorker::openDevice()
{
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
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attrs = {};
                attrs.Size = sizeof(attrs);
                HidD_GetAttributes(h, &attrs);
                // Titan Two: VID 0x1993, PID 0x00F0
                if (attrs.VendorID == 0x1993 && attrs.ProductID == 0x00F0) {
                    m_deviceHandle = h;
                    free(detail);
                    SetupDiDestroyDeviceInfoList(devInfo);
                    return true;
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return false;
}

void TitanBridgeWorker::closeDevice()
{
    if (m_deviceHandle) {
        CloseHandle(reinterpret_cast<HANDLE>(m_deviceHandle));
        m_deviceHandle = nullptr;
    }
}

void TitanBridgeWorker::start()
{
    m_running = true;
    if (openDevice()) {
        emit deviceConnected();
        m_pollTimer = new QTimer(this);
        connect(m_pollTimer, &QTimer::timeout, this, &TitanBridgeWorker::poll);
        m_pollTimer->start(4);
    }
}

void TitanBridgeWorker::stop()
{
    m_running = false;
    if (m_pollTimer) { m_pollTimer->stop(); m_pollTimer = nullptr; }
    if (m_deviceHandle) {
        closeDevice();
        emit deviceDisconnected();
    }
}

void TitanBridgeWorker::poll()
{
    if (!m_deviceHandle || !m_running) return;

    uint8_t buf[65] = {};
    DWORD bytesRead = 0;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (ReadFile(reinterpret_cast<HANDLE>(m_deviceHandle), buf, sizeof(buf), nullptr, &ov) ||
        (GetLastError() == ERROR_IO_PENDING &&
         WaitForSingleObject(ov.hEvent, 4) == WAIT_OBJECT_0 &&
         GetOverlappedResult(reinterpret_cast<HANDLE>(m_deviceHandle), &ov, &bytesRead, FALSE))) {
        if (bytesRead > 0) {
            TitanControllerReport report = {};
            memcpy(report.data, buf + 1, qMin<int>(64, bytesRead - 1));
            report.timestamp_ms = GetTickCount();
            emit controllerReceived(report);
        }
    }
    CloseHandle(ov.hEvent);
}

void TitanBridgeWorker::uploadScript(const QByteArray& gpc3Bytecode, TitanSlot slot)
{
    if (!m_deviceHandle) { emit error("Titan Two not connected"); return; }

    // T2 upload protocol: feature report 0x30 (slot select) then 0x31 chunks
    uint8_t slotSelect[65] = {};
    slotSelect[0] = 0x00; // report ID
    slotSelect[1] = 0x30; // command: slot select
    slotSelect[2] = static_cast<uint8_t>(slot);
    HidD_SetFeature(reinterpret_cast<HANDLE>(m_deviceHandle), slotSelect, sizeof(slotSelect));

    // Upload in 60-byte chunks
    int offset = 0;
    uint16_t chunkIndex = 0;
    while (offset < gpc3Bytecode.size()) {
        uint8_t chunk[65] = {};
        chunk[0] = 0x00;
        chunk[1] = 0x31; // command: upload chunk
        chunk[2] = (chunkIndex >> 8) & 0xFF;
        chunk[3] = chunkIndex & 0xFF;
        int len = qMin(60, gpc3Bytecode.size() - offset);
        memcpy(chunk + 4, gpc3Bytecode.constData() + offset, len);
        HidD_SetFeature(reinterpret_cast<HANDLE>(m_deviceHandle), chunk, sizeof(chunk));
        offset += len;
        ++chunkIndex;
    }

    // Finalize upload
    uint8_t finalize[65] = {};
    finalize[0] = 0x00;
    finalize[1] = 0x32; // command: finalize
    HidD_SetFeature(reinterpret_cast<HANDLE>(m_deviceHandle), finalize, sizeof(finalize));

    emit scriptUploaded(slot);
}

void TitanBridgeWorker::selectSlot(TitanSlot slot)
{
    if (!m_deviceHandle) return;
    uint8_t report[65] = {};
    report[0] = 0x00;
    report[1] = 0x30;
    report[2] = static_cast<uint8_t>(slot);
    HidD_SetFeature(reinterpret_cast<HANDLE>(m_deviceHandle), report, sizeof(report));
}

void TitanBridgeWorker::sendControllerData(const TitanControllerReport& report)
{
    if (!m_deviceHandle) return;
    uint8_t buf[65] = {};
    buf[0] = 0x00;
    memcpy(buf + 1, report.data, 64);
    DWORD written = 0;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    WriteFile(reinterpret_cast<HANDLE>(m_deviceHandle), buf, sizeof(buf), nullptr, &ov);
    WaitForSingleObject(ov.hEvent, 8);
    CloseHandle(ov.hEvent);
}

// ── TitanBridgePlugin ─────────────────────────────────────────────────────────

TitanBridgePlugin::TitanBridgePlugin(QObject* parent) : QObject(parent) {}
TitanBridgePlugin::~TitanBridgePlugin() { shutdown(); }

void TitanBridgePlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new TitanBridgeWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,  m_worker, &TitanBridgeWorker::start);
    connect(m_thread, &QThread::finished, m_worker, &TitanBridgeWorker::stop);
    connect(m_worker, &TitanBridgeWorker::deviceConnected,    this, &TitanBridgePlugin::titanConnected);
    connect(m_worker, &TitanBridgeWorker::deviceDisconnected, this, &TitanBridgePlugin::titanDisconnected);
    connect(m_worker, &TitanBridgeWorker::controllerReceived, this, &TitanBridgePlugin::onControllerReceived);
    connect(m_worker, &TitanBridgeWorker::scriptUploaded,     this, &TitanBridgePlugin::scriptUploaded);
    connect(m_worker, &TitanBridgeWorker::error,              this, &TitanBridgePlugin::bridgeError);
    connect(this, &TitanBridgePlugin::_uploadScript, m_worker, &TitanBridgeWorker::uploadScript);
    connect(this, &TitanBridgePlugin::_selectSlot,   m_worker, &TitanBridgeWorker::selectSlot);
    connect(this, &TitanBridgePlugin::_sendController, m_worker, &TitanBridgeWorker::sendControllerData);

    m_thread->start();
}

void TitanBridgePlugin::shutdown()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
        delete m_worker;
        m_worker = nullptr;
    }
}

QWidget* TitanBridgePlugin::createWidget(QWidget* parent)
{
    auto* w      = new QWidget(parent);
    auto* layout = new QVBoxLayout(w);
    auto* bar    = new QHBoxLayout;

    auto* lblStatus  = new QLabel("Disconnected", w);
    auto* slotCombo  = new QComboBox(w);
    for (int i = 1; i <= 8; ++i)
        slotCombo->addItem(QString("Slot %1").arg(i));

    auto* btnUpload  = new QPushButton("Upload GPC3", w);
    auto* btnSelect  = new QPushButton("Activate Slot", w);

    bar->addWidget(lblStatus, 1);
    bar->addWidget(new QLabel("Slot:", w));
    bar->addWidget(slotCombo);
    bar->addWidget(btnUpload);
    bar->addWidget(btnSelect);
    layout->addLayout(bar);

    connect(this, &TitanBridgePlugin::titanConnected,    [=]() { lblStatus->setText("Connected"); });
    connect(this, &TitanBridgePlugin::titanDisconnected, [=]() { lblStatus->setText("Disconnected"); });

    connect(btnUpload, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getOpenFileName(w, "Select GPC3 Script",
            QString(), "GPC3 Scripts (*.gpc *.gpc3 *.bin);;All Files (*)");
        if (!path.isEmpty()) {
            TitanSlot slot = static_cast<TitanSlot>(slotCombo->currentIndex() + 1);
            uploadGPC3(path, slot);
        }
    });

    connect(btnSelect, &QPushButton::clicked, [=]() {
        TitanSlot slot = static_cast<TitanSlot>(slotCombo->currentIndex() + 1);
        selectSlot(slot);
    });

    return w;
}

void TitanBridgePlugin::uploadGPC3(const QString& scriptPath, TitanSlot slot)
{
    QFile f(scriptPath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit bridgeError("Cannot open: " + scriptPath);
        return;
    }
    QByteArray bytecode = f.readAll();
    emit _uploadScript(bytecode, slot);
}

void TitanBridgePlugin::selectSlot(TitanSlot slot)
{
    emit _selectSlot(slot);
    emit slotSelected(slot);
}

void TitanBridgePlugin::onControllerReceived(const TitanControllerReport& report)
{
    Q_UNUSED(report);
    // Write to shared memory output ring buffer for other plugins to consume
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::TitanBridgePlugin();
}
