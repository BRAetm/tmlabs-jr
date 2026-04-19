#pragma once
// TitanBridge.h — Titan Two USB bridge plugin
// Connects via USB HID to Titan Two device (Collective Minds)
// Bridges controller I/O, uploads GPC3 scripts, manages memory slots

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <cstdint>

// Titan Two USB HID
// VID: 0x1993  PID: 0x00F0  (Collective Minds)
// Usage Page: 0xFF00 (vendor-defined)
// Feature report: configuration / slot selection
// Input/Output: controller data pass-through

namespace Helios {

enum class TitanSlot {
    Slot1 = 1,
    Slot2,
    Slot3,
    Slot4,
    Slot5,
    Slot6,
    Slot7,
    Slot8,
};

struct TitanControllerReport {
    // T2 raw controller data (matches DS4 layout)
    uint8_t data[64];
    uint32_t timestamp_ms;
};

class TitanBridgeWorker : public QObject {
    Q_OBJECT
public:
    explicit TitanBridgeWorker(QObject* parent = nullptr);
    ~TitanBridgeWorker();

public slots:
    void start();
    void stop();
    void uploadScript(const QByteArray& gpc3Bytecode, TitanSlot slot);
    void selectSlot(TitanSlot slot);
    void sendControllerData(const TitanControllerReport& report);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void controllerReceived(const TitanControllerReport& report);
    void scriptUploaded(TitanSlot slot);
    void error(const QString& msg);

private:
    bool openDevice();
    void closeDevice();
    void poll();

    void* m_deviceHandle = nullptr;
    QTimer* m_pollTimer  = nullptr;
    bool    m_running    = false;
};

class TitanBridgePlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit TitanBridgePlugin(QObject* parent = nullptr);
    ~TitanBridgePlugin() override;

    QString  name()        const override { return "TitanBridge"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Titan Two USB bridge & GPC3 script manager"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

    void uploadGPC3(const QString& scriptPath, TitanSlot slot);
    void selectSlot(TitanSlot slot);

signals:
    void titanConnected();
    void titanDisconnected();
    void slotSelected(TitanSlot slot);
    void scriptUploaded(TitanSlot slot);
    void bridgeError(const QString& msg);

    // internal cross-thread signals
    void _uploadScript(const QByteArray& bytecode, TitanSlot slot);
    void _selectSlot(TitanSlot slot);
    void _sendController(const TitanControllerReport& report);

private slots:
    void onControllerReceived(const TitanControllerReport& report);

private:
    QThread*           m_thread = nullptr;
    TitanBridgeWorker* m_worker = nullptr;
    IControllerOutputWriter* m_shmWriter = nullptr;
    PluginContext       m_ctx;
};

} // namespace Helios
