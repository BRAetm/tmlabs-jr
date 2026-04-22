#pragma once

#include "IPlugin.h"
#include "WgcSource.h"

#include <memory>

namespace Labs {

class WgcCapturePlugin : public QObject, public IFrameSourcePlugin {
    Q_OBJECT
public:
    WgcCapturePlugin();
    ~WgcCapturePlugin() override;

    QString name()        const override { return QStringLiteral("WGC Capture"); }
    QString author()      const override { return QStringLiteral("Labs"); }
    QString description() const override { return QStringLiteral("Windows Graphics Capture of the foreground window"); }
    QString version()     const override { return QStringLiteral("0.1.0"); }

    void initialize(const PluginContext& ctx) override;
    void shutdown() override;

    QObject* qobject() override { return this; }

    IFrameSource* frameSource() override { return m_source.get(); }

private:
    std::unique_ptr<WgcSource> m_source;
};

} // namespace Labs
