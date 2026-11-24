#include "../src/search.hpp"
#include "../src/desktop.hpp"
#include "../src/layout.hpp"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    std::cout << "PASS " << name << '\n';
}
int main() {
    try {
        using namespace vf;
        SearchIndex index({{L"Hello", {10,20,40,15}, 0}, {L"WORLD", {60,20,50,15}, 0},
                           {L"binary", {10,50,50,15}, 1}, {L"search", {70,50,50,15}, 1},
                           {L"tree", {130,50,30,15}, 1}, {L"hello", {10,80,40,15}, 2},
                           {L"world", {10,100,50,15}, 3}});
        check(index.find(L"hElLo").size() == 2, "case-insensitive matching");
        auto phrase = index.find(L"binary search");
        check(phrase.size() == 1, "phrase matching across words");
        check(phrase[0].bounds == Box{10,50,110,15}, "bounding-box combination");
        check(index.find(L"search binary").empty(), "word ordering");
        check(index.find(L"world").size() == 2, "multiple matches");
        check(index.find(L"absent").empty(), "zero matches");
        check(index.find(L"").empty() && index.find(L" \t\n").empty(), "empty query");
        check(index.find(L"  binary\t\n search ").size() == 1, "query whitespace normalization");
        auto multiline = index.find(L"hello world");
        check(multiline.size() == 2 && multiline[1].regions.size() == 2,
              "wrapped phrase preserves separate line regions");
        check(multiline[1].bounds == Box{10,80,50,35}, "multi-line bounding union");
        check(index.find(L"orl").size() == 2, "substring search");
        SearchIndex unicode({{L"\uFF28\uFF45\uFF4C\uFF4C\uFF4F", {0,0,20,10}, 0},
                             {L"CAF\u00C9", {30,0,20,10}, 0}});
        check(unicode.find(L"hello").size() == 1, "Unicode compatibility normalization");
        check(unicode.find(L"caf\u00E9").size() == 1, "Unicode case folding");
        Coordinates coordinates{-1920, -200, 3840, 2160, 1920, 1080};
        check(coordinates.desktop({100,50,80,20}) == Box{-1720,-100,160,40},
              "resized OCR coordinates and negative monitor origin");
        for (int dpi : {96,120,144,192}) {
            // Physical screenshot coordinates never pass through UI's DIP scaling.
            Coordinates native{0,0,1920,1080,1920,1080};
            check(native.desktop({120,240,50,20}) == Box{120,240,50,20},
                  ("physical coordinates at DPI " + std::to_string(dpi)).c_str());
        }
        Coordinates fractional{-1280,0,2560,1440,2600,1462};
        auto scaled = fractional.desktop({100,100,10,10});
        check(std::abs(scaled.x - (-1280 + 100.0 * 2560 / 2600)) < 1e-8 &&
              std::abs(scaled.y - 100.0 * 1440 / 1462) < 1e-8,
              "independent X/Y scaling after integer dimension rounding");
        SearchIndex repeated({{L"aaaa", {0,0,10,10}, 0}});
        check(repeated.find(L"aa").size() == 2, "non-overlapping substring occurrences");
        SearchIndex empty;
        check(empty.find(L"hello").empty(), "empty OCR result");
        SearchIndex whitespace({{L" ", {0,0,1,1}, 0}, {L"one", {0,0,20,10}, 0},
                                {L"\t", {0,0,1,1}, 0}, {L"two", {30,0,20,10}, 0}});
        check(whitespace.find(L"one two").size() == 1, "empty OCR words skipped");
        std::vector<Word> desktopWords{{L"MinCVV", {700,90,50,20}, 0},
                                       {L"Installer", {690,115,70,20}, 1},
                                       {L"wallpaper", {900,300,80,20}, 2}};
        mergeDesktopLabels(desktopWords, {{L"MinGW Installer", {672,5,112,129}, 0}});
        SearchIndex desktopIndex(desktopWords);
        check(desktopIndex.find(L"mingw").size()==1 && desktopIndex.find(L"MinGW Installer").size()==1,
              "exact desktop label replaces missed OCR spelling");
        check(desktopIndex.find(L"mincvv").empty() && desktopIndex.find(L"installer").size()==1,
              "desktop labels do not duplicate incorrect OCR matches");
        check(desktopIndex.find(L"wallpaper").size()==1 && desktopIndex.find(L"mingw")[0].bounds==Box{672,5,112,129},
              "desktop supplement preserves other text and physical shortcut bounds");
        // --- exact text (accessibility) merged with OCR ---
        {
            std::vector<Word> ocr{{L"garbage", {100,100,40,8}, 0}, {L"keep", {300,300,30,8}, 1}};
            ExactText exact;
            exact.covered = {{90,95,200,30}};
            exact.words = {{L"Real", {100,100,20,8}, 0}, {L"headline", {125,100,50,8}, 0}, {L"second", {100,112,40,8}, 1}};
            mergeExactText(ocr, exact);
            SearchIndex merged(ocr);
            check(merged.find(L"garbage").empty() && merged.find(L"keep").size() == 1,
                  "exact text replaces OCR inside covered rectangles only");
            check(merged.find(L"real headline").size() == 1 && merged.find(L"real headline")[0].regions.size() == 1,
                  "exact words on one line form one highlight region");
            check(merged.find(L"headline second")[0].regions.size() == 2, "exact text keeps separate lines");
            std::vector<Word> none{{L"only", {0,0,10,10}, 0}};
            mergeExactText(none, ExactText{});
            check(none.size() == 1, "empty exact text leaves OCR untouched");
        }
        // --- finding text lines from pixels ---
        {
            const int w = 120, h = 40;
            const std::vector<unsigned char> white(size_t(w) * h * 4, 255);
            const Box all{0, 0, double(w), double(h)};
            const auto ink = [&](std::vector<unsigned char>& image, int x0, int y0, int x1, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        auto* p = &image[(size_t(y) * w + x) * 4];
                        p[0] = p[1] = p[2] = 20;
                    }
            };
            auto two = white;
            ink(two, 10, 5, 100, 10); ink(two, 10, 18, 60, 23);  // blank rows between the lines
            auto lines = inkLines(two.data(), w, h, all);
            check(lines.size() == 2 && lines[0].box == Box{10,5,90,5} && lines[1].box == Box{10,18,50,5},
                  "two separated text lines");
            // Tiny text: the second line starts right under the first one's descenders (no blank row).
            auto tight = white;
            ink(tight, 10, 5, 110, 10);   // first line, x-height rows
            ink(tight, 12, 10, 20, 12);   // first line, descenders
            ink(tight, 10, 14, 40, 18);   // second, shorter line
            lines = inkLines(tight.data(), w, h, all);
            check(lines.size() == 2 && lines[0].box.width == 100 && lines[1].box.width == 30,
                  "tight lines split at a faint valley");
            auto single = white;
            ink(single, 10, 5, 110, 10); ink(single, 12, 10, 20, 12);  // one line with descenders
            check(inkLines(single.data(), w, h, all).size() == 1, "descenders do not make a second line");
            auto underlined = white;
            ink(underlined, 10, 5, 100, 11); ink(underlined, 10, 13, 100, 14);  // text plus underline
            check(inkLines(underlined.data(), w, h, all).size() == 1, "an underline is not a line of its own");
            check(inkLines(white.data(), w, h, all).empty(), "blank rectangle has no lines");
            auto busy = white;
            unsigned seed = 12345;  // pseudo-random brightness, like a photograph
            for (size_t i = 0; i < busy.size(); i += 4) {
                seed = seed * 1103515245u + 12345u;
                busy[i] = busy[i+1] = busy[i+2] = static_cast<unsigned char>((seed >> 16) & 255);
            }
            check(inkLines(busy.data(), w, h, all).empty(), "busy picture falls back to the whole rectangle");
            auto dark = white;
            for (size_t i = 0; i < dark.size(); i += 4) dark[i] = dark[i+1] = dark[i+2] = 30;
            for (int y = 5; y < 10; ++y)
                for (int x = 10; x < 80; ++x) {
                    auto* p = &dark[(size_t(y) * w + x) * 4];
                    p[0] = p[1] = p[2] = 235;
                }
            lines = inkLines(dark.data(), w, h, all);
            check(lines.size() == 1 && lines[0].box == Box{10,5,70,5}, "light text on a dark background");
        }
        // --- snapping words to the real spaces ---
        {
            unsigned next = 0;
            // "aaa bbbb": character counts alone would split a 70 px line at 26 px; the pixels show the space at 40..47.
            const InkLine line{Box{0,0,70,10}, {Box{10,0,1,10}, Box{40,0,7,10}}};
            auto snapped = spreadWords(L"aaa bbbb", {line}, next);
            check(snapped.size() == 2 && snapped[0].bounds.x == 0 && snapped[0].bounds.width == 40 &&
                  snapped[1].bounds.x == 47 && snapped[1].bounds.width == 23, "words snap to the widest gaps");
            // Gaps that make no sense for the letters (a 5 px first word for 12 letters) are ignored.
            next = 0;
            const InkLine odd{Box{0,0,70,10}, {Box{5,0,1,10}}};
            auto estimated = spreadWords(L"abcdefghijkl mn", {odd}, next);
            check(estimated.size() == 2 && estimated[0].bounds.width > 40, "implausible gaps fall back to estimates");
            // Too few gaps for the words: estimates again.
            next = 0;
            auto few = spreadWords(L"aaa bbb ccc", {InkLine{Box{0,0,90,10}, {Box{30,0,3,10}}}}, next);
            check(few.size() == 3 && std::abs(few[1].bounds.x - 90.0 * 4 / 11) < 1e-9,
                  "too few gaps fall back to estimates");
            // Gaps come from the pixels: two words separated by three blank columns.
            const int w = 60, h = 12;
            std::vector<unsigned char> image(size_t(w) * h * 4, 255);
            for (int y = 3; y < 8; ++y)
                for (int x = 5; x < 55; ++x)
                    if (x < 25 || x >= 28) {
                        auto* p = &image[(size_t(y) * w + x) * 4];
                        p[0] = p[1] = p[2] = 20;
                    }
            auto found = inkLines(image.data(), w, h, {0, 0, double(w), double(h)});
            check(found.size() == 1 && found[0].gaps.size() == 1 && found[0].gaps[0].x == 25 && found[0].gaps[0].width == 3,
                  "blank columns inside a line are reported as gaps");
        }
        // --- placing words along lines ---
        {
            unsigned next = 0;
            auto one = spreadWords(L"aaa bbb", {InkLine{Box{0,0,70,10}, {}}}, next);
            check(one.size() == 2 && next == 1 && one[0].bounds.x == 0 && one[1].bounds.x == 40 &&
                  one[0].line == one[1].line, "words spread along one line by character count");
            next = 5;
            auto wrapped = spreadWords(L"aaa bbb ccc ddd", {InkLine{Box{0,0,60,10}, {}}, InkLine{Box{0,12,30,10}, {}}}, next);
            check(wrapped.size() == 4 && wrapped[2].line == 5 && wrapped[3].line == 6 && wrapped[3].bounds.y == 12 &&
                  wrapped[3].bounds.x == 0 && next == 7, "wrapped text lands on the right line");
            next = 0;
            check(spreadWords(L"   ", {InkLine{Box{0,0,10,10}, {}}}, next).empty() && spreadWords(L"a", {}, next).empty(),
                  "no words or no lines produce nothing");
            SearchIndex placed(wrapped);
            check(placed.find(L"ccc ddd").size() == 1 && placed.find(L"ccc ddd")[0].regions.size() == 2,
                  "wrapped run highlights each of its lines");
        }
        std::cout << "All search and coordinate tests passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
