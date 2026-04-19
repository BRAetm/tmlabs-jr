// OBSCapture.cpp — OBS plugin integration

#include "OBSCapture.h"

#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QProcess>
#include <QCoreApplication>

namespace Helios {

// ── OBSCaptureWorker ──────────────────────────────────────────────────────────

OBSCaptureWorker::OBSCaptureWorker(QObject* parent) : QObject(parent) {}

OBSCaptureWorker::~OBSCaptureWorker() { stop(); }

void OBSCaptureWorker::startVirtualCamera(int width, int height)
{
    m_usePipe = false;
    m_width   = width;
    m_height  = height;

    // OBS Virtual Camera is a DirectShow device named "OBS Virtual Camera"
    // OpenCV with CAP_DSHOW can enumerate it by name, but we use index-based scan
    auto* cap = new cv::VideoCapture();
    for (int i = 0; i < 10; ++i) {
        cap->open(i, cv::CAP_DSHOW);
        if (cap->isOpened()) {
            // Check backend name for OBS
            std::string name = cap->getBackendName();
            // Accept first available cam as OBS virtual (user selects via UI)
            cap->set(cv::CAP_PROP_FRAME_WIDTH,  width);
            cap->set(cv::CAP_PROP_FRAME_HEIGHT, height);
            m_dsCap  = cap;
            m_running = true;
            emit obsConnected();

            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this, &OBSCaptureWorker::grabFromDS);
            m_timer->start(16);
            return;
        }
    }
    delete cap;
    emit captureError("OBS Virtual Camera device not found");
}

void OBSCaptureWorker::startPipe(const QString& pipeName)
{
    m_usePipe = true;
    // Connect to obs-helios-bridge named pipe: \\.\pipe\<pipeName>
    QString fullPipe = "\\\\.\\pipe\\" + pipeName;
    HANDLE h = CreateFile(fullPipe.toStdWString().c_str(),
                          GENERIC_READ, 0, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        emit captureError("Cannot connect to OBS pipe: " + pipeName);
        return;
    }
    m_pipe    = h;
    m_running = true;
    emit obsConnected();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &OBSCaptureWorker::grabFromPipe);
    m_timer->start(16);
}

void OBSCaptureWorker::stop()
{
    m_running = false;
    if (m_timer) { m_timer->stop(); m_timer = nullptr; }
    if (m_dsCap) {
        reinterpret_cast<cv::VideoCapture*>(m_dsCap)->release();
        delete reinterpret_cast<cv::VideoCapture*>(m_dsCap);
        m_dsCap = nullptr;
    }
    if (m_pipe) {
        CloseHandle(reinterpret_cast<HANDLE>(m_pipe));
        m_pipe = nullptr;
    }
    emit obsDisconnected();
}

void OBSCaptureWorker::grabFromDS()
{
    if (!m_running || !m_dsCap) return;
    auto* cap = reinterpret_cast<cv::VideoCapture*>(m_dsCap);
    cv::Mat frame;
    if (!cap->read(frame) || frame.empty()) return;
    QByteArray data(reinterpret_cast<const char*>(frame.data), frame.total() * 3);
    emit frameReady(frame.cols, frame.rows, data);
}

void OBSCaptureWorker::grabFromPipe()
{
    if (!m_running || !m_pipe) return;
    // pipe frame format: 4-byte width + 4-byte height + BGR data
    HANDLE h = reinterpret_cast<HANDLE>(m_pipe);
    uint32_t hdr[2] = {};
    DWORD bytesRead = 0;
    if (!ReadFile(h, hdr, 8, &bytesRead, nullptr) || bytesRead != 8) return;

    int w = hdr[0], ht = hdr[1];
    if (w <= 0 || ht <= 0 || w > 4096 || ht > 4096) return;

    QByteArray buf(w * ht * 3, '\0');
    bytesRead = 0;
    if (ReadFile(h, buf.data(), buf.size(), &bytesRead, nullptr) &&
        bytesRead == static_cast<DWORD>(buf.size())) {
        emit frameReady(w, ht, buf);
    }
}

// ── OBSCapturePlugin ──────────────────────────────────────────────────────────

OBSCapturePlugin::OBSCapturePlugin(QObject* parent) : QObject(parent) {}
OBSCapturePlugin::~OBSCapturePlugin() { shutdown(); }

void OBSCapturePlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new OBSCaptureWorker;
    m_worker->moveToThread(m_thread);

    connect(this, &OBSCapturePlugin::_startVC,   m_worker, &OBSCaptureWorker::startVirtualCamera);
    connect(this, &OBSCapturePlugin::_startPipe, m_worker, &OBSCaptureWorker::startPipe);
    connect(this, &OBSCapturePlugin::_stop,      m_worker, &OBSCaptureWorker::stop);
    connect(m_worker, &OBSCaptureWorker::frameReady,   this, &OBSCapturePlugin::onFrame);
    connect(m_worker, &OBSCaptureWorker::captureError, this, &OBSCapturePlugin::captureError);

    m_thread->start();
    m_ringWriter.open(SharedMemoryManager::blockName("VIDEO"), 1920, 1080, 0);
}

void OBSCapturePlugin::shutdown()
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

QWidget* OBSCapturePlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);

    // Source selection
    auto* srcGrp = new QGroupBox("Capture Source", w);
    auto* srcLay = new QVBoxLayout(srcGrp);
    auto* rdVC   = new QRadioButton("OBS Virtual Camera", srcGrp);
    auto* rdPipe = new QRadioButton("obs-helios-bridge pipe", srcGrp);
    rdVC->setChecked(true);
    srcLay->addWidget(rdVC);
    srcLay->addWidget(rdPipe);
    lay->addWidget(srcGrp);

    // Pipe name
    auto* pipeBar = new QHBoxLayout;
    auto* edtPipe = new QLineEdit("helios_obs", w);
    edtPipe->setEnabled(false);
    pipeBar->addWidget(new QLabel("Pipe name:", w));
    pipeBar->addWidget(edtPipe);
    lay->addLayout(pipeBar);

    // Resolution
    auto* resBar  = new QHBoxLayout;
    auto* widthSpin  = new QSpinBox(w); widthSpin->setRange(320, 3840); widthSpin->setValue(1920);
    auto* heightSpin = new QSpinBox(w); heightSpin->setRange(240, 2160); heightSpin->setValue(1080);
    resBar->addWidget(new QLabel("Resolution:", w));
    resBar->addWidget(widthSpin);
    resBar->addWidget(new QLabel("x", w));
    resBar->addWidget(heightSpin);
    lay->addLayout(resBar);

    auto* btnBar    = new QHBoxLayout;
    auto* btnInstall = new QPushButton("Install OBS Plugin", w);
    auto* btnStart  = new QPushButton("Start", w);
    auto* btnStop   = new QPushButton("Stop", w);
    btnBar->addWidget(btnInstall);
    btnBar->addStretch();
    btnBar->addWidget(btnStart);
    btnBar->addWidget(btnStop);
    lay->addLayout(btnBar);

    connect(rdPipe, &QRadioButton::toggled, edtPipe, &QLineEdit::setEnabled);

    connect(btnStart, &QPushButton::clicked, [=]() {
        if (rdVC->isChecked()) {
            emit _startVC(widthSpin->value(), heightSpin->value());
        } else {
            emit _startPipe(edtPipe->text());
        }
    });
    connect(btnStop, &QPushButton::clicked, [=]() { emit _stop(); });
    connect(btnInstall, &QPushButton::clicked, this, &OBSCapturePlugin::installOBSPlugin);

    return w;
}

void OBSCapturePlugin::installOBSPlugin()
{
    // PowerShell script installs obs-helios-bridge.dll to OBS plugins directory
    // Typical path: %ProgramFiles%\obs-studio\obs-plugins\64bit\
    QString ps1 = QString(
        "$obsDir = \"$env:ProgramFiles\\obs-studio\\obs-plugins\\64bit\"\n"
        "$src = \"%1\\obs-helios-bridge.dll\"\n"
        "if (Test-Path $src) {\n"
        "    Copy-Item $src $obsDir -Force\n"
        "    Write-Host 'OBS plugin installed.'\n"
        "} else {\n"
        "    Write-Error 'obs-helios-bridge.dll not found.'\n"
        "}\n"
    ).arg(QCoreApplication::applicationDirPath());

    QProcess* ps = new QProcess(this);
    connect(ps, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            [=](int code, QProcess::ExitStatus) {
                if (code == 0)
                    emit obsPluginInstalled();
                else
                    emit obsPluginInstallError(
                        QString::fromUtf8(ps->readAllStandardError()));
                ps->deleteLater();
            });
    ps->start("powershell.exe", {"-NonInteractive", "-Command", ps1});
}

void OBSCapturePlugin::onFrame(int width, int height, QByteArray data)
{
    emit frameReceived(width, height, data);

    VideoFrameData frame = {};
    frame.width  = width;
    frame.height = height;
    frame.stride = width * 3;
    frame.format = 0;
    frame.data   = reinterpret_cast<uint8_t*>(data.data());
    frame.size   = data.size();
    m_ringWriter.write(frame);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::OBSCapturePlugin();
}
