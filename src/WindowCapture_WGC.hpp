#pragma once

#include "WindowCapture.hpp"
#include "Timer.hpp"
#include "Logger.hpp"

#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#pragma comment(lib, "runtimeobject.lib")

// IDirect3DDxgiInterfaceAccess is not reliably exposed by the interop header
// in all SDK configurations. We provide the definition when missing so we can
// convert WinRT capture surfaces back into native D3D11 textures.
#ifndef __IDirect3DDxgiInterfaceAccess_INTERFACE_DEFINED__
#define __IDirect3DDxgiInterfaceAccess_INTERFACE_DEFINED__
// Official COM IID for IDirect3DDxgiInterfaceAccess (Windows SDK); used by QueryInterface/.as<>.
MIDL_INTERFACE("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")
IDirect3DDxgiInterfaceAccess : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** p) = 0;
};
#endif

using std::min;
using std::max;
using Microsoft::WRL::ComPtr;

namespace wgc  = winrt::Windows::Graphics::Capture;
namespace wgdd = winrt::Windows::Graphics::DirectX;
namespace wgd3 = winrt::Windows::Graphics::DirectX::Direct3D11;

class WindowCapture_WGC : public WindowCapture
{
    // Native D3D11 objects used for GPU work and CPU readback.
    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D>     m_stagingTexture;
    int m_stagingWidth  = 0;
    int m_stagingHeight = 0;

    // WinRT capture objects required by Windows.Graphics.Capture.
    // m_rtDevice wraps the native DXGI device for WinRT APIs.
    wgd3::IDirect3DDevice           m_rtDevice  { nullptr };
    wgc::GraphicsCaptureItem        m_item      { nullptr };
    wgc::Direct3D11CaptureFramePool m_framePool { nullptr };
    wgc::GraphicsCaptureSession     m_session   { nullptr };

    HWND m_capturedHwnd = nullptr;
    int  m_poolWidth    = 0;
    int  m_poolHeight   = 0;

    bool m_borderRequired = false;
    bool m_cursorEnabled  = false;

    bool initDevice()
    {
        // WGC delivers BGRA data, so the D3D device must support BGRA surfaces.
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION,
            &m_device, nullptr, &m_context
        );
        if (FAILED(hr)) { return false; }

        // WGC is a WinRT API, so we bridge native DXGI -> WinRT IDirect3DDevice.
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(m_device.As(&dxgiDevice))) { return false; }

        ComPtr<IInspectable> inspectable;
        hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &inspectable);
        if (FAILED(hr)) { return false; }

        winrt::com_ptr<::IInspectable> rtInspectable;
        rtInspectable.copy_from(inspectable.Get());
        m_rtDevice = rtInspectable.as<wgd3::IDirect3DDevice>();
        return m_rtDevice != nullptr;
    }

    bool initCapture(HWND hwnd)
    {
        // Start from a clean state whenever the target window changes.
        stopCapture();
        if (!m_device || !m_rtDevice) { return false; }

        try
        {
            // GraphicsCaptureItem has no plain HWND constructor.
            // IGraphicsCaptureItemInterop::CreateForWindow is the COM bridge that
            // turns a Win32 HWND into a WinRT capture item.
            auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(m_item));
        }
        catch (...)
        {
            CapLog() << "WGC: CreateForWindow failed\n";
            return false;
        }

        if (!m_item) { return false; }

        auto size    = m_item.Size();
        m_poolWidth  = size.Width;
        m_poolHeight = size.Height;

        // CreateFreeThreaded avoids requiring a dispatcher thread; we poll frames
        // from capture() instead of subscribing to frame-arrived callbacks.
        // Buffer count 2 is a simple double-buffered setup.
        m_framePool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_rtDevice,
            wgdd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size
        );
        m_session = m_framePool.CreateCaptureSession(m_item);

        // These toggles can fail on older Windows versions or restricted targets.
        // We treat them as best-effort options and keep capture running.
        try { m_session.IsBorderRequired(m_borderRequired);    } catch (...) {}
        try { m_session.IsCursorCaptureEnabled(m_cursorEnabled); } catch (...) {}
        m_session.StartCapture();

        m_capturedHwnd = hwnd;
        return true;
    }

    void stopCapture()
    {
        // Close in top-down ownership order.
        // Session depends on frame pool, and both depend on item.
        if (m_session)   { m_session.Close();   m_session   = nullptr; }
        if (m_framePool) { m_framePool.Close();  m_framePool = nullptr; }
        m_item         = nullptr;
        m_capturedHwnd = nullptr;
    }

    bool ensureStagingTexture(int width, int height)
    {
        if (m_stagingTexture && m_stagingWidth == width && m_stagingHeight == height)
            return true;

        // Staging texture is CPU-readable memory. We copy GPU capture frames into
        // it before mapping so OpenCV can access raw pixels.
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width          = (UINT)width;
        desc.Height         = (UINT)height;
        desc.MipLevels      = 1;
        desc.ArraySize      = 1;
        desc.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc     = { 1, 0 };
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        m_stagingTexture.Reset();
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_stagingTexture)))
        {
            CapLog() << "WGC: CreateTexture2D (staging) failed\n";
            return false;
        }
        m_stagingWidth  = width;
        m_stagingHeight = height;
        return true;
    }

public:

    WindowCapture_WGC()
    {
        m_captureMethod = CaptureMethod::WGC;
        // WGC and C++/WinRT calls require a COM apartment.
        // Multi-threaded apartment is suitable for this polling/capture path.
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}
        initDevice();
    }

    ~WindowCapture_WGC()
    {
        stopCapture();
    }

    bool getBorderRequired() const { return m_borderRequired; }
    bool getCursorEnabled()  const { return m_cursorEnabled;  }

    void setBorderRequired(bool v)
    {
        m_borderRequired = v;
        if (m_session) { try { m_session.IsBorderRequired(v);      } catch (...) {} }
    }

    void setCursorEnabled(bool v)
    {
        m_cursorEnabled = v;
        if (m_session) { try { m_session.IsCursorCaptureEnabled(v); } catch (...) {} }
    }

    cv::Mat capture(const WindowInfo& info) override
    {
        Timer t;

        if (!m_device || !m_rtDevice) { return m_image; }

        // Recreate capture session when switching to a different target window.
        if (info.hwnd != m_capturedHwnd)
        {
            if (!initCapture(info.hwnd)) { return m_image; }
        }

        // Non-blocking fetch. If no frame is ready yet, return the previous image.
        auto frame = m_framePool.TryGetNextFrame();
        if (!frame)
        {
            m_previousCaptureTimeMS = t.elapsedMS();
            return m_image;
        }

        auto contentSize = frame.ContentSize();
        int w = contentSize.Width;
        int h = contentSize.Height;

        // Frame pool size is fixed at creation time. If the captured content size
        // changes (resize/DPI changes), recreate capture objects with new size.
        if (w != m_poolWidth || h != m_poolHeight)
        {
            frame.Close();
            initCapture(info.hwnd);
            m_previousCaptureTimeMS = t.elapsedMS();
            return m_image;
        }

        // WGC gives a WinRT IDirect3DSurface; convert it back to a native
        // ID3D11Texture2D so we can issue D3D copy/map calls.
        auto dxgiAccess = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> frameTex;
        if (FAILED(dxgiAccess->GetInterface(IID_PPV_ARGS(&frameTex))))
        {
            return m_image;
        }

        if (ensureStagingTexture(w, h))
        {
            // Copy full frame from GPU texture into CPU-readable staging texture.
            D3D11_BOX box = { 0, 0, 0, (UINT)w, (UINT)h, 1 };
            m_context->CopySubresourceRegion(
                m_stagingTexture.Get(), 0, 0, 0, 0,
                frameTex.Get(), 0, &box
            );

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                m_image.create(h, w, CV_8UC4);
                const UINT rowBytes = (UINT)(w * 4);

                // Fast path when texture rows are tightly packed.
                if (mapped.RowPitch == rowBytes)
                {
                    std::memcpy(m_image.data, mapped.pData, (size_t)h * rowBytes);
                }
                else
                {
                    // Common path on many drivers: each row may include padding.
                    // Copy row-by-row to strip GPU pitch padding into contiguous cv::Mat rows.
                    for (int row = 0; row < h; ++row)
                    {
                        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch;
                        std::memcpy(m_image.ptr(row), src, rowBytes);
                    }
                }
                m_context->Unmap(m_stagingTexture.Get(), 0);
            }
        }

        double elapsed = t.elapsedMS();
        m_previousCaptureTimeMS = elapsed;
        m_totalCaptures++;
        m_totalCaptureTimeMS += elapsed;
        if (elapsed > m_maxCaptureTimeMS) { m_maxCaptureTimeMS = elapsed; }
        if (elapsed < m_minCaptureTimeMS) { m_minCaptureTimeMS = elapsed; }

        return m_image;
    }
};
