#pragma once

#include "LabsCore.h"

#include <QtGlobal>

namespace Labs {

// Button bits — layout matches XINPUT_GAMEPAD_* constants so XInput and
// ViGEm plugins can pass through without remapping.
enum ControllerButton : quint16 {
    ButtonDpadUp        = 0x0001,
    ButtonDpadDown      = 0x0002,
    ButtonDpadLeft      = 0x0004,
    ButtonDpadRight     = 0x0008,
    ButtonStart         = 0x0010,
    ButtonBack          = 0x0020,
    ButtonLeftThumb     = 0x0040,
    ButtonRightThumb    = 0x0080,
    ButtonLeftShoulder  = 0x0100,
    ButtonRightShoulder = 0x0200,
    ButtonGuide         = 0x0400,
    ButtonA             = 0x1000,
    ButtonB             = 0x2000,
    ButtonX             = 0x4000,
    ButtonY             = 0x8000,
};

struct LABSCORE_API ControllerState {
    quint16 buttons       = 0;
    quint8  leftTrigger   = 0;
    quint8  rightTrigger  = 0;
    qint16  leftThumbX    = 0;
    qint16  leftThumbY    = 0;
    qint16  rightThumbX   = 0;
    qint16  rightThumbY   = 0;
    qint64  timestampUs   = 0;
    int     slot          = 0;  // 0..3 for XInput slot; 0 for most uses
    bool    connected     = false;
};

class LABSCORE_API IControllerSink {
public:
    virtual ~IControllerSink() = default;
    virtual void pushState(const ControllerState& state) = 0;
};

class LABSCORE_API IControllerSource {
public:
    virtual ~IControllerSource() = default;
    virtual void setSink(IControllerSink* sink) = 0;
    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual bool isRunning() const = 0;
};

} // namespace Labs
