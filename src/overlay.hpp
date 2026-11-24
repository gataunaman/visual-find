#pragma once
#include "platform.hpp"
#include <cstring>

namespace vf {
class Overlay {
    HWND window_{};
    Coordinates coordinates_{};
    int surfaceLeft_{}, surfaceTop_{};
    std::unique_ptr<Bitmap> surface_;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_ERASEBKGND) return 1;
        return DefWindowProcW(window, message, w, l);
    }
    void rectangle(Box bounds, bool selected) {
        const int padding = selected ? 3 : 2;
        const int left = std::clamp(int(std::floor(bounds.x)) - surfaceLeft_ - padding, 0, surface_->width);
        const int top = std::clamp(int(std::floor(bounds.y)) - surfaceTop_ - padding, 0, surface_->height);
        const int right = std::clamp(int(std::ceil(bounds.x + bounds.width)) - surfaceLeft_ + padding, 0, surface_->width);
        const int bottom = std::clamp(int(std::ceil(bounds.y + bounds.height)) - surfaceTop_ + padding, 0, surface_->height);
        const int border = selected ? 3 : 1;
        // Premultiplied BGRA required by UpdateLayeredWindow.
        const unsigned red = selected ? 35 : 255;
        const unsigned green = selected ? 130 : 194;
        const unsigned blue = selected ? 255 : 25;
        for (int y = top; y < bottom; ++y) {
            auto row = reinterpret_cast<uint32_t*>(surface_->pixels) + size_t(y) * surface_->width;
            for (int x = left; x < right; ++x) {
                const bool edge = x < left + border || x >= right - border ||
                                  y < top + border || y >= bottom - border;
                const unsigned alpha = edge ? (selected ? 245 : 210) : (selected ? 65 : 48);
                row[x] = (alpha << 24) | ((red * alpha / 255) << 16) |
                         ((green * alpha / 255) << 8) | (blue * alpha / 255);
            }
        }
    }
public:
    Overlay() {
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = procedure;
        cls.lpszClassName = L"VisualFind.Overlay";
        RegisterClassW(&cls);
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                                  WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                  cls.lpszClassName, L"Visual Find highlights", WS_POPUP,
                                  0, 0, 1, 1, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window_) throw winError("Cannot create highlight overlay");
        // Refresh hides/releases highlights before capture; keep them visible to screenshot tools.
    }
    ~Overlay() { if (window_) DestroyWindow(window_); }
    HWND window() const { return window_; }
    void prepare(Coordinates coordinates) {
        release(); coordinates_ = coordinates;
    }
    void hide() { ShowWindow(window_, SW_HIDE); }
    void release() { hide(); surface_.reset(); }
    double render(const std::vector<Match>& matches, size_t selected) {
        const auto start = Clock::now();
        if (matches.empty()) { hide(); return milliseconds(start); }
        Box extent = matches.front().bounds;
        for (size_t i = 1; i < matches.size(); ++i) extent = unite(extent, matches[i].bounds);
        const int left = std::max(coordinates_.left, int(std::floor(extent.x)) - 4);
        const int top = std::max(coordinates_.top, int(std::floor(extent.y)) - 4);
        const int right = std::min(coordinates_.left + coordinates_.screenWidth,
                                   int(std::ceil(extent.x + extent.width)) + 4);
        const int bottom = std::min(coordinates_.top + coordinates_.screenHeight,
                                    int(std::ceil(extent.y + extent.height)) + 4);
        if (right <= left || bottom <= top) { hide(); return milliseconds(start); }
        if (!surface_ || surface_->width != right-left || surface_->height != bottom-top)
            surface_ = std::make_unique<Bitmap>(right-left, bottom-top);
        surfaceLeft_ = left; surfaceTop_ = top;
        memset(surface_->pixels, 0, size_t(surface_->width) * surface_->height * 4);
        for (size_t i = 0; i < matches.size(); ++i)
            if (i != selected) for (const auto& box : matches[i].regions) rectangle(box, false);
        if (selected < matches.size())
            for (const auto& box : matches[selected].regions) rectangle(box, true);
        POINT destination{surfaceLeft_, surfaceTop_}, source{0,0};
        SIZE size{surface_->width, surface_->height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(window_, nullptr, &destination, &size, surface_->dc,
                                 &source, 0, &blend, ULW_ALPHA))
            throw winError("Cannot render highlights");
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        return milliseconds(start);
    }
};
}
