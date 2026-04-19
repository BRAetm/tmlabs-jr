#pragma once
// GPC3.h — GPC3 VM plugin (Titan Two / Gtuner compatible scripting)
// Interprets GPC3 bytecode, executes on controller I/O shared memory

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QString>
#include <QByteArray>

namespace Helios {

// GPC3 VM — registers and execution state
struct GPC3VMState {
    int32_t  regs[32];       // general-purpose registers
    uint32_t pc;             // program counter
    bool     running;
    uint8_t  controller[32]; // current controller state (DS5 raw)
    uint8_t  output[32];     // modified output
};

class GPC3VMWorker : public QObject {
    Q_OBJECT
public:
    explicit GPC3VMWorker(QObject* parent = nullptr);
    ~GPC3VMWorker();

public slots:
    void loadScript(const QByteArray& bytecode);
    void start();
    void stop();
    void setControllerInput(const uint8_t* data, int size);

signals:
    void outputReady(QByteArray outputData);
    void vmError(const QString& msg);
    void vmStarted();
    void vmStopped();

private:
    void tick();

    QByteArray    m_bytecode;
    GPC3VMState   m_state = {};
    QTimer*       m_timer = nullptr;
    bool          m_loaded = false;

    // GPC3 opcode dispatch
    bool executeOpcode(uint8_t opcode, const uint8_t* operands, int& advance);
    int32_t getButton(int btnIndex) const;
    int32_t getAxis(int axisIndex) const;
    void    setOutput(int index, int32_t value);
};

class GPC3Plugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit GPC3Plugin(QObject* parent = nullptr);
    ~GPC3Plugin() override;

    QString  name()        const override { return "GPC3"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "GPC3 VM — Titan Two / Gtuner scripting"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

signals:
    void vmStarted();
    void vmStopped();
    void vmError(const QString& msg);

    void _load(const QByteArray& bytecode);
    void _start();
    void _stop();

private slots:
    void onOutputReady(QByteArray data);

private:
    QThread*      m_thread = nullptr;
    GPC3VMWorker* m_worker = nullptr;
    IControllerInputReader*  m_inputReader  = nullptr;
    IControllerOutputWriter* m_outputWriter = nullptr;
    PluginContext  m_ctx;
};

} // namespace Helios
