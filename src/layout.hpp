#pragma once
#include "search.hpp"
#include <array>

namespace vf {
// One drawn line of text: its box, and the columns inside it that hold no ink (the spaces between
// words, plus some gaps between letters), left to right.
struct InkLine {
    Box box;
    std::vector<Box> gaps;
};

// Finds the lines of ink inside `rect` (image pixels) of a top-down 32-bit BGRA bitmap and returns
// one box per text line, tight around the ink. Returns nothing when the rectangle holds no clear
// text (blank, or a busy photo) so callers can fall back to the whole rectangle.
inline std::vector<InkLine> inkLines(const unsigned char* pixels, int imageWidth, int imageHeight, Box rect) {
    const int x0 = std::clamp(int(std::floor(rect.x)), 0, imageWidth);
    const int y0 = std::clamp(int(std::floor(rect.y)), 0, imageHeight);
    const int x1 = std::clamp(int(std::ceil(rect.x + rect.width)), 0, imageWidth);
    const int y1 = std::clamp(int(std::ceil(rect.y + rect.height)), 0, imageHeight);
    const int w = x1 - x0, h = y1 - y0;
    if (w < 2 || h < 2) return {};
    std::vector<unsigned char> luma(size_t(w) * h);
    std::array<int, 16> histogram{};
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = pixels + (size_t(y0 + y) * imageWidth + x0) * 4;
        for (int x = 0; x < w; ++x, row += 4) {
            const auto value = static_cast<unsigned char>((row[2] * 77 + row[1] * 150 + row[0] * 29) >> 8);
            luma[size_t(y) * w + x] = value;
            ++histogram[value >> 4];
        }
    }
    // Text is the minority: the most common brightness is the background.
    const int bucket = int(std::max_element(histogram.begin(), histogram.end()) - histogram.begin());
    long long sum{}, count{};
    for (unsigned char value : luma) if ((value >> 4) == bucket) { sum += value; ++count; }
    const int background = int(sum / std::max(1LL, count));
    constexpr int threshold = 36;
    std::vector<int> rowInk(h), first(h, w), last(h, -1);
    long long ink{};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (std::abs(int(luma[size_t(y) * w + x]) - background) >= threshold) {
                ++rowInk[y]; ++ink;
                first[y] = std::min(first[y], x); last[y] = std::max(last[y], x);
            }
    if (ink == 0 || ink * 10 > static_cast<long long>(w) * h * 6) return {};
    struct Band { int top, bottom, first, last; };
    std::vector<std::pair<int, int>> runs;  // [top, bottom) rows that hold ink
    for (int y = 0; y < h; ++y) {
        if (!rowInk[y]) continue;
        if (!runs.empty() && runs.back().second == y) runs.back().second = y + 1;
        else runs.push_back({y, y + 1});
    }
    // Tiny text can leave no blank row between lines. A narrow dip in ink far below the lighter of the
    // lines on either side still separates them (descender fringes and round letters never qualify).
    std::vector<std::pair<int, int>> pieces, pending(runs.rbegin(), runs.rend());
    while (!pending.empty()) {
        const auto [top, bottom] = pending.back();
        pending.pop_back();
        if (bottom - top >= 7 && bottom - top <= 24) {
            int valley = top + 3;
            for (int y = top + 3; y <= bottom - 4; ++y) if (rowInk[y] < rowInk[valley]) valley = y;
            int left{}, right{};
            for (int y = top; y < valley; ++y) left = std::max(left, rowInk[y]);
            for (int y = valley + 1; y < bottom; ++y) right = std::max(right, rowInk[y]);
            const int limit = std::min(left, right) * 3 / 10;
            int from = valley, to = valley;  // the whole stretch of faint rows around the valley
            while (from > top && rowInk[from - 1] <= limit) --from;
            while (to + 1 < bottom && rowInk[to + 1] <= limit) ++to;
            if (rowInk[valley] <= limit && to - from + 1 <= 3 && from - top >= 3 && bottom - 1 - to >= 3) {
                pending.push_back({to + 1, bottom});  // top part is handled first
                pending.push_back({top, from});
                continue;
            }
        }
        pieces.push_back({top, bottom});
    }
    std::vector<Band> bands;
    for (const auto& [top, bottom] : pieces) {
        Band band{top, bottom, w, -1};
        for (int y = top; y < bottom; ++y) {
            band.first = std::min(band.first, first[y]); band.last = std::max(band.last, last[y]);
        }
        bands.push_back(band);
    }
    // Underlines and stray rows are not lines of their own: fold thin bands into a neighbour.
    for (size_t i = 0; bands.size() > 1 && i < bands.size();) {
        const Band band = bands[i];
        if (band.bottom - band.top >= 2) { ++i; continue; }
        const bool before = i > 0 && band.top - bands[i - 1].bottom <= 3;
        const bool after = i + 1 < bands.size() && bands[i + 1].top - band.bottom <= 3;
        if (before || after) {
            auto& target = before ? bands[i - 1] : bands[i + 1];
            target.top = std::min(target.top, band.top); target.bottom = std::max(target.bottom, band.bottom);
            target.first = std::min(target.first, band.first); target.last = std::max(target.last, band.last);
        }
        bands.erase(bands.begin() + std::ptrdiff_t(i));  // folded, or dropped as isolated noise
    }
    std::vector<InkLine> lines;
    for (const auto& band : bands) {
        InkLine line{{double(x0 + band.first), double(y0 + band.top),
                      double(band.last - band.first + 1), double(band.bottom - band.top)}, {}};
        int start = -1;  // the outer columns of a band always hold ink, so every gap is interior
        for (int x = band.first; x <= band.last; ++x) {
            bool any = false;
            for (int y = band.top; y < band.bottom && !any; ++y)
                any = std::abs(int(luma[size_t(y) * w + x]) - background) >= threshold;
            if (!any) { if (start < 0) start = x; }
            else if (start >= 0) {
                line.gaps.push_back({double(x0 + start), line.box.y, double(x - start), line.box.height});
                start = -1;
            }
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

// Places the words of `text` along `lines` (ordered top to bottom). Characters are allotted to each
// line in proportion to its width, so wrapped text lands on the right line. Inside a line, words
// snap to the real spaces when the pixels agree (N words are separated by the N-1 widest gaps);
// otherwise positions are estimated from character counts. Every used line gets a new id.
inline std::vector<Word> spreadWords(const std::wstring& text, const std::vector<InkLine>& lines, unsigned& nextLine) {
    struct Token { size_t begin, length, offset; };
    std::vector<Token> tokens;
    size_t offset{};
    for (size_t i = 0; i < text.size();) {
        while (i < text.size() && whitespace(text[i])) ++i;
        const size_t begin = i;
        while (i < text.size() && !whitespace(text[i])) ++i;
        if (i > begin) { tokens.push_back({begin, i - begin, offset}); offset += i - begin + 1; }
    }
    std::vector<Word> words;
    if (tokens.empty() || lines.empty()) return words;
    const double total = double(tokens.back().offset + tokens.back().length);
    double widths{};
    for (const auto& line : lines) widths += std::max(line.box.width, 1.0);
    size_t index{};
    double consumed{};
    for (size_t k = 0; k < lines.size() && index < tokens.size(); ++k) {
        const Box& box = lines[k].box;
        consumed += std::max(box.width, 1.0);
        const bool lastLine = k + 1 == lines.size();
        const double end = total * consumed / widths;
        const size_t firstToken = index;
        while (index < tokens.size() &&
               (lastLine || double(tokens[index].offset) + tokens[index].length / 2.0 < end)) ++index;
        if (index == firstToken) continue;
        const size_t count = index - firstToken;
        const double start = double(tokens[firstToken].offset);
        const double span = std::max(1.0, double(tokens[index - 1].offset + tokens[index - 1].length) - start);
        // Left and right edge of each word on this line.
        std::vector<double> from(count), to(count);
        for (size_t t = 0; t < count; ++t) {
            const auto& token = tokens[firstToken + t];
            from[t] = box.x + (double(token.offset) - start) / span * box.width;
            to[t] = from[t] + double(token.length) / span * box.width;
        }
        if (count > 1 && lines[k].gaps.size() >= count - 1) {
            auto widest = lines[k].gaps;
            std::stable_sort(widest.begin(), widest.end(), [](const Box& a, const Box& b) { return a.width > b.width; });
            widest.resize(count - 1);
            std::sort(widest.begin(), widest.end(), [](const Box& a, const Box& b) { return a.x < b.x; });
            std::vector<double> snapFrom(count), snapTo(count);
            snapFrom[0] = box.x;
            for (size_t t = 0; t + 1 < count; ++t) { snapTo[t] = widest[t].x; snapFrom[t + 1] = widest[t].x + widest[t].width; }
            snapTo[count - 1] = box.x + box.width;
            // Accept only if every longer word has a believable width for its letters.
            bool plausible = true;
            for (size_t t = 0; t < count && plausible; ++t) {
                const double length = double(tokens[firstToken + t].length);
                if (length < 4) continue;
                const double ratio = (snapTo[t] - snapFrom[t]) / length / (box.width / span);
                plausible = ratio >= 0.55 && ratio <= 1.8;
            }
            if (plausible) { from = std::move(snapFrom); to = std::move(snapTo); }
        }
        const unsigned id = nextLine++;
        for (size_t t = 0; t < count; ++t) {
            const auto& token = tokens[firstToken + t];
            words.push_back({text.substr(token.begin, token.length), {from[t], box.y, to[t] - from[t], box.height}, id});
        }
    }
    return words;
}
}
