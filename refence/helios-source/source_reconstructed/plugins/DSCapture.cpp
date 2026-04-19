// DSCapture.cpp — DirectShow video capture plugin

#include "DSCapture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <qedit.h>  // ISampleGrabber

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "quartz.lib")

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>

namespace Helios {

// ── Sample Grabber callback ────────────────────────────────────────────────────

class SampleGrabberCallback : public ISampleGrabberCB {
public:
    DSCaptureWorker* worker = nullptr;
    int width = 0, height = 0, stride = 0;
    DSPixelFormat fmt = DSPixelFormat::BGR24;

    STDMETHODIMP_(ULONG) AddRef()  override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_ISampleGrabberCB || riid == IID_IUnknown) { *ppv = this; return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP SampleCB(double, IMediaSample*) override { return S_OK; }
    STDMETHODIMP BufferCB(double, BYTE* pBuffer, long bufLen) override {
        if (worker) {
            QByteArray data(reinterpret_cast<const char*>(pBuffer), bufLen);
            emit worker->frameReady(width, height, stride, fmt, data);
        }
        return S_OK;
    }
};

// ── DSCaptureWorker ───────────────────────────────────────────────────────────

DSCaptureWorker::DSCaptureWorker(QObject* parent) : QObject(parent) {}

DSCaptureWorker::~DSCaptureWorker() { stop(); }

void DSCaptureWorker::start(const QString& devicePath, int width, int height, DSPixelFormat fmt)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    buildGraph(devicePath, width, height, fmt);
}

void DSCaptureWorker::stop()
{
    teardown();
    CoUninitialize();
}

void DSCaptureWorker::buildGraph(const QString& devicePath, int width, int height, DSPixelFormat fmt)
{
    HRESULT hr;

    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IGraphBuilder, reinterpret_cast<void**>(&m_graph));
    if (FAILED(hr)) { emit captureError("Failed to create filter graph"); return; }

    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ICaptureGraphBuilder2, reinterpret_cast<void**>(&m_captureGraph));
    if (FAILED(hr)) { emit captureError("Failed to create capture graph builder"); return; }

    m_captureGraph->SetFiltergraph(m_graph);

    // Enumerate and bind device by path
    ICreateDevEnum* devEnum = nullptr;
    CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                     IID_ICreateDevEnum, reinterpret_cast<void**>(&devEnum));

    IEnumMoniker* enumMon = nullptr;
    devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMon, 0);
    devEnum->Release();

    IMoniker* moniker = nullptr;
    while (enumMon && enumMon->Next(1, &moniker, nullptr) == S_OK) {
        IPropertyBag* propBag = nullptr;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                              reinterpret_cast<void**>(&propBag)))) {
            VARIANT var;
            VariantInit(&var);
            propBag->Read(L"DevicePath", &var, nullptr);
            QString path = QString::fromWCharArray(var.bstrVal);
            VariantClear(&var);
            propBag->Release();

            if (path == devicePath) {
                moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter,
                                      reinterpret_cast<void**>(&m_sourceFilter));
                moniker->Release();
                break;
            }
        }
        moniker->Release();
    }
    if (enumMon) enumMon->Release();

    if (!m_sourceFilter) { emit captureError("Device not found: " + devicePath); return; }
    m_graph->AddFilter(m_sourceFilter, L"Source");

    // Sample grabber
    IBaseFilter* grabberFilter = nullptr;
    CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IBaseFilter, reinterpret_cast<void**>(&grabberFilter));
    m_graph->AddFilter(grabberFilter, L"Grabber");
    grabberFilter->QueryInterface(IID_ISampleGrabber, reinterpret_cast<void**>(&m_grabber));

    // Set media type
    AM_MEDIA_TYPE mt = {};
    mt.majortype = MEDIATYPE_Video;
    switch (fmt) {
    case DSPixelFormat::BGR24: mt.subtype = MEDIASUBTYPE_RGB24; break;
    case DSPixelFormat::BGRA8: mt.subtype = MEDIASUBTYPE_ARGB32; break;
    case DSPixelFormat::NV12:  mt.subtype = MEDIASUBTYPE_NV12; break;
    }
    m_grabber->SetMediaType(&mt);
    m_grabber->SetBufferSamples(FALSE);

    auto* cb = new SampleGrabberCallback;
    cb->worker = this;
    cb->width  = width;
    cb->height = height;
    cb->stride = (fmt == DSPixelFormat::BGR24) ? width * 3 :
                 (fmt == DSPixelFormat::BGRA8) ? width * 4 : width;
    cb->fmt    = fmt;
    m_grabber->SetCallback(cb, 1);

    // Null renderer (we grab via callback, don't need display)
    IBaseFilter* nullRenderer = nullptr;
    CoCreateInstance(CLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IBaseFilter, reinterpret_cast<void**>(&nullRenderer));
    m_graph->AddFilter(nullRenderer, L"NullRenderer");

    m_captureGraph->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video,
                                  m_sourceFilter, grabberFilter, nullRenderer);
    nullRenderer->Release();
    grabberFilter->Release();

    m_graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&m_control));
    m_control->Run();
    m_running = true;
}

void DSCaptureWorker::teardown()
{
    if (m_control) { m_control->Stop(); m_control->Release(); m_control = nullptr; }
    if (m_grabber) { m_grabber->Release(); m_grabber = nullptr; }
    if (m_sourceFilter) { m_sourceFilter->Release(); m_sourceFilter = nullptr; }
    if (m_captureGraph) { m_captureGraph->Release(); m_captureGraph = nullptr; }
    if (m_graph) { m_graph->Release(); m_graph = nullptr; }
    m_running = false;
}

// ── DSCapturePlugin ───────────────────────────────────────────────────────────

DSCapturePlugin::DSCapturePlugin(QObject* parent) : QObject(parent) {}
DSCapturePlugin::~DSCapturePlugin() { shutdown(); }

void DSCapturePlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new DSCaptureWorker;
    m_worker->moveToThread(m_thread);

    connect(this, &DSCapturePlugin::_start, m_worker, &DSCaptureWorker::start);
    connect(this, &DSCapturePlugin::_stop,  m_worker, &DSCaptureWorker::stop);
    connect(m_worker, &DSCaptureWorker::frameReady,    this, &DSCapturePlugin::onFrameReady);
    connect(m_worker, &DSCaptureWorker::captureError,  this, &DSCapturePlugin::captureError);

    m_thread->start();

    // Open video ring buffer writer
    m_ringWriter.open(SharedMemoryManager::blockName("VIDEO"), 1920, 1080, 0 /*BGR24*/);
}

void DSCapturePlugin::shutdown()
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

QWidget* DSCapturePlugin::createWidget(QWidget* parent)
{
    auto* w      = new QWidget(parent);
    auto* layout = new QVBoxLayout(w);
    auto* bar    = new QHBoxLayout;

    auto* deviceCombo = new QComboBox(w);
    auto* fmtCombo    = new QComboBox(w);
    fmtCombo->addItems({"BGR24", "BGRA8", "NV12"});

    auto* widthSpin  = new QSpinBox(w);
    auto* heightSpin = new QSpinBox(w);
    widthSpin->setRange(320, 3840);  widthSpin->setValue(1920);
    heightSpin->setRange(240, 2160); heightSpin->setValue(1080);

    auto* btnStart = new QPushButton("Start Capture", w);
    auto* btnStop  = new QPushButton("Stop", w);

    bar->addWidget(deviceCombo, 2);
    bar->addWidget(fmtCombo);
    bar->addWidget(widthSpin);
    bar->addWidget(new QLabel("x", w));
    bar->addWidget(heightSpin);
    bar->addWidget(btnStart);
    bar->addWidget(btnStop);
    layout->addLayout(bar);

    // Populate device list
    const auto devices = DSCapturePlugin::enumerateDevices();
    for (const auto& d : devices)
        deviceCombo->addItem(d.name, d.devicePath);

    connect(btnStart, &QPushButton::clicked, [=]() {
        QString path = deviceCombo->currentData().toString();
        int w2 = widthSpin->value(), h = heightSpin->value();
        auto fmt = static_cast<DSPixelFormat>(fmtCombo->currentIndex());
        emit _start(path, w2, h, fmt);
        emit captureStarted();
    });
    connect(btnStop, &QPushButton::clicked, [=]() {
        emit _stop();
        emit captureStopped();
    });

    return w;
}

QList<DSCaptureDevice> DSCapturePlugin::enumerateDevices()
{
    QList<DSCaptureDevice> result;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ICreateDevEnum* devEnum = nullptr;
    CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                     IID_ICreateDevEnum, reinterpret_cast<void**>(&devEnum));

    IEnumMoniker* enumMon = nullptr;
    if (devEnum) {
        devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMon, 0);
        devEnum->Release();
    }

    IMoniker* moniker = nullptr;
    while (enumMon && enumMon->Next(1, &moniker, nullptr) == S_OK) {
        IPropertyBag* propBag = nullptr;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                              reinterpret_cast<void**>(&propBag)))) {
            VARIANT varName, varPath;
            VariantInit(&varName); VariantInit(&varPath);
            propBag->Read(L"FriendlyName", &varName, nullptr);
            propBag->Read(L"DevicePath",   &varPath, nullptr);

            DSCaptureDevice d;
            d.name       = QString::fromWCharArray(varName.bstrVal);
            d.devicePath = QString::fromWCharArray(varPath.bstrVal);
            result.append(d);

            VariantClear(&varName); VariantClear(&varPath);
            propBag->Release();
        }
        moniker->Release();
    }
    if (enumMon) enumMon->Release();
    CoUninitialize();
    return result;
}

void DSCapturePlugin::onFrameReady(int width, int height, int stride, DSPixelFormat fmt, QByteArray data)
{
    emit frameReceived(width, height, stride, fmt, data);
    writeFrameToSharedMemory(width, height, stride, fmt, data);
}

void DSCapturePlugin::writeFrameToSharedMemory(int width, int height, int stride,
                                                DSPixelFormat fmt, const QByteArray& data)
{
    VideoFrameData frame = {};
    frame.width  = width;
    frame.height = height;
    frame.stride = stride;
    frame.format = static_cast<uint32_t>(fmt);
    frame.data   = reinterpret_cast<uint8_t*>(const_cast<char*>(data.constData()));
    frame.size   = data.size();
    m_ringWriter.write(frame);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::DSCapturePlugin();
}
