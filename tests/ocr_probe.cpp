#include "../src/ocr.hpp"
#include <algorithm>
#include <iostream>
// Usage: ocr-probe [scale] [engines]. Times capture and OCR of the monitor under the cursor over
// several runs (the first run is a warm-up) without ever printing recognized text.
int main(int argc, char** argv) {
    try {
        const double scale = argc > 1 ? atof(argv[1]) : 1.0;
        const size_t engines = argc > 2 ? size_t(atoi(argv[2])) : 4;
        vf::enableDpiAwareness();
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::cout << "Max image dimension: " << vf::Ocr::maxDimension() << ", engines: " << engines << '\n';
        vf::Ocr ocr(L"", engines);
        std::vector<double> times;
        size_t words = 0;
        for (int run = 0; run < 6; ++run) {
            auto capture = vf::captureScreen(vf::cursorMonitor(), vf::Ocr::maxDimension(), scale);
            auto result = ocr.recognize(capture);
            words = result.words.size();
            if (run > 0) times.push_back(result.ocrMs);
        }
        std::sort(times.begin(), times.end());
        std::cout << "OCR median " << times[times.size() / 2] << " ms (min " << times.front() << ", max "
                  << times.back() << "), words: " << words << '\n';
        return 0;
    } catch (const winrt::hresult_error& e) {
        std::wcerr << "OCR failed: " << e.message().c_str() << '\n'; return 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
