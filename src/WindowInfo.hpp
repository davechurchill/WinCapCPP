#pragma once

#include <windows.h>
#include <opencv2/opencv.hpp>
#include <sstream>

struct WindowInfo
{
    HWND hwnd = nullptr;
    RECT windowRect {};
    RECT clientRect {};
    std::string title;
    int exists           = 0; // does window actually exist
    int windowX          = 0; // GetWindowRect position (includes title bar)
    int windowY          = 0;
    int windowWidth      = 0; // GetWindowRect size (includes title bar)
    int windowHeight     = 0; 
    int clientX          = 0; // GetClientRect position (without titlebar)
    int clientY          = 0;
    int clientWidth      = 0; // GetClientRect size (without title bar)
    int clientHeight     = 0;
    UINT dpi             = 0; // dpi for window (default 96 for Windows)
    int scale            = 0; // windows scaling (calculated from dpi)
    int trueWidth        = 0; // Actual size accounting for scaling
    int trueHeight       = 0;
    int isFullScreen     = 0; // is window same size as its monitor
    int isTrueFullScreen = 0; // is windows in 'true' full screen mode (quns=3)
    int isMinimized      = 0; // is fully minimized (won't be rendered)
    int isFocused        = 0; // is currently focused
    int isLayered        = 0; // possible win11 weirdness if true
    int isNoRedir        = 0; // possible win11 weirdness if true
    int quns             = 0; // value 3 means 'true' full screen is active
    int monitorIndex     = 0; // monitor that contains window
    int monitorX         = 0; // position of top-left pixel of monitor
    int monitorY         = 0;
    int monitorWidth     = 0; // size of monitor
    int monitorHeight    = 0;
    HMONITOR monitorHandle = nullptr;
    MONITORINFO monitorInfo {};
    WCHAR monitorName[32] {};
    
    const std::string str() const
    {
        std::stringstream ss;
        ss << "Window Exists:  " << (exists ? "Yes" : "No") << std::endl;
        ss << "Window Title:   " << title << std::endl;
        ss << "Window HWND:    " << hwnd << std::endl;
        ss << "Window Pos:     (" << windowX << ", " << windowY << ")" << std::endl;
        ss << "Window Size:    (" << windowWidth << ", " << windowHeight << ")" << std::endl;
        ss << "Window DPI:     " << dpi << std::endl;
        ss << "Window Scale:   " << scale << std::endl;
        ss << "Client Pos:     (" << clientX << ", " << clientY << ")" << std::endl;
        ss << "ClientRect:     (" << clientWidth << ", " << clientHeight << ")" << std::endl;
        ss << "True Size:      (" << trueWidth << ", " << trueHeight << ")" << std::endl;
        ss << "Is Fullscreen:  " << (isFullScreen ? "Yes" : "No") << std::endl;
        ss << "Is True FS:     " << (isTrueFullScreen ? "Yes" : "No") << std::endl;
        ss << "Is Minimized:   " << (isMinimized ? "Yes" : "No") << std::endl;
        ss << "Is Layered:     " << isLayered << std::endl;
        ss << "Is NoRedir:     " << isNoRedir << std::endl;
        ss << "Has Focus:      " << (isFocused ? "Yes" : "No") << std::endl;
        ss << "QUNS:           " << quns << std::endl;
        ss << "Monitor HWND:   " << monitorHandle << std::endl;
        ss << "Monitor Index:  " << monitorIndex << std::endl;
        ss << "Monitor Pos:    (" << monitorX << ", " << monitorY << ")" << std::endl;
        ss << "Monitor Size:   (" << monitorWidth << ", " << monitorHeight << ")";
        return ss.str();
    }

    bool operator == (const WindowInfo& other) const
    {
        return str() == other.str();
    }

    static inline std::string GetWindowTitle(HWND hwnd)
    {
        if (!hwnd) { return ""; }
        int length = GetWindowTextLengthA(hwnd);
        if (length == 0) { return ""; }
        std::string title(static_cast<size_t>(length) + 1, '\0');
        int copied = GetWindowTextA(hwnd, title.data(), length + 1);
        if (copied <= 0) { return ""; }
        title.resize(static_cast<size_t>(copied));
        return title;
    }

    static inline bool IsWindowFullscreen(HWND hwnd)
    {
        if (!hwnd) { return false; }

        RECT windowRect, monitorRect;
        if (GetWindowRect(hwnd, &windowRect))
        {
            HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfo(hMonitor, &mi))
            {
                monitorRect = mi.rcMonitor;
                return (windowRect.left == monitorRect.left && windowRect.top == monitorRect.top &&
                    windowRect.right == monitorRect.right && windowRect.bottom == monitorRect.bottom);
            }
        }
        return false;
    }

    static inline WindowInfo GetWindowInfo(HWND hwnd)
    {
        WindowInfo info = {};

        if (!GetWindowRect(hwnd, &info.windowRect)) { return WindowInfo(); }
        if (!GetClientRect(hwnd, &info.clientRect)) { return WindowInfo(); }

        info.hwnd           = hwnd;
        info.exists         = true;
        info.title          = GetWindowTitle(hwnd);
        info.windowX        = info.windowRect.left;
        info.windowY        = info.windowRect.top;
        info.windowWidth    = info.windowRect.right - info.windowRect.left;
        info.windowHeight   = info.windowRect.bottom - info.windowRect.top;
        info.clientX        = info.clientRect.left;
        info.clientY        = info.clientRect.top;
        info.clientWidth    = info.clientRect.right - info.clientRect.left;
        info.clientHeight   = info.clientRect.bottom - info.clientRect.top;
        info.dpi            = GetDpiForWindow(hwnd);
        if (info.dpi == 0) { info.dpi = 96; }
        info.scale          = MulDiv(static_cast<int>(info.dpi), 100, 96);
        info.trueWidth      = MulDiv(info.clientWidth, static_cast<int>(info.dpi), 96);
        info.trueHeight     = MulDiv(info.clientHeight, static_cast<int>(info.dpi), 96);
        info.isFullScreen   = IsWindowFullscreen(hwnd);
        info.isMinimized    = IsIconic(hwnd);
        info.isFocused      = GetForegroundWindow() == hwnd;

        QUERY_USER_NOTIFICATION_STATE pquns = {};
        HRESULT qunsResult = SHQueryUserNotificationState(&pquns);
        if (SUCCEEDED(qunsResult))
        {
            info.quns = static_cast<int>(pquns);
            // QUNS is process/session-global, so only treat it as "true fullscreen"
            // for this WindowInfo when the queried window is also focused.
            info.isTrueFullScreen = (info.isFocused && pquns == QUNS_RUNNING_D3D_FULL_SCREEN);
        }
        else
        {
            info.quns = 0;
            info.isTrueFullScreen = 0;
        }

        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        info.isLayered = (exStyle & WS_EX_LAYERED) != 0;
        info.isNoRedir = (exStyle & WS_EX_NOREDIRECTIONBITMAP) != 0;

        info.monitorHandle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };

        // get basic info about the monitor
        if (!GetMonitorInfo(info.monitorHandle, &mi)) { std::cerr << "Failed to get monitor info" << std::endl; return info; }
        info.monitorInfo = mi;
        info.monitorX = mi.rcMonitor.left;
        info.monitorY = mi.rcMonitor.top;
        info.monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        info.monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

        MONITORINFOEX monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFOEX);

        // get specific info about the monitor
        if (!GetMonitorInfo(info.monitorHandle, &monitorInfo)) { std::cerr << "Failed to get monitorex info." << std::endl; return info; }
        wcsncpy_s(info.monitorName, monitorInfo.szDevice, _TRUNCATE);

        DISPLAY_DEVICE displayDevice = {};
        displayDevice.cb = sizeof(DISPLAY_DEVICE);
        for (DWORD deviceIndex = 0; EnumDisplayDevices(NULL, deviceIndex, &displayDevice, 0); ++deviceIndex)
        {
            if (!(displayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) && wcscmp(displayDevice.DeviceName, monitorInfo.szDevice) == 0)
            {
                DEVMODE devMode = {};
                devMode.dmSize = sizeof(DEVMODE);

                if (EnumDisplaySettings(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &devMode))
                {
                    info.monitorIndex = deviceIndex;
                    info.monitorWidth = devMode.dmPelsWidth;
                    info.monitorHeight = devMode.dmPelsHeight;
                    info.monitorX = devMode.dmPosition.x;
                    info.monitorY = devMode.dmPosition.y;
                }
                break;
            }
        }

        return info;
    }
};
