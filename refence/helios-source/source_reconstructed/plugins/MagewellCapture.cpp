// MagewellCapture.cpp — Magewell hardware capture card plugin

#include "MagewellCapture.h"

// Magewell SDK — dynamically loaded from LibMWCapture.dll
// SDK available at: https://www.magewell.com/downloads/sdk
#include <LibMWCapture/MWCapture.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QLibrary>

namespace Helios {

// ── MagewellCaptureWorker ─────────────────────────────────────────────────────

MagewellCaptureWorker::MagewellCaptureWorker(QObject* parent) : QObject(parent) {}

MagewellCaptureWorker::~MagewellCaptureWorker() { stop(); }

void MagewellCaptureWorker::start(int channelIndex, int width, int height)
{
    m_width  = width;
    m_height = height;

    MWRefreshDevice();
    HCHANNEL ch = MWOpenChannel(0, channelIndex);
    if (!ch) {
        emit captureError(QString("Cannot open Magewell channel %1").arg(channelIndex));
        return;
    }
    m_channel = ch;

    // Set capture format: BGR24 at requested resolution
    MWCAP_VIDEO_CAPTURE_OPEN captureOpen = {};
    captureOpen.cx        = width;
    captureOpen.cy        = height;
    captureOpen.fourcc    = MWFOURCC_BGR24;
    captureOpen.frameRate = 60 * 10000; // 60 fps

    HNOTIFY notify = MWStartVideoCapture(ch, nullptr);
    m_capture  = notify;
    m_running  = true;

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MagewellCaptureWorker::grab);
    m_timer->start(16); // ~60 fps polling
}

void MagewellCaptureWorker::stop()
{
    m_running = false;
    if (m_timer) { m_timer->stop(); m_timer = nullptr; }
    if (m_capture) {
        MWStopVideoCapture(reinterpret_cast<HCHANNEL>(m_channel));
        m_capture = nullptr;
    }
    if (m_channel) {
        MWCloseChannel(reinterpret_cast<HCHANNEL>(m_channel));
        m_channel = nullptr;
    }
}

void MagewellCaptureWorker::grab()
{
    if (!m_running || !m_channel) return;

    int stride     = m_width * 3;
    QByteArray buf(stride * m_height, '\0');
    LONGLONG pts   = 0;
    MWCAP_VIDEO_BUFFER_INFO info = {};

    MW_RESULT res = MWCaptureVideoFrameToVirtualAddress(
        reinterpret_cast<HCHANNEL>(m_channel),
        -1,                           // latest frame
        reinterpret_cast<LPBYTE>(buf.data()),
        buf.size(),
        stride,
        FALSE,                        // bottom-up = FALSE for top-down
        nullptr,
        MWFOURCC_BGR24,
        m_width, m_height,
        nullptr);

    if (res == MW_SUCCEEDED)
        emit frameReady(m_width, m_height, buf);
}

// ── MagewellCapturePlugin ─────────────────────────────────────────────────────

MagewellCapturePlugin::MagewellCapturePlugin(QObject* parent) : QObject(parent) {}
MagewellCapturePlugin::~MagewellCapturePlugin() { shutdown(); }

void MagewellCapturePlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new MagewellCaptureWorker;
    m_worker->moveToThread(m_thread);

    connect(this, &MagewellCapturePlugin::_start, m_worker, &MagewellCaptureWorker::start);
    connect(this, &MagewellCapturePlugin::_stop,  m_worker, &MagewellCaptureWorker::stop);
    connect(m_worker, &MagewellCaptureWorker::frameReady,   this, &MagewellCapturePlugin::onFrame);
    connect(m_worker, &MagewellCaptureWorker::captureError, this, &MagewellCapturePlugin::captureError);

    m_thread->start();
    m_ringWriter.open(SharedMemoryManager::blockName("VIDEO"), 1920, 1080, 0 /*BGR24*/);
}

void MagewellCapturePlugin::shutdown()
{
    m_ringWriter.close();
    if (m_thread) {
        emit _stop();
        m_thread->quit();
        m_thread->wait(3000);
        delete m_worker;
        m_worker = nullptr;
    }
}

QWidget* MagewellCapturePlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);
    auto* bar = new QHBoxLayout;

    auto* deviceCombo = new QComboBox(w);
    auto* widthSpin   = new QSpinBox(w); widthSpin->setRange(320, 3840); widthSpin->setValue(1920);
    auto* heightSpin  = new QSpinBox(w); heightSpin->setRange(240, 2160); heightSpin->setValue(1080);
    auto* btnStart    = new QPushButton("Start", w);
    auto* btnStop     = new QPushButton("Stop", w);

    bar->addWidget(deviceCombo, 2);
    bar->addWidget(widthSpin);
    bar->addWidget(new QLabel("x", w));
    bar->addWidget(heightSpin);
    bar->addWidget(btnStart);
    bar->addWidget(btnStop);
    lay->addLayout(bar);

    const auto devices = MagewellCapturePlugin::enumerateDevices();
    for (const auto& d : devices)
        deviceCombo->addItem(
            QString("%1 — %2").arg(d.boardName).arg(d.channelName),
            d.channelIndex);

    if (devices.isEmpty())
        deviceCombo->addItem("No Magewell device detected");

    connect(btnStart, &QPushButton::clicked, [=]() {
        int idx = deviceCombo->currentData().toInt();
        emit _start(idx, widthSpin->value(), heightSpin->value());
    });
    connect(btnStop, &QPushButton::clicked, [=]() { emit _stop(); });

    return w;
}

QList<MagewellDevice> MagewellCapturePlugin::enumerateDevices()
{
    QList<MagewellDevice> result;
    MWRefreshDevice();

    int count = MWGetChannelCount();
    for (int i = 0; i < count; ++i) {
        MWCAP_CHANNEL_INFO info = {};
        HCHANNEL ch = MWOpenChannel(0, i);
        if (!ch) continue;
        MWGetChannelInfo(ch, &info);
        MWCloseChannel(ch);

        MagewellDevice d;
        d.channelIndex = i;
        d.boardName    = QString::fromUtf8(info.szBoardSerialNo);
        d.channelName  = QString("Channel %1").arg(i);
        result.append(d);
    }
    return result;
}

void MagewellCapturePlugin::onFrame(int width, int height, QByteArray bgrData)
{
    emit frameReceived(width, height, bgrData);

    VideoFrameData frame = {};
    frame.width  = width;
    frame.height = height;
    frame.stride = width * 3;
    frame.format = 0;
    frame.data   = reinterpret_cast<uint8_t*>(bgrData.data());
    frame.size   = bgrData.size();
    m_ringWriter.write(frame);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::MagewellCapturePlugin();
}
