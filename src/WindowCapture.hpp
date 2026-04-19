#pragma once

#include "WindowInfo.hpp"

#include <opencv2/opencv.hpp>
#include <Windows.h>

class WindowCapture
{

public:

    enum class CaptureMethod { None, GDI_PrintWindow, GDI_BitBlt, DXGI, WGC };

protected:

    WindowInfo m_windowInfo;
        
    // Captured frame buffer in BGRA (CV_8UC4) format.
    cv::Mat m_image;

    int    m_totalCaptures          = 0;
    double m_totalCaptureTimeMS     = 0;
    double m_previousCaptureTimeMS  = 0;
    double m_maxCaptureTimeMS       = 0;
    double m_minCaptureTimeMS       = 100000;

    CaptureMethod m_captureMethod   = CaptureMethod::None;

public:

    WindowCapture() {}

    virtual cv::Mat capture(const WindowInfo& info) = 0;

    const cv::Mat& image() const
    {
        return m_image;
    }

    void setCaptureMethod(CaptureMethod method)
    {
        m_captureMethod = method;
    }

    CaptureMethod getCaptureMethod() const
    {
        return m_captureMethod;
    }

    double getPreviousCaptureTime() const
    {
        return m_previousCaptureTimeMS;
    }

    std::string getCaptureStats()
    {
        double avgCaptureTime = m_totalCaptures > 0 ? (m_totalCaptureTimeMS / m_totalCaptures) : 0;
        std::stringstream ss;
        ss << "[" << m_totalCaptures << ", " << m_previousCaptureTimeMS << " | " << m_minCaptureTimeMS << ", " << avgCaptureTime << ", " << m_maxCaptureTimeMS << "]";
        return ss.str();
    }
};
