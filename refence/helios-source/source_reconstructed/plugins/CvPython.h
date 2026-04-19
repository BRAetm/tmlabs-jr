#pragma once
// CvPython.h — Python CV script host plugin

#include "../helios_core/IPlugin.h"
#include <QObject>
#include <QString>
#include <QProcess>
#include <QTimer>

namespace Helios {

class CvPythonPlugin : public QObject, public IUIPlugin {
    Q_OBJECT

public:
    explicit CvPythonPlugin(QObject* parent = nullptr);
    ~CvPythonPlugin() override;

    // IPlugin
    QString  name()        const override { return "CvPython"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Python CV script host"; }
    QString  version()     const override { return "1.0.0"; }
    bool     requiresAuthentication() const override { return true; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    // IUIPlugin
    QWidget* createWidget(QWidget* parent) override;

    // Script control
    void     startScript(const QString& scriptPath);
    void     stopScript();
    bool     isRunning() const;

signals:
    void scriptStarted(const QString& path);
    void scriptStopped();
    void scriptOutput(const QString& line);
    void scriptError(const QString& msg);
    void workerReady();

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onRestartTimer();

private:
    void launchWorker(const QString& scriptPath);
    void connectSharedMemory();
    QString resolveInterpreter() const;

    // Python interpreter: .henv/python.exe in Helios app data
    // %APPDATA%\HeliosProject\Helios\python\InputSense-CUDA\.henv\python.exe
    QString          m_interpreterPath;
    QString          m_currentScript;
    QProcess*        m_process       = nullptr;
    QTimer*          m_restartTimer  = nullptr;
    bool             m_running       = false;
    int              m_restartDelay  = 3000; // ms

    PluginContext    m_ctx;
};

} // namespace Helios
