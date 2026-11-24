#include "../src/ocr.hpp"
#include <iostream>
int main(int argc, char** argv) {
    const double scale = argc > 1 ? atof(argv[1]) : 1.0;
    try {
        vf::enableDpiAwareness();
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::cout << "Max image dimension: " << vf::Ocr::maxDimension() << '\n';
        for (auto language : winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages())
            std::wcout << "OCR language: " << language.LanguageTag().c_str() << '\n';
        vf::Ocr ocr;
        auto capture = vf::captureScreen(vf::cursorMonitor(), vf::Ocr::maxDimension(), scale);
        auto result = ocr.recognize(capture);
        std::cout << "Capture ms: " << capture.captureMs << ", OCR ms: " << result.ocrMs
                  << ", words: " << result.words.size() << '\n';
        // Deliberately never prints recognized screen text.
        return 0;
    } catch (const winrt::hresult_error& e) {
        std::wcerr << "OCR failed: " << e.message().c_str() << '\n'; return 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
