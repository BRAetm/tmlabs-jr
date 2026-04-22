#pragma once

#include "IPlugin.h"
#include "ShmBus.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <memory>

namespace Labs {

class SettingsManager;

class CvPythonPlugin : public QObject,
                      public IFrameSinkPlugin,
                      public IControllerSourcePlugin,
                      public IFrameSink,
                      public IControllerSource {
    Q_OBJECT
public:
    CvPythonPlugin();
    ~CvPythonPlugin() override;

    QString name()        const override { return QStringLiteral("CV Python"); }
    QString author()      const override { return QStringLiteral("Labs"); }
    QString description() const override { return QStringLiteral("Bridges frames ↔ Python script ↔ virtual pad"); }
    QString version()     const override { return QStringLiteral("0.1.0"); }

    void initialize(const PluginContext& ctx) override;
    void shutdown() override;

    QObject* qobject() override { return this; }

    // IFrameSinkPlugin
    IFrameSink* frameSink() override { return this; }
    // IControllerSourcePlugin
    IControllerSource* controllerSource() override { return this; }

    // IFrameSink
    void pushFrame(const Frame& frame) override;

    // IControllerSource
    void setSink(IControllerSink* sink) override;
    bool start() override;
    void stop()  override;
    bool isRunning() const override { return m_running; }

private:
    void launchPython();
    void stopPython();

    SettingsManager* m_settings = nullptr;
    FrameShmWriter   m_frameShm;
    std::unique_ptr<GamepadShmReader> m_gamepadReader;
    QPointer<QProcess> m_process;
    IControllerSink* m_ctrlSink = nullptr;
    qint32 m_sessionId = 1;
    bool   m_running   = false;
};

} // namespace Labs
