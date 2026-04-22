#include "ShmBus.h"

#include <QDateTime>
#include <QDebug>

#include <cstring>

namespace Labs {

// Atomic helpers — sequence must be bumped AFTER payload write so readers
// can detect a coherent snapshot. All writes use memory barriers.
static inline void storeU32(volatile quint8* base, int off, quint32 v) {
    std::memcpy(const_cast<quint8*>(base + off), &v, 4);
    MemoryBarrier();
}
static inline void storeI32(volatile quint8* base, int off, qint32 v) {
    std::memcpy(const_cast<quint8*>(base + off), &v, 4);
    MemoryBarrier();
}
static inline void storeI64(volatile quint8* base, int off, qint64 v) {
    std::memcpy(const_cast<quint8*>(base + off), &v, 8);
    MemoryBarrier();
}
static inline quint32 loadU32(const volatile quint8* base, int off) {
    quint32 v; std::memcpy(&v, const_cast<const quint8*>(base + off), 4); return v;
}
static inline qint32 loadI32(const volatile quint8* base, int off) {
    qint32 v; std::memcpy(&v, const_cast<const quint8*>(base + off), 4); return v;
}
static inline float loadF32(const volatile quint8* base, int off) {
    float v; std::memcpy(&v, const_cast<const quint8*>(base + off), 4); return v;
}

// ── FrameShmWriter ──────────────────────────────────────────────────────────

FrameShmWriter::FrameShmWriter() = default;
FrameShmWriter::~FrameShmWriter() { close(); }

bool FrameShmWriter::open(quint32 writerPid, qint32 sessionId)
{
    close();
    m_sessionId = sessionId;
    m_blockName = QStringLiteral("Labs_%1_Frame_%2").arg(writerPid).arg(sessionId);
    const QString eventName = QStringLiteral("Global\\Labs_%1_Frame_%2_Written").arg(writerPid).arg(sessionId);

    m_mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, kBlockSize,
        reinterpret_cast<LPCWSTR>(m_blockName.utf16()));
    if (!m_mapping) { qWarning() << "FrameShm: CreateFileMapping failed"; return false; }

    m_view = static_cast<quint8*>(::MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, kBlockSize));
    if (!m_view) { qWarning() << "FrameShm: MapViewOfFile failed"; close(); return false; }

    // Event may already exist (reader could have opened it first) — that's fine.
    m_event = ::CreateEventW(nullptr, FALSE, FALSE,
        reinterpret_cast<LPCWSTR>(eventName.utf16()));
    if (!m_event) {
        // Fall back to session-local event if Global\ ACL prevented creation.
        const QString localEvent = QStringLiteral("Labs_%1_Frame_%2_Written").arg(writerPid).arg(sessionId);
        m_event = ::CreateEventW(nullptr, FALSE, FALSE,
            reinterpret_cast<LPCWSTR>(localEvent.utf16()));
    }

    // Zero the header so readers waiting for the magic don't get garbage.
    std::memset(m_view, 0, kHeaderSize);
    storeU32(m_view, 0,  kMagic);
    storeU32(m_view, 4,  kVersion);
    storeU32(m_view, 8,  writerPid);
    storeU32(m_view, 12, 0);         // sequence
    storeU32(m_view, 16, 0);         // payload_size (until first frame)
    storeI32(m_view, 36, sessionId);
    return true;
}

void FrameShmWriter::close()
{
    if (m_view)    { ::UnmapViewOfFile(m_view); m_view = nullptr; }
    if (m_mapping) { ::CloseHandle(m_mapping); m_mapping = nullptr; }
    if (m_event)   { ::CloseHandle(m_event); m_event = nullptr; }
    m_sequence = 0;
    m_blockName.clear();
}

bool FrameShmWriter::write(const Frame& frame)
{
    if (!m_view || !frame.isValid()) return false;

    const int payloadBytes = frame.stride * frame.height;
    if (payloadBytes > kMaxPayload) return false;

    // Write payload first.
    std::memcpy(m_view + kHeaderSize, frame.data.constData(), payloadBytes);

    // Then header fields.
    storeU32(m_view, 16, static_cast<quint32>(payloadBytes));
    storeU32(m_view, 20, static_cast<quint32>(frame.width));
    storeU32(m_view, 24, static_cast<quint32>(frame.height));
    storeU32(m_view, 28, static_cast<quint32>(frame.stride));
    storeU32(m_view, 32, 0);   // format: 0 = BGRA
    storeI64(m_view, 40, frame.timestampUs / 1000);

    // Sequence last — this is the "publish" signal for readers.
    ++m_sequence;
    storeU32(m_view, 12, m_sequence);

    if (m_event) ::SetEvent(m_event);
    return true;
}

// ── GamepadShmReader ────────────────────────────────────────────────────────

GamepadShmReader::GamepadShmReader(QObject* parent) : QThread(parent) {}
GamepadShmReader::~GamepadShmReader() { requestStop(); wait(500); }

void GamepadShmReader::configure(quint32 writerPid)
{
    m_writerPid = writerPid;
}

void GamepadShmReader::run()
{
    const QString blockName = QStringLiteral("Labs_%1_Gamepad").arg(m_writerPid);
    const QString eventName = QStringLiteral("Global\\Labs_%1_Gamepad_Written").arg(m_writerPid);
    const QString localEventName = QStringLiteral("Labs_%1_Gamepad_Written").arg(m_writerPid);

    HANDLE mapping = nullptr;
    HANDLE event   = nullptr;
    quint8* view   = nullptr;
    quint32 lastSeq = 0;

    while (!m_stop.load()) {
        if (!mapping) {
            mapping = ::OpenFileMappingW(FILE_MAP_READ, FALSE,
                reinterpret_cast<LPCWSTR>(blockName.utf16()));
            if (!mapping) { QThread::msleep(200); continue; }
            view = static_cast<quint8*>(::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, kBlockSize));
            if (!view) { ::CloseHandle(mapping); mapping = nullptr; QThread::msleep(200); continue; }
        }
        if (!event) {
            event = ::OpenEventW(SYNCHRONIZE, FALSE,
                reinterpret_cast<LPCWSTR>(eventName.utf16()));
            if (!event) {
                event = ::OpenEventW(SYNCHRONIZE, FALSE,
                    reinterpret_cast<LPCWSTR>(localEventName.utf16()));
            }
        }

        if (event) {
            ::WaitForSingleObject(event, 50);
        } else {
            QThread::msleep(10);
        }

        if (loadU32(view, 0) != kMagic) continue;
        const quint32 seq = loadU32(view, 12);
        if (seq == lastSeq) continue;
        lastSeq = seq;

        // Payload begins at offset 32 (header size).
        const int axesOff    = kHeaderSize + 0;
        const int buttonsOff = kHeaderSize + 16;

        const float leftX  = loadF32(view, axesOff + 0);
        const float leftY  = loadF32(view, axesOff + 4);
        const float rightX = loadF32(view, axesOff + 8);
        const float rightY = loadF32(view, axesOff + 12);

        auto clampShort = [](float v) -> qint16 {
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            const float scaled = v * 32767.0f;
            return static_cast<qint16>(scaled);
        };

        ControllerState s;
        s.leftThumbX  = clampShort(leftX);
        s.leftThumbY  = clampShort(leftY);
        s.rightThumbX = clampShort(rightX);
        s.rightThumbY = clampShort(rightY);

        // Button bit order per LabsGamepadShm.cs:
        //   0  A, 1  B, 2  X, 3  Y,
        //   4  LB, 5 RB,
        //   6  Back, 7 Start,
        //   8  LS, 9 RS,
        //   10 DUp, 11 DDown, 12 DLeft, 13 DRight,
        //   14 Guide,
        //   15 LT (threshold), 16 RT (threshold)
        const auto b = [&](int i) -> bool { return view[buttonsOff + i] != 0; };
        quint16 btn = 0;
        if (b(0))  btn |= ButtonA;
        if (b(1))  btn |= ButtonB;
        if (b(2))  btn |= ButtonX;
        if (b(3))  btn |= ButtonY;
        if (b(4))  btn |= ButtonLeftShoulder;
        if (b(5))  btn |= ButtonRightShoulder;
        if (b(6))  btn |= ButtonBack;
        if (b(7))  btn |= ButtonStart;
        if (b(8))  btn |= ButtonLeftThumb;
        if (b(9))  btn |= ButtonRightThumb;
        if (b(10)) btn |= ButtonDpadUp;
        if (b(11)) btn |= ButtonDpadDown;
        if (b(12)) btn |= ButtonDpadLeft;
        if (b(13)) btn |= ButtonDpadRight;
        if (b(14)) btn |= ButtonGuide;
        s.buttons      = btn;
        s.leftTrigger  = b(15) ? 255 : 0;
        s.rightTrigger = b(16) ? 255 : 0;
        s.timestampUs  = QDateTime::currentMSecsSinceEpoch() * 1000;
        s.connected    = true;

        if (m_sink) m_sink->pushState(s);
    }

    if (view)    ::UnmapViewOfFile(view);
    if (mapping) ::CloseHandle(mapping);
    if (event)   ::CloseHandle(event);
}

} // namespace Labs
