#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vf {
struct Box {
    double x{}, y{}, width{}, height{};
    bool operator==(const Box&) const = default;
};
inline Box unite(Box a, Box b) {
    const double right = std::max(a.x + a.width, b.x + b.width);
    const double bottom = std::max(a.y + a.height, b.y + b.height);
    a.x = std::min(a.x, b.x); a.y = std::min(a.y, b.y);
    a.width = right - a.x; a.height = bottom - a.y;
    return a;
}
struct Coordinates {
    int left{}, top{}, screenWidth{}, screenHeight{}, imageWidth{}, imageHeight{};
    Box desktop(Box b) const {
        const double sx = double(screenWidth) / imageWidth;
        const double sy = double(screenHeight) / imageHeight;
        return {left + b.x * sx, top + b.y * sy, b.width * sx, b.height * sy};
    }
};
struct Word { std::wstring text; Box bounds; unsigned line{}; };
struct Match { Box bounds; std::vector<Box> regions; };

inline bool whitespace(wchar_t c) {
    WORD type{};
    return GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & C1_SPACE);
}
inline std::wstring normalize(const std::wstring& input) {
    if (input.empty()) return {};
    int size = NormalizeString(NormalizationKC, input.data(), int(input.size()), nullptr, 0);
    std::wstring normalized(size > 0 ? size : input.size(), L'\0');
    int written = size > 0 ? NormalizeString(NormalizationKC, input.data(), int(input.size()),
                                             normalized.data(), int(normalized.size())) : 0;
    if (written > 0) normalized.resize(written); else normalized = input;
    size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, normalized.data(),
                         int(normalized.size()), nullptr, 0, nullptr, nullptr, 0);
    if (size > 0) {
        std::wstring lower(size, L'\0');
        if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, normalized.data(),
                          int(normalized.size()), lower.data(), size, nullptr, nullptr, 0))
            normalized = std::move(lower);
    }
    std::wstring result;
    result.reserve(normalized.size());
    bool pendingSpace = false;
    for (wchar_t c : normalized) {
        if (whitespace(c)) { pendingSpace = !result.empty(); continue; }
        if (pendingSpace) result += L' ';
        pendingSpace = false; result += c;
    }
    return result;
}

// Text supplied exactly by the application itself (accessibility) or by Explorer (desktop icons).
// OCR words inside a `covered` rectangle are replaced by `words`.
struct ExactText {
    std::vector<Word> words;
    std::vector<Box> covered;
};
inline void mergeExactText(std::vector<Word>& words, const ExactText& exact) {
    if (exact.words.empty()) return;
    unsigned base{};
    for (const auto& word : words) base = std::max(base, word.line + 1);
    if (!exact.covered.empty())
        words.erase(std::remove_if(words.begin(), words.end(), [&](const Word& word) {
            const double x = word.bounds.x + word.bounds.width / 2, y = word.bounds.y + word.bounds.height / 2;
            return std::any_of(exact.covered.begin(), exact.covered.end(), [&](const Box& b) {
                return x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height;
            });
        }), words.end());
    for (const auto& word : exact.words) {
        words.push_back(word);
        words.back().line += base;
    }
}
// Exact labels (each its own line) that replace whatever OCR read inside their bounds.
inline ExactText exactLabels(std::vector<Word> labels) {
    ExactText exact;
    unsigned line{};
    for (auto& label : labels) { exact.covered.push_back(label.bounds); label.line = line++; }
    exact.words = std::move(labels);
    return exact;
}
inline void mergeDesktopLabels(std::vector<Word>& words, std::vector<Word> labels) {
    mergeExactText(words, exactLabels(std::move(labels)));
}

class SearchIndex {
    struct Span { size_t begin, end; Box box; unsigned line; };
    std::wstring text_;
    std::vector<Span> spans_;
public:
    SearchIndex() = default;
    explicit SearchIndex(const std::vector<Word>& words) {
        spans_.reserve(words.size());
        for (const auto& word : words) {
            auto text = normalize(word.text);
            if (text.empty()) continue;
            if (!text_.empty()) text_ += L' ';
            const size_t begin = text_.size();
            text_ += text;
            spans_.push_back({begin, text_.size(), word.bounds, word.line});
        }
    }
    size_t wordCount() const { return spans_.size(); }
    std::vector<Match> find(const std::wstring& query) const {
        const auto needle = normalize(query);
        std::vector<Match> results;
        if (needle.empty()) return results;
        size_t pos = 0;
        while ((pos = text_.find(needle, pos)) != std::wstring::npos) {
            const size_t end = pos + needle.size();
            auto first = std::lower_bound(spans_.begin(), spans_.end(), pos,
                [](const Span& span, size_t offset) { return span.end <= offset; });
            Match match{};
            unsigned line = 0;
            for (auto it = first; it != spans_.end() && it->begin < end; ++it) {
                if (match.regions.empty()) { match.bounds = it->box; line = it->line; }
                else match.bounds = unite(match.bounds, it->box);
                if (!match.regions.empty() && line == it->line)
                    match.regions.back() = unite(match.regions.back(), it->box);
                else { match.regions.push_back(it->box); line = it->line; }
            }
            if (!match.regions.empty()) results.push_back(std::move(match));
            pos = end;
        }
        return results;
    }
};
}
