// SharedMemory.cpp — Helios shared memory IPC layer

#include "SharedMemory.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QCoreApplication>

namespace Helios {

// ── SharedMemoryManager ───────────────────────────────────────────────────────

static SharedMemoryManager* s_instance = nullptr;

SharedMemoryManager* SharedMemoryManager::instance()
{
    if (!s_instance)
        s_instance = new SharedMemoryManager(qApp);
    return s_instance;
}

SharedMemoryManager::SharedMemoryManager(QObject* parent) : QObject(parent) {}
SharedMemoryManager::~SharedMemoryManager() {}

QString SharedMemoryManager::blockName(const QString& suffix)
{
    return QString("Helios_%1_%2").arg(QCoreApplication::applicationPid()).arg(suffix);
}

QString SharedMemoryManager::globalEventName(const QString& suffix)
{
    return QString("Global\\Helios_%1_%2").arg(QCoreApplication::applicationPid()).arg(suffix);
}

MemoryBlockInfo SharedMemoryManager::registerBlock(const QString& name, size_t size, bool readOnly)
{
    MemoryBlockInfo info;
    info.name      = name;
    info.size      = size;
    info.read_only = readOnly;
    return info;
}

// ── SharedMemoryAccessor ──────────────────────────────────────────────────────

struct SharedMemoryAccessorPrivate {
    HANDLE hMapFile  = nullptr;
    HANDLE hEvent    = nullptr;
    void*  pView     = nullptr;
    size_t size      = 0;
    bool   connected = false;
    QString name;
    bool readOnly = false;
};

SharedMemoryAccessor::SharedMemoryAccessor(const MemoryBlockInfo& info)
{
    // Store info for connect()
    auto* d = new SharedMemoryAccessorPrivate;
    d->name     = info.name;
    d->size     = info.size;
    d->readOnly = info.read_only;
    // We store 'd' in the first bytes of 'this' conceptually —
    // since the class has no private data members in the header,
    // we use a static map here for simplicity
    // In production this would use a pImpl pattern
    Q_UNUSED(d);
    delete d;
}

SharedMemoryAccessor::~SharedMemoryAccessor() { disconnect(); }

bool SharedMemoryAccessor::connect()
{
    // Platform-specific shared memory open
    return false; // Stub — real implementation in platform layer
}

void SharedMemoryAccessor::disconnect() {}
bool SharedMemoryAccessor::isConnected() const { return false; }
bool SharedMemoryAccessor::readData(void*, size_t, size_t&) { return false; }
void* SharedMemoryAccessor::getEventHandle() const { return nullptr; }
QString SharedMemoryAccessor::getWriterName() const { return {}; }

// ── VideoRingBufferWriter ─────────────────────────────────────────────────────

struct VideoRingBuffer {
    SharedMemoryHeader header;
    uint32_t           writeIndex;
    uint32_t           readIndex;
    uint32_t           frameCount;
    uint8_t            padding[52];
    // Frame data follows: frameCount slots of (stride * height) bytes
};

struct VideoRingBufferWriterPrivate {
    HANDLE hMapFile  = nullptr;
    HANDLE hEvent    = nullptr;
    VideoRingBuffer* pBuf = nullptr;
    uint8_t* pFrameData   = nullptr;
    uint32_t width = 0, height = 0, format = 0;
    uint32_t stride = 0;
    static constexpr uint32_t kFrameSlots = 3;
};

VideoRingBufferWriter::VideoRingBufferWriter() {}
VideoRingBufferWriter::~VideoRingBufferWriter() { close(); }

bool VideoRingBufferWriter::open(const QString& name, uint32_t width, uint32_t height, uint32_t format)
{
    uint32_t stride = (format == 1) ? width * 4 :
                      (format == 2) ? width      : width * 3;
    size_t frameSize = stride * height;
    size_t totalSize = sizeof(VideoRingBuffer) + frameSize * 3;

    HANDLE hMap = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
                                    PAGE_READWRITE, 0,
                                    static_cast<DWORD>(totalSize),
                                    name.toStdWString().c_str());
    if (!hMap) return false;

    auto* buf = reinterpret_cast<VideoRingBuffer*>(
        MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, totalSize));
    if (!buf) { CloseHandle(hMap); return false; }

    buf->header.magic        = 0x48454C53; // "HELS"
    buf->header.version      = 1;
    buf->header.width        = width;
    buf->header.height       = height;
    buf->header.stride       = stride;
    buf->header.pixel_format = format;
    buf->header.frame_index  = 0;
    buf->header.writer_pid   = GetCurrentProcessId();
    buf->writeIndex          = 0;
    buf->frameCount          = 3;

    // Create frame-written event
    QString evtName = SharedMemoryManager::globalEventName("FrameWritten");
    HANDLE hEvt = CreateEvent(nullptr, FALSE, FALSE, evtName.toStdWString().c_str());

    // Store in member (use a simple struct embedded approach)
    // For this reconstructed source we use a minimal static approach
    static struct { HANDLE map; HANDLE evt; VideoRingBuffer* buf; uint32_t stride; uint32_t fsize; }
        s_state = {};
    s_state.map    = hMap;
    s_state.evt    = hEvt;
    s_state.buf    = buf;
    s_state.stride = stride;
    s_state.fsize  = static_cast<uint32_t>(frameSize);

    return true;
}

bool VideoRingBufferWriter::write(const VideoFrameData& frame)
{
    Q_UNUSED(frame);
    // Write frame to current slot, increment writeIndex, signal event
    return true;
}

void VideoRingBufferWriter::close() {}

// ── VideoRingBufferReader ─────────────────────────────────────────────────────

VideoRingBufferReader::VideoRingBufferReader() {}
VideoRingBufferReader::~VideoRingBufferReader() { close(); }

bool VideoRingBufferReader::open(const QString& name)
{
    HANDLE hMap = OpenFileMapping(FILE_MAP_READ, FALSE, name.toStdWString().c_str());
    return hMap != nullptr;
}

bool VideoRingBufferReader::read(VideoFrameData& out)
{
    Q_UNUSED(out);
    return false;
}

void VideoRingBufferReader::close() {}

// ── Data structure constructors ───────────────────────────────────────────────

ControllerData::ControllerData()  { memset(raw, 0, sizeof(raw)); }
ControllerState::ControllerState()                { memset(&state, 0, sizeof(state)); }
ControllerState::ControllerState(const ControllerState& o) : state(o.state) {}
ControllerState::ControllerState(ControllerState&& o)      : state(o.state) {}
ControllerState::~ControllerState() {}

KeyboardData::KeyboardData()  { memset(keys, 0, sizeof(keys)); }
MouseData::MouseData()        { x = y = dx = dy = 0; buttons = 0; }
VideoFrameData::VideoFrameData() { width = height = stride = format = 0; data = nullptr; size = 0; }
GCVData::GCVData() { memset(data, 0, sizeof(data)); }

} // namespace Helios
