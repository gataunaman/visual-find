#pragma once
#include "search.hpp"
#include <chrono>
#include <memory>
#include <stdexcept>
#include <sstream>

namespace vf {
using Clock = std::chrono::steady_clock;
inline double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
inline std::runtime_error winError(const char* message) {
    return std::runtime_error(std::string(message) + " (Windows error " +
                              std::to_string(GetLastError()) + ")");
}
struct Bitmap {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    unsigned char* pixels{};
    int width{}, height{};
    Bitmap(int w, int h) : width(w), height(h) {
        if (w <= 0 || h <= 0) throw std::runtime_error("Invalid display dimensions");
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                                  reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!dc || !bitmap) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            throw winError("Cannot allocate capture buffer");
        }
        previous = SelectObject(dc, bitmap);
    }
    ~Bitmap() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
};
struct Capture {
    Coordinates coordinates;
    std::unique_ptr<Bitmap> bitmap;
    double captureMs{};
};
inline MONITORINFO monitorInfo(HMONITOR monitor) {
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) throw winError("Monitor is no longer available");
    return info;
}
inline HMONITOR cursorMonitor() {
    POINT point{};
    if (!GetCursorPos(&point)) throw winError("Cannot read cursor position");
    return MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
}
inline Capture captureScreen(HMONITOR monitor, unsigned maxDimension) {
    const auto start = Clock::now();
    const auto info = monitorInfo(monitor);
    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;
    // Scale only to satisfy the OCR engine's actual limit, preserving aspect ratio.
    const double scale = std::min(1.0, double(maxDimension) / std::max(width, height));
    const int w = std::max(1, int(std::floor(width * scale)));
    const int h = std::max(1, int(std::floor(height * scale)));
    Capture result{{info.rcMonitor.left, info.rcMonitor.top, width, height, w, h},
                   std::make_unique<Bitmap>(w, h), 0};
    HDC screen = GetDC(nullptr);
    if (!screen) throw winError("Cannot access the desktop");
    BOOL ok;
    if (scale == 1.0) {
        ok = BitBlt(result.bitmap->dc, 0, 0, w, h, screen,
                    info.rcMonitor.left, info.rcMonitor.top, SRCCOPY | CAPTUREBLT);
    } else {
        SetStretchBltMode(result.bitmap->dc, HALFTONE);
        SetBrushOrgEx(result.bitmap->dc, 0, 0, nullptr);
        ok = StretchBlt(result.bitmap->dc, 0, 0, w, h, screen,
                        info.rcMonitor.left, info.rcMonitor.top, width, height,
                        SRCCOPY | CAPTUREBLT);
    }
    const DWORD error = GetLastError();
    ReleaseDC(nullptr, screen);
    if (!ok) { SetLastError(error); throw winError("Screen capture failed"); }
    GdiFlush();
    result.captureMs = milliseconds(start);
    return result;
}
inline void enableDpiAwareness() {
    // Manifest sets this before any HWND is created. This also covers console tools.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
}
