#include "../src/ocr.hpp"
#include <iostream>
int main(int argc, char** argv) {
    try {
        vf::enableDpiAwareness();
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        HWND target=GetForegroundWindow();
        bool settingsMode = false;
        for (int i=1;i<argc;++i) {
            const std::string argument(argv[i]);
            if (argument == "--settings") settingsMode = true;
            else if (argument == "--window" && i+1<argc)
                target=reinterpret_cast<HWND>(static_cast<UINT_PTR>(std::stoull(argv[++i])));
            else throw std::runtime_error("Use --window HANDLE and/or --settings");
        }
        if (!IsWindow(target) || !IsWindowVisible(target))
            throw std::runtime_error("Test fixture window is not visible");
        RECT rectangle{}; GetWindowRect(target,&rectangle);
        POINT center{(rectangle.left+rectangle.right)/2,(rectangle.top+rectangle.bottom)/2};
        HWND visible=WindowFromPoint(center);
        if (GetAncestor(visible,GA_ROOT) != GetAncestor(target,GA_ROOT) && !IsChild(target,visible))
            throw std::runtime_error("Test fixture is covered by another window");
        wchar_t title[512]{}; GetWindowTextW(target,title,512);
        bool settingsProcess = false;
        if (settingsMode) {
            DWORD pid{}; GetWindowThreadProcessId(target,&pid);
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if (process) {
                wchar_t path[32768]{}; DWORD size = 32768;
                if (QueryFullProcessImageNameW(process,0,path,&size))
                    settingsProcess = wcsstr(path,L"\\SystemSettings.exe") != nullptr ||
                                      (wcsstr(path,L"\\ApplicationFrameHost.exe") != nullptr && wcsstr(title,L"Settings"));
                CloseHandle(process);
            }
        }
        if (settingsMode && !settingsProcess)
            throw std::runtime_error("Target is not the Windows Settings process");
        if (!wcsstr(title,L"Visual Find fixture") && !wcsstr(title,L"VisualFind-fixture.txt") &&
            !wcsstr(title,L"fixture.pdf") && !wcsstr(title,L"Settings") && !settingsProcess)
            throw std::runtime_error("Target window is not an authorized test fixture");
        vf::Ocr ocr(L"en-US");
        auto capture=vf::captureScreen(MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST),vf::Ocr::maxDimension());
        auto result=ocr.recognize(capture); vf::SearchIndex index(result.words);
        const auto algorithms=index.find(L"algorithm"), phrase=index.find(L"binary search"), hello=index.find(L"hello world");
        const bool settings = settingsProcess || wcsstr(title,L"Settings") != nullptr;
        const size_t settingsTerms=index.find(L"display").size()+index.find(L"system").size()+
                                    index.find(L"settings").size()+index.find(L"scale").size();
        const bool passed=settings ? index.wordCount()>0 && settingsTerms>0 :
                                     algorithms.size()>=3 && !phrase.empty() && !hello.empty();
        std::cout<<"{\"passed\":"<<(passed?"true":"false")<<",\"capture_ms\":"<<capture.captureMs
                 <<",\"ocr_ms\":"<<result.ocrMs<<",\"word_count\":"<<index.wordCount()
                 <<",\"algorithm_matches\":"<<algorithms.size()<<",\"phrase_matches\":"<<phrase.size()
                 <<",\"hello_matches\":"<<hello.size()<<",\"settings_text_only\":"<<(settings?"true":"false")
                 <<",\"settings_term_matches\":"<<settingsTerms<<"}\n";
        return passed?0:1;
    } catch (const winrt::hresult_error& e) { std::wcerr<<e.message().c_str()<<'\n';return 1; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
