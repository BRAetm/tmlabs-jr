#pragma once

#include "IPlugin.h"

#include <QObject>
#include <QTimer>
#include <atomic>

namespace Labs {

class XInputSource : public QObject, public IControllerSource {
    Q_OBJECT
public:
    explicit XInputSource(QObject* parent = nullptr);
    ~XInputSource() override;

    void setSink(IControllerSink* sink) override { m_sink = sink; }
    bool start() override;
    void stop()  override;
    bool isRunning() const override { return m_running.load(); }

    // bit N set → skip slot N. Reset active slot so the next poll re-latches.
    void setSkipMask(int mask) { m_skipMask = mask; m_activeSlot = -1; }

private slots:
    void poll();

private:
    IControllerSink*  m_sink = nullptr;
    QTimer            m_timer;
    std::atomic<bool> m_running { false };
    int               m_activeSlot = -1;
    int               m_skipMask   = 0;
};

class XInputPlugin : public QObject, public IControllerSourcePlugin {
    Q_OBJECT
public:
    XInputPlugin();
    ~XInputPlugin() override;

    QString name()        const override { return QStringLiteral("XInput"); }
    QString author()      const override { return QStringLiteral("Labs"); }
    QString description() const override { return QStringLiteral("Polls physical XInput gamepads"); }
    QString version()     const override { return QStringLiteral("0.1.0"); }

    void initialize(const PluginContext&) override {}
    void shutdown() override { if (m_source) m_source->stop(); }

    QObject* qobject() override { return this; }
    IControllerSource* controllerSource() override { return m_source.get(); }

    // Invokable across DLL boundaries via QMetaObject::invokeMethod so
    // LabsMainWindow can tell XInput which slots to skip per mode.
    Q_INVOKABLE void setSkipMask(int mask) { if (m_source) m_source->setSkipMask(mask); }

private:
    std::unique_ptr<XInputSource> m_source;
};

} // namespace Labs
