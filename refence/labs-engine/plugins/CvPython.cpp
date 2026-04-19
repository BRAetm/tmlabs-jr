// CvPython.cpp — Python CV script host plugin

#include "CvPython.h"
#include "../helios_core/SharedMemory.h"
#include "../helios_core/LicenseService.h"
#include "../helios_core/LoggingService.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QProcessEnvironment>

namespace Helios {

CvPythonPlugin::CvPythonPlugin(QObject* parent)
    : QObject(parent)
    , m_restartTimer(new QTimer(this))
{
    m_restartTimer->setSingleShot(true);
    connect(m_restartTimer, &QTimer::timeout, this, &CvPythonPlugin::onRestartTimer);
}

CvPythonPlugin::~CvPythonPlugin()
{
    shutdown();
}

void CvPythonPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
    m_interpreterPath = resolveInterpreter();

    if (m_ctx.logging)
        m_ctx.logging->info("CvPython: initialized, interpreter=" + m_interpreterPath);
}

void CvPythonPlugin::shutdown()
{
    stopScript();
}

QWidget* CvPythonPlugin::createWidget(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* layout = new QVBoxLayout(w);

    auto* topBar = new QHBoxLayout;
    auto* lblScript = new QLabel("No script loaded", w);
    auto* btnOpen   = new QPushButton("Open Script", w);
    auto* btnStart  = new QPushButton("Start", w);
    auto* btnStop   = new QPushButton("Stop", w);
    btnStop->setEnabled(false);

    topBar->addWidget(lblScript, 1);
    topBar->addWidget(btnOpen);
    topBar->addWidget(btnStart);
    topBar->addWidget(btnStop);

    auto* output = new QTextEdit(w);
    output->setReadOnly(true);
    output->setFontFamily("Consolas");

    layout->addLayout(topBar);
    layout->addWidget(output);

    connect(btnOpen, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getOpenFileName(w, "Select Python Script",
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
            "Python Scripts (*.py)");
        if (!path.isEmpty()) {
            m_currentScript = path;
            lblScript->setText(QDir::toNativeSeparators(path));
        }
    });

    connect(btnStart, &QPushButton::clicked, [=]() {
        if (!m_currentScript.isEmpty()) {
            startScript(m_currentScript);
            btnStart->setEnabled(false);
            btnStop->setEnabled(true);
        }
    });

    connect(btnStop, &QPushButton::clicked, [=]() {
        stopScript();
        btnStart->setEnabled(true);
        btnStop->setEnabled(false);
    });

    connect(this, &CvPythonPlugin::scriptOutput, [=](const QString& line) {
        output->append(line);
    });
    connect(this, &CvPythonPlugin::scriptError, [=](const QString& msg) {
        output->append("<span style='color:red'>" + msg.toHtmlEscaped() + "</span>");
    });

    return w;
}

void CvPythonPlugin::startScript(const QString& scriptPath)
{
    stopScript();
    m_currentScript = scriptPath;
    launchWorker(scriptPath);
}

void CvPythonPlugin::stopScript()
{
    m_restartTimer->stop();
    m_running = false;

    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(3000))
                m_process->kill();
        }
        m_process->deleteLater();
        m_process = nullptr;
        emit scriptStopped();
    }
}

bool CvPythonPlugin::isRunning() const
{
    return m_running && m_process && m_process->state() == QProcess::Running;
}

void CvPythonPlugin::launchWorker(const QString& scriptPath)
{
    m_process = new QProcess(this);

    // Pass session token and HWID as environment variables
    auto env = QProcessEnvironment::systemEnvironment();
    if (m_ctx.license && m_ctx.license->isAuthenticated()) {
        env.insert("HELIOS_SESSION_TOKEN", m_ctx.license->sessionToken());
        env.insert("HELIOS_DISCORD_ID",    m_ctx.license->discordId());
    }
    // Shared memory block name for video ring buffer
    env.insert("HELIOS_SHM_NAME", SharedMemoryManager::blockName("VIDEO"));
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::started,              this, &CvPythonPlugin::onProcessStarted);
    connect(m_process, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CvPythonPlugin::onProcessFinished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &CvPythonPlugin::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,  this, &CvPythonPlugin::onReadyReadStderr);

    m_process->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    m_process->start(m_interpreterPath, {scriptPath});
}

void CvPythonPlugin::connectSharedMemory()
{
    // Video ring buffer reader is opened by the Python subprocess via HELIOS_SHM_NAME env var
}

QString CvPythonPlugin::resolveInterpreter() const
{
    // Primary: Helios bundled .henv
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // AppDataLocation for "Helios" org = %APPDATA%\HeliosProject\Helios
    QString henv = appData + "/python/InputSense-CUDA/.henv/python.exe";
    if (QFile::exists(henv))
        return henv;

    // Fallback: system python
    return "python";
}

void CvPythonPlugin::onProcessStarted()
{
    m_running = true;
    emit scriptStarted(m_currentScript);
    if (m_ctx.logging)
        m_ctx.logging->info("CvPython: script started — " + m_currentScript);
}

void CvPythonPlugin::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode); Q_UNUSED(status);
    if (m_running) {
        // Auto-restart after delay
        m_restartTimer->start(m_restartDelay);
    }
}

void CvPythonPlugin::onReadyReadStdout()
{
    if (!m_process) return;
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        emit scriptOutput(line);
    }
}

void CvPythonPlugin::onReadyReadStderr()
{
    if (!m_process) return;
    QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (!err.isEmpty())
        emit scriptError(err);
}

void CvPythonPlugin::onRestartTimer()
{
    if (m_running && !m_currentScript.isEmpty())
        launchWorker(m_currentScript);
}

} // namespace Helios

// Plugin factory
extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::CvPythonPlugin();
}
