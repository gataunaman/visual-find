#pragma once
#include "platform.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

namespace vf {
// Documented SoftwareBitmap interop interface; no image encoding or disk I/O.
#if defined(_MSC_VER)
struct __declspec(uuid("5B0D3235-4DBA-4D44-865E-8F1D0E4FD04D"))
#else
struct
#endif
    MemoryBufferByteAccess : ::IUnknown {
    virtual HRESULT __stdcall GetBuffer(uint8_t** value, uint32_t* capacity) = 0;
};
}
#if defined(__GNUC__)
__CRT_UUID_DECL(vf::MemoryBufferByteAccess, 0x5b0d3235, 0x4dba, 0x4d44,
               0x86, 0x5e, 0x8f, 0x1d, 0x0e, 0x4f, 0xd0, 0x4d)
#endif
namespace vf {
struct OcrOutput {
    std::vector<Word> words;
    std::wstring language;
    double ocrMs{};
};
class Ocr {
    winrt::Windows::Media::Ocr::OcrEngine engine_{nullptr};
    std::wstring requestedLanguage_;
public:
    explicit Ocr(std::wstring language = {}) : requestedLanguage_(std::move(language)) {}
    static unsigned maxDimension() {
        return winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension();
    }
    void initialize() {
        if (engine_) return;
        using namespace winrt::Windows::Media::Ocr;
        if (!requestedLanguage_.empty()) {
            engine_ = OcrEngine::TryCreateFromLanguage(
                winrt::Windows::Globalization::Language(requestedLanguage_));
            if (!engine_) throw std::runtime_error("Requested OCR language is not installed");
        } else {
            engine_ = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine_) {
                const auto languages = OcrEngine::AvailableRecognizerLanguages();
                if (languages.Size()) engine_ = OcrEngine::TryCreateFromLanguage(languages.GetAt(0));
            }
        }
        if (!engine_) throw std::runtime_error(
            "No Windows OCR language installed. Add an OCR language in Windows Settings, then retry.");
    }
    OcrOutput recognize(Capture& capture) {
        const auto start = Clock::now();
        initialize();
        using namespace winrt::Windows::Graphics::Imaging;
        SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, capture.bitmap->width,
                              capture.bitmap->height, BitmapAlphaMode::Ignore);
        {
            auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
            auto reference = buffer.CreateReference();
            auto access = reference.as<MemoryBufferByteAccess>();
            uint8_t* bytes{}; uint32_t capacity{};
            winrt::check_hresult(access->GetBuffer(&bytes, &capacity));
            const auto plane = buffer.GetPlaneDescription(0);
            const size_t rowBytes = size_t(capture.bitmap->width) * 4;
            if (plane.Stride < int(rowBytes) || plane.StartIndex < 0 ||
                size_t(plane.StartIndex) + size_t(plane.Stride) * (capture.bitmap->height - 1) + rowBytes > capacity)
                throw std::runtime_error("Unexpected OCR buffer layout");
            for (int y = 0; y < capture.bitmap->height; ++y)
                memcpy(bytes + plane.StartIndex + size_t(y) * plane.Stride,
                       capture.bitmap->pixels + size_t(y) * rowBytes, rowBytes);
        }
        // The screenshot's GDI buffer is no longer needed after the one memory copy.
        capture.bitmap.reset();
        auto result = engine_.RecognizeAsync(bitmap).get();
        bitmap.Close();
        OcrOutput output;
        output.language = engine_.RecognizerLanguage().LanguageTag().c_str();
        unsigned lineNumber = 0;
        for (const auto& line : result.Lines()) {
            for (const auto& word : line.Words()) {
                const auto b = word.BoundingRect();
                output.words.push_back({std::wstring(word.Text()),
                    capture.coordinates.desktop({b.X, b.Y, b.Width, b.Height}), lineNumber});
            }
            ++lineNumber;
        }
        output.ocrMs = milliseconds(start);
        return output;
    }
};
}
