#pragma once

#include "FrameTypes.h"
#include "InputTypes.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace Labs {

// ── Frame SHM ───────────────────────────────────────────────────────────────
//
// Binary-compatible with LabsSharp's LabsFrameShm:
//   Block  : Labs_<pid>_Frame_<sessionId>
//   Event  : Global\Labs_<pid>_Frame_<sessionId>_Written
//   Header : 64 bytes, magic "FRML" (0x4D52464C), version=1
//   Payload: up to 1920×1080×4 BGRA
class FrameShmWriter {
public:
    static constexpr quint32 kMagic      = 0x4D52464Cu; // "FRML"
    static constexpr quint32 kVersion    = 1u;
    static constexpr int     kHeaderSize = 64;
    static constexpr int     kMaxPayload = 1920 * 1080 * 4;
    static constexpr int     kBlockSize  = kHeaderSize + kMaxPayload;

    FrameShmWriter();
    ~FrameShmWriter();

    bool open(quint32 writerPid, qint32 sessionId);
    void close();
    bool write(const Frame& frame);

    QString blockName() const { return m_blockName; }

private:
    QString  m_blockName;
    HANDLE   m_mapping = nullptr;
    HANDLE   m_event   = nullptr;
    quint8*  m_view    = nullptr;
    quint32  m_sequence = 0;
    qint32   m_sessionId = 0;
};

// ── Gamepad SHM ─────────────────────────────────────────────────────────────
//
// Binary-compatible with LabsSharp's LabsGamepadShm:
//   Block  : Labs_<writerPid>_Gamepad   (writer is the Python script)
//   Event  : Global\Labs_<writerPid>_Gamepad_Written
//   Header : LabsShmHeader (32 bytes, magic "LABS" 0x5342414C)
//   Payload (64 bytes):
//     [ 0..15] axes[4]    (float32, each -1.0..1.0)
//     [16..32] buttons[17](uint8, 0 or 1)
//     [33..35] pad
//     [36..39] session_id (int32)
//     [40..63] reserved
class GamepadShmReader : public QThread {
    Q_OBJECT
public:
    static constexpr quint32 kMagic      = 0x5342414Cu; // "LABS"
    static constexpr quint32 kVersion    = 1u;
    static constexpr int     kHeaderSize = 32;
    static constexpr int     kPayloadSize = 64;
    static constexpr int     kBlockSize  = kHeaderSize + kPayloadSize;

    explicit GamepadShmReader(QObject* parent = nullptr);
    ~GamepadShmReader() override;

    void configure(quint32 writerPid);
    void setSink(IControllerSink* sink) { m_sink = sink; }
    void requestStop() { m_stop.store(true); }

protected:
    void run() override;

private:
    IControllerSink*  m_sink = nullptr;
    quint32           m_writerPid = 0;
    std::atomic<bool> m_stop { false };
};

} // namespace Labs
