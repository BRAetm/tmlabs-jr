// OpencvCapture.cpp — OpenCV VideoCapture plugin

#include "OpencvCapture.h"

#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>

namespace Helios {

// ── OpencvCaptureWorker ───────────────────────────────────────────────────────

OpencvCaptureWorker::OpencvCaptureWorker(QObject* parent) : QObject(parent) {}

OpencvCaptureWorker::~OpencvCaptureWorker() { stop(); }

void OpencvCaptureWorker::start(int deviceIndex, int width, int height, int fps)
{
    auto* cap = new cv::VideoCapture(deviceIndex, cv::CAP_DSHOW);
    if (!cap->isOpened()) {
        delete cap;
        emit captureError(QString("Cannot open device %1").arg(deviceIndex));
        return;
    }
    cap->set(cv::CAP_PROP_FRAME_WIDTH,  width);
    cap->set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap->set(cv::CAP_PROP_FPS, fps);
    m_cap     = cap;
    m_running = true;

    m_timer = new QTimer(this);
    int intervalMs = (fps > 0) ? (1000 / fps) : 33;
    connect(m_timer, &QTimer::timeout, this, &OpencvCaptureWorker::grab);
    m_timer->start(intervalMs);
}

void OpencvCaptureWorker::startFile(const QString& path)
{
    auto* cap = new cv::VideoCapture(path.toStdString());
    if (!cap->isOpened()) {
        delete cap;
        emit captureError("Cannot open file: " + path);
        return;
    }
    m_cap     = cap;
    m_running = true;

    double fps = cap->get(cv::CAP_PROP_FPS);
    int intervalMs = (fps > 0) ? static_cast<int>(1000.0 / fps) : 33;
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &OpencvCaptureWorker::grab);
    m_timer->start(intervalMs);
}

void OpencvCaptureWorker::stop()
{
    m_running = false;
    if (m_timer) { m_timer->stop(); m_timer = nullptr; }
    if (m_cap) {
        auto* cap = reinterpret_cast<cv::VideoCapture*>(m_cap);
        cap->release();
        delete cap;
        m_cap = nullptr;
    }
}

void OpencvCaptureWorker::grab()
{
    if (!m_running || !m_cap) return;
    auto* cap = reinterpret_cast<cv::VideoCapture*>(m_cap);
    cv::Mat frame;
    if (!cap->read(frame) || frame.empty()) return;

    int w = frame.cols, h = frame.rows;
    QByteArray data(reinterpret_cast<const char*>(frame.data), w * h * 3);
    emit frameReady(w, h, data);
}

// ── OpencvCapturePlugin ───────────────────────────────────────────────────────

OpencvCapturePlugin::OpencvCapturePlugin(QObject* parent) : QObject(parent) {}
OpencvCapturePlugin::~OpencvCapturePlugin() { shutdown(); }

void OpencvCapturePlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new OpencvCaptureWorker;
    m_worker->moveToThread(m_thread);

    connect(this, &OpencvCapturePlugin::_start,     m_worker, &OpencvCaptureWorker::start);
    connect(this, &OpencvCapturePlugin::_startFile, m_worker, &OpencvCaptureWorker::startFile);
    connect(this, &OpencvCapturePlugin::_stop,      m_worker, &OpencvCaptureWorker::stop);
    connect(m_worker, &OpencvCaptureWorker::frameReady,   this, &OpencvCapturePlugin::onFrame);
    connect(m_worker, &OpencvCaptureWorker::captureError, this, &OpencvCapturePlugin::captureError);

    m_thread->start();
    m_ringWriter.open(SharedMemoryManager::blockName("VIDEO"), 1920, 1080, 0 /*BGR24*/);
}

void OpencvCapturePlugin::shutdown()
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

QWidget* OpencvCapturePlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);
    auto* bar = new QHBoxLayout;

    auto* idxSpin    = new QSpinBox(w);
    idxSpin->setRange(0, 9);
    idxSpin->setPrefix("Device ");

    auto* widthSpin  = new QSpinBox(w); widthSpin->setRange(320, 3840); widthSpin->setValue(1920);
    auto* heightSpin = new QSpinBox(w); heightSpin->setRange(240, 2160); heightSpin->setValue(1080);
    auto* fpsSpin    = new QSpinBox(w); fpsSpin->setRange(1, 240); fpsSpin->setValue(60);

    auto* btnStart  = new QPushButton("Start", w);
    auto* btnFile   = new QPushButton("Open File", w);
    auto* btnStop   = new QPushButton("Stop", w);

    bar->addWidget(idxSpin);
    bar->addWidget(widthSpin);
    bar->addWidget(new QLabel("x", w));
    bar->addWidget(heightSpin);
    bar->addWidget(new QLabel("@", w));
    bar->addWidget(fpsSpin);
    bar->addWidget(new QLabel("fps", w));
    bar->addWidget(btnStart);
    bar->addWidget(btnFile);
    bar->addWidget(btnStop);
    lay->addLayout(bar);

    connect(btnStart, &QPushButton::clicked, [=]() {
        emit _start(idxSpin->value(), widthSpin->value(), heightSpin->value(), fpsSpin->value());
    });
    connect(btnFile, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getOpenFileName(w, "Open Video File",
            QString(), "Video Files (*.mp4 *.avi *.mkv *.mov *.ts);;All Files (*)");
        if (!path.isEmpty()) emit _startFile(path);
    });
    connect(btnStop, &QPushButton::clicked, [=]() { emit _stop(); });

    return w;
}

void OpencvCapturePlugin::onFrame(int width, int height, QByteArray bgrData)
{
    emit frameReceived(width, height, bgrData);

    VideoFrameData frame = {};
    frame.width  = width;
    frame.height = height;
    frame.stride = width * 3;
    frame.format = 0; // BGR24
    frame.data   = reinterpret_cast<uint8_t*>(bgrData.data());
    frame.size   = bgrData.size();
    m_ringWriter.write(frame);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::OpencvCapturePlugin();
}
