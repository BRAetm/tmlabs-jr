#pragma once

#include "IPlugin.h"

#include <QObject>
#include <QMutex>
#include <atomic>
#include <memory>

namespace Labs {

class SettingsManager;

class PSRemotePlayPlugin : public QObject,
                          public IFrameSourcePlugin,
                          public IControllerSinkPlugin,
                          public IPairablePlugin,
                          public IFrameSource,
                          public IControllerSink {
    Q_OBJECT
public:
    PSRemotePlayPlugin();
    ~PSRemotePlayPlugin() override;

    QString name()        const override { return QStringLiteral("PS Remote Play"); }
    QString author()      const override { return QStringLiteral("Labs"); }
    QString description() const override { return QStringLiteral("PS4 / PS5 Remote Play (labs.dll + FFmpeg)"); }
    QString version()     const override { return QStringLiteral("0.1.0"); }

    void initialize(const PluginContext& ctx) override;
    void shutdown() override;

    QObject* qobject() override { return this; }

    IFrameSource*    frameSource()     override { return this; }
    IControllerSink* controllerSink()  override { return this; }

    // IPairablePlugin
    bool pair(QWidget* parent) override;

    // IFrameSource
    void setSink(IFrameSink* sink) override { m_sink = sink; }
    bool start() override;
    void stop()  override;
    bool isRunning() const override { return m_running.load(); }
    qint64  frameCount() const override { return m_frameCount.load(); }
    QString targetLabel() const override { return m_targetLabel; }

    // IControllerSink
    void pushState(const ControllerState& state) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    IFrameSink*         m_sink     = nullptr;
    SettingsManager*    m_settings = nullptr;
    QString             m_targetLabel;
    std::atomic<bool>   m_running     { false };
    std::atomic<qint64> m_frameCount  { 0 };
};

} // namespace Labs
