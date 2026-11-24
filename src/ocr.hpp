#pragma once
#include "platform.hpp"
#include <thread>
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
    std::vector<winrt::Windows::Media::Ocr::OcrEngine> engines_;
    std::wstring requestedLanguage_;
    size_t maxEngines_;
    // Copies rows [from, to) of a capture into a SoftwareBitmap: one memory copy, no encoding.
    static winrt::Windows::Graphics::Imaging::SoftwareBitmap copyRows(const Bitmap& source, int from, int to) {
        using namespace winrt::Windows::Graphics::Imaging;
        const int rows = to - from;
        SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, source.width, rows, BitmapAlphaMode::Ignore);
        auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
        auto reference = buffer.CreateReference();
        auto access = reference.as<MemoryBufferByteAccess>();
        uint8_t* bytes{}; uint32_t capacity{};
        winrt::check_hresult(access->GetBuffer(&bytes, &capacity));
        const auto plane = buffer.GetPlaneDescription(0);
        const size_t rowBytes = size_t(source.width) * 4;
        if (plane.Stride < int(rowBytes) || plane.StartIndex < 0 ||
            size_t(plane.StartIndex) + size_t(plane.Stride) * (rows - 1) + rowBytes > capacity)
            throw std::runtime_error("Unexpected OCR buffer layout");
        for (int y = 0; y < rows; ++y)
            memcpy(bytes + plane.StartIndex + size_t(y) * plane.Stride,
                   source.pixels + size_t(from + y) * rowBytes, rowBytes);
        return bitmap;
    }
public:
    explicit Ocr(std::wstring language = {}, size_t maxEngines = 4)
        : requestedLanguage_(std::move(language)), maxEngines_(std::max<size_t>(1, maxEngines)) {}
    static unsigned maxDimension() {
        return winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension();
    }
    void initialize() {
        if (!engines_.empty()) return;
        using namespace winrt::Windows::Media::Ocr;
        OcrEngine engine{nullptr};
        if (!requestedLanguage_.empty()) {
            engine = OcrEngine::TryCreateFromLanguage(
                winrt::Windows::Globalization::Language(requestedLanguage_));
            if (!engine) throw std::runtime_error("Requested OCR language is not installed");
        } else {
            engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                const auto languages = OcrEngine::AvailableRecognizerLanguages();
                if (languages.Size()) engine = OcrEngine::TryCreateFromLanguage(languages.GetAt(0));
            }
        }
        if (!engine) throw std::runtime_error(
            "No Windows OCR language installed. Add an OCR language in Windows Settings, then retry.");
        engines_.push_back(engine);
        // Extra engines share the loaded model (no measurable memory or start-up cost) and let
        // several strips of one screenshot be recognized at the same time.
        const size_t wanted = std::clamp<size_t>(std::thread::hardware_concurrency() / 2, 1, maxEngines_);
        while (engines_.size() < wanted) {
            auto extra = OcrEngine::TryCreateFromLanguage(engine.RecognizerLanguage());
            if (!extra) break;
            engines_.push_back(extra);
        }
    }
    OcrOutput recognize(Capture& capture) {
        const auto start = Clock::now();
        initialize();
        using namespace winrt::Windows::Media::Ocr;
        // Independent engines read horizontal strips at once. Strips overlap so no text line is cut
        // in half; each word is kept by the strip that holds its vertical centre.
        constexpr int overlap = 64, minimumStrip = 270;
        const Bitmap& source = *capture.bitmap;
        const int strips = int(std::clamp<size_t>(size_t(source.height / minimumStrip), 1, engines_.size()));
        struct Strip {
            int top{}, bottom{}, from{}, to{};
            winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap{nullptr};
            winrt::Windows::Foundation::IAsyncOperation<OcrResult> operation{nullptr};
        };
        std::vector<Strip> parts;
        parts.resize(size_t(strips));
        for (int i = 0; i < strips; ++i) {
            auto& part = parts[size_t(i)];
            part.top = int(int64_t(source.height) * i / strips);
            part.bottom = int(int64_t(source.height) * (i + 1) / strips);
            part.from = i == 0 ? 0 : std::max(0, part.top - overlap);
            part.to = i + 1 == strips ? source.height : std::min(source.height, part.bottom + overlap);
            part.bitmap = copyRows(source, part.from, part.to);
            part.operation = engines_[size_t(i)].RecognizeAsync(part.bitmap);  // starts immediately
        }
        const auto coordinates = capture.coordinates;
        // This capture's pixels are no longer needed here (other holders keep theirs).
        capture.bitmap.reset();
        OcrOutput output;
        output.language = engines_[0].RecognizerLanguage().LanguageTag().c_str();
        unsigned lineBase{};
        for (auto& part : parts) {
            const auto result = part.operation.get();
            part.bitmap.Close();
            unsigned lines{};
            for (const auto& line : result.Lines()) {
                for (const auto& word : line.Words()) {
                    const auto b = word.BoundingRect();
                    const double centre = part.from + b.Y + b.Height / 2.0;
                    if (centre < part.top || centre >= part.bottom) continue;
                    output.words.push_back({std::wstring(word.Text()),
                        coordinates.desktop({b.X, part.from + b.Y, b.Width, b.Height}), lineBase + lines});
                }
                ++lines;
            }
            lineBase += lines;
        }
        output.ocrMs = milliseconds(start);
        return output;
    }
};
}
