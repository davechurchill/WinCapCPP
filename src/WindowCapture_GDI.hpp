#pragma once

#include "WindowCapture.hpp"
#include "Timer.hpp"

#include <Windows.h>

#include <windows.h>
#include <opencv2/opencv.hpp>
#include "Logger.hpp"

class WindowCapture_GDI : public WindowCapture
{
public:

    enum PrintWindowOptions
    {
        OPT_PW_DEFAULT           = 0x00, // Entire window with default behavior.
        OPT_PW_CLIENTONLY        = 0x01, // Client area only.
        OPT_PW_RENDERFULLCONTENT = 0x02, // Full content when supported by target app.
        OPT_PW_NOTITLEBAR        = 0x03  // Content without title bar decoration.
    };

private:

    WindowInfo         m_info;
    PrintWindowOptions m_printWindowOption = OPT_PW_NOTITLEBAR;

    // GDI resource cache reused across frames for performance.
    HDC    m_windowDC           = nullptr;
    HDC    m_compatibleDC       = nullptr;
    HWND   m_cachedWindowHandle = nullptr;
    HBITMAP m_bitmap            = nullptr;
    void*   m_bits              = nullptr;
    int     m_width             = 0;
    int     m_height            = 0;

    cv::Mat m_image;

    void updateCompatibleDC()
    {
        // Memory DCs are created with CreateCompatibleDC and selected with a bitmap.
        // Recreate if this is first use or if we changed target window.
        if (m_compatibleDC == nullptr || m_cachedWindowHandle != m_info.hwnd)
        {
            if (m_compatibleDC != nullptr) { DeleteDC(m_compatibleDC); }

            // For BitBlt, m_windowDC is a real window DC.
            // For PrintWindow, m_windowDC can be null and CreateCompatibleDC still
            // creates a valid memory DC based on the current screen.
            m_compatibleDC = CreateCompatibleDC(m_windowDC);
            m_cachedWindowHandle = m_info.hwnd;
        }
    }

    void updateBitmap()
    {
        // Allocate/reallocate the DIB when dimensions change.
        if (m_bitmap == nullptr || m_info.trueWidth != m_width || m_info.trueHeight != m_height)
        {
            if (m_bitmap != nullptr)
            {
                DeleteObject(m_bitmap);
                m_bitmap = nullptr;
            }

            m_width = m_info.trueWidth;
            m_height = m_info.trueHeight;

            BITMAPINFO bmi;
            ZeroMemory(&bmi, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = m_info.trueWidth;
            // Negative height creates a top-down DIB so row 0 is the top scanline.
            // That matches OpenCV's default expectation and avoids vertical flipping.
            bmi.bmiHeader.biHeight = -m_info.trueHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            // CreateDIBSection gives us both a bitmap handle and a direct pointer
            // (m_bits) to pixel memory for fast cv::Mat wrapping.
            m_bitmap = CreateDIBSection(m_windowDC, &bmi, DIB_RGB_COLORS, &m_bits, NULL, 0);
            if (!m_bitmap)
            {
                ReleaseDC(m_info.hwnd, m_windowDC);
                return;
            }
        }

        // The target bitmap must be selected into the memory DC before drawing.
        SelectObject(m_compatibleDC, m_bitmap);
    }

    bool captureFullScreen()
    {
        // Some "true fullscreen" scenarios are more reliable when copied from the
        // desktop surface rather than from the window DC.
        HWND desktopHandle = GetDesktopWindow();
        HDC desktopDC = GetDC(desktopHandle);
        if (!BitBlt(m_compatibleDC, 0, 0, m_info.trueWidth, m_info.trueHeight, desktopDC, 0, 0, SRCCOPY))
        {
            CapLog() << "BitBlt DesktopDC failed with error: " << GetLastError() << "\n";
            return false;
        }
        ReleaseDC(desktopHandle, desktopDC);
        return true;
    }

    bool captureWindowed()
    {
        if (m_captureMethod == CaptureMethod::GDI_BitBlt)
        {
            // Fast path: copy from the target window DC into our memory DC.
            if (!BitBlt(m_compatibleDC, 0, 0, m_info.trueWidth, m_info.trueHeight, m_windowDC, 0, 0, SRCCOPY))
            {
                CapLog() << "GDI BitBlt WindowDC failed with error: " << GetLastError() << "\n";
                return false;
            }
        }
        else if (m_captureMethod == CaptureMethod::GDI_PrintWindow)
        {
            // Slower but often more reliable for certain protected/composited windows.
            if (!PrintWindow(m_info.hwnd, m_compatibleDC, m_printWindowOption))
            {
                CapLog() << "GDI PrintWindow WindowDC failed with error: " << GetLastError() << "\n";
                return false;
            }
        }
        return true;
    }

public:

    WindowCapture_GDI()
    {
        m_captureMethod = CaptureMethod::GDI_BitBlt;
    }

    void setPrintWindowOption(PrintWindowOptions option)
    {
        m_printWindowOption = option;
    }

    PrintWindowOptions getPrintWindowOption() const
    {
        return m_printWindowOption;
    }

    ~WindowCapture_GDI()
    {
        if (m_compatibleDC) { DeleteDC(m_compatibleDC); }
        if (m_bitmap)       { DeleteObject(m_bitmap);   }
    }

    cv::Mat capture(const WindowInfo& info)
    {
        Timer t;

        m_info = info;
        m_image = cv::Mat();

        // BitBlt needs the target window DC. PrintWindow does not.
        if (m_captureMethod == CaptureMethod::GDI_BitBlt)
        {
            m_windowDC = GetDC(m_info.hwnd);
            if (!m_windowDC) { return m_image; }
        }

        // Ensure our reusable GDI buffers match the current target and size.
        updateCompatibleDC();
        updateBitmap();

        // For true fullscreen targets, desktop copy is often the reliable path.
        // Otherwise use the selected windowed capture method.
        if (m_info.isTrueFullScreen) { captureFullScreen(); }
        else                         { captureWindowed();   }

        // Wrap the DIB memory directly (no copy). This makes capture fast, but
        // the data is overwritten on the next capture call.
        m_image = cv::Mat(m_info.trueHeight, m_info.trueWidth, CV_8UC4, m_bits);

        if (m_windowDC)
        {
            ReleaseDC(m_info.hwnd, m_windowDC);
            m_windowDC = nullptr;
        }

        // Update timing statistics used by the on-screen overlay.
        double elapsed = t.elapsedMS();
        m_previousCaptureTimeMS = elapsed;
        m_totalCaptures++;
        m_totalCaptureTimeMS += elapsed;
        if (elapsed > m_maxCaptureTimeMS) { m_maxCaptureTimeMS = elapsed; }
        if (elapsed < m_minCaptureTimeMS) { m_minCaptureTimeMS = elapsed; }

        return m_image;
    }

    // Auto-select the better GDI method for this window by comparing results:
    // 1) capture with PrintWindow
    // 2) capture with BitBlt
    // 3) compute mean-squared error (MSE) between images
    // 4) choose PrintWindow if mismatch is large, else keep BitBlt
    void calculateCaptureMethod(const WindowInfo& info)
    {
        // PrintWindow is generally the "reliability baseline" image.
        setCaptureMethod(CaptureMethod::GDI_PrintWindow);
        cv::Mat capturePrintWindow = capture(info).clone();

        // BitBlt is usually faster but can fail for some windows/compositors.
        setCaptureMethod(CaptureMethod::GDI_BitBlt);
        cv::Mat captureBitBlt = capture(info).clone();

        cv::Mat diff;
        cv::absdiff(captureBitBlt, capturePrintWindow, diff);
        diff.convertTo(diff, CV_32F);
        diff = diff.mul(diff);
        double mse = (captureBitBlt.total() == 0) ? 10000 : (cv::sum(diff)[0] / captureBitBlt.total());

        if (mse > 500)
        {
            setCaptureMethod(CaptureMethod::GDI_PrintWindow);
        }
        else
        {
            setCaptureMethod(CaptureMethod::GDI_BitBlt);
        }
    }
};
