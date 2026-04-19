#pragma once
// SharedMemory.h — Helios shared memory IPC layer

#include <QString>
#include <QObject>
#include <cstdint>
#include <cstddef>

namespace Helios {

// Named shared memory segments
// Format: Helios_<pid>_<type>  or  Global\Helios_<pid>_<event>
//
// HELIOS_VIDEO_BUFFER   — raw video ring buffer
// HELIOS_PROCESS_ID     — main process PID
// Global\Helios_<pid>_FrameWritten     — event: new frame available
// Global\Helios_<pid>_InferenceReady   — event: inference results ready

struct SharedMemoryHeader {
    uint32_t magic;          // 0x48454C53 "HELS"
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;   // 0=BGR24 1=BGRA8 2=NV12
    uint32_t frame_index;
    uint32_t writer_pid;
    uint8_t  reserved[32];
};

struct SharedMemoryControllerState {
    // Raw controller bytes (32 bytes, DS5 format)
    uint8_t  data[32];
    uint32_t timestamp_ms;
    uint8_t  source;         // 0=DS5 1=DS4 2=XInput 3=ViGEm
};

struct MemoryBlockInfo {
    MemoryBlockInfo() = default;
    MemoryBlockInfo(const MemoryBlockInfo&) = default;
    MemoryBlockInfo(MemoryBlockInfo&&) = default;
    ~MemoryBlockInfo() = default;

    QString name;
    size_t  size;
    bool    read_only;
};

// ── Typed ring buffer readers/writers ─────────────────────────────────────

class ISharedMemoryReader {
public:
    ISharedMemoryReader() = default;
    ISharedMemoryReader(const ISharedMemoryReader&) = default;
    virtual ~ISharedMemoryReader() = default;
    virtual bool    connect()    = 0;
    virtual void    disconnect() = 0;
    virtual bool    isConnected() const = 0;
    virtual bool    readData(void* buf, size_t size, size_t& bytesRead) = 0;
    virtual void*   getEventHandle() const = 0;
    virtual QString getWriterName() const  = 0;
};

class ISharedMemoryWriter {
public:
    ISharedMemoryWriter() = default;
    virtual ~ISharedMemoryWriter() = default;
    virtual bool connect()    = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
};

class SharedMemoryAccessor {
public:
    explicit SharedMemoryAccessor(const MemoryBlockInfo& info);
    SharedMemoryAccessor(const SharedMemoryAccessor&) = default;
    ~SharedMemoryAccessor();

    bool    connect();
    void    disconnect();
    bool    isConnected()  const;
    bool    readData(void* buf, size_t size, size_t& bytesRead);
    void*   getEventHandle() const;
    QString getWriterName()  const;
};

// Typed ring buffer interfaces for each data type
class IControllerInputReader  : public ISharedMemoryReader { public: virtual ~IControllerInputReader()  = default; };
class IControllerInputWriter  : public ISharedMemoryWriter { public: virtual ~IControllerInputWriter()  = default; };
class IControllerOutputReader : public ISharedMemoryReader { public: virtual ~IControllerOutputReader() = default; };
class IControllerOutputWriter : public ISharedMemoryWriter { public: virtual ~IControllerOutputWriter() = default; };
class IKeyboardInputReader    : public ISharedMemoryReader { public: virtual ~IKeyboardInputReader()    = default; };
class IKeyboardInputWriter    : public ISharedMemoryWriter { public: virtual ~IKeyboardInputWriter()    = default; };
class IKeyboardOutputReader   : public ISharedMemoryReader { public: virtual ~IKeyboardOutputReader()   = default; };
class IKeyboardOutputWriter   : public ISharedMemoryWriter { public: virtual ~IKeyboardOutputWriter()   = default; };
class IMouseInputReader       : public ISharedMemoryReader { public: virtual ~IMouseInputReader()       = default; };
class IMouseInputWriter       : public ISharedMemoryWriter { public: virtual ~IMouseInputWriter()       = default; };
class IMouseOutputReader      : public ISharedMemoryReader { public: virtual ~IMouseOutputReader()      = default; };
class IMouseOutputWriter      : public ISharedMemoryWriter { public: virtual ~IMouseOutputWriter()      = default; };
class IVideoInputReader       : public ISharedMemoryReader { public: virtual ~IVideoInputReader()       = default; };
class IVideoInputWriter       : public ISharedMemoryWriter { public: virtual ~IVideoInputWriter()       = default; };
class IVideoOutputReader      : public ISharedMemoryReader { public: virtual ~IVideoOutputReader()      = default; };
class IVideoOutputWriter      : public ISharedMemoryWriter { public: virtual ~IVideoOutputWriter()      = default; };
class IVideoFrameWriter       : public ISharedMemoryWriter { public: virtual ~IVideoFrameWriter()       = default; };
class IGCVDataReader          : public ISharedMemoryReader { public: virtual ~IGCVDataReader()          = default; };
class IGCVDataWriter          : public ISharedMemoryWriter { public: virtual ~IGCVDataWriter()          = default; };

// ── Data structures ────────────────────────────────────────────────────────

struct ControllerData {
    ControllerData();
    uint8_t  raw[32];
};

struct ControllerState {
    ControllerState();
    ControllerState(const ControllerState&);
    ControllerState(ControllerState&&);
    ~ControllerState();
    SharedMemoryControllerState state;
};

struct KeyboardData   { KeyboardData(); uint8_t keys[256]; };
struct MouseData      { MouseData();    int32_t x, y, dx, dy; uint8_t buttons; };
struct VideoFrameData { VideoFrameData(); uint32_t width, height, stride, format; uint8_t* data; size_t size; };
struct GCVData        { GCVData(); uint8_t data[32]; };

// ── Manager ────────────────────────────────────────────────────────────────

class SharedMemoryManager : public QObject {
    Q_OBJECT
public:
    static SharedMemoryManager* instance();

    // Named block format: Helios_<pid>_<name>
    static QString blockName(const QString& suffix);
    static QString globalEventName(const QString& suffix);

    MemoryBlockInfo registerBlock(const QString& name, size_t size, bool readOnly = false);

private:
    SharedMemoryManager(QObject* parent = nullptr);
    ~SharedMemoryManager() override;
};

// ── Video ring buffer ──────────────────────────────────────────────────────

class VideoRingBufferReader {
public:
    VideoRingBufferReader();
    ~VideoRingBufferReader();
    bool open(const QString& name);
    bool read(VideoFrameData& out);
    void close();
};

class VideoRingBufferWriter {
public:
    VideoRingBufferWriter();
    ~VideoRingBufferWriter();
    bool open(const QString& name, uint32_t width, uint32_t height, uint32_t format);
    bool write(const VideoFrameData& frame);
    void close();
};

} // namespace Helios
