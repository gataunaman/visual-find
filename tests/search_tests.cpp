#include "../src/search.hpp"
#include "../src/desktop.hpp"
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
        std::cout << "All search and coordinate tests passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
