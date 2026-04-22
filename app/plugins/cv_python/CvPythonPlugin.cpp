#include "CvPythonPlugin.h"
#include "SettingsManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

#include <Windows.h>

namespace Labs {

CvPythonPlugin::CvPythonPlugin()
    : m_gamepadReader(std::make_unique<GamepadShmReader>(this))
{
}

CvPythonPlugin::~CvPythonPlugin()
{
    stop();
}

void CvPythonPlugin::initialize(const PluginContext& ctx)
{
    m_settings = ctx.settings;
    if (m_settings) {
        m_sessionId = m_settings->value(QStringLiteral("cv/sessionId"), 1).toInt();
    }
}

void CvPythonPlugin::shutdown()
{
    stop();
}

// ── IFrameSink ──────────────────────────────────────────────────────────────

void CvPythonPlugin::pushFrame(const Frame& frame)
{
    if (!m_running) return;
    m_frameShm.write(frame);
}

// ── IControllerSource ───────────────────────────────────────────────────────

void CvPythonPlugin::setSink(IControllerSink* sink)
{
    m_ctrlSink = sink;
    if (m_gamepadReader) m_gamepadReader->setSink(sink);
}

bool CvPythonPlugin::start()
{
    if (m_running) return true;

    const quint32 pid = ::GetCurrentProcessId();
    if (!m_frameShm.open(pid, m_sessionId)) {
        qWarning() << "CvPython: frame SHM open failed";
        return false;
    }

    m_gamepadReader->configure(pid);
    m_gamepadReader->start();

    m_running = true;
    return true;
}

void CvPythonPlugin::stop()
{
    if (!m_running) return;
    m_running = false;

    stopPython();

    if (m_gamepadReader) {
        m_gamepadReader->requestStop();
        m_gamepadReader->wait(500);
    }
    m_frameShm.close();
}

// ── Python subprocess ───────────────────────────────────────────────────────

void CvPythonPlugin::launchPython()
{
    if (!m_settings) return;
    const QString scriptPath = m_settings->value(QStringLiteral("cv/scriptPath")).toString();
    if (scriptPath.isEmpty() || !QFileInfo::exists(scriptPath)) {
        qInfo() << "CvPython: no script configured (cv/scriptPath). Frames flow to SHM; attach Python manually with --labs-pid"
                << ::GetCurrentProcessId() << "--session" << m_sessionId;
        return;
    }
    QString python = m_settings->value(QStringLiteral("cv/pythonPath"), QStringLiteral("python")).toString();

    m_process = new QProcess(this);
    m_process->setProgram(python);
    m_process->setArguments({
        scriptPath,
        QStringLiteral("--labs-pid"), QString::number(::GetCurrentProcessId()),
        QStringLiteral("--session"),  QString::number(m_sessionId),
    });
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_process) qInfo().noquote() << "[py]" << m_process->readAllStandardOutput().trimmed();
    });
    m_process->start();
    if (!m_process->waitForStarted(2000)) {
        qWarning() << "CvPython: failed to start" << python << scriptPath;
    } else {
        qInfo() << "CvPython: launched" << python << scriptPath
                << "pid=" << m_process->processId();
    }
}

void CvPythonPlugin::stopPython()
{
    if (m_process) {
        m_process->terminate();
        if (!m_process->waitForFinished(1500)) m_process->kill();
        m_process.clear();
    }
}

} // namespace Labs

extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin()
{
    return new Labs::CvPythonPlugin();
}
