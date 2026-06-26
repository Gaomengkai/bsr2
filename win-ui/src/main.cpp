#include "bsr/core.hpp"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <filesystem>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"BsrWinUiMainWindow";
constexpr wchar_t kWindowTitle[] = L"字幕改名器";

constexpr int kMargin = 16;
constexpr int kTopHintHeight = 20;
constexpr int kLabelHeight = 20;
constexpr int kControlHeight = 28;
constexpr int kButtonWidth = 96;
constexpr int kCheckboxHeight = 24;
constexpr int kActionButtonWidth = 132;
constexpr int kWindowWidth = 680;
constexpr int kWindowHeight = 340;

enum ControlId {
    kIdEditSubtitle = 1001,
    kIdButtonSubtitle = 1002,
    kIdEditVideo = 1003,
    kIdButtonVideo = 1004,
    kIdCheckboxCopy = 1005,
    kIdButtonRun = 1006,
    kIdStatus = 1007,
};

struct AppState {
    HWND window = nullptr;
    HWND hint_label = nullptr;
    HWND subtitle_edit = nullptr;
    HWND subtitle_button = nullptr;
    HWND video_edit = nullptr;
    HWND video_button = nullptr;
    HWND copy_checkbox = nullptr;
    HWND run_button = nullptr;
    HWND status_bar = nullptr;
    HFONT font = nullptr;
    HBRUSH background_brush = nullptr;
};

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring path_to_wstring(const std::filesystem::path& path) {
    return path.native();
}

std::wstring get_window_text(HWND handle) {
    int length = GetWindowTextLengthW(handle);
    if (length <= 0) {
        return L"";
    }

    std::wstring value(length + 1, L'\0');
    GetWindowTextW(handle, value.data(), length + 1);
    value.resize(length);
    return value;
}

void set_window_text(HWND handle, const std::wstring& value) {
    SetWindowTextW(handle, value.c_str());
}

std::filesystem::path get_edit_path(HWND edit) {
    std::wstring text = get_window_text(edit);
    return text.empty() ? std::filesystem::path() : std::filesystem::path(text);
}

void set_status(AppState* state, const std::wstring& text) {
    SendMessageW(state->status_bar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void show_error(HWND owner, const std::wstring& message) {
    MessageBoxW(owner, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
}

bool choose_folder(HWND owner, std::wstring& out_path) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        return false;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    hr = dialog->Show(owner);
    if (FAILED(hr)) {
        dialog->Release();
        return false;
    }

    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    if (FAILED(hr)) {
        dialog->Release();
        return false;
    }

    PWSTR raw_path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path);
    if (SUCCEEDED(hr) && raw_path != nullptr) {
        out_path = raw_path;
        CoTaskMemFree(raw_path);
    }

    item->Release();
    dialog->Release();
    return SUCCEEDED(hr) && !out_path.empty();
}

bool point_in_window(HWND handle, POINT client_point) {
    RECT rect{};
    GetWindowRect(handle, &rect);
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    ScreenToClient(GetParent(handle), &top_left);
    ScreenToClient(GetParent(handle), &bottom_right);
    RECT client_rect{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    return PtInRect(&client_rect, client_point) == TRUE;
}

HWND choose_drop_target(AppState* state, POINT client_point) {
    if (point_in_window(state->subtitle_edit, client_point)) {
        return state->subtitle_edit;
    }
    if (point_in_window(state->video_edit, client_point)) {
        return state->video_edit;
    }

    HWND focused = GetFocus();
    if (focused == state->subtitle_edit || focused == state->video_edit) {
        return focused;
    }

    if (get_window_text(state->subtitle_edit).empty()) {
        return state->subtitle_edit;
    }
    if (get_window_text(state->video_edit).empty()) {
        return state->video_edit;
    }
    return state->subtitle_edit;
}

void handle_drop_files(AppState* state, HDROP drop) {
    POINT client_point{};
    DragQueryPoint(drop, &client_point);

    UINT file_count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < file_count; ++i) {
        wchar_t buffer[MAX_PATH];
        UINT copied = DragQueryFileW(drop, i, buffer, MAX_PATH);
        if (copied == 0) {
            continue;
        }

        std::filesystem::path path(buffer);
        if (!std::filesystem::is_directory(path)) {
            continue;
        }

        HWND target = choose_drop_target(state, client_point);
        set_window_text(target, path.native());

        if (target == state->subtitle_edit) {
            set_status(state, L"已填充字幕目录");
        } else {
            set_status(state, L"已填充视频目录");
        }
        break;
    }

    DragFinish(drop);
}

void run_rename(AppState* state) {
    const std::filesystem::path subtitle_path = get_edit_path(state->subtitle_edit);
    const std::filesystem::path video_path = get_edit_path(state->video_edit);
    const bool will_copy =
        SendMessageW(state->copy_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (subtitle_path.empty() || video_path.empty()) {
        show_error(state->window, L"请先选择字幕目录和视频目录。");
        set_status(state, L"缺少目录");
        return;
    }
    if (!std::filesystem::exists(subtitle_path) || !std::filesystem::is_directory(subtitle_path)) {
        show_error(state->window, L"字幕目录不存在。");
        set_status(state, L"字幕目录不存在");
        return;
    }
    if (!std::filesystem::exists(video_path) || !std::filesystem::is_directory(video_path)) {
        show_error(state->window, L"视频目录不存在。");
        set_status(state, L"视频目录不存在");
        return;
    }

    set_status(state, L"正在处理...");
    EnableWindow(state->run_button, FALSE);

    try {
        const std::vector<std::filesystem::path> created =
            bsr::core::rename_subtitles(video_path, subtitle_path, will_copy);

        if (created.empty()) {
            set_status(state, L"没有找到可匹配的字幕");
        } else {
            set_status(state, L"完成 " + std::to_wstring(created.size()) + L" 个文件");
        }
    } catch (const std::exception& ex) {
        show_error(state->window, utf8_to_wide(ex.what()));
        set_status(state, L"处理失败");
    }

    EnableWindow(state->run_button, TRUE);
}

void layout_controls(AppState* state, int width) {
    RECT client_rect{};
    GetClientRect(state->window, &client_rect);
    SendMessageW(state->status_bar, WM_SIZE, 0, 0);

    RECT status_rect{};
    GetWindowRect(state->status_bar, &status_rect);
    const int status_height = status_rect.bottom - status_rect.top;
    const int field_width = width - (kMargin * 2) - kButtonWidth - 8;
    int y = kMargin;
    const int action_row_height = 40;
    const int bottom_limit = client_rect.bottom - status_height - kMargin;

    MoveWindow(state->hint_label, kMargin, y, width - (kMargin * 2), kTopHintHeight, TRUE);
    y += kTopHintHeight + 18;

    MoveWindow(GetDlgItem(state->window, 2001), kMargin, y, width - (kMargin * 2), kLabelHeight, TRUE);
    y += kLabelHeight + 4;
    MoveWindow(state->subtitle_edit, kMargin, y, field_width, kControlHeight, TRUE);
    MoveWindow(state->subtitle_button, kMargin + field_width + 8, y, kButtonWidth, kControlHeight, TRUE);

    y += kControlHeight + 12;
    MoveWindow(GetDlgItem(state->window, 2002), kMargin, y, width - (kMargin * 2), kLabelHeight, TRUE);
    y += kLabelHeight + 4;
    MoveWindow(state->video_edit, kMargin, y, field_width, kControlHeight, TRUE);
    MoveWindow(state->video_button, kMargin + field_width + 8, y, kButtonWidth, kControlHeight, TRUE);

    y += kControlHeight + 14;
    MoveWindow(state->copy_checkbox, kMargin, y + 2, 190, kCheckboxHeight, TRUE);
    MoveWindow(
        state->run_button,
        width - kMargin - kActionButtonWidth,
        bottom_limit - action_row_height,
        kActionButtonWidth,
        36,
        TRUE);
}

void apply_font(AppState* state, HWND handle) {
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        auto* new_state = reinterpret_cast<AppState*>(create->lpCreateParams);
        new_state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new_state));
        return TRUE;
    }
    case WM_CREATE: {
        state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        state->background_brush = CreateSolidBrush(RGB(255, 255, 255));

        state->hint_label = CreateWindowExW(
            0, L"STATIC", L"拖拽文件夹到输入框，或点击“浏览...”选择目录。",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(2003), nullptr, nullptr);

        HWND subtitle_label = CreateWindowExW(
            0, L"STATIC", L"字幕目录", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(2001), nullptr, nullptr);
        HWND video_label = CreateWindowExW(
            0, L"STATIC", L"视频目录", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(2002), nullptr, nullptr);

        state->subtitle_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdEditSubtitle), nullptr, nullptr);
        state->subtitle_button = CreateWindowExW(
            0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdButtonSubtitle), nullptr, nullptr);
        state->video_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdEditVideo), nullptr, nullptr);
        state->video_button = CreateWindowExW(
            0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdButtonVideo), nullptr, nullptr);
        state->copy_checkbox = CreateWindowExW(
            0, L"BUTTON", L"复制到视频目录", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdCheckboxCopy), nullptr, nullptr);
        state->run_button = CreateWindowExW(
            0, L"BUTTON", L"开始处理", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdButtonRun), nullptr, nullptr);
        state->status_bar = CreateWindowExW(
            0,
            STATUSCLASSNAMEW,
            nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(kIdStatus),
            nullptr,
            nullptr);

        apply_font(state, state->hint_label);
        apply_font(state, subtitle_label);
        apply_font(state, video_label);
        apply_font(state, state->subtitle_edit);
        apply_font(state, state->subtitle_button);
        apply_font(state, state->video_edit);
        apply_font(state, state->video_button);
        apply_font(state, state->copy_checkbox);
        apply_font(state, state->run_button);

        SendMessageW(state->copy_checkbox, BM_SETCHECK, BST_CHECKED, 0);
        set_status(state, L"准备就绪");
        DragAcceptFiles(window, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
        info->ptMinTrackSize.x = 580;
        info->ptMinTrackSize.y = 320;
        return 0;
    }
    case WM_SIZE: {
        if (state != nullptr) {
            layout_controls(state, LOWORD(l_param));
        }
        return 0;
    }
    case WM_DROPFILES:
        handle_drop_files(state, reinterpret_cast<HDROP>(w_param));
        return 0;
    case WM_COMMAND: {
        switch (LOWORD(w_param)) {
        case kIdButtonSubtitle: {
            std::wstring selected;
            if (choose_folder(window, selected)) {
                set_window_text(state->subtitle_edit, selected);
                set_status(state, L"已选择字幕目录");
            }
            return 0;
        }
        case kIdButtonVideo: {
            std::wstring selected;
            if (choose_folder(window, selected)) {
                set_window_text(state->video_edit, selected);
                set_status(state, L"已选择视频目录");
            }
            return 0;
        }
        case kIdButtonRun:
            run_rename(state);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(32, 32, 32));
        return reinterpret_cast<INT_PTR>(state->background_brush);
    }
    case WM_NCDESTROY:
        if (state != nullptr) {
            if (state->background_brush != nullptr) {
                DeleteObject(state->background_brush);
            }
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common_controls);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool com_initialized = SUCCEEDED(hr);

    auto* state = new AppState();

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;

    RegisterClassExW(&window_class);

    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        state);

    if (window == nullptr) {
        delete state;
        if (com_initialized) {
            CoUninitialize();
        }
        return 1;
    }

    SetWindowTextW(window, kWindowTitle);
    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (com_initialized) {
        CoUninitialize();
    }
    return static_cast<int>(message.wParam);
}
