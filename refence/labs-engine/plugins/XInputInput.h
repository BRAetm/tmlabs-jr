#pragma once
// XInputInput.h — XInput gamepad polling plugin

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <cstdint>

namespace Helios {

struct XInputState {
    uint32_t packet_number;
    uint16_t buttons;
    uint8_t  left_trigger;
    uint8_t  right_trigger;
    int16_t  thumb_lx;
    int16_t  thumb_ly;
    int16_t  thumb_rx;
    int16_t  thumb_ry;
};

class XInputWorker : public QObject {
    Q_OBJECT
public:
    explicit XInputWorker(QObject* parent = nullptr);

public slots:
    void start();
    void stop();

signals:
    void stateChanged(int index, const XInputState& state);
    void deviceConnected(int index);
    void deviceDisconnected(int index);

private:
    void poll();
    QTimer* m_timer   = nullptr;
    bool    m_running = false;
    bool    m_connected[4] = {};
    uint32_t m_lastPacket[4] = {};
};

class XInputInputPlugin : public QObject, public IPlugin {
    Q_OBJECT
public:
    explicit XInputInputPlugin(QObject* parent = nullptr);
    ~XInputInputPlugin() override;

    QString  name()        const override { return "XInputInput"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "XInput gamepad polling"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

signals:
    void stateChanged(int index, const XInputState& state);
    void deviceConnected(int index);
    void deviceDisconnected(int index);

private:
    QThread*      m_thread = nullptr;
    XInputWorker* m_worker = nullptr;
    PluginContext  m_ctx;
};

} // namespace Helios
