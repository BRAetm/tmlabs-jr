#pragma once
// ViGEmOutput.h — ViGEm Bus virtual controller output plugin
// Creates virtual DS4 or Xbox 360 controller via ViGEmBus driver

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <cstdint>

// ViGEm client API (ViGEmClient.dll)
// https://github.com/nefarius/ViGEmBus
struct _VIGEM_CLIENT_T;
struct _VIGEM_TARGET_T;
typedef _VIGEM_CLIENT_T* PVIGEM_CLIENT;
typedef _VIGEM_TARGET_T* PVIGEM_TARGET;

namespace Helios {

enum class ViGEmTargetType {
    DS4,
    Xbox360,
};

struct DS4OutputReport {
    uint8_t  thumb_lx;
    uint8_t  thumb_ly;
    uint8_t  thumb_rx;
    uint8_t  thumb_ry;
    uint16_t buttons;
    uint8_t  special;
    uint8_t  trigger_l;
    uint8_t  trigger_r;
};

class ViGEmOutputWorker : public QObject {
    Q_OBJECT
public:
    explicit ViGEmOutputWorker(QObject* parent = nullptr);
    ~ViGEmOutputWorker();

public slots:
    void start(Helios::ViGEmTargetType type);
    void stop();
    void sendReport(const DS4OutputReport& report);

signals:
    void connected();
    void disconnected();
    void error(const QString& msg);

private:
    PVIGEM_CLIENT m_client = nullptr;
    PVIGEM_TARGET m_target = nullptr;
    ViGEmTargetType m_type = ViGEmTargetType::DS4;
};

class ViGEmOutputPlugin : public QObject, public IPlugin {
    Q_OBJECT
public:
    explicit ViGEmOutputPlugin(QObject* parent = nullptr);
    ~ViGEmOutputPlugin() override;

    QString  name()        const override { return "ViGEmOutput"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Virtual DS4/Xbox controller via ViGEm Bus"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    void setTargetType(ViGEmTargetType type);

public slots:
    void onControllerData(const SharedMemoryControllerState& state);

signals:
    void virtualControllerConnected();
    void virtualControllerDisconnected();
    void outputError(const QString& msg);

    // internal
    void _sendReport(const DS4OutputReport& report);
    void _start(Helios::ViGEmTargetType type);
    void _stop();

private:
    QThread*          m_thread  = nullptr;
    ViGEmOutputWorker* m_worker = nullptr;
    IControllerOutputReader* m_shmReader = nullptr;
    ViGEmTargetType   m_targetType = ViGEmTargetType::DS4;
    PluginContext      m_ctx;
};

} // namespace Helios
