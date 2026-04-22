#pragma once

#include "IPlugin.h"

#include <QObject>
#include <QMutex>
#include <atomic>
#include <memory>

namespace Labs {

class ViGEmSink : public QObject, public IControllerSink {
    Q_OBJECT
public:
    explicit ViGEmSink(QObject* parent = nullptr);
    ~ViGEmSink() override;

    bool isReady() const { return m_ready.load(); }
    QString statusText() const { return m_status; }

    void pushState(const ControllerState& state) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::atomic<bool>     m_ready { false };
    QString               m_status;
    QMutex                m_mx;
};

class ViGEmPlugin : public QObject, public IControllerSinkPlugin {
    Q_OBJECT
public:
    ViGEmPlugin();
    ~ViGEmPlugin() override;

    QString name()        const override { return QStringLiteral("ViGEm Output"); }
    QString author()      const override { return QStringLiteral("Labs"); }
    QString description() const override { return QStringLiteral("Virtual Xbox 360 pad via ViGEmBus"); }
    QString version()     const override { return QStringLiteral("0.1.0"); }

    void initialize(const PluginContext&) override {}
    void shutdown() override {}

    QObject* qobject() override { return this; }
    IControllerSink* controllerSink() override { return m_sink.get(); }

private:
    std::unique_ptr<ViGEmSink> m_sink;
};

} // namespace Labs
