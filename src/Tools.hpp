#pragma once

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "Logger.hpp"


std::string GetWindowTitle(HWND hwnd)
{
    char windowTitle[256];
    GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
    return std::string(windowTitle);
}

std::vector<HWND> GetWindowHandles()
{
    std::vector<HWND> windowHandles;

    HWND hwnd = GetTopWindow(NULL);
    while (hwnd != NULL)
    {
        if (IsWindowVisible(hwnd))
        {
            windowHandles.push_back(hwnd);
        }
        hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
    }

    return windowHandles;
}

HWND FindWindowByTitleSubstring(const std::string& titleSubstring)
{
    if (titleSubstring.empty()) { return nullptr; }

    // Fast path for exact title matches.
    HWND exact = FindWindowA(nullptr, titleSubstring.c_str());
    if (exact && IsWindowVisible(exact)) { return exact; }

    std::string query = titleSubstring;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (HWND hwnd : GetWindowHandles())
    {
        std::string title = GetWindowTitle(hwnd);
        std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (title.find(query) != std::string::npos)
        {
            return hwnd;
        }
    }

    return nullptr;
}

void PrintMonitorInfo()
{
    DISPLAY_DEVICE displayDevice;
    displayDevice.cb = sizeof(DISPLAY_DEVICE);

    for (DWORD deviceIndex = 0; EnumDisplayDevices(NULL, deviceIndex, &displayDevice, 0); ++deviceIndex)
    {
        if (!(displayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
        {
            DEVMODE devMode;
            devMode.dmSize = sizeof(DEVMODE);

            if (!EnumDisplaySettings(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &devMode))
            {
                CapLog() << "Tools: EnumDisplaySettings failed for monitor index "
                         << deviceIndex << " with error: " << GetLastError() << "\n";
            }
        }
    }
}
