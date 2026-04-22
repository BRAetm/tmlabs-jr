#pragma once

#include "LabsCore.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace Labs {

enum class PixelFormat : int {
    Unknown = 0,
    BGRA8,
    RGBA8,
};

struct LABSCORE_API Frame {
    QByteArray  data;          // packed pixel bytes, length >= stride * height
    int         width  = 0;
    int         height = 0;
    int         stride = 0;    // bytes per row
    PixelFormat format = PixelFormat::Unknown;
    qint64      timestampUs = 0;

    bool isValid() const {
        return width > 0 && height > 0 && stride >= width * 4
            && format != PixelFormat::Unknown
            && data.size() >= stride * height;
    }
};

class LABSCORE_API IFrameSink {
public:
    virtual ~IFrameSink() = default;
    virtual void pushFrame(const Frame& frame) = 0;
};

class LABSCORE_API IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual void setSink(IFrameSink* sink) = 0;
    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual bool isRunning() const = 0;
    virtual qint64 frameCount() const = 0;
    virtual QString targetLabel() const { return {}; }

    // Opt-in: source shows its own target picker UI. parentWindow is an HWND
    // (cast to quintptr so headers don't drag in Windows.h). Returns true if
    // the user selected a target.
    virtual bool showPicker(quintptr /*parentWindow*/) { return false; }
};

} // namespace Labs
