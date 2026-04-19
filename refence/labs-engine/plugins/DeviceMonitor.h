#pragma once
// DeviceMonitor.h — Real-time controller input/output display plugin

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QTimer>

namespace Helios {

class DeviceMonitorPlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit DeviceMonitorPlugin(QObject* parent = nullptr);
    ~DeviceMonitorPlugin() override;

    QString  name()        const override { return "DeviceMonitor"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Real-time controller input/output monitor"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

private slots:
    void poll();

private:
    void updateUI(const SharedMemoryControllerState& state, bool isInput);
    void renderStick(class QLabel* label, int x, int y, const QString& name);

    QTimer*  m_pollTimer   = nullptr;
    IControllerInputReader*  m_inputReader  = nullptr;
    IControllerOutputReader* m_outputReader = nullptr;

    // UI elements (populated in createWidget)
    class QLabel* m_lblInputStickL  = nullptr;
    class QLabel* m_lblInputStickR  = nullptr;
    class QLabel* m_lblOutputStickL = nullptr;
    class QLabel* m_lblOutputStickR = nullptr;
    class QLabel* m_lblInputButtons = nullptr;
    class QLabel* m_lblOutputButtons = nullptr;
    class QLabel* m_lblInputTriggers = nullptr;
    class QLabel* m_lblOutputTriggers = nullptr;

    PluginContext m_ctx;
};

} // namespace Helios
