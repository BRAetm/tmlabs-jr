#include "ViGEmPlugin.h"

#include <QDebug>
#include <QMutexLocker>

#include <Windows.h>

namespace Labs {

// ── ViGEmClient.dll runtime loading ─────────────────────────────────────────
//
// ViGEmBus is an external driver (https://vigem.org/). Rather than hard-link
// against ViGEmClient.lib we load ViGEmClient.dll at runtime. This means the
// plugin builds and the pipeline wires up even if the user hasn't installed
// ViGEmBus yet — we just log a clear message and no-op on pushState().

struct XusbReport {
    quint16 wButtons;
    quint8  bLeftTrigger;
    quint8  bRightTrigger;
    qint16  sThumbLX;
    qint16  sThumbLY;
    qint16  sThumbRX;
    qint16  sThumbRY;
};

using VIGEM_ERROR = int;

using FN_alloc         = void* (*)();
using FN_free          = void  (*)(void*);
using FN_connect       = VIGEM_ERROR (*)(void*);
using FN_disconnect    = void  (*)(void*);
using FN_x360_alloc    = void* (*)();
using FN_target_add    = VIGEM_ERROR (*)(void*, void*);
using FN_target_remove = VIGEM_ERROR (*)(void*, void*);
using FN_target_free   = void  (*)(void*);
using FN_x360_update   = VIGEM_ERROR (*)(void*, void*, XusbReport);

struct ViGEmSink::Impl {
    HMODULE dll = nullptr;
    void*   client = nullptr;
    void*   x360   = nullptr;

    FN_alloc         vigem_alloc         = nullptr;
    FN_free          vigem_free          = nullptr;
    FN_connect       vigem_connect       = nullptr;
    FN_disconnect    vigem_disconnect    = nullptr;
    FN_x360_alloc    vigem_target_x360_alloc = nullptr;
    FN_target_add    vigem_target_add    = nullptr;
    FN_target_remove vigem_target_remove = nullptr;
    FN_target_free   vigem_target_free   = nullptr;
    FN_x360_update   vigem_target_x360_update = nullptr;

    bool loadDll()
    {
        dll = ::LoadLibraryW(L"ViGEmClient.dll");
        if (!dll) return false;

        vigem_alloc              = reinterpret_cast<FN_alloc>        (::GetProcAddress(dll, "vigem_alloc"));
        vigem_free               = reinterpret_cast<FN_free>         (::GetProcAddress(dll, "vigem_free"));
        vigem_connect            = reinterpret_cast<FN_connect>      (::GetProcAddress(dll, "vigem_connect"));
        vigem_disconnect         = reinterpret_cast<FN_disconnect>   (::GetProcAddress(dll, "vigem_disconnect"));
        vigem_target_x360_alloc  = reinterpret_cast<FN_x360_alloc>   (::GetProcAddress(dll, "vigem_target_x360_alloc"));
        vigem_target_add         = reinterpret_cast<FN_target_add>   (::GetProcAddress(dll, "vigem_target_add"));
        vigem_target_remove      = reinterpret_cast<FN_target_remove>(::GetProcAddress(dll, "vigem_target_remove"));
        vigem_target_free        = reinterpret_cast<FN_target_free>  (::GetProcAddress(dll, "vigem_target_free"));
        vigem_target_x360_update = reinterpret_cast<FN_x360_update>  (::GetProcAddress(dll, "vigem_target_x360_update"));

        return vigem_alloc && vigem_free && vigem_connect && vigem_disconnect
            && vigem_target_x360_alloc && vigem_target_add && vigem_target_remove
            && vigem_target_free && vigem_target_x360_update;
    }

    void unloadDll()
    {
        if (dll) { ::FreeLibrary(dll); dll = nullptr; }
    }
};

// ── ViGEmSink ───────────────────────────────────────────────────────────────

ViGEmSink::ViGEmSink(QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    if (!m_impl->loadDll()) {
        m_status = QStringLiteral("ViGEmClient.dll not found — install ViGEmBus driver and place ViGEmClient.dll next to LabsEngine.exe");
        qWarning() << "ViGEm:" << m_status;
        return;
    }

    m_impl->client = m_impl->vigem_alloc();
    if (!m_impl->client) {
        m_status = QStringLiteral("vigem_alloc failed");
        qWarning() << "ViGEm:" << m_status;
        return;
    }

    if (m_impl->vigem_connect(m_impl->client) != 0) {
        m_status = QStringLiteral("vigem_connect failed — ViGEmBus driver not installed?");
        qWarning() << "ViGEm:" << m_status;
        m_impl->vigem_free(m_impl->client);
        m_impl->client = nullptr;
        return;
    }

    m_impl->x360 = m_impl->vigem_target_x360_alloc();
    if (!m_impl->x360 || m_impl->vigem_target_add(m_impl->client, m_impl->x360) != 0) {
        m_status = QStringLiteral("vigem_target_add failed");
        qWarning() << "ViGEm:" << m_status;
        if (m_impl->x360) m_impl->vigem_target_free(m_impl->x360);
        m_impl->vigem_disconnect(m_impl->client);
        m_impl->vigem_free(m_impl->client);
        m_impl->client = nullptr;
        m_impl->x360   = nullptr;
        return;
    }

    m_status = QStringLiteral("virtual X360 pad online");
    m_ready.store(true);
    qInfo() << "ViGEm:" << m_status;
}

ViGEmSink::~ViGEmSink()
{
    QMutexLocker lock(&m_mx);
    if (m_impl->x360) {
        if (m_impl->vigem_target_remove) m_impl->vigem_target_remove(m_impl->client, m_impl->x360);
        if (m_impl->vigem_target_free)   m_impl->vigem_target_free(m_impl->x360);
    }
    if (m_impl->client) {
        if (m_impl->vigem_disconnect) m_impl->vigem_disconnect(m_impl->client);
        if (m_impl->vigem_free)       m_impl->vigem_free(m_impl->client);
    }
    m_impl->unloadDll();
}

void ViGEmSink::pushState(const ControllerState& state)
{
    if (!m_ready.load()) return;

    XusbReport r;
    r.wButtons     = state.buttons;
    r.bLeftTrigger = state.leftTrigger;
    r.bRightTrigger= state.rightTrigger;
    r.sThumbLX     = state.leftThumbX;
    r.sThumbLY     = state.leftThumbY;
    r.sThumbRX     = state.rightThumbX;
    r.sThumbRY     = state.rightThumbY;

    QMutexLocker lock(&m_mx);
    if (m_impl->client && m_impl->x360 && m_impl->vigem_target_x360_update) {
        m_impl->vigem_target_x360_update(m_impl->client, m_impl->x360, r);
    }
}

// ── ViGEmPlugin ─────────────────────────────────────────────────────────────

ViGEmPlugin::ViGEmPlugin() : m_sink(std::make_unique<ViGEmSink>(this)) {}
ViGEmPlugin::~ViGEmPlugin() = default;

} // namespace Labs

extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin()
{
    return new Labs::ViGEmPlugin();
}
