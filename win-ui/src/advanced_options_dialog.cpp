#include "win_ui/advanced_options_dialog.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>

namespace win_ui {
namespace {

constexpr wchar_t kDialogClassName[] = L"BsrAdvancedOptionsDialog";
constexpr wchar_t kDialogTitle[] = L"高级";

constexpr int kDialogWidth = 428;
constexpr int kDialogHeight = 220;
constexpr int kDialogMinWidth = 360;
constexpr int kDialogMinHeight = 206;
constexpr int kMargin = 18;
constexpr int kDescriptionHeight = 20;
constexpr int kCheckboxHeight = 24;
constexpr int kEditHeight = 30;
constexpr int kHintHeight = 20;
constexpr int kButtonWidth = 90;
constexpr int kButtonHeight = 32;
constexpr int kButtonGap = 10;

enum ControlId {
    kIdDescription = 2001,
    kIdCheckboxAddSuffix = 2002,
    kIdEditSuffix = 2003,
    kIdHint = 2004,
    kIdButtonOk = IDOK,
    kIdButtonCancel = IDCANCEL,
};

int scale_for_dpi(UINT dpi, int value) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

std::wstring trim_wide_whitespace(std::wstring value) {
    auto is_not_space = [](wchar_t ch) {
        return !std::iswspace(ch);
    };

    const auto begin = std::find_if(value.begin(), value.end(), is_not_space);
    if (begin == value.end()) {
        return {};
    }

    const auto end = std::find_if(value.rbegin(), value.rend(), is_not_space).base();
    return std::wstring(begin, end);
}

}  // namespace

bool AdvancedOptionsDialog::ShowModal(HINSTANCE instance, HWND owner, AdvancedOptionsState& state) {
    instance_ = instance;
    owner_ = owner;
    working_state_ = state;
    accepted_ = false;
    finished_ = false;
    current_dpi_ = owner_ == nullptr ? GetDpiForSystem() : GetDpiForWindow(owner_);

    if (!RegisterWindowClass() || !CreateDialogWindow()) {
        Cleanup();
        return false;
    }

    if (owner_ != nullptr) {
        EnableWindow(owner_, FALSE);
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);

    MSG message{};
    while (!finished_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (window_ != nullptr && IsDialogMessageW(window_, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (owner_ != nullptr) {
        EnableWindow(owner_, TRUE);
        SetActiveWindow(owner_);
    }

    if (accepted_) {
        working_state_.extra_suffix = NormalizedSuffixText();
        state = working_state_;
    }

    Cleanup();
    return accepted_;
}

LRESULT CALLBACK AdvancedOptionsDialog::WindowProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    AdvancedOptionsDialog* self =
        reinterpret_cast<AdvancedOptionsDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<AdvancedOptionsDialog*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }

    if (self == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }
    return self->HandleMessage(message, w_param, l_param);
}

bool AdvancedOptionsDialog::RegisterWindowClass() const {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kDialogClassName;
    const ATOM atom = RegisterClassExW(&window_class);
    return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool AdvancedOptionsDialog::CreateDialogWindow() {
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE;
    RECT window_rect{
        0,
        0,
        scale_for_dpi(current_dpi_, kDialogWidth),
        scale_for_dpi(current_dpi_, kDialogHeight)};
    AdjustWindowRectExForDpi(&window_rect, style, FALSE, WS_EX_DLGMODALFRAME, current_dpi_);

    window_ = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kDialogClassName,
        kDialogTitle,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        owner_,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        return false;
    }

    CenterOverOwner();
    return true;
}

void AdvancedOptionsDialog::RefreshFonts() {
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }

    font_ = CreateFontW(
        -MulDiv(9, static_cast<int>(current_dpi_), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI");

    const HWND controls[] = {
        description_label_,
        add_suffix_checkbox_,
        suffix_edit_,
        suffix_hint_label_,
        ok_button_,
        cancel_button_};
    for (HWND control : controls) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
}

void AdvancedOptionsDialog::Layout() {
    RECT client_rect{};
    GetClientRect(window_, &client_rect);

    const int margin = scale_for_dpi(current_dpi_, kMargin);
    const int description_height = scale_for_dpi(current_dpi_, kDescriptionHeight);
    const int checkbox_height = scale_for_dpi(current_dpi_, kCheckboxHeight);
    const int edit_height = scale_for_dpi(current_dpi_, kEditHeight);
    const int hint_height = scale_for_dpi(current_dpi_, kHintHeight);
    const int button_width = scale_for_dpi(current_dpi_, kButtonWidth);
    const int button_height = scale_for_dpi(current_dpi_, kButtonHeight);
    const int button_gap = scale_for_dpi(current_dpi_, kButtonGap);
    const int content_width = client_rect.right - margin * 2;

    int y = margin;
    MoveWindow(description_label_, margin, y, content_width, description_height, TRUE);

    y += description_height + scale_for_dpi(current_dpi_, 16);
    MoveWindow(add_suffix_checkbox_, margin, y, content_width, checkbox_height, TRUE);

    y += checkbox_height + scale_for_dpi(current_dpi_, 10);
    MoveWindow(suffix_edit_, margin, y, content_width, edit_height, TRUE);

    y += edit_height + scale_for_dpi(current_dpi_, 8);
    MoveWindow(suffix_hint_label_, margin, y, content_width, hint_height, TRUE);

    const int button_top = client_rect.bottom - margin - button_height;
    const int cancel_left = client_rect.right - margin - button_width;
    const int ok_left = cancel_left - button_gap - button_width;
    MoveWindow(ok_button_, ok_left, button_top, button_width, button_height, TRUE);
    MoveWindow(cancel_button_, cancel_left, button_top, button_width, button_height, TRUE);
}

void AdvancedOptionsDialog::UpdateControlState() {
    const BOOL enabled =
        SendMessageW(add_suffix_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED ? TRUE : FALSE;
    EnableWindow(suffix_edit_, enabled);
    EnableWindow(suffix_hint_label_, enabled);
}

void AdvancedOptionsDialog::CenterOverOwner() const {
    if (window_ == nullptr) {
        return;
    }

    RECT owner_rect{};
    if (owner_ != nullptr) {
        GetWindowRect(owner_, &owner_rect);
    } else {
        owner_rect = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }

    RECT dialog_rect{};
    GetWindowRect(window_, &dialog_rect);
    const int width = dialog_rect.right - dialog_rect.left;
    const int height = dialog_rect.bottom - dialog_rect.top;
    const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    SetWindowPos(window_, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void AdvancedOptionsDialog::Finish(bool accepted) {
    accepted_ = accepted;
    finished_ = true;
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
}

std::wstring AdvancedOptionsDialog::NormalizedSuffixText() const {
    std::wstring value = working_state_.extra_suffix;
    value = trim_wide_whitespace(std::move(value));
    while (!value.empty() && value.front() == L'.') {
        value.erase(value.begin());
    }
    return value;
}

LRESULT AdvancedOptionsDialog::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CREATE: {
        current_dpi_ = GetDpiForWindow(window_);
        background_brush_ = CreateSolidBrush(RGB(255, 255, 255));

        description_label_ = CreateWindowExW(
            0,
            L"STATIC",
            L"这些选项会影响字幕生成后的文件名。",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdDescription),
            instance_,
            nullptr);

        add_suffix_checkbox_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"添加后缀",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdCheckboxAddSuffix),
            instance_,
            nullptr);

        suffix_edit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            working_state_.extra_suffix.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdEditSuffix),
            instance_,
            nullptr);

        suffix_hint_label_ = CreateWindowExW(
            0,
            L"STATIC",
            L"例如 chs、cht、abc；最终会生成 xxxx.chs.ass 这样的名字。",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdHint),
            instance_,
            nullptr);

        ok_button_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"确定",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdButtonOk),
            instance_,
            nullptr);

        cancel_button_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"取消",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdButtonCancel),
            instance_,
            nullptr);

        SendMessageW(
            add_suffix_checkbox_,
            BM_SETCHECK,
            working_state_.add_suffix_enabled ? BST_CHECKED : BST_UNCHECKED,
            0);
        SendMessageW(suffix_edit_, EM_LIMITTEXT, 64, 0);

        RefreshFonts();
        Layout();
        UpdateControlState();
        SetFocus(working_state_.add_suffix_enabled ? suffix_edit_ : add_suffix_checkbox_);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
        RECT min_rect{
            0,
            0,
            scale_for_dpi(current_dpi_, kDialogMinWidth),
            scale_for_dpi(current_dpi_, kDialogMinHeight)};
        AdjustWindowRectExForDpi(
            &min_rect,
            GetWindowLongW(window_, GWL_STYLE),
            FALSE,
            GetWindowLongW(window_, GWL_EXSTYLE),
            current_dpi_);
        info->ptMinTrackSize.x = min_rect.right - min_rect.left;
        info->ptMinTrackSize.y = min_rect.bottom - min_rect.top;
        return 0;
    }
    case WM_DPICHANGED: {
        current_dpi_ = HIWORD(w_param);
        auto* suggested = reinterpret_cast<RECT*>(l_param);
        SetWindowPos(
            window_,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshFonts();
        Layout();
        return 0;
    }
    case WM_SIZE:
        Layout();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w_param)) {
        case kIdCheckboxAddSuffix:
            if (HIWORD(w_param) == BN_CLICKED) {
                UpdateControlState();
            }
            return 0;
        case kIdButtonOk:
            if (HIWORD(w_param) == BN_CLICKED) {
                working_state_.add_suffix_enabled =
                    SendMessageW(add_suffix_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;

                const int length = GetWindowTextLengthW(suffix_edit_);
                std::wstring buffer(length + 1, L'\0');
                if (length > 0) {
                    GetWindowTextW(suffix_edit_, buffer.data(), length + 1);
                }
                buffer.resize(length);
                working_state_.extra_suffix = buffer;

                if (working_state_.add_suffix_enabled && NormalizedSuffixText().empty()) {
                    MessageBoxW(window_, L"请先输入后缀内容。", kDialogTitle, MB_OK | MB_ICONWARNING);
                    SetFocus(suffix_edit_);
                    return 0;
                }

                Finish(true);
            }
            return 0;
        case kIdButtonCancel:
            if (HIWORD(w_param) == BN_CLICKED) {
                Finish(false);
            }
            return 0;
        default:
            return 0;
        }
    case WM_CLOSE:
        Finish(false);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(
            dc,
            reinterpret_cast<HWND>(l_param) == suffix_hint_label_ ? RGB(112, 118, 126)
                                                                  : RGB(60, 66, 74));
        return reinterpret_cast<INT_PTR>(background_brush_);
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        window_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(window_, message, w_param, l_param);
    }
}

void AdvancedOptionsDialog::Cleanup() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    description_label_ = nullptr;
    add_suffix_checkbox_ = nullptr;
    suffix_edit_ = nullptr;
    suffix_hint_label_ = nullptr;
    ok_button_ = nullptr;
    cancel_button_ = nullptr;

    if (background_brush_ != nullptr) {
        DeleteObject(background_brush_);
        background_brush_ = nullptr;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

}  // namespace win_ui
