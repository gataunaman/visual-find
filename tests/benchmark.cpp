#include "../src/ocr.hpp"
#include "../src/overlay.hpp"
#include <dwmapi.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Samples {
    std::vector<double> values;
    double median() const {
        auto sorted = values; std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size()/2];
    }
    double percentile(double p) const {
        auto sorted = values; std::sort(sorted.begin(), sorted.end());
        return sorted[std::min(sorted.size()-1,size_t(std::ceil(p*sorted.size())-1))];
    }
    void write(std::ostream& out, const char* name, bool comma = true) const {
        out << "  \"" << name << "\": {\"samples\": " << values.size()
            << ", \"median_ms\": " << median() << ", \"p95_ms\": " << percentile(.95)
            << ", \"min_ms\": " << *std::min_element(values.begin(),values.end())
            << ", \"max_ms\": " << *std::max_element(values.begin(),values.end()) << "}"
            << (comma ? ",\n" : "\n");
    }
};
LRESULT CALLBACK fixtureProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; auto dc = BeginPaint(window,&paint);
        RECT client{}; GetClientRect(window,&client); FillRect(dc,&client,GetSysColorBrush(COLOR_WINDOW));
        auto font = CreateFontW(-30,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto old = SelectObject(dc,font); SetTextColor(dc,RGB(20,30,40)); SetBkMode(dc,TRANSPARENT);
        const wchar_t* lines[] = {
            L"Visual Find benchmark fixture",
            L"algorithm algorithm ALGORITHM",
            L"binary search tree",
            L"Hello World",
            L"A wrapped phrase says hello",
            L"world on the next line",
            L"0123456789 local screen search",
            L"No screenshot or recognized desktop text is saved."
        };
        for (int i = 0; i < 8; ++i) TextOutW(dc,60,60+i*65,lines[i],int(wcslen(lines[i])));
        const wchar_t* marker = L"capture exclusion marker";
        TextOutW(dc,client.right-500,50,marker,int(wcslen(marker)));
        SelectObject(dc,old); DeleteObject(font);
        RECT desktopLabel{50,610,250,670};
        auto desktopBrush = CreateSolidBrush(RGB(35,60,88)); FillRect(dc,&desktopLabel,desktopBrush); DeleteObject(desktopBrush);
        font = CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        old = SelectObject(dc,font);
        SetTextColor(dc,RGB(0,0,0)); TextOutW(dc,71,626,L"MinGW Installer",15);
        SetTextColor(dc,RGB(255,255,255)); TextOutW(dc,70,625,L"MinGW Installer",15);
        SelectObject(dc,old); DeleteObject(font); EndPaint(window,&paint); return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void wait(double milliseconds) {
    const auto start = vf::Clock::now();
    while (vf::milliseconds(start) < milliseconds) {
        MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT); pump();
    }
}
struct Child {
    PROCESS_INFORMATION process{};
    Child(const std::filesystem::path& executable, const std::wstring& args) {
        std::wstring command = L"\"" + executable.wstring() + L"\" " + args;
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        if (!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))
            throw vf::winError("Cannot start application benchmark");
    }
    ~Child() {
        if (WaitForSingleObject(process.hProcess,0) == WAIT_TIMEOUT) {
            HWND window = FindWindowW(L"VisualFind.Search",nullptr);
            DWORD pid{}; if (window) GetWindowThreadProcessId(window,&pid);
            if (pid == process.dwProcessId) PostMessageW(window,WM_COMMAND,202,0);
            WaitForSingleObject(process.hProcess,5000);
        }
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
    HWND window() const {
        const HWND window = FindWindowW(L"VisualFind.Search",nullptr);
        DWORD pid{}; if (window) GetWindowThreadProcessId(window,&pid);
        return pid == process.dwProcessId ? window : nullptr;
    }
    DWORD code() const { DWORD result{}; GetExitCodeProcess(process.hProcess,&result); return result; }
};
void awaitSession(const Child& child) {
    const auto start = vf::Clock::now();
    while ((!IsWindowVisible(child.window()) || GetForegroundWindow()!=child.window()) &&
           vf::milliseconds(start)<4000) wait(10);
    require(IsWindowVisible(child.window()) && GetForegroundWindow()==child.window(),
            "Search did not become visible and focused");
}
void awaitOverlay(const Child& child) {
    SendMessageW(GetDlgItem(child.window(),100),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"algorithm"));
    const auto start = vf::Clock::now();
    while (!IsWindowVisible(FindWindowW(L"VisualFind.Overlay",nullptr)) && vf::milliseconds(start)<4000) wait(10);
    require(IsWindowVisible(FindWindowW(L"VisualFind.Overlay",nullptr)),"Matching text did not show overlay");
}
uint64_t cpuTime(HANDLE process) {
    FILETIME creation{},exit{},kernel{},user{};
    require(GetProcessTimes(process,&creation,&exit,&kernel,&user),"Cannot measure process CPU");
    ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return k.QuadPart+u.QuadPart;
}
PROCESS_MEMORY_COUNTERS_EX memory(HANDLE process) {
    PROCESS_MEMORY_COUNTERS_EX result{}; result.cb=sizeof(result);
    require(GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&result),sizeof(result)),
            "Cannot measure memory"); return result;
}
void hotkey() {
    INPUT input[6]{};
    const WORD keys[] = {VK_CONTROL,VK_SHIFT,'F','F',VK_SHIFT,VK_CONTROL};
    for (int i=0;i<6;++i) { input[i].type=INPUT_KEYBOARD; input[i].ki.wVk=keys[i];
        if (i>=3) input[i].ki.dwFlags=KEYEVENTF_KEYUP; }
    require(SendInput(6,input,sizeof(INPUT))==6,"Could not send fixture hotkey");
}
void clickFixture(const MONITORINFO& info) {
    POINT original{}; GetCursorPos(&original);
    require(SetCursorPos(info.rcMonitor.left+70,info.rcMonitor.top+130),"Cannot position fixture click");
    INPUT input[2]{}; input[0].type=input[1].type=INPUT_MOUSE;
    input[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;input[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    require(SendInput(2,input,sizeof(INPUT))==2,"Cannot click fixture");
    wait(80); SetCursorPos(original.x,original.y);
}
}

int main(int argc, char**) {
    try {
        require(argc == 1,"Run this benchmark without arguments from the project root");
        require(!FindWindowW(L"VisualFind.Search",nullptr),"Exit the existing Visual Find instance before benchmarking");
        vf::enableDpiAwareness(); winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto monitor = vf::cursorMonitor(); const auto info = vf::monitorInfo(monitor);
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc=fixtureProcedure;
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.lpszClassName=L"VisualFind.BenchmarkFixture";
        require(RegisterClassW(&cls),"Cannot register fixture");
        const HWND fixture=CreateWindowExW(WS_EX_TOPMOST,cls.lpszClassName,L"Visual Find test fixture",WS_POPUP,
            info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,
            info.rcMonitor.bottom-info.rcMonitor.top,nullptr,nullptr,cls.hInstance,nullptr);
        require(fixture,"Cannot create fixture");
        ShowWindow(fixture,SW_HIDE); // Consume a background launcher's initial show command.
        ShowWindow(fixture,SW_SHOW);
        SetWindowPos(fixture,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
        UpdateWindow(fixture); pump(); clickFixture(info); wait(100); DwmFlush();
        require(IsWindowVisible(fixture) && GetForegroundWindow()==fixture,"Fixture must be visible and focused");
        vf::Ocr ocr(L"en-US");
        Samples captures,recognitions,indexing,searches,renders,startups;
        double coldOcr{},coldCapture{};
        std::unique_ptr<vf::SearchIndex> index;
        vf::Coordinates coordinates{};
        for (int i=0;i<13;++i) {
            auto capture=vf::captureScreen(monitor,vf::Ocr::maxDimension()); coordinates=capture.coordinates;
            auto output=ocr.recognize(capture);
            const auto indexStart=vf::Clock::now(); index=std::make_unique<vf::SearchIndex>(output.words);
            const double indexMs=vf::milliseconds(indexStart);
            if (i==0) {
                std::cout << "Small shadowed MinGW fixture matches: " << index->find(L"mingw").size() << '\n';
                for (const auto& word : output.words)
                    if (word.bounds.y >= info.rcMonitor.top+610 && word.bounds.y < info.rcMonitor.top+670 &&
                        word.bounds.x >= info.rcMonitor.left+50 && word.bounds.x < info.rcMonitor.left+250)
                        std::wcout << L"Known small-label fixture OCR: " << word.text << L'\n';
            }
            auto algorithms=index->find(L"algorithm"), phrase=index->find(L"binary search"), hello=index->find(L"hello world");
            require(index->find(L"Visual Find benchmark fixture").size()==1,"Captured screen did not contain the benchmark fixture title");
            require(algorithms.size()==3,"OCR fixture: expected three algorithm matches");
            require(phrase.size()==1,"OCR fixture: phrase was not recognized");
            require(hello.size()==2,"OCR fixture: wrapped phrase was not recognized");
            require(hello[1].regions.size()==2,"Wrapped phrase should have separate highlight regions");
            require(index->find(L"not-present-in-fixture").empty(),"Zero match check failed");
            require(algorithms[0].bounds.x >= info.rcMonitor.left+50 &&
                    algorithms[0].bounds.y >= info.rcMonitor.top+110 &&
                    algorithms[0].bounds.y <= info.rcMonitor.top+150,"OCR box alignment failed");
            if (i==0) { coldCapture=capture.captureMs;coldOcr=output.ocrMs; }
            else { captures.values.push_back(capture.captureMs);recognitions.values.push_back(output.ocrMs);indexing.values.push_back(indexMs); }
            pump();
        }
        for (int i=0;i<2000;++i) {
            const auto start=vf::Clock::now();
            auto matches=index->find(i%2 ? L"algorithm" : L"binary search");
            searches.values.push_back(vf::milliseconds(start));
        }
        {
            vf::Overlay overlay; overlay.prepare(coordinates);
            auto matches=index->find(L"algorithm");
            for (int i=0;i<120;++i) renders.values.push_back(overlay.render(matches,i%3));
            overlay.release();
        }
        const auto executable=std::filesystem::absolute(L"dist/VisualFind/VisualFind.exe");
        double cpuPercent{},idleMs{}; PROCESS_MEMORY_COUNTERS_EX idleMemory{},afterMemory{};
        bool hotkeyPassed{},restorePassed{},focusPassed{},displayPassed{},cancelPassed{};
        for (int i=0;i<5;++i) {
            const auto start=vf::Clock::now(); Child child(executable,L"--quiet");
            require(WaitForInputIdle(child.process.hProcess,10000)==0,"App did not become input-ready");
            startups.values.push_back(vf::milliseconds(start));
            require(child.window(),"App HWND is missing");
            if (i==4) {
                wait(200); const auto cpuStart=cpuTime(child.process.hProcess); const auto startIdle=vf::Clock::now();
                wait(6000); idleMs=vf::milliseconds(startIdle);
                cpuPercent=(cpuTime(child.process.hProcess)-cpuStart)/10000.0/idleMs*100.0;
                idleMemory=memory(child.process.hProcess);
                clickFixture(info); wait(100); hotkey();
                awaitSession(child);
                hotkeyPassed=GetForegroundWindow()==child.window();
                const HWND edit=GetDlgItem(child.window(),100);
                awaitOverlay(child);
                DWORD affinity{};
                require(GetWindowDisplayAffinity(child.window(),&affinity) && affinity==0,
                        "Search bar must be available to normal screenshots after capture");
                require(GetWindowDisplayAffinity(FindWindowW(L"VisualFind.Overlay",nullptr),&affinity) && affinity==0,
                        "Highlights must be available to normal screenshots");
                RECT bar{}; GetWindowRect(child.window(),&bar); DwmFlush();
                HDC screenshot=GetDC(nullptr);
                const UINT barDpi=GetDpiForWindow(child.window());
                const auto barPixel=GetPixel(screenshot,bar.left+MulDiv(40,barDpi,96),bar.top+MulDiv(55,barDpi,96));
                ReleaseDC(nullptr,screenshot);
                if (barPixel!=RGB(255,255,255))
                    std::cout << "Screenshot bar pixel: " << barPixel << "; DPI: " << barDpi << '\n';
                require(barPixel==RGB(255,255,255),"Search bar background should capture white, not black");
                const HWND under=WindowFromPoint(POINT{info.rcMonitor.left+70,info.rcMonitor.top+130});
                require(under==fixture,"Overlay should allow mouse input through to the fixture");
                SendMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0); wait(50);
                restorePassed=!IsWindowVisible(child.window())&&GetForegroundWindow()==fixture;
                hotkey(); awaitSession(child); awaitOverlay(child); clickFixture(info); wait(80);
                focusPassed=!IsWindowVisible(child.window()) && GetForegroundWindow()==fixture &&
                            !IsWindowVisible(FindWindowW(L"VisualFind.Overlay",nullptr));
                hotkey(); awaitSession(child); awaitOverlay(child);
                PostMessageW(child.window(),WM_DISPLAYCHANGE,32,0); wait(80);
                displayPassed=!IsWindowVisible(child.window()) && !IsWindowVisible(FindWindowW(L"VisualFind.Overlay",nullptr));
                hotkey(); awaitSession(child); PostMessageW(child.window(),WM_CLOSE,0,0); wait(3000);
                cancelPassed=!IsWindowVisible(child.window())&&!IsWindowVisible(FindWindowW(L"VisualFind.Overlay",nullptr));
                afterMemory=memory(child.process.hProcess);
            }
            PostMessageW(child.window(),WM_COMMAND,202,0);
            require(WaitForSingleObject(child.process.hProcess,5000)==WAIT_OBJECT_0,"App failed to exit");
            require(child.code()==0,"App returned a failing exit code");
        }
        std::cout << "Lifecycle: hotkey=" << hotkeyPassed << " restore=" << restorePassed
                  << " focus=" << focusPassed << " display=" << displayPassed << " cancel=" << cancelPassed << '\n';
        require(hotkeyPassed&&restorePassed&&focusPassed&&displayPassed&&cancelPassed,"App lifecycle check failed");
        SetForegroundWindow(fixture); wait(100);
        const auto smokePath=std::filesystem::absolute(L"docs/app-smoke.json");
        {
            Child child(executable,L"--smoke-test \""+smokePath.wstring()+L"\"");
            const auto start=vf::Clock::now();
            while (child.code()==STILL_ACTIVE && vf::milliseconds(start)<20000) wait(20);
            require(child.code()==0,"App smoke test failed; inspect docs/app-smoke.json");
        }
        std::ofstream out("docs/benchmark-results.json"); out<<std::fixed<<std::setprecision(6);
        out<<"{\n  \"fixture_verified\": true,\n  \"resolution\": ["<<coordinates.screenWidth<<", "<<coordinates.screenHeight
           <<"],\n  \"monitor_origin\": ["<<coordinates.left<<", "<<coordinates.top
           <<"],\n  \"ocr_language\": \"en-US\",\n  \"ocr_words\": "<<index->wordCount()
           <<",\n  \"cold_capture_ms\": "<<coldCapture<<",\n  \"cold_ocr_ms\": "<<coldOcr<<",\n";
        captures.write(out,"capture");recognitions.write(out,"ocr");indexing.write(out,"index");
        searches.write(out,"cached_search");renders.write(out,"overlay_submit");startups.write(out,"startup_to_input_idle");
        out<<"  \"idle_sample_ms\": "<<idleMs<<",\n  \"idle_cpu_one_core_percent\": "<<cpuPercent
           <<",\n  \"idle_working_set_mib\": "<<idleMemory.WorkingSetSize/1048576.0
           <<",\n  \"idle_private_mib\": "<<idleMemory.PrivateUsage/1048576.0
           <<",\n  \"after_session_working_set_mib\": "<<afterMemory.WorkingSetSize/1048576.0
           <<",\n  \"after_session_private_mib\": "<<afterMemory.PrivateUsage/1048576.0
           <<",\n  \"global_hotkey_over_fixture\": true,\n  \"escape_restores_focus\": true,\n"
             "  \"overlay_click_through\": true,\n  \"focus_loss_closes\": true,\n"
             "  \"display_change_closes\": true,\n  \"cancel_discards_stale_ocr\": true\n}\n";
        out.close(); DestroyWindow(fixture);
        std::cout<<"PASS local OCR fixture, phrases, bounding boxes, transparent overlay, global hotkey, focus, display change, cancel, refresh, cached search, and Escape.\n"
                 <<"Capture median "<<captures.median()<<" ms; OCR "<<recognitions.median()<<" ms; cached search "<<searches.median()
                 <<" ms; overlay submit "<<renders.median()<<" ms; idle CPU "<<cpuPercent<<"%.\nResults: docs/benchmark-results.json and docs/app-smoke.json\n";
        return 0;
    } catch (const winrt::hresult_error& e) { std::wcerr<<L"FAIL "<<e.message().c_str()<<L'\n';return 1; }
    catch (const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
