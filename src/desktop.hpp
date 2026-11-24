#pragma once
#include "platform.hpp"
#include <oleacc.h>

namespace vf {
inline bool isDesktopWindow(HWND window) {
    wchar_t cls[64]{};
    GetClassNameW(GetAncestor(window, GA_ROOT), cls, 64);
    return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;
}

// Read only visible Explorer desktop items, once per desktop session. This avoids
// guessing proper names from small shadowed glyphs on a photographic wallpaper.
inline std::vector<Word> desktopLabels(Coordinates coordinates, HWND excludedSearch = nullptr) {
    struct Scan {
        Coordinates coordinates;
        HWND excludedSearch;
        std::vector<Word> words;
        static BOOL CALLBACK root(HWND window, LPARAM data) {
            auto& scan = *reinterpret_cast<Scan*>(data);
            const HWND view = FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr);
            const HWND list = view ? FindWindowExW(view, nullptr, L"SysListView32", nullptr) : nullptr;
            if (!list || !IsWindowVisible(list)) return TRUE;
            IAccessible* raw{};
            if (FAILED(AccessibleObjectFromWindow(list, OBJID_CLIENT, IID_IAccessible,
                                                  reinterpret_cast<void**>(&raw))) || !raw) return TRUE;
            const auto release = [](IAccessible* value) { value->Release(); };
            std::unique_ptr<IAccessible, decltype(release)> accessible(raw, release);
            try {
                long count{};
                if (FAILED(accessible->get_accChildCount(&count)) || count < 0 || count > 1024) return TRUE;
                for (long i = 1; i <= count; ++i) {
                    VARIANT child{}; child.vt = VT_I4; child.lVal = i;
                    VARIANT state{};
                    const HRESULT stateResult = accessible->get_accState(child, &state);
                    const bool hidden = SUCCEEDED(stateResult) && state.vt == VT_I4 &&
                        (state.lVal & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN));
                    VariantClear(&state);
                    if (hidden) continue;
                    long x{}, y{}, w{}, h{};
                    if (FAILED(accessible->accLocation(&x, &y, &w, &h, child)) || w < 3 || h < 3) continue;
                    const auto& c = scan.coordinates;
                    if (x < c.left || y < c.top || x+w > c.left+c.screenWidth || y+h > c.top+c.screenHeight) continue;
                    bool visible = true;
                    for (POINT point : {POINT{x+1,y+1}, POINT{x+w-2,y+1}, POINT{x+1,y+h-2},
                                        POINT{x+w-2,y+h-2}, POINT{x+w/2,y+h/2}}) {
                        const HWND at = WindowFromPoint(point);
                        const HWND rootWindow = GetAncestor(at, GA_ROOT);
                        if (!at || (at != list && !IsChild(list, at) &&
                            (!scan.excludedSearch || rootWindow != scan.excludedSearch))) {
                            visible = false; break;
                        }
                    }
                    if (!visible) continue;
                    BSTR name{};
                    const HRESULT result = accessible->get_accName(child, &name);
                    const auto freeName = [](wchar_t* value) { SysFreeString(value); };
                    std::unique_ptr<wchar_t, decltype(freeName)> ownedName(name, freeName);
                    if (FAILED(result) || !name || !SysStringLen(name)) continue;
                    scan.words.push_back({std::wstring(name, SysStringLen(name)),
                                          {double(x),double(y),double(w),double(h)}, unsigned(scan.words.size())});
                }
            } catch (...) { /* Desktop metadata is optional; retain ordinary OCR on failure. */ }
            return TRUE;
        }
    } scan{coordinates, excludedSearch, {}};
    EnumWindows(Scan::root, reinterpret_cast<LPARAM>(&scan));
    return std::move(scan.words);
}

inline void mergeDesktopLabels(std::vector<Word>& words, std::vector<Word> labels) {
    if (labels.empty()) return;
    unsigned line{};
    for (const auto& word : words) line = std::max(line, word.line+1);
    // Remove OCR readings of the same icon/label so they cannot produce duplicate
    // or misspelled matches alongside Explorer's exact visible item name.
    words.erase(std::remove_if(words.begin(), words.end(), [&](const Word& word) {
        const double x = word.bounds.x+word.bounds.width/2, y = word.bounds.y+word.bounds.height/2;
        return std::any_of(labels.begin(), labels.end(), [&](const Word& label) {
            const auto& b = label.bounds;
            return x >= b.x && x < b.x+b.width && y >= b.y && y < b.y+b.height;
        });
    }), words.end());
    for (auto& label : labels) { label.line = line++; words.push_back(std::move(label)); }
}
}
