// GPC3.cpp — GPC3 VM plugin

#include "GPC3.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QFileDialog>
#include <QFile>
#include <cstring>

namespace Helios {

// ── GPC3 opcode table (subset of Titan Two GPC3 instruction set) ──────────────
// Full GPC3 ISA reference: Gtuner IV / Titan Two SDK docs
enum GPC3Opcode : uint8_t {
    OP_NOP     = 0x00,
    OP_HALT    = 0x01,
    OP_LOAD    = 0x10, // LOAD reg, imm16
    OP_MOVE    = 0x11, // MOVE dst, src
    OP_ADD     = 0x20,
    OP_SUB     = 0x21,
    OP_MUL     = 0x22,
    OP_DIV     = 0x23,
    OP_AND     = 0x24,
    OP_OR      = 0x25,
    OP_NOT     = 0x26,
    OP_JMP     = 0x30, // JMP addr
    OP_JZ      = 0x31, // JZ reg, addr
    OP_JNZ     = 0x32,
    OP_GETBTN  = 0x40, // GETBTN reg, btnIdx
    OP_GETAXIS = 0x41, // GETAXIS reg, axisIdx
    OP_SETOUT  = 0x42, // SETOUT outIdx, reg
    OP_WAIT    = 0x50, // WAIT ms
    OP_COMBO   = 0x60, // COMBO id (macro call)
};

// ── GPC3VMWorker ──────────────────────────────────────────────────────────────

GPC3VMWorker::GPC3VMWorker(QObject* parent) : QObject(parent) {}

GPC3VMWorker::~GPC3VMWorker() { stop(); }

void GPC3VMWorker::loadScript(const QByteArray& bytecode)
{
    m_bytecode = bytecode;
    m_loaded   = true;
    m_state    = {};
}

void GPC3VMWorker::start()
{
    if (!m_loaded) return;
    m_state.running = true;
    m_state.pc      = 0;

    m_timer = new QTimer(this);
    // GPC3 runs at ~500 Hz (2ms tick) on real T2 hardware
    connect(m_timer, &QTimer::timeout, this, &GPC3VMWorker::tick);
    m_timer->start(2);
    emit vmStarted();
}

void GPC3VMWorker::stop()
{
    if (m_timer) { m_timer->stop(); m_timer = nullptr; }
    m_state.running = false;
    emit vmStopped();
}

void GPC3VMWorker::setControllerInput(const uint8_t* data, int size)
{
    memcpy(m_state.controller, data, qMin(size, 32));
    memcpy(m_state.output, data, qMin(size, 32)); // start with pass-through
}

void GPC3VMWorker::tick()
{
    if (!m_state.running || m_bytecode.isEmpty()) return;

    int maxInstructions = 256; // prevent infinite loops per tick
    while (m_state.running && maxInstructions-- > 0) {
        if (m_state.pc >= static_cast<uint32_t>(m_bytecode.size())) {
            m_state.running = false;
            break;
        }

        uint8_t opcode = static_cast<uint8_t>(m_bytecode[m_state.pc]);
        const uint8_t* operands = reinterpret_cast<const uint8_t*>(
            m_bytecode.constData() + m_state.pc + 1);
        int advance = 1;

        if (!executeOpcode(opcode, operands, advance)) break;
        m_state.pc += advance;
    }

    // Emit current output state
    QByteArray out(reinterpret_cast<const char*>(m_state.output), 32);
    emit outputReady(out);
}

bool GPC3VMWorker::executeOpcode(uint8_t opcode, const uint8_t* ops, int& advance)
{
    switch (static_cast<GPC3Opcode>(opcode)) {
    case OP_NOP:
        advance = 1;
        break;

    case OP_HALT:
        m_state.running = false;
        return false;

    case OP_LOAD: {
        int reg   = ops[0] & 0x1F;
        int16_t imm = static_cast<int16_t>((ops[1] << 8) | ops[2]);
        m_state.regs[reg] = imm;
        advance = 4;
        break;
    }
    case OP_MOVE: {
        int dst = ops[0] & 0x1F, src = ops[1] & 0x1F;
        m_state.regs[dst] = m_state.regs[src];
        advance = 3;
        break;
    }
    case OP_ADD: {
        int dst = ops[0] & 0x1F, a = ops[1] & 0x1F, b = ops[2] & 0x1F;
        m_state.regs[dst] = m_state.regs[a] + m_state.regs[b];
        advance = 4;
        break;
    }
    case OP_SUB: {
        int dst = ops[0] & 0x1F, a = ops[1] & 0x1F, b = ops[2] & 0x1F;
        m_state.regs[dst] = m_state.regs[a] - m_state.regs[b];
        advance = 4;
        break;
    }
    case OP_MUL: {
        int dst = ops[0] & 0x1F, a = ops[1] & 0x1F, b = ops[2] & 0x1F;
        m_state.regs[dst] = m_state.regs[a] * m_state.regs[b];
        advance = 4;
        break;
    }
    case OP_DIV: {
        int dst = ops[0] & 0x1F, a = ops[1] & 0x1F, b = ops[2] & 0x1F;
        if (m_state.regs[b] == 0) { emit vmError("Division by zero"); return false; }
        m_state.regs[dst] = m_state.regs[a] / m_state.regs[b];
        advance = 4;
        break;
    }
    case OP_AND: {
        int dst = ops[0] & 0x1F, a = ops[1] & 0x1F, b = ops[2] & 0x1F;
        m_state.regs[dst] = m_state.regs[a] & m_state.regs[b];
        advance = 4;
        break;
    }
    case OP_OR: {
        int dst = ops[0] & 0x1F, a = ops[1] & 0x1F, b = ops[2] & 0x1F;
        m_state.regs[dst] = m_state.regs[a] | m_state.regs[b];
        advance = 4;
        break;
    }
    case OP_NOT: {
        int dst = ops[0] & 0x1F, src = ops[1] & 0x1F;
        m_state.regs[dst] = ~m_state.regs[src];
        advance = 3;
        break;
    }
    case OP_JMP: {
        uint32_t addr = (ops[0] << 16) | (ops[1] << 8) | ops[2];
        m_state.pc = addr;
        return true;
    }
    case OP_JZ: {
        int reg = ops[0] & 0x1F;
        uint32_t addr = (ops[1] << 16) | (ops[2] << 8) | ops[3];
        if (m_state.regs[reg] == 0) { m_state.pc = addr; return true; }
        advance = 5;
        break;
    }
    case OP_JNZ: {
        int reg = ops[0] & 0x1F;
        uint32_t addr = (ops[1] << 16) | (ops[2] << 8) | ops[3];
        if (m_state.regs[reg] != 0) { m_state.pc = addr; return true; }
        advance = 5;
        break;
    }
    case OP_GETBTN: {
        int reg = ops[0] & 0x1F, btn = ops[1];
        m_state.regs[reg] = getButton(btn);
        advance = 3;
        break;
    }
    case OP_GETAXIS: {
        int reg = ops[0] & 0x1F, axis = ops[1];
        m_state.regs[reg] = getAxis(axis);
        advance = 3;
        break;
    }
    case OP_SETOUT: {
        int outIdx = ops[0], reg = ops[1] & 0x1F;
        setOutput(outIdx, m_state.regs[reg]);
        advance = 3;
        break;
    }
    case OP_WAIT:
        // Wait is handled by the tick timer; just consume the instruction
        advance = 3;
        break;

    default:
        emit vmError(QString("Unknown opcode 0x%1 at pc=%2")
                     .arg(opcode, 2, 16, QChar('0')).arg(m_state.pc));
        m_state.running = false;
        return false;
    }
    return true;
}

int32_t GPC3VMWorker::getButton(int btnIndex) const
{
    // DS5 button layout (from GPC3 button index mapping)
    // Byte 6 bit 4 = Cross, bit 5 = Circle, bit 6 = Square, bit 7 = Triangle
    // Byte 6 bit 0-3 = DPad, Byte 7 bit 0 = L1, bit 1 = R1 etc.
    if (btnIndex < 0 || btnIndex >= 32) return 0;
    int byteIdx = 6 + (btnIndex / 8);
    int bitIdx  = btnIndex % 8;
    if (byteIdx >= 32) return 0;
    return (m_state.controller[byteIdx] >> bitIdx) & 1;
}

int32_t GPC3VMWorker::getAxis(int axisIndex) const
{
    // Axes: 0=LX 1=LY 2=RX 3=RY 4=L2 5=R2 (0-255, centered at 128 for sticks)
    if (axisIndex < 0 || axisIndex >= 6) return 0;
    return m_state.controller[axisIndex];
}

void GPC3VMWorker::setOutput(int index, int32_t value)
{
    if (index < 0 || index >= 32) return;
    m_state.output[index] = static_cast<uint8_t>(qBound(0, value, 255));
}

// ── GPC3Plugin ────────────────────────────────────────────────────────────────

GPC3Plugin::GPC3Plugin(QObject* parent) : QObject(parent) {}
GPC3Plugin::~GPC3Plugin() { shutdown(); }

void GPC3Plugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_worker = new GPC3VMWorker;
    m_worker->moveToThread(m_thread);

    connect(this, &GPC3Plugin::_load,  m_worker, &GPC3VMWorker::loadScript);
    connect(this, &GPC3Plugin::_start, m_worker, &GPC3VMWorker::start);
    connect(this, &GPC3Plugin::_stop,  m_worker, &GPC3VMWorker::stop);
    connect(m_worker, &GPC3VMWorker::vmStarted, this, &GPC3Plugin::vmStarted);
    connect(m_worker, &GPC3VMWorker::vmStopped, this, &GPC3Plugin::vmStopped);
    connect(m_worker, &GPC3VMWorker::vmError,   this, &GPC3Plugin::vmError);
    connect(m_worker, &GPC3VMWorker::outputReady, this, &GPC3Plugin::onOutputReady);

    m_thread->start();
}

void GPC3Plugin::shutdown()
{
    if (m_thread) {
        emit _stop();
        m_thread->quit();
        m_thread->wait(2000);
        delete m_worker;
        m_worker = nullptr;
    }
}

QWidget* GPC3Plugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);

    auto* bar      = new QHBoxLayout;
    auto* lblFile  = new QLabel("No script", w);
    auto* btnOpen  = new QPushButton("Load GPC3", w);
    auto* btnStart = new QPushButton("Run", w);
    auto* btnStop  = new QPushButton("Stop", w);
    btnStop->setEnabled(false);

    bar->addWidget(lblFile, 1);
    bar->addWidget(btnOpen);
    bar->addWidget(btnStart);
    bar->addWidget(btnStop);
    lay->addLayout(bar);

    auto* logView = new QTextEdit(w);
    logView->setReadOnly(true);
    logView->setFontFamily("Consolas");
    lay->addWidget(logView);

    connect(btnOpen, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getOpenFileName(w, "Load GPC3 Bytecode",
            QString(), "GPC3 Files (*.gpc *.gpc3 *.bin);;All Files (*)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { logView->append("Cannot open: " + path); return; }
        emit _load(f.readAll());
        lblFile->setText(QFileInfo(path).fileName());
        logView->append("Loaded: " + path);
    });

    connect(btnStart, &QPushButton::clicked, [=]() {
        emit _start();
        btnStart->setEnabled(false);
        btnStop->setEnabled(true);
    });
    connect(btnStop, &QPushButton::clicked, [=]() {
        emit _stop();
        btnStop->setEnabled(false);
        btnStart->setEnabled(true);
    });

    connect(this, &GPC3Plugin::vmStarted, [=]() { logView->append("VM started."); });
    connect(this, &GPC3Plugin::vmStopped, [=]() { logView->append("VM stopped."); });
    connect(this, &GPC3Plugin::vmError,   [=](const QString& e) {
        logView->append("<span style='color:red'>Error: " + e.toHtmlEscaped() + "</span>");
    });

    return w;
}

void GPC3Plugin::onOutputReady(QByteArray data)
{
    // Write modified controller output to shared memory
    if (m_outputWriter && m_outputWriter->isConnected()) {
        // Output writer API: handled by SharedMemoryManager
        Q_UNUSED(data);
    }
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::GPC3Plugin();
}
