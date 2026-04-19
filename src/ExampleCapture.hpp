#pragma once

#include "Tools.hpp"
#include "WindowCapture_DXGI.hpp"
#include "WindowCapture_GDI.hpp"
#include "WindowCapture_WGC.hpp"
#include "WindowInfo.hpp"

#include <string>

inline bool TryGetWindowInfoFromTitleSubstring(const std::string& titleSubstring, WindowInfo& info)
{
    HWND hwnd = FindWindowByTitleSubstring(titleSubstring);
    if (!hwnd) { return false; }
    info = WindowInfo::GetWindowInfo(hwnd);
    return info.hwnd != nullptr;
}

inline void ExampleCaptureGDIByTitleSubstring(const std::string& titleSubstring)
{
    WindowInfo info {};
    if (!TryGetWindowInfoFromTitleSubstring(titleSubstring, info)) { return; }

    WindowCapture_GDI capture;
    // PrintWindow is usually more compatible than BitBlt for GPU-rendered windows,
    // because it asks the target window to draw itself into our capture DC.
    capture.setPrintWindowOption(WindowCapture_GDI::OPT_PW_RENDERFULLCONTENT);
    // Request "full content" so PrintWindow includes modern app content paths
    // (instead of only a minimal client-only result on some windows).
    capture.setCaptureMethod(WindowCapture::CaptureMethod::GDI_PrintWindow);
    capture.capture(info);
    const cv::Mat& opencvImage = capture.image();
    (void)opencvImage;
}

inline void ExampleCaptureDXGIByTitleSubstring(const std::string& titleSubstring)
{
    WindowInfo info {};
    if (!TryGetWindowInfoFromTitleSubstring(titleSubstring, info)) { return; }

    WindowCapture_DXGI capture;
    // DXGI duplication is configuration-light here: this class currently does not expose
    // extra runtime toggles (it always captures monitor output then crops to the window).
    capture.capture(info);
    const cv::Mat& opencvImage = capture.image();
    (void)opencvImage;
}

inline void ExampleCaptureWGCByTitleSubstring(const std::string& titleSubstring)
{
    WindowInfo info {};
    if (!TryGetWindowInfoFromTitleSubstring(titleSubstring, info)) { return; }

    WindowCapture_WGC capture;
    // Border controls the yellow/colored Windows capture outline around the target.
    // Disable it for a clean output frame, enable it if users should see capture state.
    capture.setBorderRequired(false);
    // Cursor controls whether the mouse cursor is composited into the captured image.
    // Disable for "raw" pixels, enable for demos/tutorials where cursor context matters.
    capture.setCursorEnabled(false);
    capture.capture(info);
    const cv::Mat& opencvImage = capture.image();
    (void)opencvImage;
}
