#pragma once
#include "layout.hpp"
#include "platform.hpp"
#include <uiautomation.h>
#include <cwchar>
#include <initializer_list>

namespace vf {
// A run of text an application reports through UI Automation, with its on-screen rectangle.
struct TextNode { std::wstring text; Box rect; };

struct PageRead {
    std::vector<TextNode> nodes;
    bool ready{true};  // false: a browser has not exposed its page yet (accessibility starts lazily)
};

namespace detail {
template<class T> class Com {
    T* pointer_{};
public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (pointer_) pointer_->Release(); }
    T** put() { if (pointer_) { pointer_->Release(); pointer_ = nullptr; } return &pointer_; }
    T* get() const { return pointer_; }
    void attach(T* pointer) { if (pointer_) pointer_->Release(); pointer_ = pointer; }
    T* detach() { T* pointer = pointer_; pointer_ = nullptr; return pointer; }
    T* operator->() const { return pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }
};
inline std::wstring windowClass(HWND window) {
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    return name;
}
}

inline bool isBrowserWindow(HWND window) {
    const auto name = detail::windowClass(window);
    return name == L"Chrome_WidgetWin_1" || name == L"MozillaWindowClass";
}

// Asking Electron code editors for accessibility makes them offer their screen-reader mode, so they
// are left to OCR.
inline bool avoidsAccessibility(HWND window) {
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (!handle) return false;
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(handle, 0, path, &size) != FALSE;
    CloseHandle(handle);
    if (!ok) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    for (const wchar_t* denied : {L"code.exe", L"code - insiders.exe", L"cursor.exe"})
        if (_wcsicmp(name, denied) == 0) return true;
    return false;
}

// Reads what an application says its visible text is, instead of guessing it from pixels. This is
// exact at any zoom level; browsers publish their page this way.
class AccessibleText {
    detail::Com<IUIAutomation> automation_;
    detail::Com<IUIAutomationCacheRequest> cache_;
    detail::Com<IUIAutomationCondition> condition_;
public:
    AccessibleText() {
        static const GUID automation8 = {0xe22ad333, 0xb25f, 0x460c, {0x83, 0xd0, 0x05, 0x81, 0x10, 0x73, 0x95, 0xc9}};
        static const GUID automation7 = {0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
        HRESULT result = CoCreateInstance(automation8, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                          reinterpret_cast<void**>(automation_.put()));
        if (FAILED(result))
            result = CoCreateInstance(automation7, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                      reinterpret_cast<void**>(automation_.put()));
        if (FAILED(result) || !automation_) throw std::runtime_error("UI Automation is unavailable");
        detail::Com<IUIAutomation2> extended;
        if (SUCCEEDED(automation_->QueryInterface(__uuidof(IUIAutomation2),
                                                  reinterpret_cast<void**>(extended.put()))) && extended) {
            extended->put_ConnectionTimeout(1000);
            extended->put_TransactionTimeout(800);  // the default is 20 seconds
        }
        if (FAILED(automation_->CreateCacheRequest(cache_.put())) || !cache_)
            throw std::runtime_error("Cannot create UI Automation cache");
        cache_->AddProperty(UIA_NamePropertyId);
        cache_->AddProperty(UIA_BoundingRectanglePropertyId);
        cache_->AddProperty(UIA_ControlTypePropertyId);
        // Browsers publish a link's text as the link's name with no Text child, so links and buttons
        // are read along with text; a Document element marks where a browser's page is.
        detail::Com<IUIAutomationCondition> kind;
        for (long id : {UIA_TextControlTypeId, UIA_DocumentControlTypeId, UIA_HyperlinkControlTypeId,
                        UIA_ButtonControlTypeId}) {
            VARIANT type{}; type.vt = VT_I4; type.lVal = id;
            detail::Com<IUIAutomationCondition> one;
            if (FAILED(automation_->CreatePropertyCondition(UIA_ControlTypePropertyId, type, one.put())) || !one)
                throw std::runtime_error("Cannot create UI Automation condition");
            if (!kind) { kind.attach(one.detach()); continue; }
            detail::Com<IUIAutomationCondition> joined;
            if (FAILED(automation_->CreateOrCondition(kind.get(), one.get(), joined.put())) || !joined)
                throw std::runtime_error("Cannot create UI Automation condition");
            kind.attach(joined.detach());
        }
        VARIANT onScreen{}; onScreen.vt = VT_BOOL; onScreen.boolVal = VARIANT_FALSE;
        detail::Com<IUIAutomationCondition> visible;
        automation_->CreatePropertyCondition(UIA_IsOffscreenPropertyId, onScreen, visible.put());
        if (!kind || !visible ||
            FAILED(automation_->CreateAndCondition(kind.get(), visible.get(), condition_.put())) || !condition_)
            throw std::runtime_error("Cannot create UI Automation condition");
    }
    // Visible text runs of one top-level window, clipped to `monitor`. Runs that lie under a window
    // other than `allowed` are skipped.
    PageRead read(HWND root, const RECT& monitor, std::initializer_list<HWND> allowed) const {
        PageRead page;
        detail::Com<IUIAutomationElement> element;
        if (FAILED(automation_->ElementFromHandle(root, element.put())) || !element) return page;
        detail::Com<IUIAutomationElementArray> found;
        if (FAILED(element->FindAllBuildCache(TreeScope_Descendants, condition_.get(), cache_.get(), found.put())) ||
            !found) return page;
        int count{};
        found->get_Length(&count);
        count = std::min(count, 6000);
        std::vector<RECT> documents;
        struct Candidate { TextNode node; POINT centre; bool container; };
        std::vector<Candidate> found_;
        for (int i = 0; i < count && found_.size() < 4000; ++i) {
            detail::Com<IUIAutomationElement> item;
            if (FAILED(found->GetElement(i, item.put())) || !item) continue;
            CONTROLTYPEID type{};
            RECT r{};
            if (FAILED(item->get_CachedControlType(&type)) || FAILED(item->get_CachedBoundingRectangle(&r))) continue;
            if (type == UIA_DocumentControlTypeId) { documents.push_back(r); continue; }
            BSTR name{};
            item->get_CachedName(&name);
            std::unique_ptr<wchar_t, decltype(&SysFreeString)> owned(name, SysFreeString);
            if (!name || !SysStringLen(name)) continue;
            const RECT clipped{std::max(r.left, monitor.left), std::max(r.top, monitor.top),
                               std::min(r.right, monitor.right), std::min(r.bottom, monitor.bottom)};
            const long w = clipped.right - clipped.left, h = clipped.bottom - clipped.top;
            // Tiny rectangles and rectangles far too small for their text are screen-reader-only text.
            if (w < 3 || h < 3 || double(w) * h < 3.0 * SysStringLen(name)) continue;
            const bool container = type != UIA_TextControlTypeId;
            // A small square link or button is an icon; its name is a label nobody sees.
            if (container && w < h * 3 / 2 && w * h < 1600) continue;
            const POINT centre{(clipped.left + clipped.right) / 2, (clipped.top + clipped.bottom) / 2};
            const HWND at = WindowFromPoint(centre);
            const HWND top = at ? GetAncestor(at, GA_ROOT) : nullptr;
            if (std::find(allowed.begin(), allowed.end(), top) == allowed.end()) continue;
            found_.push_back({{std::wstring(name, SysStringLen(name)),
                               {double(clipped.left), double(clipped.top), double(w), double(h)}}, centre, container});
        }
        // A link or button whose text is already reported by a Text node inside it is a duplicate.
        std::vector<POINT> centres;
        for (size_t i = 0; i < found_.size(); ++i) {
            const auto& candidate = found_[i];
            bool repeated = false;
            if (candidate.container) {
                const Box& b = candidate.node.rect;
                for (const auto& other : found_) {
                    if (other.container) continue;
                    const Box& o = other.node.rect;
                    if (o.x >= b.x - 1 && o.y >= b.y - 1 && o.x + o.width <= b.x + b.width + 1 &&
                        o.y + o.height <= b.y + b.height + 1 &&
                        candidate.node.text.find(other.node.text) != std::wstring::npos) { repeated = true; break; }
                }
            }
            if (repeated) continue;
            page.nodes.push_back(candidate.node);
            centres.push_back(candidate.centre);
        }
        if (isBrowserWindow(root)) {
            const bool inDocument = std::any_of(centres.begin(), centres.end(), [&](const POINT& c) {
                return std::any_of(documents.begin(), documents.end(), [&](const RECT& d) { return PtInRect(&d, c); });
            });
            page.ready = inDocument;
        }
        return page;
    }
};

// Places each run's words on the lines actually drawn: line positions come from the ink inside the
// run's rectangle in the capture (when there is one), words are spread along them by character count.
inline ExactText layoutText(const std::vector<TextNode>& nodes, const Coordinates* coordinates, const Bitmap* bitmap) {
    ExactText result;
    unsigned line{};
    for (const auto& node : nodes) {
        std::vector<InkLine> lines;
        if (coordinates && bitmap && coordinates->screenWidth > 0 && coordinates->screenHeight > 0) {
            const double sx = double(coordinates->imageWidth) / coordinates->screenWidth;
            const double sy = double(coordinates->imageHeight) / coordinates->screenHeight;
            const Box image{(node.rect.x - coordinates->left) * sx, (node.rect.y - coordinates->top) * sy,
                            node.rect.width * sx, node.rect.height * sy};
            for (const InkLine& drawn : inkLines(bitmap->pixels, bitmap->width, bitmap->height, image)) {
                InkLine line{coordinates->desktop(drawn.box), {}};
                for (const Box& gap : drawn.gaps) line.gaps.push_back(coordinates->desktop(gap));
                lines.push_back(std::move(line));
            }
        }
        if (lines.empty()) lines.push_back(InkLine{node.rect, {}});
        auto words = spreadWords(node.text, lines, line);
        if (words.empty()) continue;
        result.covered.push_back(node.rect);
        result.words.insert(result.words.end(), std::make_move_iterator(words.begin()),
                            std::make_move_iterator(words.end()));
    }
    return result;
}
}
