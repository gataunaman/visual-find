// Measures the accessibility text path on one window: how many text runs it reports, how long that
// takes, and how many words land on detected lines. Usage: text-probe "title substring" [--show N]
// Prints counts and timings only; --show prints N sample runs (use on your own test pages).
#include "../src/accessible.hpp"
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    try {
        vf::enableDpiAwareness();
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const std::string needle = argc > 1 ? argv[1] : "";
        int show = 0;
        std::string dump;
        for (int i = 2; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--show") show = atoi(argv[i + 1]);
            if (std::string(argv[i]) == "--dump") dump = argv[i + 1];  // BMP with the computed word boxes drawn on it
        }
        struct Search { std::string needle; HWND found{}; } search{needle};
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(data);
            if (!IsWindowVisible(window)) return TRUE;
            char title[256]{};
            GetWindowTextA(window, title, 255);
            if (!s.needle.empty() && std::string(title).find(s.needle) != std::string::npos) { s.found = window; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        const HWND root = search.found ? search.found : GetForegroundWindow();
        if (!root) { std::cerr << "No window\n"; return 1; }
        for (int i = 2; i < argc; ++i)
            if (std::string(argv[i]) == "--front") {  // raise a covered test window so its text is visible
                SetWindowPos(root, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
        const HMONITOR monitor = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
        const RECT area = vf::monitorInfo(monitor).rcMonitor;
        std::cout << "Browser window: " << (vf::isBrowserWindow(root) ? "yes" : "no")
                  << ", avoided: " << (vf::avoidsAccessibility(root) ? "yes" : "no") << '\n';
        auto start = vf::Clock::now();
        vf::AccessibleText reader;
        std::cout << "Create UI Automation client: " << vf::milliseconds(start) << " ms\n";
        vf::PageRead page;
        for (int run = 1; run <= 5; ++run) {
            start = vf::Clock::now();
            page = reader.read(root, area, {root});
            std::cout << "read " << run << ": " << vf::milliseconds(start) << " ms, " << page.nodes.size()
                      << " runs, page ready: " << (page.ready ? "yes" : "no") << '\n';
            if (page.ready && run >= 2) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        start = vf::Clock::now();
        auto capture = vf::captureScreen(monitor, 10000);
        const double captureMs = vf::milliseconds(start);
        start = vf::Clock::now();
        const auto exact = vf::layoutText(page.nodes, &capture.coordinates, capture.bitmap.get());
        const double layoutMs = vf::milliseconds(start);
        size_t multi{};
        unsigned last = ~0u, lines{};
        for (const auto& word : exact.words) if (word.line != last) { last = word.line; ++lines; }
        for (const auto& node : page.nodes) multi += node.rect.height > 14 ? 1 : 0;
        std::cout << "capture " << captureMs << " ms; layout " << layoutMs << " ms: " << exact.words.size()
                  << " words on " << lines << " lines from " << page.nodes.size() << " runs (" << multi
                  << " taller than 14 px)\n";
        if (!dump.empty()) {
            const HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
            for (const auto& word : exact.words) {
                const auto& c = capture.coordinates;
                RECT r{int(word.bounds.x - c.left), int(word.bounds.y - c.top),
                       int(word.bounds.x + word.bounds.width - c.left), int(word.bounds.y + word.bounds.height - c.top)};
                FrameRect(capture.bitmap->dc, &r, red);
            }
            DeleteObject(red);
            BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
            const DWORD bytes = DWORD(capture.bitmap->width) * capture.bitmap->height * 4;
            file.bfType = 0x4D42; file.bfOffBits = sizeof(file) + sizeof(info); file.bfSize = file.bfOffBits + bytes;
            info.biSize = sizeof(info); info.biWidth = capture.bitmap->width; info.biHeight = -capture.bitmap->height;
            info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
            FILE* out = fopen(dump.c_str(), "wb");
            if (out) { fwrite(&file, sizeof(file), 1, out); fwrite(&info, sizeof(info), 1, out);
                       fwrite(capture.bitmap->pixels, 1, bytes, out); fclose(out); }
        }
        vf::SearchIndex index(exact.words);
        start = vf::Clock::now();
        const auto hits = index.find(L"the");
        std::cout << "index search 'the': " << hits.size() << " matches in " << vf::milliseconds(start) << " ms\n";
        for (int i = 0; i < show && size_t(i) < page.nodes.size(); ++i) {
            const auto& n = page.nodes[size_t(i)];
            std::wcout << L"  [" << int(n.rect.x) << L"," << int(n.rect.y) << L" " << int(n.rect.width) << L"x"
                       << int(n.rect.height) << L"] " << n.text.substr(0, 60) << L'\n';
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
