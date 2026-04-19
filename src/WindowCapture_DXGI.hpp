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
    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D>        m_stagingTexture;

    HMONITOR m_cachedMonitor  = nullptr;
    int      m_outputWidth    = 0;
    int      m_outputHeight   = 0;
    int      m_stagingWidth   = 0;
    int      m_stagingHeight  = 0;

    bool initDevice()
    {
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
        m_duplication.Reset();
        m_cachedMonitor = nullptr;

        ComPtr<IDXGIDevice>  dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(m_device.As(&dxgiDevice)))       { return false; }
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) { return false; }

        for (UINT i = 0; ; ++i)
        {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) { break; }

            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            if (desc.Monitor != monitor) { continue; }

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1))) { break; }

            HRESULT hr = output1->DuplicateOutput(m_device.Get(), &m_duplication);
            if (FAILED(hr)) { CapLog() << "DXGI: DuplicateOutput failed: " << hr << "\n"; return false; }

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

        if (!m_duplication || info.monitorHandle != m_cachedMonitor)
        {
            if (!initDuplication(info.monitorHandle)) { return m_image; }
        }

        ComPtr<IDXGIResource>    resource;
        DXGI_OUTDUPL_FRAME_INFO  frameInfo;
        HRESULT hr = m_duplication->AcquireNextFrame(0, &frameInfo, &resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            // no new frame from compositor — return last image unchanged
            m_previousCaptureTimeMS = t.elapsedMS();
            return m_image;
        }
        if (FAILED(hr))
        {
            // lost access (monitor change, UAC prompt, etc.) — reinit next frame
            CapLog() << "DXGI: AcquireNextFrame lost: " << hr << "\n";
            m_duplication.Reset();
            m_previousCaptureTimeMS = t.elapsedMS();
            return m_image;
        }

        ComPtr<ID3D11Texture2D> desktopTexture;
        resource.As(&desktopTexture);

        // client area in screen coords → monitor-local coords for the DXGI output
        POINT clientOrigin = { 0, 0 };
        ClientToScreen(info.hwnd, &clientOrigin);
        int x = max(0, (int)clientOrigin.x - info.monitorX);
        int y = max(0, (int)clientOrigin.y - info.monitorY);
        int w = min(info.trueWidth,  m_outputWidth  - x);
        int h = min(info.trueHeight, m_outputHeight - y);

        if (w > 0 && h > 0 && ensureStagingTexture(w, h))
        {
            D3D11_BOX box = { (UINT)x, (UINT)y, 0, (UINT)(x + w), (UINT)(y + h), 1 };
            m_context->CopySubresourceRegion(
                m_stagingTexture.Get(), 0, 0, 0, 0,
                desktopTexture.Get(),  0, &box
            );

            m_duplication->ReleaseFrame();

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                m_image.create(h, w, CV_8UC4);
                const UINT rowBytes = (UINT)(w * 4);
                if (mapped.RowPitch == rowBytes)
                {
                    std::memcpy(m_image.data, mapped.pData, (size_t)h * rowBytes);
                }
                else
                {
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
