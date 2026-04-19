#pragma once

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "Timer.hpp"
#include "Tools.hpp"
#include "WindowCapture_DXGI.hpp"
#include "WindowCapture_GDI.hpp"
#include "WindowCapture_WGC.hpp"
#include "WindowInfo.hpp"

class CaptureGUI
{
public:

    enum class Backend { DXGI, GDI, WGC };
    enum class ScreenMode { Menu, Capture };

private:

    WindowCapture_GDI  m_gdi;
    WindowCapture_DXGI m_dxgi;
    WindowCapture_WGC  m_wgc;

    Backend        m_backend = Backend::WGC;
    WindowCapture* m_wincap  = &m_wgc;

    std::vector<HWND> m_windows;
    int               m_selectedIndex = -1;
    WindowInfo        m_currentInfo {};
    bool              m_hasWindowSelection = false;
    ScreenMode        m_mode = ScreenMode::Menu;

    std::string m_windowName = "Window Capture";
    HWND        m_ownHwnd    = nullptr;
    double      m_displayScale = 0.5;

    Timer m_frameTimer;

    static const char* backendName(Backend backend)
    {
        switch (backend)
        {
        case Backend::WGC:  return "WGC";
        case Backend::DXGI: return "DXGI";
        case Backend::GDI:  return "GDI";
        }
        return "Unknown";
    }

    static const char* captureMethodName(WindowCapture::CaptureMethod method)
    {
        switch (method)
        {
        case WindowCapture::CaptureMethod::None:            return "None";
        case WindowCapture::CaptureMethod::GDI_PrintWindow: return "PrintWindow";
        case WindowCapture::CaptureMethod::GDI_BitBlt:      return "BitBlt";
        case WindowCapture::CaptureMethod::DXGI:            return "DXGI";
        case WindowCapture::CaptureMethod::WGC:             return "WGC";
        }
        return "Unknown";
    }

    static const char* printWindowOptionName(WindowCapture_GDI::PrintWindowOptions option)
    {
        switch (option)
        {
        case WindowCapture_GDI::OPT_PW_DEFAULT:           return "Default";
        case WindowCapture_GDI::OPT_PW_CLIENTONLY:        return "Client Only";
        case WindowCapture_GDI::OPT_PW_RENDERFULLCONTENT: return "Full Content";
        case WindowCapture_GDI::OPT_PW_NOTITLEBAR:        return "No Title Bar";
        }
        return "Unknown";
    }

    bool isWindowCandidate(HWND hwnd) const
    {
        if (!hwnd || hwnd == m_ownHwnd) { return false; }
        std::string title = GetWindowTitle(hwnd);
        if (title.empty()) { return false; }

        // Hard blacklist for known non-target system windows.
        std::string lowered = title;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowered == "program manager") { return false; }
        if (lowered.find("windows input experience") != std::string::npos) { return false; }

        RECT rect {};
        if (!GetClientRect(hwnd, &rect)) { return false; }
        if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) { return false; }
        return true;
    }

    void setBackend(Backend backend)
    {
        m_backend = backend;
        switch (m_backend)
        {
        case Backend::WGC:  m_wincap = &m_wgc;  break;
        case Backend::DXGI: m_wincap = &m_dxgi; break;
        case Backend::GDI:  m_wincap = &m_gdi;  break;
        }
    }

    bool selectWindowByIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_windows.size())) { return false; }

        WindowInfo info = WindowInfo::GetWindowInfo(m_windows[static_cast<size_t>(index)]);
        if (!info.hwnd)
        {
            m_hasWindowSelection = false;
            m_selectedIndex = -1;
            m_currentInfo = {};
            return false;
        }

        bool changedWindow = (!m_hasWindowSelection || info.hwnd != m_currentInfo.hwnd);
        m_currentInfo = info;
        m_hasWindowSelection = true;
        m_selectedIndex = index;

        if (changedWindow)
        {
            m_gdi.calculateCaptureMethod(m_currentInfo);
        }
        return true;
    }

    bool selectRelativeWindow(int delta)
    {
        if (m_windows.empty())
        {
            refreshWindowList();
        }
        if (m_windows.empty()) { return false; }

        if (!m_hasWindowSelection || m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_windows.size()))
        {
            return selectWindowByIndex(0);
        }

        int next = (m_selectedIndex + delta) % static_cast<int>(m_windows.size());
        if (next < 0) { next += static_cast<int>(m_windows.size()); }
        return selectWindowByIndex(next);
    }

    bool selectInitialWindow()
    {
        if (m_windows.empty())
        {
            refreshWindowList();
        }
        if (m_windows.empty())
        {
            m_hasWindowSelection = false;
            m_selectedIndex = -1;
            m_currentInfo = {};
            return false;
        }
        return selectWindowByIndex(0);
    }

    void refreshWindowList()
    {
        HWND selectedHwnd = m_hasWindowSelection ? m_currentInfo.hwnd : nullptr;

        std::vector<HWND> refreshed;
        for (HWND hwnd : GetWindowHandles())
        {
            if (!isWindowCandidate(hwnd)) { continue; }
            refreshed.push_back(hwnd);
        }
        m_windows = std::move(refreshed);

        m_selectedIndex = -1;
        if (!selectedHwnd)
        {
            m_hasWindowSelection = false;
            return;
        }

        for (size_t i = 0; i < m_windows.size(); ++i)
        {
            if (m_windows[i] != selectedHwnd) { continue; }
            m_selectedIndex = static_cast<int>(i);
            WindowInfo updated = WindowInfo::GetWindowInfo(selectedHwnd);
            if (!updated.hwnd)
            {
                m_hasWindowSelection = false;
                m_currentInfo = {};
                m_selectedIndex = -1;
            }
            else
            {
                m_hasWindowSelection = true;
                m_currentInfo = updated;
            }
            return;
        }

        m_hasWindowSelection = false;
        m_currentInfo = {};
    }

    void refreshSelectedWindowInfo()
    {
        if (!m_hasWindowSelection) { return; }

        WindowInfo updated = WindowInfo::GetWindowInfo(m_currentInfo.hwnd);
        if (!updated.hwnd)
        {
            m_hasWindowSelection = false;
            m_selectedIndex = -1;
            m_currentInfo = {};
            return;
        }
        m_currentInfo = updated;
    }

    static bool isLeftArrow(int key)
    {
        return key == 2424832 || key == 0x250000 || key == 81;
    }

    static bool isRightArrow(int key)
    {
        return key == 2555904 || key == 0x270000 || key == 83;
    }

    static int keyToMenuIndex(int key)
    {
        if (key >= 'a' && key <= 'z') { return key - 'a'; }
        if (key >= 'A' && key <= 'Z') { return key - 'A'; }
        return -1;
    }

    static std::string clampTitle(const std::string& title, size_t maxLen)
    {
        if (title.size() <= maxLen) { return title; }
        if (maxLen <= 3) { return title.substr(0, maxLen); }
        return title.substr(0, maxLen - 3) + "...";
    }

    static void onMouse(int event, int x, int y, int flags, void* userdata)
    {
        (void)x;
        (void)y;
        auto* self = static_cast<CaptureGUI*>(userdata);
        if (!self) { return; }
        self->handleMouseEvent(event, flags);
    }

    void handleMouseEvent(int event, int flags)
    {
        if (m_mode != ScreenMode::Capture) { return; }
        if (event != cv::EVENT_MOUSEWHEEL) { return; }

        int delta = cv::getMouseWheelDelta(flags);
        if (delta == 0) { return; }

        const int steps = (std::max)(1, std::abs(delta) / 120);
        const double stepScale = (delta > 0) ? 1.1 : (1.0 / 1.1);
        for (int i = 0; i < steps; ++i)
        {
            m_displayScale *= stepScale;
        }

        m_displayScale = std::clamp(m_displayScale, 0.1, 4.0);
    }

    void cycleGdiPrintWindowOption()
    {
        auto current = m_gdi.getPrintWindowOption();
        WindowCapture_GDI::PrintWindowOptions next = WindowCapture_GDI::OPT_PW_DEFAULT;

        switch (current)
        {
        case WindowCapture_GDI::OPT_PW_DEFAULT:           next = WindowCapture_GDI::OPT_PW_CLIENTONLY;        break;
        case WindowCapture_GDI::OPT_PW_CLIENTONLY:        next = WindowCapture_GDI::OPT_PW_RENDERFULLCONTENT; break;
        case WindowCapture_GDI::OPT_PW_RENDERFULLCONTENT: next = WindowCapture_GDI::OPT_PW_NOTITLEBAR;        break;
        case WindowCapture_GDI::OPT_PW_NOTITLEBAR:        next = WindowCapture_GDI::OPT_PW_DEFAULT;           break;
        }

        m_gdi.setPrintWindowOption(next);
    }

    void drawText(cv::Mat& img, const std::string& text, int& y, const cv::Scalar& color = cv::Scalar(255, 255, 255), double scale = 0.55, int thickness = 1)
    {
        cv::putText(img, text, cv::Point(12, y), cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0, 0, 0), thickness + 2, cv::LINE_AA);
        cv::putText(img, text, cv::Point(12, y), cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness, cv::LINE_AA);
        y += 22;
    }

    cv::Mat makeFallbackFrame() const
    {
        return cv::Mat(720, 1280, CV_8UC3, cv::Scalar(32, 32, 32));
    }

    void renderOverlay(cv::Mat& display, double frameMs, double captureMs)
    {
        (void)frameMs;
        int y = 24;
        drawText(display, "Backend: " + std::string(backendName(m_backend)), y);
        drawText(display, "Window: " + std::string(m_hasWindowSelection ? m_currentInfo.title : "<none>"), y);
        drawText(display, cv::format("Capture: %.1f ms", captureMs), y);

        if (m_backend == Backend::GDI)
        {
            drawText(display, "GDI Method: " + std::string(captureMethodName(m_gdi.getCaptureMethod())), y, cv::Scalar(160, 220, 255));
            drawText(display, "PW Option: " + std::string(printWindowOptionName(m_gdi.getPrintWindowOption())), y, cv::Scalar(160, 220, 255));
        }
        if (m_backend == Backend::WGC)
        {
            drawText(display, std::string("WGC Border: ") + (m_wgc.getBorderRequired() ? "On" : "Off"), y, cv::Scalar(160, 255, 180));
            drawText(display, std::string("WGC Cursor: ") + (m_wgc.getCursorEnabled() ? "On" : "Off"), y, cv::Scalar(160, 255, 180));
        }
    }

    cv::Mat makeMenuFrame() const
    {
        cv::Mat menu(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));

        int y = 42;
        auto drawMenuText = [&](const std::string& text, const cv::Scalar& color = cv::Scalar(255, 255, 255), double scale = 0.62, int thickness = 1)
        {
            cv::putText(menu, text, cv::Point(24, y), cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0, 0, 0), thickness + 2, cv::LINE_AA);
            cv::putText(menu, text, cv::Point(24, y), cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness, cv::LINE_AA);
            y += 26;
        };

        drawMenuText("Window Capture Menu (A-Z to select window)", cv::Scalar(180, 255, 180), 0.8, 2);
        drawMenuText("Current Backend: " + std::string(backendName(m_backend)), cv::Scalar(180, 220, 255), 0.62, 1);
        y += 8;
        drawMenuText("Controls:", cv::Scalar(255, 230, 160), 0.62, 1);
        drawMenuText("  a-z: open window    <-/->: previous/next window", cv::Scalar(255, 230, 160), 0.56, 1);
        drawMenuText("  1/2/3: backend      4: refresh list      0: quit", cv::Scalar(255, 230, 160), 0.56, 1);
        drawMenuText("  esc: return to menu (from capture)", cv::Scalar(255, 230, 160), 0.56, 1);
        drawMenuText("  mouse wheel in capture: zoom image", cv::Scalar(255, 230, 160), 0.56, 1);
        drawMenuText("  5: WGC border       6: WGC cursor", cv::Scalar(255, 230, 160), 0.56, 1);
        drawMenuText("  7: GDI auto mode    8: GDI method    9: GDI PW option", cv::Scalar(255, 230, 160), 0.56, 1);

        y += 8;
        drawMenuText("Windows:", cv::Scalar(180, 255, 180), 0.64, 1);

        const int maxShown = (std::min)(26, static_cast<int>(m_windows.size()));
        if (maxShown == 0)
        {
            drawMenuText("  <no windows available>", cv::Scalar(180, 180, 180), 0.56, 1);
            return menu;
        }

        for (int i = 0; i < maxShown; ++i)
        {
            const char hotkey = static_cast<char>('a' + i);
            std::string title = clampTitle(GetWindowTitle(m_windows[static_cast<size_t>(i)]), 86);
            if (title.empty()) { title = "<untitled>"; }
            drawMenuText("  [" + std::string(1, hotkey) + "] " + title, cv::Scalar(230, 230, 230), 0.56, 1);
            if (y > 700) { break; }
        }

        return menu;
    }

    void applyMenuKey(int key)
    {
        if (key < 0) { return; }
        if (key == 27)
        {
            m_shouldQuit = true;
            return;
        }

        int menuIndex = keyToMenuIndex(key);
        if (menuIndex >= 0)
        {
            const int maxShown = (std::min)(26, static_cast<int>(m_windows.size()));
            if (menuIndex >= maxShown) { return; }
            if (!selectWindowByIndex(menuIndex)) { return; }
            m_mode = ScreenMode::Capture;
            return;
        }

        switch (key)
        {
        case '4':
            refreshWindowList();
            return;
        case '1':
            setBackend(Backend::WGC);
            return;
        case '2':
            setBackend(Backend::DXGI);
            return;
        case '3':
            setBackend(Backend::GDI);
            return;
        case '0':
            m_shouldQuit = true;
            return;
        }
    }

    void applyCaptureKey(int key)
    {
        if (key < 0) { return; }

        if (isLeftArrow(key))  { selectRelativeWindow(-1); return; }
        if (isRightArrow(key)) { selectRelativeWindow(1);  return; }

        switch (key)
        {
        case 27: // ESC
            m_mode = ScreenMode::Menu;
            refreshWindowList();
            break;

        case '0':
            m_shouldQuit = true;
            break;

        case '4':
            refreshWindowList();
            if (!m_hasWindowSelection) { selectInitialWindow(); }
            break;

        case '1':
            setBackend(Backend::WGC);
            break;

        case '2':
            setBackend(Backend::DXGI);
            break;

        case '3':
            setBackend(Backend::GDI);
            break;

        case '5':
            m_wgc.setBorderRequired(!m_wgc.getBorderRequired());
            break;

        case '6':
            m_wgc.setCursorEnabled(!m_wgc.getCursorEnabled());
            break;

        case '7':
            if (m_hasWindowSelection)
            {
                m_gdi.calculateCaptureMethod(m_currentInfo);
            }
            break;

        case '8':
        {
            auto method = m_gdi.getCaptureMethod();
            if (method == WindowCapture::CaptureMethod::GDI_BitBlt)
            {
                m_gdi.setCaptureMethod(WindowCapture::CaptureMethod::GDI_PrintWindow);
            }
            else
            {
                m_gdi.setCaptureMethod(WindowCapture::CaptureMethod::GDI_BitBlt);
            }
            break;
        }

        case '9':
            cycleGdiPrintWindowOption();
            break;
        }
    }

    bool m_shouldQuit = false;

    bool isMainWindowClosed() const
    {
        // OpenCV can throw here after a user closes the window via title-bar X.
        // Treat any error as "window is closed" so we can exit gracefully.
        try
        {
            double visible = cv::getWindowProperty(m_windowName, cv::WND_PROP_VISIBLE);
            return visible < 1.0;
        }
        catch (const cv::Exception&)
        {
            return true;
        }
    }

public:

    CaptureGUI()
    {
        setBackend(Backend::WGC);
        refreshWindowList();
        m_mode = ScreenMode::Menu;
    }

    int run()
    {
        cv::namedWindow(m_windowName, cv::WINDOW_AUTOSIZE);
        cv::setMouseCallback(m_windowName, &CaptureGUI::onMouse, this);
        m_ownHwnd = FindWindowA(nullptr, m_windowName.c_str());

        refreshWindowList();

        while (!m_shouldQuit)
        {
            if (isMainWindowClosed())
            {
                m_shouldQuit = true;
                break;
            }

            if (m_mode == ScreenMode::Menu)
            {
                cv::Mat menu = makeMenuFrame();
                cv::imshow(m_windowName, menu);
                int key = cv::waitKeyEx(1);
                applyMenuKey(key);
                continue;
            }

            double frameMs = m_frameTimer.elapsedMS();
            m_frameTimer.start();

            refreshSelectedWindowInfo();
            if (!m_hasWindowSelection)
            {
                refreshWindowList();
                if (!m_hasWindowSelection)
                {
                    selectInitialWindow();
                }
            }

            cv::Mat captureBgr;
            if (m_hasWindowSelection && m_wincap)
            {
                const cv::Mat& frame = m_wincap->capture(m_currentInfo);
                if (!frame.empty())
                {
                    cv::cvtColor(frame, captureBgr, cv::COLOR_BGRA2BGR);
                }
            }

            if (captureBgr.empty())
            {
                captureBgr = makeFallbackFrame();
            }

            // Display at a wheel-controlled scale (defaults to half size).
            cv::Mat display;
            const int targetWidth  = (std::max)(1, static_cast<int>(std::round(captureBgr.cols * m_displayScale)));
            const int targetHeight = (std::max)(1, static_cast<int>(std::round(captureBgr.rows * m_displayScale)));
            cv::resize(captureBgr, display, cv::Size(targetWidth, targetHeight), 0.0, 0.0, cv::INTER_AREA);

            double captureMs = (m_wincap ? m_wincap->getPreviousCaptureTime() : 0.0);
            // Keep text size constant regardless of capture size by drawing overlay after scaling.
            renderOverlay(display, frameMs, captureMs);

            cv::imshow(m_windowName, display);
            int key = cv::waitKeyEx(1);
            applyCaptureKey(key);
        }

        try
        {
            cv::destroyWindow(m_windowName);
        }
        catch (const cv::Exception&)
        {
            // Window can already be gone if user closed it manually.
        }
        return 0;
    }
};
