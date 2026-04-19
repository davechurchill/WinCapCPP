#pragma once

#include "WindowCapture.hpp"
#include "Timer.hpp"
#include "Logger.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>

using std::min;
using std::max;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

class WindowCapture_DXGI : public WindowCapture
{
    // Core D3D11 objects used for desktop duplication and CPU readback.
    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D>        m_stagingTexture;

    // Desktop duplication is created for one monitor/output at a time.
    // If the target window moves to another monitor, duplication is recreated.
    HMONITOR m_cachedMonitor  = nullptr;
    int      m_outputWidth    = 0;
    int      m_outputHeight   = 0;
    int      m_stagingWidth   = 0;
    int      m_stagingHeight  = 0;

    bool initDevice()
    {
        // Create a hardware D3D11 device/context used by DXGI duplication.
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &m_device, &featureLevel, &m_context
        );
        if (FAILED(hr)) { CapLog() << "DXGI: D3D11CreateDevice failed: " << hr << "\n"; }
        return SUCCEEDED(hr);
    }

    bool initDuplication(HMONITOR monitor)
    {
        // Clear old state first so failed re-inits do not keep stale objects.
        m_duplication.Reset();
        m_cachedMonitor = nullptr;

        // DuplicateOutput is exposed from an adapter's output, so walk:
        // D3D device -> DXGI adapter -> outputs and find the matching monitor.
        ComPtr<IDXGIDevice>  dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(m_device.As(&dxgiDevice)))         { return false; }
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) { return false; }

        for (UINT i = 0; ; ++i)
        {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) { break; }

            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            if (desc.Monitor != monitor) { continue; }

            // DuplicateOutput is available on IDXGIOutput1.
            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1))) { break; }

            HRESULT hr = output1->DuplicateOutput(m_device.Get(), &m_duplication);
            if (FAILED(hr)) { CapLog() << "DXGI: DuplicateOutput failed: " << hr << "\n"; return false; }

            // Cache output dimensions so capture rectangles can be clamped safely.
            DXGI_OUTDUPL_DESC dupDesc;
            m_duplication->GetDesc(&dupDesc);
            m_outputWidth   = (int)dupDesc.ModeDesc.Width;
            m_outputHeight  = (int)dupDesc.ModeDesc.Height;
            m_cachedMonitor = monitor;
            return true;
        }
        return false;
    }

    bool ensureStagingTexture(int width, int height)
    {
        if (m_stagingTexture && m_stagingWidth == width && m_stagingHeight == height)
            return true;

        // Staging textures are CPU-readable. We copy a GPU subregion into this
        // texture before mapping memory into a cv::Mat.
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
            CapLog() << "DXGI: CreateTexture2D (staging) failed\n";
            return false;
        }
        m_stagingWidth  = width;
        m_stagingHeight = height;
        return true;
    }

public:

    WindowCapture_DXGI()
    {
        m_captureMethod = CaptureMethod::DXGI;
        initDevice();
    }

    ~WindowCapture_DXGI() = default;

    cv::Mat capture(const WindowInfo& info) override
    {
        Timer t;

        if (!m_device) { return m_image; }

        // Recreate duplication when first used or when monitor target changes.
        if (!m_duplication || info.monitorHandle != m_cachedMonitor)
        {
            if (!initDuplication(info.monitorHandle)) { return m_image; }
        }

        ComPtr<IDXGIResource>   resource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        HRESULT hr = m_duplication->AcquireNextFrame(0, &frameInfo, &resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            // No new compositor frame yet, so return the previous image unchanged.
            m_previousCaptureTimeMS = t.elapsedMS();
            return m_image;
        }
        if (FAILED(hr))
        {
            // Duplication can be invalidated by display changes, secure desktop,
            // monitor reconnects, etc. Reset and reinitialize on next capture.
            CapLog() << "DXGI: AcquireNextFrame lost: " << hr << "\n";
            m_duplication.Reset();
            m_previousCaptureTimeMS = t.elapsedMS();
            return m_image;
        }

        // AcquireNextFrame returns a generic DXGI resource.
        // Convert it to the underlying D3D11 desktop texture.
        ComPtr<ID3D11Texture2D> desktopTexture;
        resource.As(&desktopTexture);

        // Convert the window client origin from screen space into monitor-local
        // coordinates required by the duplicated output texture.
        POINT clientOrigin = { 0, 0 };
        ClientToScreen(info.hwnd, &clientOrigin);
        int x = max(0, (int)clientOrigin.x - info.monitorX);
        int y = max(0, (int)clientOrigin.y - info.monitorY);

        // Clamp to monitor bounds in case the window is partially off-screen or
        // crossing monitor edges.
        int w = min(info.trueWidth,  m_outputWidth  - x);
        int h = min(info.trueHeight, m_outputHeight - y);

        if (w > 0 && h > 0 && ensureStagingTexture(w, h))
        {
            // Copy only the target window region into the staging texture.
            D3D11_BOX box = { (UINT)x, (UINT)y, 0, (UINT)(x + w), (UINT)(y + h), 1 };
            m_context->CopySubresourceRegion(
                m_stagingTexture.Get(), 0, 0, 0, 0,
                desktopTexture.Get(),  0, &box
            );

            // Every successful AcquireNextFrame must be paired with ReleaseFrame.
            m_duplication->ReleaseFrame();

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                m_image.create(h, w, CV_8UC4);
                const UINT rowBytes = (UINT)(w * 4);

                // Fast path when rows are tightly packed.
                if (mapped.RowPitch == rowBytes)
                {
                    std::memcpy(m_image.data, mapped.pData, (size_t)h * rowBytes);
                }
                else
                {
                    // Common driver behavior: row pitch includes padding, so copy
                    // each row explicitly into tightly packed OpenCV memory.
                    for (int row = 0; row < h; ++row)
                    {
                        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch;
                        std::memcpy(m_image.ptr(row), src, rowBytes);
                    }
                }
                m_context->Unmap(m_stagingTexture.Get(), 0);
            }
        }
        else
        {
            // Even if we skip processing, release the acquired frame.
            m_duplication->ReleaseFrame();
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
