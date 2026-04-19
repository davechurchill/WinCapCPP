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
        OPT_PW_DEFAULT           = 0x00, // entire window + default title bar style
        OPT_PW_CLIENTONLY        = 0x01, // only the client area
        OPT_PW_RENDERFULLCONTENT = 0x02, // window and title bar (if supported)
        OPT_PW_NOTITLEBAR        = 0x03  // window content only
    };

private:

    WindowInfo          m_info;
    PrintWindowOptions  m_printWindowOption = OPT_PW_NOTITLEBAR;
    HDC         m_windowDC              = nullptr;
    HDC         m_compatibleDC          = nullptr;
    HWND        m_cachedWindowHandle    = nullptr;
    HBITMAP     m_bitmap                = nullptr;
    void*       m_bits                  = nullptr;
    int         m_width                 = 0;
    int         m_height                = 0;

    cv::Mat m_image;

    void updateCompatibleDC()
    {
        // If the compatible DC has not been created yet, or the window changed,
        // create (or recreate) the compatible DC.
        if (m_compatibleDC == nullptr || m_cachedWindowHandle != m_info.hwnd)
        {
            if (m_compatibleDC != nullptr) { DeleteDC(m_compatibleDC); }
            m_compatibleDC = CreateCompatibleDC(m_windowDC);
            m_cachedWindowHandle = m_info.hwnd;
        }
    }

    void updateBitmap()
    {
        // If the bitmap is not created or its dimensions differ, (re)create it.
        if (m_bitmap == nullptr || m_info.trueWidth != m_width || m_info.trueHeight != m_height)
        {
            // Free any existing bitmap if the size has changed.
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
            bmi.bmiHeader.biHeight = -m_info.trueHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            m_bitmap = CreateDIBSection(m_windowDC, &bmi, DIB_RGB_COLORS, &m_bits, NULL, 0);
            if (!m_bitmap)
            {
                // Optionally log or handle the error.
                ReleaseDC(m_info.hwnd, m_windowDC);
                return;
            }
        }

        // always (re)select the bitmap into the compatible DC.
        SelectObject(m_compatibleDC, m_bitmap);
    }

    bool captureFullScreen()
    {
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
            if (!BitBlt(m_compatibleDC, 0, 0, m_info.trueWidth, m_info.trueHeight, m_windowDC, 0, 0, SRCCOPY))
            {
                CapLog() << "GDI BitBlt WindowDC failed with error: " << GetLastError() << "\n";
                return false;
            }
        }
        else if (m_captureMethod == CaptureMethod::GDI_PrintWindow)
        {
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
        if (m_compatibleDC) { DeleteDC(m_compatibleDC);  }
        if (m_bitmap)       { DeleteObject(m_bitmap);    }
    }

    cv::Mat capture(const WindowInfo& info)
    {
        Timer t;

        m_info = info;
        m_image = cv::Mat();

        // only acquire the window DC for methods that actually use it
        if (m_captureMethod == CaptureMethod::GDI_BitBlt)
        {
            m_windowDC = GetDC(m_info.hwnd);
            if (!m_windowDC) { return m_image; }
        }

        // update our cached compatible DC if the window handle has changed.
        updateCompatibleDC();

        // update or create the bitmap if the size has changed.
        updateBitmap();

        // choose the appropriate method if 'true' fullscreen or not
        if (m_info.isTrueFullScreen) { captureFullScreen(); }
        else                         { captureWindowed();   }

        // wrap the DIB section bits with a cv::Mat header.
        // CAUTION: This header directly wraps the internal bitmap memory,
        // so the contents will be overwritten on subsequent captures.
        m_image = cv::Mat(m_info.trueHeight, m_info.trueWidth, CV_8UC4, m_bits);

        if (m_windowDC)
        {
            ReleaseDC(m_info.hwnd, m_windowDC);
            m_windowDC = nullptr;
        }

        // keep track of stats
        double elapsed = t.elapsedMS();
        m_previousCaptureTimeMS = elapsed;
        m_totalCaptures++;
        m_totalCaptureTimeMS += elapsed;
        if (elapsed > m_maxCaptureTimeMS) { m_maxCaptureTimeMS = elapsed; }
        if (elapsed < m_minCaptureTimeMS) { m_minCaptureTimeMS = elapsed; }

        return m_image;
    }

    // calculates which window capture method should be used
    // should only be called when window properties change, since it's a bit slow
    //
    // 1. captures screen with both bitblt (may work) and pw (always works)
    // 2. compares the two images to see if they are about the same
    // 3. if they are the same, bitblt will be used (faster)
    // 4. if they are different, only pw works so use it (slower)
    void calculateCaptureMethod(const WindowInfo& info)
    {
        // capture an image with PrintWindow (slower but pretty much always works)
        setCaptureMethod(CaptureMethod::GDI_PrintWindow);
        cv::Mat capturePrintWindow = capture(info).clone();

        // capture an image with bit BitBlt (faster but not always guaranteed to work)
        setCaptureMethod(CaptureMethod::GDI_BitBlt);
        cv::Mat captureBitBlt = capture(info).clone();

        // calculate a difference between the two images
        cv::Mat diff;
        cv::absdiff(captureBitBlt, capturePrintWindow, diff);
        diff.convertTo(diff, CV_32F);
        diff = diff.mul(diff);
        double mse = (captureBitBlt.total() == 0) ? 10000 : (cv::sum(diff)[0] / captureBitBlt.total());

        // if the difference is more than a given threshold, then BitBlt must have failed so use PrintWindow
        if (mse > 500)
        {
            setCaptureMethod(CaptureMethod::GDI_PrintWindow);
        }
        // otherwise BitBlt is fine to use
        else
        {
            setCaptureMethod(CaptureMethod::GDI_BitBlt);
        }
    }
};
