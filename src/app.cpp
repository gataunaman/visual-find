#include "ocr.hpp"
#include "overlay.hpp"
#include "desktop.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace {
constexpr UINT CapturedMessage = WM_APP + 1, FinishedMessage = WM_APP + 2;
constexpr UINT TrayMessage = WM_APP + 3, ScrollMessage = WM_APP + 4;
constexpr UINT_PTR ScrollDebounceTimer = 10, ScrollMaxTimer = 11;
constexpr int SearchId = 100, PreviousId = 101, NextId = 102, RefreshId = 103, CloseId = 104;
constexpr int TraySearchId = 201, TrayExitId = 202;
struct Options {
    UINT modifiers = MOD_CONTROL | MOD_SHIFT, key = 'F';
    std::wstring hotkey = L"Ctrl+Shift+F", language, smokeOutput;
    bool quiet{};
};
Options parseOptions() {
    Options result;
    int count{};
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) throw vf::winError("Cannot read arguments");
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> holder(args, LocalFree);
    for (int i = 1; i < count; ++i) {
        std::wstring argument(args[i]);
        if (argument == L"--quiet") result.quiet = true;
        else if (argument == L"--language" && i + 1 < count) result.language = args[++i];
        else if (argument == L"--smoke-test" && i + 1 < count) result.smokeOutput = args[++i];
        else if (argument == L"--hotkey" && i + 1 < count) {
            result.hotkey = args[++i]; result.modifiers = 0; result.key = 0;
            size_t start = 0;
            while (start < result.hotkey.size()) {
                const size_t end = result.hotkey.find(L'+', start);
                auto token = vf::normalize(result.hotkey.substr(start, end - start));
                if (token == L"ctrl" || token == L"control") result.modifiers |= MOD_CONTROL;
                else if (token == L"shift") result.modifiers |= MOD_SHIFT;
                else if (token == L"alt") result.modifiers |= MOD_ALT;
                else if (token == L"win") result.modifiers |= MOD_WIN;
                else if (token.size() == 1 && ((token[0] >= L'a' && token[0] <= L'z') ||
                                              (token[0] >= L'0' && token[0] <= L'9'))) {
                    if (result.key) throw std::runtime_error("Hotkey must contain exactly one letter or digit");
                    result.key = token[0] >= L'a' ? token[0] - L'a' + 'A' : token[0];
                } else throw std::runtime_error("Use a hotkey such as Ctrl+Shift+F or Ctrl+Alt+G");
                if (end == std::wstring::npos) break;
                start = end + 1;
            }
            if (!result.key || !result.modifiers) throw std::runtime_error("Hotkey needs a modifier and a letter or digit");
        } else throw std::runtime_error("Options: --hotkey Ctrl+Alt+F, --language en-US");
    }
    return result;
}
struct Job { uint64_t token; HMONITOR monitor; vf::Clock::time_point started; bool desktop; };
struct Captured { uint64_t token; vf::Coordinates coordinates; double captureMs; };
struct Finished {
    uint64_t token{};
    std::unique_ptr<vf::SearchIndex> index;
    std::wstring error, language;
    double captureMs{}, ocrMs{}, indexMs{}, totalMs{};
};
std::wstring wide(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
    return result;
}

class Application {
    HINSTANCE instance_{};
    Options options_;
    HWND window_{}, edit_{}, count_{}, status_{}, previous_{}, next_{}, refresh_{}, close_{};
    HWND originalWindow_{};
    HMONITOR monitor_{};
    bool scrollMaxArmed_{};
    static inline HHOOK mouseHook_{};
    static inline Application* hooked_{};
    HFONT font_{}, smallFont_{};
    HBRUSH whiteBrush_ = CreateSolidBrush(RGB(255,255,255));
    HBRUSH editBrush_ = CreateSolidBrush(RGB(245,247,250));
    std::unique_ptr<vf::Overlay> overlay_;
    std::unique_ptr<vf::SearchIndex> index_;
    std::vector<vf::Match> matches_;
    size_t selection_{};
    UINT dpi_ = 96;
    bool active_{}, loading_{}, capturing_{}, ready_{}, trayAdded_{}, hotkeyRegistered_{}, exiting_{};
    std::atomic<uint64_t> token_{0};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Job> pending_;
    bool stopping_{};
    std::thread worker_;
    vf::Clock::time_point launch_, sessionStart_;
    double startupMs_{}, uiMs_{}, searchMs_{}, overlayMs_{};
    double captureMs_{}, ocrMs_{}, indexMs_{}, totalMs_{};
    unsigned smokeSessions_{}, smokeSearches_{};
    bool smokePassed_ = true;
    std::vector<std::string> smokeFailures_;
    const UINT taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    const UINT activateMessage_ = RegisterWindowMessageW(L"VisualFind.Activate");

    int px(int value) const { return MulDiv(value, dpi_, 96); }
    static Application* self(HWND window) {
        return reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        Application* app = self(window);
        if (message == WM_NCCREATE) {
            app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(window, message, w, l);
        try { return app->message(message, w, l); }
        catch (const winrt::hresult_error& e) { app->showError(e.message().c_str()); }
        catch (const std::exception& e) { app->showError(wide(e.what())); }
        catch (...) { app->showError(L"Unexpected error. Press F5 to retry."); }
        return 0;
    }
    static LRESULT CALLBACK childProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                           UINT_PTR, DWORD_PTR data) {
        auto app = reinterpret_cast<Application*>(data);
        try {
            if (message == WM_GETDLGCODE) {
                const auto value = DefSubclassProc(window, message, w, l);
                return value | DLGC_WANTALLKEYS;
            }
            if (message == WM_KEYDOWN) {
                if (w == VK_ESCAPE) { app->closeSession(); return 0; }
                if (w == VK_F5) { app->beginSession(); return 0; }
                if (w == VK_TAB) {
                    const HWND next = GetNextDlgTabItem(app->window_, window, GetKeyState(VK_SHIFT) < 0);
                    if (next) SetFocus(next);
                    return 0;
                }
                if (w == VK_RETURN) {
                    if (window == app->edit_) app->navigate(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                    else SendMessageW(window, BM_CLICK, 0, 0);
                    return 0;
                }
            }
            if (message == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE || w == VK_TAB)) return 0;
            if (message == WM_NCDESTROY) RemoveWindowSubclass(window, childProcedure, 1);
            return DefSubclassProc(window, message, w, l);
        }
        catch (const winrt::hresult_error& e) { app->showError(e.message().c_str()); }
        catch (const std::exception& e) { app->showError(wide(e.what())); }
        catch (...) { app->showError(L"Unexpected error. Press F5 to retry."); }
        return 0;
    }
    HWND control(const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
        auto window = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                     0,0,1,1,window_, reinterpret_cast<HMENU>(INT_PTR(id)), instance_, nullptr);
        if (!window) throw vf::winError("Cannot create search control");
        if (id) SetWindowSubclass(window, childProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        return window;
    }
    void layout(UINT dpi) {
        dpi_ = dpi;
        auto oldFont = font_, oldSmall = smallFont_;
        font_ = CreateFontW(-px(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                            DEFAULT_PITCH,L"Segoe UI");
        smallFont_ = CreateFontW(-px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH,L"Segoe UI");
        for (HWND child : {edit_,count_,previous_,next_,refresh_,close_})
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), TRUE);
        if (oldFont) DeleteObject(oldFont);
        if (oldSmall) DeleteObject(oldSmall);
        MoveWindow(edit_,px(18),px(17),px(300),px(30),TRUE);
        MoveWindow(count_,px(325),px(20),px(88),px(26),TRUE);
        MoveWindow(previous_,px(418),px(12),px(32),px(36),TRUE);
        MoveWindow(next_,px(452),px(12),px(32),px(36),TRUE);
        MoveWindow(refresh_,px(486),px(12),px(32),px(36),TRUE);
        MoveWindow(close_,px(520),px(12),px(32),px(36),TRUE);
        MoveWindow(status_,px(18),px(61),px(532),px(38),TRUE);
        RECT rect{}; GetClientRect(window_, &rect);
        SetWindowRgn(window_, CreateRoundRectRgn(0,0,rect.right+1,rect.bottom+1,px(14),px(14)), TRUE);
    }
    void setStatus(const std::wstring& text) { SetWindowTextW(status_, text.c_str()); }
    void showError(const std::wstring& error) {
        if (exiting_) return;
        loading_ = false; ready_ = false;
        index_.reset(); matches_.clear();
        if (overlay_) overlay_->release();
        SetWindowTextW(count_, L"Error");
        setStatus(error + L"\nF5 retry   |   Esc close");
        if (active_) showSearch();
        if (!options_.smokeOutput.empty()) { smokePassed_ = false; finishSmoke(); }
    }
    void showSearch() {
        if (!active_) return;
        capturing_ = false;
        ShowWindow(window_, SW_SHOW);
        SetWindowPos(window_, HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        SetForegroundWindow(window_);
        SetFocus(edit_);
    }
    void positionSearch(HMONITOR monitor) {
        auto info = vf::monitorInfo(monitor);
        // Obtain monitor DPI by temporarily positioning the hidden HWND there.
        if (MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST) != monitor)
            SetWindowPos(window_,nullptr,info.rcWork.left,info.rcWork.top,0,0,
                         SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        const UINT dpi = GetDpiForWindow(window_);
        dpi_ = dpi ? dpi : 96;
        const int width = px(568), height = px(110);
        const int x = std::max(info.rcWork.left, info.rcWork.right - width - px(24));
        const int y = info.rcWork.top + px(24);
        SetWindowPos(window_,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);
        layout(dpi_);
    }
    // Wheel events only post a message; the UI thread debounces them into one rescan.
    static LRESULT CALLBACK mouseHook(int code, WPARAM w, LPARAM l) {
        if (code == HC_ACTION && (w == WM_MOUSEWHEEL || w == WM_MOUSEHWHEEL) && hooked_)
            PostMessageW(hooked_->window_, ScrollMessage, 0, 0);
        return CallNextHookEx(nullptr, code, w, l);
    }
    void startScrollWatch() {
        if (mouseHook_) return;
        hooked_ = this;
        mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, instance_, 0);
    }
    void stopScrollWatch() {
        if (mouseHook_) { UnhookWindowsHookEx(mouseHook_); mouseHook_ = nullptr; }
        hooked_ = nullptr;
        KillTimer(window_, ScrollDebounceTimer); KillTimer(window_, ScrollMaxTimer);
        scrollMaxArmed_ = false;
    }
    void onScroll() {
        if (!active_ || exiting_) return;
        POINT point{}; GetCursorPos(&point);
        const HWND at = WindowFromPoint(point);
        if ((at && GetAncestor(at, GA_ROOT) == window_) ||
            MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST) != monitor_) return;
        overlay_->hide();  // highlights are stale as soon as content moves
        SetTimer(window_, ScrollDebounceTimer, 200, nullptr);
        if (!scrollMaxArmed_) { scrollMaxArmed_ = true; SetTimer(window_, ScrollMaxTimer, 1000, nullptr); }
    }
    void beginSession(HWND foreground = nullptr, bool automatic = false) {
        if (exiting_) return;
        const auto monitor = vf::cursorMonitor();
        const bool existing = active_;
        scrollMaxArmed_ = false;
        KillTimer(window_, ScrollDebounceTimer); KillTimer(window_, ScrollMaxTimer);
        if (!existing) originalWindow_ = foreground ? foreground : GetForegroundWindow();
        active_ = true; loading_ = true; capturing_ = true; ready_ = false;
        const uint64_t token = ++token_;
        sessionStart_ = vf::Clock::now();
        overlay_->release(); index_.reset(); matches_.clear(); selection_ = 0;
        if (!existing) SetWindowTextW(edit_, L"");
        SetWindowTextW(count_, automatic ? L"Updating" : L"Scanning");
        if (!automatic) setStatus(L"Reading this monitor locally...\nEnter next   |   Shift+Enter previous   |   F5 refresh   |   Esc close");
        if (!existing || monitor != monitor_) positionSearch(monitor);  // keep a dragged bar in place
        monitor_ = monitor;
        startScrollWatch();
        if (!SetWindowDisplayAffinity(window_, 0x00000011 /* WDA_EXCLUDEFROMCAPTURE */))
            throw vf::winError("Cannot exclude search bar from OCR capture");
        // Use WM_HOTKEY's foreground permission before asynchronous capture.
        // Exclude only until the worker has copied the pixels, then allow screenshots.
        showSearch(); uiMs_ = vf::milliseconds(sessionStart_);
        {
            std::lock_guard lock(mutex_);
            pending_ = Job{token,monitor,sessionStart_,vf::isDesktopWindow(originalWindow_)};
        }
        condition_.notify_one();
    }
    void closeSession(bool restore = true) {
        const bool hadFocus = GetForegroundWindow() == window_;
        stopScrollWatch();
        ++token_; active_ = false; loading_ = false; capturing_ = false; ready_ = false;
        { std::lock_guard lock(mutex_); pending_.reset(); }
        ShowWindow(window_, SW_HIDE);
        SetWindowDisplayAffinity(window_, 0 /* WDA_NONE */);
        if (overlay_) overlay_->release();
        index_.reset(); std::vector<vf::Match>().swap(matches_);
        SetWindowTextW(edit_, L"");
        if (restore && hadFocus && IsWindow(originalWindow_)) SetForegroundWindow(originalWindow_);
        originalWindow_ = nullptr;
    }
    std::wstring query() const {
        const int length = GetWindowTextLengthW(edit_);
        std::wstring text(size_t(length) + 1, L'\0');
        GetWindowTextW(edit_,text.data(),length+1); text.resize(length);
        return text;
    }
    void updateSearch() {
        if (!active_ || !ready_ || !index_) return;
        const auto text = query();
        const auto start = vf::Clock::now();
        matches_ = index_->find(text); selection_ = 0;
        searchMs_ = vf::milliseconds(start);
        render();
        if (text.empty()) setStatus(L"Type to find text on this monitor.\nEnter next   |   Shift+Enter previous   |   F5 refresh   |   Esc close");
        else if (matches_.empty()) setStatus(L"No matches. Try a shorter phrase or refresh the screen.\nEnter next   |   Shift+Enter previous   |   F5 refresh   |   Esc close");
        else setStatus(L"Enter next   |   Shift+Enter previous   |   F5 refresh   |   Esc close");
    }
    void render() {
        overlayMs_ = overlay_->render(matches_, selection_);
        std::wstring count = matches_.empty() ? L"0 matches" :
            std::to_wstring(selection_+1) + L" / " + std::to_wstring(matches_.size());
        SetWindowTextW(count_, count.c_str());
    }
    void navigate(int direction) {
        if (matches_.empty()) return;
        selection_ = direction > 0 ? (selection_+1) % matches_.size() :
                     (selection_+matches_.size()-1) % matches_.size();
        render(); SetFocus(edit_);
    }
    template<class T> void post(UINT message, std::unique_ptr<T> data) {
        // Keep ownership if the UI no longer accepts messages.
        if (PostMessageW(window_,message,0,reinterpret_cast<LPARAM>(data.get()))) data.release();
    }
    void work() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            {
            vf::Ocr ocr(options_.language);
            for (;;) {
                Job job{};
                {
                    std::unique_lock lock(mutex_);
                    condition_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
                    if (stopping_) break;
                    job = *pending_; pending_.reset();
                }
                auto finished = std::make_unique<Finished>(); finished->token = job.token;
                try {
                    // Let the compositor apply capture exclusion before taking the screenshot.
                    DwmFlush();
                    auto capture = vf::captureScreen(job.monitor, vf::Ocr::maxDimension());
                    if (job.token != token_) continue;
                    post(CapturedMessage, std::make_unique<Captured>(Captured{
                        job.token,capture.coordinates,capture.captureMs}));
                    finished->captureMs = capture.captureMs;
                    auto labels = job.desktop ? vf::desktopLabels(capture.coordinates,window_) : std::vector<vf::Word>{};
                    auto output = ocr.recognize(capture);
                    if (job.token != token_) continue;
                    finished->ocrMs = output.ocrMs;
                    finished->language = std::move(output.language);
                    const auto indexStart = vf::Clock::now();
                    vf::mergeDesktopLabels(output.words,std::move(labels));
                    finished->index = std::make_unique<vf::SearchIndex>(output.words);
                    finished->indexMs = vf::milliseconds(indexStart);
                    finished->totalMs = vf::milliseconds(job.started);
                } catch (const winrt::hresult_error& e) {
                    finished->error = L"Windows OCR: " + std::wstring(e.message());
                } catch (const std::exception& e) { finished->error = wide(e.what()); }
                if (job.token == token_) post(FinishedMessage, std::move(finished));
            }
            }
            winrt::uninit_apartment();
        } catch (const winrt::hresult_error& e) {
            auto finished = std::make_unique<Finished>(); finished->token = token_;
            finished->error = e.message().c_str(); post(FinishedMessage, std::move(finished));
        } catch (const std::exception& e) {
            auto finished = std::make_unique<Finished>(); finished->token = token_;
            finished->error = wide(e.what()); post(FinishedMessage, std::move(finished));
        }
    }
    void addTray(bool notify) {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = window_; data.uID = 1; data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = TrayMessage;
        data.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
        const auto tip = L"Visual Find - " + options_.hotkey;
        wcsncpy_s(data.szTip,tip.c_str(),_TRUNCATE);
        if (notify) {
            data.uFlags |= NIF_INFO; data.dwInfoFlags = NIIF_INFO;
            wcscpy_s(data.szInfoTitle,L"Visual Find is ready");
            const auto info = L"Press " + options_.hotkey + L" to search the monitor under your cursor. Right-click this icon to exit.";
            wcsncpy_s(data.szInfo,info.c_str(),_TRUNCATE);
        }
        trayAdded_ = Shell_NotifyIconW(NIM_ADD,&data) != FALSE;
    }
    void trayMenu() {
        const HWND foreground = GetForegroundWindow();
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,TraySearchId,L"Search screen");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,TrayExitId,L"Exit Visual Find");
        POINT point{}; GetCursorPos(&point);
        SetForegroundWindow(window_);
        const int selected = TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,
                                            point.x,point.y,0,window_,nullptr);
        DestroyMenu(menu);
        if (selected == TraySearchId) beginSession(foreground);
        else if (selected == TrayExitId) exit();
    }
    void exit() {
        exiting_ = true; closeSession(false);
        if (hotkeyRegistered_) { UnregisterHotKey(window_,1); hotkeyRegistered_ = false; }
        if (trayAdded_) {
            NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = window_; data.uID = 1;
            Shell_NotifyIconW(NIM_DELETE,&data); trayAdded_ = false;
        }
        PostQuitMessage(smokePassed_ ? 0 : 1);
    }
    void finishSmoke() {
        KillTimer(window_, 7); KillTimer(window_, 8);
        std::ofstream file(std::filesystem::path(options_.smokeOutput));
        file << "{\n  \"passed\": " << (smokePassed_ ? "true" : "false")
             << ",\n  \"sessions\": " << smokeSessions_ << ",\n  \"cached_searches\": " << smokeSearches_
             << ",\n  \"startup_ms\": " << startupMs_ << ",\n  \"search_ui_ms\": " << uiMs_
             << ",\n  \"capture_ms\": " << captureMs_ << ",\n  \"ocr_ms\": " << ocrMs_
             << ",\n  \"index_ms\": " << indexMs_ << ",\n  \"search_ms\": " << searchMs_
             << ",\n  \"overlay_ms\": " << overlayMs_ << ",\n  \"session_total_ms\": " << totalMs_
             << ",\n  \"failed_checks\": [";
        for (size_t i = 0; i < smokeFailures_.size(); ++i)
            file << (i ? ", " : "") << '"' << smokeFailures_[i] << '"';
        file << "]\n}\n";
        file.close(); exit();
    }
    void smoke() {
        ++smokeSessions_;
        const auto check = [this](bool condition, const char* label) {
            if (!condition) smokeFailures_.push_back(std::to_string(smokeSessions_) + ": " + label);
            smokePassed_ &= condition;
        };
        check(index_->find(L"capture exclusion marker").size() == 1, "underlying text captured behind search box");
        check(index_->find(L"Reading this monitor locally").empty(), "search UI excluded from capture");
        // Fixture text is supplied by the separate benchmark tool, never extracted into logs.
        SetWindowTextW(edit_,L"algorithm"); ++smokeSearches_;
        check(matches_.size() >= 2, "multiple cached matches");
        check(GetForegroundWindow() == window_, "search retains foreground");
        const auto first = selection_;
        SendMessageW(edit_,WM_KEYDOWN,VK_RETURN,0);
        check(!matches_.empty() && selection_ == (first+1) % matches_.size(), "Enter moves to next match");
        navigate(-1); check(selection_ == first, "previous match restores selection");
        for (const auto* text : {L"a",L"al",L"alg",L"algorithm",L"binary search",L"nothing-to-match-here"}) {
            SetWindowTextW(edit_,text); ++smokeSearches_;
        }
        check(matches_.empty(), "absent query returns zero matches");
        if (smokeSessions_ == 1) {
            SetWindowTextW(edit_,L"algorithm"); ++smokeSearches_;
            SendMessageW(edit_,WM_KEYDOWN,VK_F5,0);
        } else {
            const auto overlayWindow = overlay_->window();
            SendMessageW(edit_,WM_KEYDOWN,VK_ESCAPE,0);
            check(!IsWindowVisible(window_) && !IsWindowVisible(overlayWindow) && !index_, "Escape releases search and overlay");
            finishSmoke();
        }
    }
    LRESULT message(UINT message, WPARAM w, LPARAM l) {
        if (message == taskbarCreated_) { addTray(false); return 0; }
        if (message == activateMessage_) { beginSession(); return 0; }
        switch (message) {
        case ScrollMessage: onScroll(); return 0;
        case WM_HOTKEY: if (w == 1) beginSession(); return 0;
        case CapturedMessage: {
            std::unique_ptr<Captured> captured(reinterpret_cast<Captured*>(l));
            if (active_ && captured->token == token_) {
                if (!SetWindowDisplayAffinity(window_, 0 /* WDA_NONE */))
                    throw vf::winError("Cannot restore search bar in screenshots");
                overlay_->prepare(captured->coordinates);
            }
            return 0;
        }
        case FinishedMessage: {
            std::unique_ptr<Finished> finished(reinterpret_cast<Finished*>(l));
            if (!active_ || finished->token != token_) return 0;
            SetWindowDisplayAffinity(window_, 0 /* WDA_NONE */);
            if (!finished->error.empty()) { showError(finished->error); return 0; }
            index_ = std::move(finished->index); loading_ = false; ready_ = true;
            captureMs_ = finished->captureMs; ocrMs_ = finished->ocrMs;
            indexMs_ = finished->indexMs; totalMs_ = finished->totalMs;
            updateSearch();
            if (!index_->wordCount()) setStatus(L"No readable text found on this monitor.\nF5 refresh   |   Esc close");
            if (!options_.smokeOutput.empty()) SetTimer(window_,8,80,nullptr);
            return 0;
        }
        case WM_TIMER:
            if (w == 7) { KillTimer(window_,7); PostMessageW(window_,WM_HOTKEY,1,0); }
            else if (w == 8) { KillTimer(window_,8); smoke(); }
            else if (w == ScrollDebounceTimer || w == ScrollMaxTimer) beginSession(nullptr, true);
            else if (w == 9) { KillTimer(window_,9); smokePassed_ = false; finishSmoke(); }
            return 0;
        case WM_COMMAND:
            if (LOWORD(w) == TrayExitId) exit();
            else if (LOWORD(w) == TraySearchId) beginSession();
            else if (LOWORD(w) == SearchId && HIWORD(w) == EN_CHANGE) updateSearch();
            else if (HIWORD(w) == BN_CLICKED) {
                switch (LOWORD(w)) {
                case PreviousId: navigate(-1); break;
                case NextId: navigate(1); break;
                case RefreshId: beginSession(); break;
                case CloseId: closeSession(); break;
                }
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE && active_ && !capturing_ && IsWindowVisible(window_))
                closeSession(false);
            return 0;
        case WM_DISPLAYCHANGE: if (active_) closeSession(); return 0;
        case WM_DPICHANGED: {
            const auto rect = reinterpret_cast<RECT*>(l);
            SetWindowPos(window_,nullptr,rect->left,rect->top,rect->right-rect->left,
                         rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
            layout(HIWORD(w)); return 0;
        }
        case WM_CLOSE: closeSession(); return 0;
        case WM_QUERYENDSESSION: exit(); return TRUE;
        case TrayMessage:
            if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) trayMenu();
            else if (l == WM_LBUTTONDBLCLK) beginSession();
            return 0;
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(w),RGB(24,34,52));
            SetBkColor(reinterpret_cast<HDC>(w),RGB(245,247,250));
            return reinterpret_cast<LRESULT>(editBrush_);
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l) == status_ ? RGB(92,102,119) : RGB(35,52,75));
            SetBkColor(reinterpret_cast<HDC>(w),RGB(255,255,255));
            return reinterpret_cast<LRESULT>(whiteBrush_);
        case WM_DRAWITEM: {
            const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            const bool pressed = draw->itemState & ODS_SELECTED;
            FillRect(draw->hDC,&draw->rcItem,pressed ? editBrush_ : whiteBrush_);
            SetTextColor(draw->hDC,RGB(57,77,106)); SetBkMode(draw->hDC,TRANSPARENT);
            SelectObject(draw->hDC,font_);
            wchar_t text[8]{}; GetWindowTextW(draw->hwndItem,text,8);
            auto rect = draw->rcItem;
            DrawTextW(draw->hDC,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC,&rect);
            return TRUE;
        }
        case WM_ERASEBKGND: {
            RECT rect{}; GetClientRect(window_,&rect); FillRect(reinterpret_cast<HDC>(w),&rect,whiteBrush_); return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window_, &paint);
            RECT rect{}; GetClientRect(window_, &rect);
            SetDCBrushColor(dc, RGB(170,182,198));
            for (int offset : {-8,-1,6}) {
                RECT dot{rect.right/2+px(offset),px(5),rect.right/2+px(offset+3),px(8)};
                FillRect(dc, &dot, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
            EndPaint(window_, &paint); return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCAPTION) { SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); return TRUE; }
            break;
        case WM_NCHITTEST: {
            auto result = DefWindowProcW(window_,message,w,l);
            if (result == HTCLIENT) {
                POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; ScreenToClient(window_,&point);
                const HWND child = ChildWindowFromPointEx(window_, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
                if (child != edit_ && child != previous_ && child != next_ && child != refresh_ && child != close_)
                    return HTCAPTION;
            }
            return result;
        }
        }
        return DefWindowProcW(window_,message,w,l);
    }
public:
    Application(HINSTANCE instance, Options options, vf::Clock::time_point launch)
        : instance_(instance), options_(std::move(options)), launch_(launch) {
        vf::enableDpiAwareness();
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        WNDCLASSW cls{};
        cls.hInstance = instance_; cls.lpfnWndProc = procedure;
        cls.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
        cls.hCursor = LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground = whiteBrush_;
        cls.lpszClassName = L"VisualFind.Search";
        if (!RegisterClassW(&cls)) throw vf::winError("Cannot register search window");
        if (!CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Visual Find",
                              WS_POPUP,0,0,568,110,nullptr,nullptr,instance_,this))
            throw vf::winError("Cannot create search window");
        // Consume the launcher's initial show command while the window is idle.
        ShowWindow(window_,SW_HIDE);
        if (!SetWindowDisplayAffinity(window_,0x00000011))
            throw std::runtime_error("Capture exclusion is unavailable. Visual Find requires Windows 10 version 2004 or newer with desktop composition enabled.");
        SetWindowDisplayAffinity(window_,0 /* WDA_NONE */);
        edit_ = control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,SearchId);
        SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Find text on screen"));
        SendMessageW(edit_,EM_SETLIMITTEXT,1024,0);
        count_ = control(L"STATIC",L"0 matches",SS_CENTER,0);
        status_ = control(L"STATIC",L"",SS_LEFT,0);
        previous_ = control(L"BUTTON",L"\u2191",BS_OWNERDRAW|WS_TABSTOP,PreviousId);
        next_ = control(L"BUTTON",L"\u2193",BS_OWNERDRAW|WS_TABSTOP,NextId);
        refresh_ = control(L"BUTTON",L"\u21BB",BS_OWNERDRAW|WS_TABSTOP,RefreshId);
        close_ = control(L"BUTTON",L"\u00D7",BS_OWNERDRAW|WS_TABSTOP,CloseId);
        layout(96);
        overlay_ = std::make_unique<vf::Overlay>();
        if (!RegisterHotKey(window_,1,options_.modifiers|MOD_NOREPEAT,options_.key))
            throw std::runtime_error("Hotkey is already in use. Restart with --hotkey Ctrl+Alt+F (or another combination).");
        hotkeyRegistered_ = true;
        addTray(options_.smokeOutput.empty() && !options_.quiet);
        worker_ = std::thread([this] { work(); });
        startupMs_ = vf::milliseconds(launch_);
        if (!options_.smokeOutput.empty()) {
            SetTimer(window_,7,100,nullptr); SetTimer(window_,9,15000,nullptr);
        }
    }
    ~Application() {
        exiting_ = true; ++token_;
        { std::lock_guard lock(mutex_); stopping_ = true; pending_.reset(); }
        condition_.notify_one();
        if (worker_.joinable()) worker_.join();
        MSG message{};
        while (PeekMessageW(&message,window_,CapturedMessage,FinishedMessage,PM_REMOVE)) {
            if (message.message == CapturedMessage) delete reinterpret_cast<Captured*>(message.lParam);
            if (message.message == FinishedMessage) delete reinterpret_cast<Finished*>(message.lParam);
        }
        if (hotkeyRegistered_) UnregisterHotKey(window_,1);
        if (trayAdded_) {
            NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = window_; data.uID = 1;
            Shell_NotifyIconW(NIM_DELETE,&data);
        }
        overlay_.reset();
        if (window_) DestroyWindow(window_);
        if (font_) DeleteObject(font_);
        if (smallFont_) DeleteObject(smallFont_);
        DeleteObject(whiteBrush_); DeleteObject(editBrush_);
    }
    int run() {
        MSG message{}; int result;
        while ((result = GetMessageW(&message,nullptr,0,0)) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        return result < 0 ? 1 : int(message.wParam);
    }
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const auto start = vf::Clock::now();
    HANDLE mutex = CreateMutexW(nullptr,FALSE,L"Local\\VisualFind.Desktop.Instance");
    if (!mutex) { MessageBoxW(nullptr,L"Cannot create application instance.",L"Visual Find",MB_OK|MB_ICONERROR); return 1; }
    const bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
    std::unique_ptr<void, decltype(&CloseHandle)> guard(mutex,CloseHandle);
    if (existing) {
        const HWND window = FindWindowW(L"VisualFind.Search",nullptr);
        if (window) {
            DWORD process{}; GetWindowThreadProcessId(window,&process); AllowSetForegroundWindow(process);
            PostMessageW(window,RegisterWindowMessageW(L"VisualFind.Activate"),0,0);
        }
        return 0;
    }
    try {
        Application application(instance,parseOptions(),start);
        return application.run();
    } catch (const winrt::hresult_error& e) {
        MessageBoxW(nullptr,e.message().c_str(),L"Visual Find",MB_OK|MB_ICONERROR);
    } catch (const std::exception& e) {
        MessageBoxW(nullptr,wide(e.what()).c_str(),L"Visual Find",MB_OK|MB_ICONERROR);
    }
    return 1;
}
