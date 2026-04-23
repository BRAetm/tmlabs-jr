#pragma once

#include "IPlugin.h"

#include <QObject>
#include <QThread>
#include <QString>
#include <atomic>
#include <memory>
#include <vector>

namespace Labs {

// Reads Sony DualSense / DualShock 4 controllers via the Windows HID API and
// re-emits them as a virtual Xbox 360 controller via ViGEmBus. Lets the rest
// of the app (which speaks XInput) work with PlayStation pads transparently.
//
// No external dependencies — uses Windows hidapi.h + setupapi.h, and loads
// ViGEmClient.dll at runtime (same pattern as ViGEmPlugin).
class DualSensePlugin : public QObject, public IPlugin {
    Q_OBJECT
public:
    DualSensePlugin();
    ~DualSensePlugin() override;

    QString  name()        const override { return QStringLiteral("DualSense Input"); }
    QString  author()      const override { return QStringLiteral("TM Labs"); }
    QString  description() const override { return QStringLiteral("Reads PlayStation controllers via HID and re-emits them as a virtual Xbox 360 pad."); }
    QString  version()     const override { return QStringLiteral("1.0.0"); }
    QObject* qobject() override           { return this; }

    void initialize(const PluginContext&) override;
    void shutdown() override;

private:
    QString m_status;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Labs
