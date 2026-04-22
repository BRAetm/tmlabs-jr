#include "WgcSource.h"
#include "SettingsManager.h"

#include <QDateTime>
#include <QDebug>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <ShObjIdl.h>   // IInitializeWithWindow

using Microsoft::WRL::ComPtr;
namespace wgx  = winrt::Windows::Graphics;
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wgc  = winrt::Windows::Graphics::Capture;

namespace Labs {

struct WgcSource::Impl {
    ComPtr<ID3D11Device>        d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    winrt::com_ptr<::IInspectable> winrtDevice;

    wgc::GraphicsCaptureItem        item { nullptr };
    wgc::Direct3D11CaptureFramePool framePool { nullptr };
    wgc::GraphicsCaptureSession     session { nullptr };

    winrt::event_token frameArrivedToken {};

    bool initD3D()
    {
        if (d3dDevice) return true;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL selected;
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        };
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            d3dDevice.GetAddressOf(), &selected, d3dContext.GetAddressOf());
        if (FAILED(hr)) return false;

        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3dDevice.As(&dxgi))) return false;

        winrt::com_ptr<::IInspectable> insp;
        if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),
                reinterpret_cast<::IInspectable**>(winrt::put_abi(insp))))) return false;
        winrtDevice = std::move(insp);
        return true;
    }
};

// ── life-cycle ──────────────────────────────────────────────────────────────

WgcSource::WgcSource(QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    // Qt already CoInitializes the main thread as STA. WinRT apartment setup
    // will throw RPC_E_CHANGED_MODE if we try to switch to MTA. WGC works fine
    // from either apartment, so just swallow "already initialized" and move on.
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
        // already initialized — fine.
    } catch (...) {
        qWarning() << "WgcSource: init_apartment unexpected exception";
    }
}

WgcSource::~WgcSource() { stop(); }

// ── target ──────────────────────────────────────────────────────────────────

void WgcSource::setSettings(SettingsManager* settings)
{
    m_settings = settings;
    if (m_settings) {
        m_targetLabel = m_settings->value(QStringLiteral("wgc/lastTargetTitle")).toString();
    }
}

void WgcSource::setTargetWindow(HWND hwnd, const QString& label)
{
    m_target = hwnd;
    m_targetLabel = label;
    if (m_settings && !label.isEmpty()) {
        m_settings->setValue(QStringLiteral("wgc/lastTargetTitle"), label);
    }
}

bool WgcSource::pickTarget(HWND parent)
{
    if (!wgc::GraphicsCaptureSession::IsSupported()) return false;

    wgc::GraphicsCapturePicker picker;

    // Parent the picker to the main window so it's modal to the app.
    auto initWithWindow = picker.as<::IInitializeWithWindow>();
    if (initWithWindow) initWithWindow->Initialize(parent);

    auto async = picker.PickSingleItemAsync();
    // Block until the picker returns. Pump messages to avoid freezing the UI.
    while (async.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(10);
    }
    auto item = async.GetResults();
    if (!item) return false;

    // Find the HWND for the chosen item (window-only scenario).
    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                       ::IGraphicsCaptureItemInterop>();
    Q_UNUSED(interop); // the picker provides item directly; we store it via setTargetWindow below

    m_impl->item = item;
    m_targetLabel = QString::fromStdWString(std::wstring(item.DisplayName()));
    m_target = nullptr; // item is authoritative; start() checks m_impl->item first
    if (m_settings) m_settings->setValue(QStringLiteral("wgc/lastTargetTitle"), m_targetLabel);
    return true;
}

// ── control ─────────────────────────────────────────────────────────────────

bool WgcSource::start()
{
    if (m_running.load()) return true;
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        qWarning() << "WGC: not supported on this system";
        return false;
    }
    if (!m_impl->initD3D()) {
        qWarning() << "WGC: D3D init failed";
        return false;
    }

    // If the picker already set an item, use it. Otherwise build from HWND.
    if (!m_impl->item) {
        HWND hwnd = m_target ? m_target : ::GetForegroundWindow();
        if (!hwnd || !::IsWindow(hwnd)) {
            qWarning() << "WGC: no valid target HWND";
            return false;
        }
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                           ::IGraphicsCaptureItemInterop>();
        HRESULT hr = interop->CreateForWindow(hwnd,
            winrt::guid_of<wgc::GraphicsCaptureItem>(),
            winrt::put_abi(m_impl->item));
        if (FAILED(hr) || !m_impl->item) return false;

        wchar_t title[256] { 0 };
        ::GetWindowTextW(hwnd, title, 255);
        if (m_targetLabel.isEmpty())
            m_targetLabel = QString::fromWCharArray(title);
    }

    const auto size = m_impl->item.Size();
    auto device = m_impl->winrtDevice.as<wgdx::Direct3D11::IDirect3DDevice>();
    m_impl->framePool = wgc::Direct3D11CaptureFramePool::Create(device,
        wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);

    m_impl->frameArrivedToken = m_impl->framePool.FrameArrived(
        [this](wgc::Direct3D11CaptureFramePool const& pool, auto&&) {
            auto frame = pool.TryGetNextFrame();
            if (!frame || !m_sink) return;

            auto surface = frame.Surface();
            winrt::com_ptr<ID3D11Texture2D> tex;
            auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            if (FAILED(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), tex.put_void()))) return;

            D3D11_TEXTURE2D_DESC desc {};
            tex->GetDesc(&desc);

            D3D11_TEXTURE2D_DESC stagingDesc = desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;

            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(m_impl->d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &staging))) return;
            m_impl->d3dContext->CopyResource(staging.Get(), tex.get());

            D3D11_MAPPED_SUBRESOURCE m {};
            if (FAILED(m_impl->d3dContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return;

            Frame out;
            out.width  = int(desc.Width);
            out.height = int(desc.Height);
            out.stride = int(m.RowPitch);
            out.format = PixelFormat::BGRA8;
            out.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
            out.data = QByteArray(reinterpret_cast<const char*>(m.pData),
                                  int(m.RowPitch) * int(desc.Height));

            m_impl->d3dContext->Unmap(staging.Get(), 0);

            m_frameCount.fetch_add(1, std::memory_order_relaxed);
            if (m_sink) m_sink->pushFrame(out);
        });

    m_impl->session = m_impl->framePool.CreateCaptureSession(m_impl->item);
    m_impl->session.IsCursorCaptureEnabled(false);
    try { m_impl->session.IsBorderRequired(false); } catch (...) {}
    m_impl->session.StartCapture();

    m_running.store(true);
    return true;
}

void WgcSource::stop()
{
    if (!m_running.exchange(false)) return;
    if (m_impl->session)   { m_impl->session.Close();   m_impl->session   = nullptr; }
    if (m_impl->framePool) {
        if (m_impl->frameArrivedToken) m_impl->framePool.FrameArrived(m_impl->frameArrivedToken);
        m_impl->framePool.Close();
        m_impl->framePool = nullptr;
    }
    m_impl->item = nullptr;
}

} // namespace Labs
