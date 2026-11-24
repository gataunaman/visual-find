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
