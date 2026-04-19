#pragma once
// DS4Input.h — DualShock 4 HID input plugin

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <cstdint>

// DS4 USB HID
// VID: 0x054C  PID: 0x05C4 (gen1) / 0x09CC (gen2)
// Input report ID: 0x01

namespace Helios {

struct DS4Report {
    uint8_t report_id;
    uint8_t left_stick_x;
    uint8_t left_stick_y;
    uint8_t right_stick_x;
    uint8_t right_stick_y;
    uint8_t buttons[3];
    uint8_t l2_trigger;
    uint8_t r2_trigger;
    uint16_t timestamp;
    uint8_t  battery;
    int16_t  gyro_x;
    int16_t  gyro_y;
    int16_t  gyro_z;
    int16_t  accel_x;
    int16_t  accel_y;
    int16_t  accel_z;
    uint8_t  reserved[5];
    uint8_t  touch_data[9];
};

class DS4InputWorker : public QObject {
    Q_OBJECT
public:
    explicit DS4InputWorker(QObject* parent = nullptr);
    ~DS4InputWorker();

public slots:
    void start();
    void stop();

signals:
    void reportReceived(const DS4Report& report);
    void deviceConnected();
    void deviceDisconnected();

private:
    void pollDevices();
    void* m_deviceHandle = nullptr;
    QTimer* m_pollTimer  = nullptr;
    bool    m_running    = false;
};

class DS4InputPlugin : public QObject, public IPlugin {
    Q_OBJECT
public:
    explicit DS4InputPlugin(QObject* parent = nullptr);
    ~DS4InputPlugin() override;

    QString  name()        const override { return "DS4Input"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "DualShock 4 HID input"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

signals:
    void controllerReport(const DS4Report& report);
    void deviceConnected();
    void deviceDisconnected();

private:
    QThread*        m_thread = nullptr;
    DS4InputWorker* m_worker = nullptr;
    PluginContext   m_ctx;
};

} // namespace Helios
