#include "DualSensePlugin.h"

#include <QDebug>
#include <QString>
#include <QStringList>

#include <Windows.h>
#include <SetupAPI.h>
extern "C" {
#include <hidsdi.h>
}

#include <atomic>
#include <thread>
#include <vector>

namespace Labs {

// ── HID identifiers ──────────────────────────────────────────────────────────
static constexpr USHORT kSonyVid      = 0x054C;
static constexpr USHORT kPidDualSense = 0x0CE6;
static constexpr USHORT kPidDualSenseEdge = 0x0DF2;
static constexpr USHORT kPidDualShock4_v1 = 0x05C4;
static constexpr USHORT kPidDualShock4_v2 = 0x09CC;

static bool isSupportedPid(USHORT pid)
{
    return pid == kPidDualSense || pid == kPidDualSenseEdge
        || pid == kPidDualShock4_v1 || pid == kPidDualShock4_v2;
}

// ── ViGEmClient.dll runtime load (mirrors ViGEmPlugin) ───────────────────────
struct XusbReport {
    quint16 wButtons;
    quint8  bLeftTrigger;
    quint8  bRightTrigger;
    qint16  sThumbLX;
    qint16  sThumbLY;
    qint16  sThumbRX;
    qint16  sThumbRY;
};

using FN_alloc        = void* (*)();
using FN_free         = void  (*)(void*);
using FN_connect      = int   (*)(void*);
using FN_disconnect   = void  (*)(void*);
using FN_x360_alloc   = void* (*)();
using FN_target_add   = int   (*)(void*, void*);
using FN_target_remove= int   (*)(void*, void*);
using FN_target_free  = void  (*)(void*);
using FN_x360_update  = int   (*)(void*, void*, XusbReport);

struct Vigem {
    HMODULE dll = nullptr;
    void*   client = nullptr;
    void*   x360   = nullptr;
    FN_alloc         alloc = nullptr;
    FN_free          free_ = nullptr;
    FN_connect       connect_ = nullptr;
    FN_disconnect    disconnect_ = nullptr;
    FN_x360_alloc    x360_alloc = nullptr;
    FN_target_add    target_add = nullptr;
    FN_target_remove target_remove = nullptr;
    FN_target_free   target_free = nullptr;
    FN_x360_update   x360_update = nullptr;

    bool load() {
        dll = ::LoadLibraryW(L"ViGEmClient.dll");
        if (!dll) return false;
        alloc        = reinterpret_cast<FN_alloc>        (::GetProcAddress(dll, "vigem_alloc"));
        free_        = reinterpret_cast<FN_free>         (::GetProcAddress(dll, "vigem_free"));
        connect_     = reinterpret_cast<FN_connect>      (::GetProcAddress(dll, "vigem_connect"));
        disconnect_  = reinterpret_cast<FN_disconnect>   (::GetProcAddress(dll, "vigem_disconnect"));
        x360_alloc   = reinterpret_cast<FN_x360_alloc>   (::GetProcAddress(dll, "vigem_target_x360_alloc"));
        target_add   = reinterpret_cast<FN_target_add>   (::GetProcAddress(dll, "vigem_target_add"));
        target_remove= reinterpret_cast<FN_target_remove>(::GetProcAddress(dll, "vigem_target_remove"));
        target_free  = reinterpret_cast<FN_target_free>  (::GetProcAddress(dll, "vigem_target_free"));
        x360_update  = reinterpret_cast<FN_x360_update>  (::GetProcAddress(dll, "vigem_target_x360_update"));
        return alloc && free_ && connect_ && disconnect_
            && x360_alloc && target_add && target_remove && target_free && x360_update;
    }

    bool createPad() {
        client = alloc();
        if (!client || connect_(client) != 0) return false;
        x360 = x360_alloc();
        if (!x360 || target_add(client, x360) != 0) return false;
        return true;
    }

    void shutdown() {
        if (x360 && client) target_remove(client, x360);
        if (x360)           target_free(x360);
        if (client)         { disconnect_(client); free_(client); }
        if (dll)            ::FreeLibrary(dll);
        x360 = nullptr; client = nullptr; dll = nullptr;
    }

    void send(const XusbReport& r) {
        if (client && x360) x360_update(client, x360, r);
    }
};

// ── HID enum + reader ────────────────────────────────────────────────────────
struct HidDevicePath {
    QString path;
    USHORT  pid = 0;
    bool    isDualSense() const { return pid == kPidDualSense || pid == kPidDualSenseEdge; }
};

static std::vector<HidDevicePath> enumPlaystationHids()
{
    std::vector<HidDevicePath> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return out;

    SP_DEVICE_INTERFACE_DATA ifd{}; ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifd, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;

        std::vector<BYTE> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifd, detail, needed, nullptr, nullptr))
            continue;

        const QString path = QString::fromWCharArray(detail->DevicePath);
        HANDLE h = ::CreateFileW(detail->DevicePath,
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attr{}; attr.Size = sizeof(attr);
        if (HidD_GetAttributes(h, &attr) && attr.VendorID == kSonyVid && isSupportedPid(attr.ProductID)) {
            HidDevicePath d;
            d.path = path;
            d.pid  = attr.ProductID;
            out.push_back(d);
        }
        ::CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return out;
}

// XInput button bits — same numeric values as XInput.h
constexpr quint16 XI_DPAD_UP    = 0x0001;
constexpr quint16 XI_DPAD_DOWN  = 0x0002;
constexpr quint16 XI_DPAD_LEFT  = 0x0004;
constexpr quint16 XI_DPAD_RIGHT = 0x0008;
constexpr quint16 XI_START      = 0x0010;
constexpr quint16 XI_BACK       = 0x0020;
constexpr quint16 XI_LSTICK     = 0x0040;
constexpr quint16 XI_RSTICK     = 0x0080;
constexpr quint16 XI_LB         = 0x0100;
constexpr quint16 XI_RB         = 0x0200;
constexpr quint16 XI_A          = 0x1000;
constexpr quint16 XI_B          = 0x2000;
constexpr quint16 XI_X          = 0x4000;
constexpr quint16 XI_Y          = 0x8000;

// DualSense USB input report layout (offsets into the 64-byte report)
//   [0]    = report id (0x01 over USB)
//   [1..4] = LX, LY, RX, RY  (0..255, center 128)
//   [5..6] = L2, R2 trigger pressure
//   [8]    = digital buttons + dpad   (low nibble = dpad, high nibble = ◯△□✕)
//   [9]    = shoulders + sticks       (L1, R1, L2 click, R2 click, Share, Options, L3, R3)
//   [10]   = misc
static XusbReport mapDualSenseReport(const BYTE* r)
{
    XusbReport x{};

    // Sticks: 0..255 centered at 128 → -32768..32767. Y inverted (DualSense up
    // is high value, XInput up is positive).
    auto cnv = [](BYTE v) -> qint16 {
        const int s = int(v) - 128;
        return qint16(qBound(-32768, s * 257, 32767));
    };
    x.sThumbLX =  cnv(r[1]);
    x.sThumbLY = -cnv(r[2]);
    x.sThumbRX =  cnv(r[3]);
    x.sThumbRY = -cnv(r[4]);

    x.bLeftTrigger  = r[5];
    x.bRightTrigger = r[6];

    const BYTE btns8 = r[8];
    const BYTE dpad  = btns8 & 0x0F;
    // DualSense dpad encoding: 0=up, 1=upright, ..., 7=upleft, 8=neutral
    quint16 dp = 0;
    switch (dpad) {
        case 0: dp = XI_DPAD_UP; break;
        case 1: dp = XI_DPAD_UP   | XI_DPAD_RIGHT; break;
        case 2: dp = XI_DPAD_RIGHT; break;
        case 3: dp = XI_DPAD_DOWN | XI_DPAD_RIGHT; break;
        case 4: dp = XI_DPAD_DOWN; break;
        case 5: dp = XI_DPAD_DOWN | XI_DPAD_LEFT; break;
        case 6: dp = XI_DPAD_LEFT; break;
        case 7: dp = XI_DPAD_UP   | XI_DPAD_LEFT; break;
        default: dp = 0; break;
    }
    quint16 buttons = dp;
    if (btns8 & 0x10) buttons |= XI_X;   // Square  → X
    if (btns8 & 0x20) buttons |= XI_A;   // Cross   → A
    if (btns8 & 0x40) buttons |= XI_B;   // Circle  → B
    if (btns8 & 0x80) buttons |= XI_Y;   // Triangle→ Y

    const BYTE btns9 = r[9];
    if (btns9 & 0x01) buttons |= XI_LB;     // L1
    if (btns9 & 0x02) buttons |= XI_RB;     // R1
    if (btns9 & 0x10) buttons |= XI_BACK;   // Share
    if (btns9 & 0x20) buttons |= XI_START;  // Options
    if (btns9 & 0x40) buttons |= XI_LSTICK; // L3
    if (btns9 & 0x80) buttons |= XI_RSTICK; // R3

    x.wButtons = buttons;
    return x;
}

// ── Plugin Impl ──────────────────────────────────────────────────────────────
struct DualSensePlugin::Impl {
    Vigem               vigem;
    std::atomic<bool>   running{false};
    std::thread         readerThread;
    HANDLE              hidHandle = INVALID_HANDLE_VALUE;
    USHORT              pid = 0;

    void start() {
        running.store(true);
        readerThread = std::thread([this]{ runReader(); });
    }

    void stop() {
        running.store(false);
        if (hidHandle != INVALID_HANDLE_VALUE) ::CancelIoEx(hidHandle, nullptr);
        if (readerThread.joinable()) readerThread.join();
        if (hidHandle != INVALID_HANDLE_VALUE) { ::CloseHandle(hidHandle); hidHandle = INVALID_HANDLE_VALUE; }
        vigem.shutdown();
    }

    void runReader() {
        // Loop: scan for a controller, open it, read until disconnect, repeat.
        while (running.load()) {
            auto devs = enumPlaystationHids();
            if (devs.empty()) {
                ::Sleep(800);
                continue;
            }
            const auto d = devs.front();
            std::wstring wpath = d.path.toStdWString();
            hidHandle = ::CreateFileW(wpath.c_str(),
                                      GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (hidHandle == INVALID_HANDLE_VALUE) {
                ::Sleep(500);
                continue;
            }
            pid = d.pid;
            qInfo().noquote() << "[DualSense] connected — pid=0x"
                              << QString::number(d.pid, 16).toUpper();

            // Read loop
            BYTE buf[78] = {0};
            OVERLAPPED ov{};
            ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            while (running.load()) {
                ::ResetEvent(ov.hEvent);
                DWORD got = 0;
                BOOL ok = ::ReadFile(hidHandle, buf, sizeof(buf), &got, &ov);
                if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
                    DWORD wait = ::WaitForSingleObject(ov.hEvent, 1000);
                    if (wait != WAIT_OBJECT_0) {
                        if (!running.load()) break;
                        continue;
                    }
                    ok = ::GetOverlappedResult(hidHandle, &ov, &got, FALSE);
                }
                if (!ok || got == 0) {
                    qInfo() << "[DualSense] disconnected";
                    break;
                }
                // Report id at byte 0 — DualSense USB is 0x01.
                // Bluetooth uses 0x31 with extra header bytes; offset by 1 to align.
                const BYTE* report = buf;
                if (got >= 78 && buf[0] == 0x31) report = buf + 2;  // BT layout

                XusbReport x = mapDualSenseReport(report);
                vigem.send(x);
            }
            ::CloseHandle(ov.hEvent);
            ::CloseHandle(hidHandle);
            hidHandle = INVALID_HANDLE_VALUE;
        }
    }
};

// ── Plugin lifecycle ─────────────────────────────────────────────────────────
DualSensePlugin::DualSensePlugin()
    : m_impl(std::make_unique<Impl>())
{}

DualSensePlugin::~DualSensePlugin()
{
    if (m_impl) m_impl->stop();
}

void DualSensePlugin::initialize(const PluginContext&)
{
    if (!m_impl->vigem.load()) {
        m_status = QStringLiteral("ViGEmClient.dll missing");
        qWarning() << "[DualSense]" << m_status;
        return;
    }
    if (!m_impl->vigem.createPad()) {
        m_status = QStringLiteral("ViGEm pad create failed");
        qWarning() << "[DualSense]" << m_status;
        return;
    }
    m_impl->start();
    m_status = QStringLiteral("watching for DualSense / DualShock 4");
    qInfo() << "[DualSense]" << m_status;
}

void DualSensePlugin::shutdown()
{
    if (m_impl) m_impl->stop();
}

} // namespace Labs

extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin()
{
    return new Labs::DualSensePlugin();
}
