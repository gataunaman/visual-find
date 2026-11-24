#include "../src/desktop.hpp"
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>

void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::wstring countText(HWND window) {
    HWND child{};
    while ((child=FindWindowExW(window,child,L"Static",nullptr))) {
        if ((GetWindowLongPtrW(child,GWL_STYLE)&SS_TYPEMASK)==SS_CENTER) {
            wchar_t text[128]{}; GetWindowTextW(child,text,128); return text;
        }
    }
    return {};
}
int main() {
    PROCESS_INFORMATION process{};
    HWND window{};
    try {
        vf::enableDpiAwareness(); CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        require(!FindWindowW(L"VisualFind.Search",nullptr),"Exit Visual Find before desktop validation");
        const auto monitor=vf::cursorMonitor(); const auto info=vf::monitorInfo(monitor);
        vf::Coordinates coordinates{info.rcMonitor.left,info.rcMonitor.top,
            info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top,1,1};
        const auto start=vf::Clock::now(); auto labels=vf::desktopLabels(coordinates);
        const double labelMs=vf::milliseconds(start);
        if (!vf::isDesktopWindow(GetForegroundWindow())) {
            POINT original{}; GetCursorPos(&original);
            POINT blank{}; bool found{};
            for (int y=info.rcMonitor.top+32; y<info.rcMonitor.bottom-32 && !found; y+=64)
                for (int x=info.rcMonitor.right-32; x>info.rcMonitor.left+32 && !found; x-=64) {
                    const bool insideLabel=std::any_of(labels.begin(),labels.end(),[&](const vf::Word& word) {
                        const auto& b=word.bounds; return x>=b.x && x<b.x+b.width && y>=b.y && y<b.y+b.height;
                    });
                    if (!insideLabel && vf::isDesktopWindow(WindowFromPoint(POINT{x,y}))) { blank={x,y}; found=true; }
                }
            require(found,"A visible empty desktop area is required for safe focus");
            require(SetCursorPos(blank.x,blank.y),"Cannot position safe desktop focus click");
            INPUT mouse[2]{}; mouse[0].type=mouse[1].type=INPUT_MOUSE;
            mouse[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; mouse[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
            require(SendInput(2,mouse,sizeof(INPUT))==2,"Cannot focus desktop fixture");
            Sleep(80); SetCursorPos(original.x,original.y);
        }
        require(vf::isDesktopWindow(GetForegroundWindow()),"Desktop must be focused before this test");
        const auto matches=vf::SearchIndex(labels).find(L"mingw");
        require(matches.size()==1,"Expected one visible MinGW desktop shortcut on the cursor monitor");
        const auto expected=matches.front().bounds;
        const auto executable=std::filesystem::absolute(L"dist/VisualFind/VisualFind.exe");
        std::wstring command=L"\""+executable.wstring()+L"\" --quiet";
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
        require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process),"Cannot start updated app");
        require(WaitForInputIdle(process.hProcess,10000)==0,"App startup timed out");
        window=FindWindowW(L"VisualFind.Search",nullptr); require(window,"App window missing");
        INPUT keys[6]{}; const WORD codes[]={VK_CONTROL,VK_SHIFT,'F','F',VK_SHIFT,VK_CONTROL};
        for (int i=0;i<6;++i) { keys[i].type=INPUT_KEYBOARD; keys[i].ki.wVk=codes[i]; if(i>=3)keys[i].ki.dwFlags=KEYEVENTF_KEYUP; }
        require(SendInput(6,keys,sizeof(INPUT))==6,"Cannot deliver desktop hotkey");
        auto deadline=vf::Clock::now();
        while (!IsWindowVisible(window) && vf::milliseconds(deadline)<3000) Sleep(10);
        require(IsWindowVisible(window),"Desktop hotkey did not open search");
        SendMessageW(GetDlgItem(window,100),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"mingw"));
        deadline=vf::Clock::now();
        while (countText(window)!=L"1 / 1" && vf::milliseconds(deadline)<10000) Sleep(20);
        require(countText(window)==L"1 / 1","Production desktop query did not find MinGW");
        HWND overlay=FindWindowW(L"VisualFind.Overlay",nullptr); require(IsWindowVisible(overlay),"Desktop highlight missing");
        RECT region{}; GetWindowRect(overlay,&region);
        require(region.left<=expected.x && region.top<=expected.y && region.right>=expected.x+expected.width &&
                region.bottom>=expected.y+expected.height && region.right-region.left<=expected.width+8 &&
                region.bottom-region.top<=expected.height+8,"Desktop overlay does not align with the actual shortcut");
        DWORD affinity{};
        require(GetWindowDisplayAffinity(window,&affinity) && affinity==0,"Search bar remains excluded from screenshots");
        require(GetWindowDisplayAffinity(overlay,&affinity) && affinity==0,"Highlight remains excluded from screenshots");
        require(GetClassLongPtrW(window,GCLP_HICON)!=0,"Custom executable/window icon missing");
        RECT bar{}; GetWindowRect(window,&bar); const UINT dpi=GetDpiForWindow(window); DwmFlush();
        HDC screen=GetDC(nullptr); const COLORREF pixel=GetPixel(screen,bar.left+MulDiv(40,dpi,96),bar.top+MulDiv(55,dpi,96));
        ReleaseDC(nullptr,screen); require(pixel==RGB(255,255,255),"Search bar captures black instead of white");
        SendMessageW(GetDlgItem(window,100),WM_KEYDOWN,VK_F5,0);
        deadline=vf::Clock::now();
        while (countText(window)!=L"1 / 1" && vf::milliseconds(deadline)<10000) Sleep(20);
        require(countText(window)==L"1 / 1","Desktop refresh lost MinGW match");
        PostMessageW(window,WM_COMMAND,202,0);
        require(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"App did not exit cleanly");
        std::ofstream report("docs/desktop-validation.json");
        report<<"{\n  \"desktop_mingw_matches\": 1,\n  \"refresh_preserves_match\": true,\n"
                 "  \"shortcut_bounds_physical\": true,\n  \"screenshots_include_search_bar\": true,\n"
                 "  \"screenshots_include_highlights\": true,\n  \"custom_window_icon\": true,\n"
              <<"  \"desktop_label_read_ms\": "<<labelMs<<"\n}\n";
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CoUninitialize();
        std::cout<<"PASS actual MinGW desktop shortcut, bounds, global hotkey, refresh, normal screenshot bar/highlights, and custom icon. Label read "<<labelMs<<" ms.\n";
        return 0;
    } catch (const std::exception& e) {
        if (window) PostMessageW(window,WM_COMMAND,202,0);
        if (process.hProcess) { WaitForSingleObject(process.hProcess,5000); CloseHandle(process.hProcess); CloseHandle(process.hThread); }
        std::cerr<<"FAIL "<<e.what()<<'\n'; return 1;
    }
}
