#pragma once
// DS5Input.h — DualSense HID input plugin (Raw Input)

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <cstdint>

// DualSense USB HID
// VID: 0x054C  PID: 0x0CE6
// Usage Page: 0x0001  Usage: 0x0005 (Gamepad)
// Input report ID: 0x01 (USB) / 0x31 (BT)

namespace Helios {

struct DS5Report {
    uint8_t  report_id;        // 0x01
    uint8_t  left_stick_x;
    uint8_t  left_stick_y;
    uint8_t  right_stick_x;
    uint8_t  right_stick_y;
    uint8_t  l2_trigger;
    uint8_t  r2_trigger;
    uint8_t  seq_num;
    uint8_t  buttons[3];       // face/dpad/shoulder bits
    uint8_t  reserved1;
    int16_t  gyro_x;
    int16_t  gyro_y;
    int16_t  gyro_z;
    int16_t  accel_x;
    int16_t  accel_y;
    int16_t  accel_z;
    uint32_t timestamp_us;
    uint8_t  touch_data[9];
    uint8_t  reserved2[13];
};

class DS5InputWorker : public QObject {
    Q_OBJECT
public:
    explicit DS5InputWorker(QObject* parent = nullptr);
    ~DS5InputWorker();

public slots:
    void start();
    void stop();

signals:
    void reportReceived(const DS5Report& report);
    void deviceConnected();
    void deviceDisconnected();

private:
    void pollDevices();
    void* m_deviceHandle = nullptr;  // HANDLE
    QTimer* m_pollTimer  = nullptr;
    bool    m_running    = false;
};

class DS5InputPlugin : public QObject, public IPlugin {
    Q_OBJECT
public:
    explicit DS5InputPlugin(QObject* parent = nullptr);
    ~DS5InputPlugin() override;

    QString  name()        const override { return "DS5Input"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "DualSense HID input via Raw Input"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

signals:
    void controllerReport(const DS5Report& report);
    void deviceConnected();
    void deviceDisconnected();

private slots:
    void onReport(const DS5Report& report);

private:
    void writeToSharedMemory(const DS5Report& report);

    QThread*         m_thread  = nullptr;
    DS5InputWorker*  m_worker  = nullptr;
    IControllerInputWriter* m_shmWriter = nullptr;
    PluginContext    m_ctx;
};

} // namespace Helios
