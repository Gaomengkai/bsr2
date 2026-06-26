#include "win_ui/directory_card.hpp"

#include <windows.h>
#include <oleidl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace win_ui {
namespace {

constexpr wchar_t kCardClassName[] = L"BsrDirectoryCard";
constexpr int kChooseButtonId = 1;
constexpr int kCardCornerRadius = 18;
constexpr int kCardPadding = 16;
constexpr int kButtonWidth = 96;
constexpr int kButtonHeight = 32;
constexpr int kTitleHeight = 18;

int scale_for_window(HWND window, int value) {
    const UINT dpi = window == nullptr ? 96U : GetDpiForWindow(window);
    return MulDiv(value, static_cast<int>(dpi), 96);
}

COLORREF background_color(bool enabled, bool mouse_hot, bool drop_highlighted) {
    if (!enabled) {
        return RGB(245, 246, 248);
    }
    if (drop_highlighted) {
        return RGB(235, 244, 255);
    }
    if (mouse_hot) {
        return RGB(246, 249, 253);
    }
    return RGB(250, 251, 252);
}

COLORREF border_color(bool enabled, bool mouse_hot, bool drop_highlighted) {
    if (!enabled) {
        return RGB(212, 216, 220);
    }
    if (drop_highlighted) {
        return RGB(34, 116, 225);
    }
    if (mouse_hot) {
        return RGB(148, 172, 205);
    }
    return RGB(210, 214, 220);
}

COLORREF title_color(bool enabled) {
    return enabled ? RGB(48, 54, 61) : RGB(136, 142, 150);
}

COLORREF body_color(bool enabled, bool has_path) {
    if (!enabled) {
        return RGB(150, 156, 164);
    }
    return has_path ? RGB(78, 84, 92) : RGB(138, 144, 152);
}

std::optional<std::filesystem::path> first_directory_from_data_object(IDataObject* data_object) {
    if (data_object == nullptr) {
        return std::nullopt;
    }

    FORMATETC format{};
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;

    STGMEDIUM medium{};
    if (FAILED(data_object->GetData(&format, &medium))) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> result;
    HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    if (drop != nullptr) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT index = 0; index < count; ++index) {
            const UINT length = DragQueryFileW(drop, index, nullptr, 0);
            std::wstring buffer(length + 1, L'\0');
            DragQueryFileW(drop, index, buffer.data(), length + 1);
            buffer.resize(length);

            std::filesystem::path path(buffer);
            if (std::filesystem::is_directory(path)) {
                result = path;
                break;
            }
        }
        GlobalUnlock(medium.hGlobal);
    }

    ReleaseStgMedium(&medium);
    return result;
}

class MemoryPaintBuffer {
public:
    MemoryPaintBuffer(HDC target_dc, const RECT& bounds)
        : target_dc_(target_dc), bounds_(bounds) {
        memory_dc_ = CreateCompatibleDC(target_dc_);
        bitmap_ = CreateCompatibleBitmap(
            target_dc_, bounds_.right - bounds_.left, bounds_.bottom - bounds_.top);
        old_bitmap_ = static_cast<HBITMAP>(SelectObject(memory_dc_, bitmap_));
    }

    ~MemoryPaintBuffer() {
        BitBlt(
            target_dc_,
            bounds_.left,
            bounds_.top,
            bounds_.right - bounds_.left,
            bounds_.bottom - bounds_.top,
            memory_dc_,
            0,
            0,
            SRCCOPY);
        SelectObject(memory_dc_, old_bitmap_);
        DeleteObject(bitmap_);
        DeleteDC(memory_dc_);
    }

    HDC dc() const {
        return memory_dc_;
    }

private:
    HDC target_dc_ = nullptr;
    RECT bounds_{};
    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HBITMAP old_bitmap_ = nullptr;
};

}  // namespace

class DirectoryCard::DropTarget : public IDropTarget {
public:
    explicit DropTarget(DirectoryCard* owner) : owner_(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++ref_count_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        if (ref_count_ > 0) {
            --ref_count_;
        }
        return ref_count_;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(
        IDataObject* data_object,
        DWORD,
        POINTL,
        DWORD* effect) override {
        candidate_path_ = first_directory_from_data_object(data_object);
        owner_->SetDropHighlighted(candidate_path_.has_value());
        if (effect != nullptr) {
            *effect = candidate_path_.has_value() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        owner_->SetDropHighlighted(candidate_path_.has_value());
        if (effect != nullptr) {
            *effect = candidate_path_.has_value() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        candidate_path_.reset();
        owner_->SetDropHighlighted(false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(
        IDataObject* data_object,
        DWORD,
        POINTL,
        DWORD* effect) override {
        auto path = first_directory_from_data_object(data_object);
        owner_->SetDropHighlighted(false);
        candidate_path_.reset();

        if (!path.has_value()) {
            if (effect != nullptr) {
                *effect = DROPEFFECT_NONE;
            }
            return S_OK;
        }

        owner_->SetPath(*path);
        owner_->NotifyPathChanged();
        if (effect != nullptr) {
            *effect = DROPEFFECT_COPY;
        }
        return S_OK;
    }

private:
    DirectoryCard* owner_ = nullptr;
    ULONG ref_count_ = 1;
    std::optional<std::filesystem::path> candidate_path_;
};

DirectoryCard::DirectoryCard() = default;

DirectoryCard::~DirectoryCard() {
    Destroy();
}

bool DirectoryCard::Create(HWND parent,
                           HINSTANCE instance,
                           int control_id,
                           const std::wstring& title,
                           const std::wstring& placeholder,
                           HFONT font) {
    parent_ = parent;
    control_id_ = control_id;
    title_ = title;
    placeholder_ = placeholder;
    font_ = font;

    RegisterWindowClass(instance);

    hwnd_ = CreateWindowExW(
        0,
        kCardClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0,
        0,
        0,
        0,
        parent_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_)),
        instance,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }

    choose_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"浏览...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChooseButtonId)),
        instance,
        nullptr);
    SendMessageW(choose_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SetWindowTheme(choose_button_, L"Explorer", nullptr);

    drop_target_ = new DropTarget(this);
    drop_registered_ = SUCCEEDED(RegisterDragDrop(hwnd_, drop_target_));
    return true;
}

void DirectoryCard::Destroy() {
    if (hwnd_ != nullptr) {
        if (drop_registered_) {
            RevokeDragDrop(hwnd_);
            drop_registered_ = false;
        }
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    choose_button_ = nullptr;
    if (drop_target_ != nullptr) {
        drop_target_->Release();
        drop_target_ = nullptr;
    }
}

void DirectoryCard::SetBounds(const RECT& bounds) {
    MoveWindow(
        hwnd_,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        TRUE);
}

void DirectoryCard::SetPath(const std::filesystem::path& path) {
    path_ = path;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void DirectoryCard::ClearPath() {
    path_.clear();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

const std::filesystem::path& DirectoryCard::path() const {
    return path_;
}

bool DirectoryCard::HasPath() const {
    return !path_.empty();
}

void DirectoryCard::SetEnabled(bool enabled) {
    EnableWindow(hwnd_, enabled ? TRUE : FALSE);
    EnableWindow(choose_button_, enabled ? TRUE : FALSE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void DirectoryCard::SetDropHighlighted(bool highlighted) {
    if (drop_highlighted_ == highlighted) {
        return;
    }
    drop_highlighted_ = highlighted;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void DirectoryCard::SetFont(HFONT font) {
    font_ = font;
    if (choose_button_ != nullptr) {
        SendMessageW(choose_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

HWND DirectoryCard::hwnd() const {
    return hwnd_;
}

void DirectoryCard::SetOnChoose(std::function<void()> callback) {
    on_choose_ = std::move(callback);
}

void DirectoryCard::SetOnPathChanged(std::function<void(const std::filesystem::path&)> callback) {
    on_path_changed_ = std::move(callback);
}

ATOM DirectoryCard::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_HAND);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kCardClassName;
    const ATOM atom = RegisterClassExW(&window_class);
    if (atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }
    return 0;
}

LRESULT CALLBACK DirectoryCard::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    DirectoryCard* self = reinterpret_cast<DirectoryCard*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<DirectoryCard*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = window;
    }

    if (self == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }
    return self->HandleMessage(message, w_param, l_param);
}

LRESULT DirectoryCard::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_SIZE:
        Layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        Paint(dc);
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_MOUSEMOVE:
        SetMouseHot(true);
        StartMouseTracking();
        return 0;
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        SetMouseHot(false);
        return 0;
    case WM_LBUTTONUP:
        if (IsInteractive()) {
            NotifyChooseRequested();
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(w_param) == kChooseButtonId && HIWORD(w_param) == BN_CLICKED) {
            NotifyChooseRequested();
            return 0;
        }
        break;
    case WM_ENABLE:
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    case WM_DESTROY:
        if (drop_registered_) {
            RevokeDragDrop(hwnd_);
            drop_registered_ = false;
        }
        return 0;
    case WM_NCDESTROY:
        choose_button_ = nullptr;
        hwnd_ = nullptr;
        tracking_mouse_ = false;
        mouse_hot_ = false;
        drop_highlighted_ = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

void DirectoryCard::Layout() {
    RECT rect{};
    GetClientRect(hwnd_, &rect);

    const int padding = scale_for_window(hwnd_, kCardPadding);
    const int button_width = scale_for_window(hwnd_, kButtonWidth);
    const int button_height = scale_for_window(hwnd_, kButtonHeight);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int button_x = width - padding - button_width;
    const int button_y = (height - button_height) / 2;

    MoveWindow(choose_button_, button_x, button_y, button_width, button_height, TRUE);
}

void DirectoryCard::Paint(HDC dc) {
    RECT rect{};
    GetClientRect(hwnd_, &rect);

    MemoryPaintBuffer buffer(dc, rect);
    HDC mem_dc = buffer.dc();

    HBRUSH window_brush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem_dc, &rect, window_brush);
    DeleteObject(window_brush);

    HPEN border_pen = CreatePen(
        PS_SOLID,
        drop_highlighted_ ? 2 : 1,
        border_color(IsWindowEnabled(hwnd_) == TRUE, mouse_hot_, drop_highlighted_));
    HBRUSH fill_brush = CreateSolidBrush(
        background_color(IsWindowEnabled(hwnd_) == TRUE, mouse_hot_, drop_highlighted_));
    HPEN old_pen = static_cast<HPEN>(SelectObject(mem_dc, border_pen));
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(mem_dc, fill_brush));

    RECT frame = rect;
    InflateRect(&frame, -1, -1);
    const int corner_radius = scale_for_window(hwnd_, kCardCornerRadius);
    RoundRect(mem_dc, frame.left, frame.top, frame.right, frame.bottom, corner_radius, corner_radius);

    SelectObject(mem_dc, old_pen);
    SelectObject(mem_dc, old_brush);
    DeleteObject(border_pen);
    DeleteObject(fill_brush);

    SetBkMode(mem_dc, TRANSPARENT);
    SetTextColor(mem_dc, title_color(IsWindowEnabled(hwnd_) == TRUE));
    SelectObject(mem_dc, font_);

    RECT button_rect{};
    GetWindowRect(choose_button_, &button_rect);
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<POINT*>(&button_rect), 2);

    const int padding = scale_for_window(hwnd_, kCardPadding);
    const int title_height = scale_for_window(hwnd_, kTitleHeight);
    const int title_gap = scale_for_window(hwnd_, 8);
    const int body_right_gap = scale_for_window(hwnd_, 18);
    const int title_right_gap = scale_for_window(hwnd_, 14);

    RECT title_rect{
        padding,
        padding - 1,
        button_rect.left - title_right_gap,
        padding - 1 + title_height};
    DrawTextW(mem_dc, title_.c_str(), -1, &title_rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(mem_dc, body_color(IsWindowEnabled(hwnd_) == TRUE, HasPath()));
    RECT body_rect{
        padding,
        title_rect.bottom + title_gap,
        button_rect.left - body_right_gap,
        rect.bottom - padding};

    const std::wstring text = DisplayText();
    DrawTextW(
        mem_dc,
        text.c_str(),
        -1,
        &body_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
}

void DirectoryCard::StartMouseTracking() {
    if (tracking_mouse_) {
        return;
    }

    TRACKMOUSEEVENT event{};
    event.cbSize = sizeof(event);
    event.dwFlags = TME_LEAVE;
    event.hwndTrack = hwnd_;
    TrackMouseEvent(&event);
    tracking_mouse_ = true;
}

void DirectoryCard::SetMouseHot(bool hot) {
    if (mouse_hot_ == hot) {
        return;
    }
    mouse_hot_ = hot;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void DirectoryCard::NotifyChooseRequested() {
    if (!IsInteractive() || on_choose_ == nullptr) {
        return;
    }
    on_choose_();
}

void DirectoryCard::NotifyPathChanged() {
    if (on_path_changed_ != nullptr) {
        on_path_changed_(path_);
    }
}

std::wstring DirectoryCard::DisplayText() const {
    return HasPath() ? path_.native() : placeholder_;
}

bool DirectoryCard::IsInteractive() const {
    return IsWindowEnabled(hwnd_) == TRUE;
}

}  // namespace win_ui
