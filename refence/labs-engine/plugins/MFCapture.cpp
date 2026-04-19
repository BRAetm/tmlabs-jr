// MFCapture.cpp — Media Foundation video capture plugin

#include "MFCapture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>

namespace Helios {

// ── MFCaptureWorker ───────────────────────────────────────────────────────────

MFCaptureWorker::MFCaptureWorker(QObject* parent) : QObject(parent) {}

MFCaptureWorker::~MFCaptureWorker() { stop(); }

void MFCaptureWorker::start(const QString& symbolicLink, int width, int height)
{
    MFStartup(MF_VERSION);

    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 2);
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                     symbolicLink.toStdWString().c_str());

    IMFMediaSource* source = nullptr;
    HRESULT hr = MFCreateDeviceSource(attrs, &source);
    attrs->Release();

    if (FAILED(hr)) { emit captureError("MFCreateDeviceSource failed"); return; }
    m_source  = source;

    IMFAttributes* readerAttrs = nullptr;
    MFCreateAttributes(&readerAttrs, 1);

    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromMediaSource(source, readerAttrs, &reader);
    readerAttrs->Release();

    if (FAILED(hr)) { emit captureError("MFCreateSourceReaderFromMediaSource failed"); return; }
    m_reader = reader;

    // Set output format to NV12
    IMFMediaType* mt = nullptr;
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(mt, MF_MT_FRAME_SIZE, width, height);
    reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt);
    mt->Release();

    m_running = true;
    readLoop();
}

void MFCaptureWorker::stop()
{
    m_running = false;
    if (m_reader) { reinterpret_cast<IMFSourceReader*>(m_reader)->Release(); m_reader = nullptr; }
    if (m_source) { reinterpret_cast<IMFMediaSource*>(m_source)->Shutdown();
                    reinterpret_cast<IMFMediaSource*>(m_source)->Release(); m_source = nullptr; }
    MFShutdown();
}

void MFCaptureWorker::readLoop()
{
    auto* reader = reinterpret_cast<IMFSourceReader*>(m_reader);
    while (m_running) {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;

        HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                         0, &streamIndex, &flags, &timestamp, &sample);
        if (FAILED(hr) || !sample) continue;

        IMFMediaBuffer* buf = nullptr;
        sample->ConvertToContiguousBuffer(&buf);
        if (buf) {
            BYTE* data = nullptr; DWORD maxLen = 0, curLen = 0;
            buf->Lock(&data, &maxLen, &curLen);

            // Read frame dimensions from current media type
            IMFMediaType* curType = nullptr;
            reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &curType);
            UINT32 w = 0, h = 0;
            MFGetAttributeSize(curType, MF_MT_FRAME_SIZE, &w, &h);
            curType->Release();

            QByteArray frameData(reinterpret_cast<const char*>(data), curLen);
            emit frameReady(w, h, w, frameData); // NV12 stride = width

            buf->Unlock();
            buf->Release();
        }
        sample->Release();
    }
}

// ── MFCapturePlugin ───────────────────────────────────────────────────────────

MFCapturePlugin::MFCapturePlugin(QObject* parent) : QObject(parent) {}
MFCapturePlugin::~MFCapturePlugin() { shutdown(); }

void MFCapturePlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new MFCaptureWorker;
    m_worker->moveToThread(m_thread);

    connect(this, &MFCapturePlugin::_start, m_worker, &MFCaptureWorker::start);
    connect(this, &MFCapturePlugin::_stop,  m_worker, &MFCaptureWorker::stop);
    connect(m_worker, &MFCaptureWorker::frameReady,   this, &MFCapturePlugin::onFrame);
    connect(m_worker, &MFCaptureWorker::captureError, this, &MFCapturePlugin::captureError);

    m_thread->start();
    m_ringWriter.open(SharedMemoryManager::blockName("VIDEO"), 1920, 1080, 2 /*NV12*/);
}

void MFCapturePlugin::shutdown()
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

QWidget* MFCapturePlugin::createWidget(QWidget* parent)
{
    auto* w      = new QWidget(parent);
    auto* layout = new QVBoxLayout(w);
    auto* bar    = new QHBoxLayout;

    auto* deviceCombo = new QComboBox(w);
    auto* widthSpin   = new QSpinBox(w);
    auto* heightSpin  = new QSpinBox(w);
    widthSpin->setRange(320, 3840);  widthSpin->setValue(1920);
    heightSpin->setRange(240, 2160); heightSpin->setValue(1080);

    auto* btnStart = new QPushButton("Start", w);
    auto* btnStop  = new QPushButton("Stop", w);

    bar->addWidget(deviceCombo, 2);
    bar->addWidget(widthSpin);
    bar->addWidget(new QLabel("x", w));
    bar->addWidget(heightSpin);
    bar->addWidget(btnStart);
    bar->addWidget(btnStop);
    layout->addLayout(bar);

    const auto devices = MFCapturePlugin::enumerateDevices();
    for (const auto& d : devices)
        deviceCombo->addItem(d.name, d.symbolicLink);

    connect(btnStart, &QPushButton::clicked, [=]() {
        emit _start(deviceCombo->currentData().toString(),
                    widthSpin->value(), heightSpin->value());
    });
    connect(btnStop, &QPushButton::clicked, [=]() { emit _stop(); });

    return w;
}

QList<MFCaptureDevice> MFCapturePlugin::enumerateDevices()
{
    QList<MFCaptureDevice> result;
    MFStartup(MF_VERSION);

    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 1);
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    MFEnumDeviceSources(attrs, &devices, &count);
    attrs->Release();

    for (UINT32 i = 0; i < count; ++i) {
        WCHAR* name = nullptr; UINT32 nameLen = 0;
        WCHAR* link = nullptr; UINT32 linkLen = 0;
        devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen);
        devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &linkLen);

        MFCaptureDevice d;
        d.name         = QString::fromWCharArray(name);
        d.symbolicLink = QString::fromWCharArray(link);
        result.append(d);

        CoTaskMemFree(name);
        CoTaskMemFree(link);
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    MFShutdown();
    return result;
}

void MFCapturePlugin::onFrame(int width, int height, int stride, QByteArray data)
{
    emit frameReceived(width, height, stride, data);

    VideoFrameData frame = {};
    frame.width  = width;
    frame.height = height;
    frame.stride = stride;
    frame.format = 2; // NV12
    frame.data   = reinterpret_cast<uint8_t*>(data.data());
    frame.size   = data.size();
    m_ringWriter.write(frame);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::MFCapturePlugin();
}
