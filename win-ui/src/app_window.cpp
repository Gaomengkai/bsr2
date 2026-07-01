#include "win_ui/app_window.hpp"

#include "bsr/core.hpp"
#include "win_ui/advanced_options_dialog.hpp"
#include "win_ui/directory_card.hpp"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace win_ui {
namespace {

constexpr wchar_t kWindowClassName[] = L"BsrWinUiMainWindow";
constexpr wchar_t kWindowTitle[] = L"字幕改名器";

constexpr int kMargin = 18;
constexpr int kTopHintHeight = 20;
constexpr int kCardHeight = 92;
constexpr int kCardSpacing = 14;
constexpr int kBottomSectionGap = 18;
constexpr int kActionSpacing = 14;
constexpr int kCheckboxHeight = 24;
constexpr int kSecondaryButtonWidth = 94;
constexpr int kRunButtonWidth = 132;
constexpr int kRunButtonHeight = 36;
constexpr int kWindowWidth = 688;
constexpr int kWindowHeight = 396;
constexpr int kWindowMinWidth = 600;
constexpr int kWindowMinHeight = 372;

enum ControlId {
    kIdSubtitleCard = 1001,
    kIdVideoCard = 1002,
    kIdCheckboxCopy = 1003,
    kIdButtonRun = 1004,
    kIdStatus = 1005,
    kIdHint = 1006,
    kIdButtonAdvanced = 1007,
};

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

bool choose_folder(HWND owner, std::filesystem::path& out_path) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
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
        out_path = std::filesystem::path(raw_path);
        CoTaskMemFree(raw_path);
    }

    item->Release();
    dialog->Release();
    return SUCCEEDED(hr) && !out_path.empty();
}

std::wstring describe_selection(const DirectoryCard::Selection& selection,
                                const wchar_t* directory_label,
                                const wchar_t* file_label) {
    switch (selection.kind) {
    case DirectoryCard::Selection::Kind::Directory:
        return directory_label;
    case DirectoryCard::Selection::Kind::Files:
        return std::wstring(file_label) + L"（仅当前拖入）";
    case DirectoryCard::Selection::Kind::None:
    default:
        return L"";
    }
}

}  // namespace

class AppWindow::Impl {
public:
    int Run(HINSTANCE instance, int show_command) {
        instance_ = instance;
        if (!CreateWindowClass()) {
            return 1;
        }
        if (!CreateMainWindow(show_command)) {
            return 1;
        }
        return MessageLoop();
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND hint_label_ = nullptr;
    HWND copy_checkbox_ = nullptr;
    HWND advanced_button_ = nullptr;
    HWND run_button_ = nullptr;
    HWND status_bar_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_brush_ = nullptr;
    DirectoryCard subtitle_card_;
    DirectoryCard video_card_;
    AdvancedOptionsState advanced_options_;
    bool running_ = false;
    UINT current_dpi_ = 96;

    static int ScaleForDpi(UINT dpi, int value) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    int Scale(int value) const {
        return ScaleForDpi(current_dpi_, value);
    }

    HFONT CreateUiFont() const {
        return CreateFontW(
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
    }

    void RefreshFonts() {
        if (font_ != nullptr) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        font_ = CreateUiFont();

        if (hint_label_ != nullptr) {
            SendMessageW(hint_label_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        if (copy_checkbox_ != nullptr) {
            SendMessageW(copy_checkbox_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        if (advanced_button_ != nullptr) {
            SendMessageW(advanced_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        if (run_button_ != nullptr) {
            SendMessageW(run_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        subtitle_card_.SetFont(font_);
        video_card_.SetFont(font_);
    }

    bool CreateWindowClass() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = WindowProc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.lpszClassName = kWindowClassName;
        return RegisterClassExW(&window_class) != 0;
    }

    bool CreateMainWindow(int show_command) {
        current_dpi_ = GetDpiForSystem();
        const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
        RECT window_rect{
            0,
            0,
            ScaleForDpi(current_dpi_, kWindowWidth),
            ScaleForDpi(current_dpi_, kWindowHeight)};
        AdjustWindowRectExForDpi(&window_rect, style, FALSE, 0, current_dpi_);

        window_ = CreateWindowExW(
            0,
            kWindowClassName,
            kWindowTitle,
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            nullptr,
            nullptr,
            instance_,
            this);

        if (window_ == nullptr) {
            return false;
        }

        ShowWindow(window_, show_command);
        UpdateWindow(window_);
        return true;
    }

    int MessageLoop() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }

        if (self == nullptr) {
            return DefWindowProcW(window, message, w_param, l_param);
        }
        return self->HandleMessage(message, w_param, l_param);
    }

    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
        switch (message) {
        case WM_CREATE:
            return OnCreate();
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
            RECT min_rect{
                0,
                0,
                Scale(kWindowMinWidth),
                Scale(kWindowMinHeight)};
            AdjustWindowRectExForDpi(
                &min_rect, GetWindowLongW(window_, GWL_STYLE), FALSE, 0, current_dpi_);
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
            OnSize();
            InvalidateRect(window_, nullptr, TRUE);
            return 0;
        }
        case WM_SIZE:
            OnSize();
            return 0;
        case WM_COMMAND:
            return OnCommand(LOWORD(w_param), HIWORD(w_param));
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(74, 80, 88));
            return reinterpret_cast<INT_PTR>(background_brush_);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            Cleanup();
            SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
            return 0;
        default:
            return DefWindowProcW(window_, message, w_param, l_param);
        }
    }

    LRESULT OnCreate() {
        current_dpi_ = GetDpiForWindow(window_);
        background_brush_ = CreateSolidBrush(RGB(255, 255, 255));

        hint_label_ = CreateWindowExW(
            0,
            L"STATIC",
            L"可拖入文件夹，或直接拖入一批散装视频/字幕文件；拖入散装文件时仅处理当前这批文件。",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdHint),
            instance_,
            nullptr);

        copy_checkbox_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"复制到视频目录",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdCheckboxCopy),
            instance_,
            nullptr);

        run_button_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"开始处理",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdButtonRun),
            instance_,
            nullptr);

        advanced_button_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"高级",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdButtonAdvanced),
            instance_,
            nullptr);

        status_bar_ = CreateWindowExW(
            0,
            STATUSCLASSNAMEW,
            nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(kIdStatus),
            instance_,
            nullptr);

        subtitle_card_.Create(
            window_,
            instance_,
            kIdSubtitleCard,
            L"字幕目录",
            L"拖入字幕文件夹或散装字幕文件",
            font_);
        video_card_.Create(
            window_,
            instance_,
            kIdVideoCard,
            L"视频目录",
            L"拖入视频文件夹或散装视频文件",
            font_);

        RefreshFonts();
        SendMessageW(copy_checkbox_, BM_SETCHECK, BST_CHECKED, 0);

        subtitle_card_.SetOnChoose([this]() {
            ChooseDirectory(subtitle_card_, L"已选择字幕目录");
        });
        video_card_.SetOnChoose([this]() {
            ChooseDirectory(video_card_, L"已选择视频目录");
        });
        subtitle_card_.SetOnSelectionChanged([this](const DirectoryCard::Selection& selection) {
            SetStatus(describe_selection(selection, L"已填充字幕目录", L"已填充字幕文件"));
            UpdateRunAvailability();
        });
        video_card_.SetOnSelectionChanged([this](const DirectoryCard::Selection& selection) {
            SetStatus(describe_selection(selection, L"已填充视频目录", L"已填充视频文件"));
            UpdateRunAvailability();
        });

        SetStatus(L"准备就绪");
        UpdateRunAvailability();
        return 0;
    }

    void OnSize() {
        if (status_bar_ != nullptr) {
            SendMessageW(status_bar_, WM_SIZE, 0, 0);
        }

        RECT client_rect{};
        GetClientRect(window_, &client_rect);

        RECT status_rect{};
        GetWindowRect(status_bar_, &status_rect);
        const int status_height = status_rect.bottom - status_rect.top;
        const int margin = Scale(kMargin);
        const int top_hint_height = Scale(kTopHintHeight);
        const int card_height = Scale(kCardHeight);
        const int card_spacing = Scale(kCardSpacing);
        const int bottom_section_gap = Scale(kBottomSectionGap);
        const int checkbox_height = Scale(kCheckboxHeight);
        const int secondary_button_width = Scale(kSecondaryButtonWidth);
        const int run_button_width = Scale(kRunButtonWidth);
        const int run_button_height = Scale(kRunButtonHeight);
        const int content_width = client_rect.right - (margin * 2);
        const int bottom_actions_top = client_rect.bottom - status_height - margin - run_button_height;

        int y = margin;
        MoveWindow(hint_label_, margin, y, content_width, top_hint_height, TRUE);

        y += top_hint_height + Scale(14);
        subtitle_card_.SetBounds(RECT{margin, y, margin + content_width, y + card_height});

        y += card_height + card_spacing;
        video_card_.SetBounds(RECT{margin, y, margin + content_width, y + card_height});

        const int action_top = std::max(y + card_height + bottom_section_gap, bottom_actions_top);
        MoveWindow(copy_checkbox_, margin, action_top + Scale(6), Scale(200), checkbox_height, TRUE);
        MoveWindow(
            advanced_button_,
            client_rect.right - margin - run_button_width - Scale(kActionSpacing) - secondary_button_width,
            action_top,
            secondary_button_width,
            run_button_height,
            TRUE);
        MoveWindow(
            run_button_,
            client_rect.right - margin - run_button_width,
            action_top,
            run_button_width,
            run_button_height,
            TRUE);
    }

    LRESULT OnCommand(int control_id, int notification) {
        if (notification != BN_CLICKED) {
            return 0;
        }

        switch (control_id) {
        case kIdButtonAdvanced:
            OpenAdvancedOptions();
            return 0;
        case kIdButtonRun:
            RunRename();
            return 0;
        default:
            return 0;
        }
    }

    void SetStatus(const std::wstring& text) {
        SendMessageW(status_bar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void ShowError(const std::wstring& message) {
        MessageBoxW(window_, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }

    void ChooseDirectory(DirectoryCard& card, const wchar_t* status_text) {
        std::filesystem::path path;
        if (!choose_folder(window_, path)) {
            return;
        }

        card.SetPath(path);
        SetStatus(status_text);
        UpdateRunAvailability();
    }

    bool ReadyToRun() const {
        return !running_ && subtitle_card_.HasSelection() && video_card_.HasSelection();
    }

    void SetRunning(bool running) {
        running_ = running;
        subtitle_card_.SetEnabled(!running);
        video_card_.SetEnabled(!running);
        EnableWindow(copy_checkbox_, !running);
        EnableWindow(advanced_button_, !running);
        UpdateRunAvailability();
    }

    void UpdateRunAvailability() {
        EnableWindow(run_button_, ReadyToRun() ? TRUE : FALSE);
    }

    void RunRename() {
        const DirectoryCard::Selection& subtitle_selection = subtitle_card_.selection();
        const DirectoryCard::Selection& video_selection = video_card_.selection();
        bsr::core::RenameOptions options;
        options.will_copy = SendMessageW(copy_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (advanced_options_.add_suffix_enabled) {
            options.extra_suffix = wide_to_utf8(advanced_options_.extra_suffix);
        }

        if (!subtitle_card_.HasSelection() || !video_card_.HasSelection()) {
            ShowError(L"请先选择字幕输入和视频输入。");
            SetStatus(L"缺少输入");
            return;
        }

        SetRunning(true);
        SetStatus(L"正在处理...");

        try {
            const std::vector<std::filesystem::path> subtitle_files =
                subtitle_selection.kind == DirectoryCard::Selection::Kind::Files
                    ? subtitle_selection.files
                    : bsr::core::list_subtitle_files(subtitle_selection.directory);
            const std::vector<std::filesystem::path> video_files =
                video_selection.kind == DirectoryCard::Selection::Kind::Files
                    ? video_selection.files
                    : bsr::core::list_video_files(video_selection.directory);
            const std::vector<std::filesystem::path> created =
                bsr::core::rename_subtitles(video_files, subtitle_files, options);

            if (created.empty()) {
                SetStatus(L"没有找到可匹配的字幕");
            } else {
                auto created_count = static_cast<int>(created.size());
                if (options.will_copy && created_count % 2 == 0) {
                    created_count /= 2;
                }
                SetStatus(L"完成 " + std::to_wstring(created_count) + L" 个文件");
            }
        } catch (const std::exception& ex) {
            ShowError(utf8_to_wide(ex.what()));
            SetStatus(L"处理失败");
        }

        SetRunning(false);
    }

    void OpenAdvancedOptions() {
        AdvancedOptionsDialog dialog;
        AdvancedOptionsState updated_options = advanced_options_;
        if (!dialog.ShowModal(instance_, window_, updated_options)) {
            return;
        }

        advanced_options_ = std::move(updated_options);
        if (advanced_options_.add_suffix_enabled) {
            SetStatus(L"高级选项已更新：添加后缀 ." + advanced_options_.extra_suffix);
        } else {
            SetStatus(L"高级选项已更新");
        }
    }

    void Cleanup() {
        subtitle_card_.Destroy();
        video_card_.Destroy();

        if (background_brush_ != nullptr) {
            DeleteObject(background_brush_);
            background_brush_ = nullptr;
        }
        if (font_ != nullptr) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        advanced_button_ = nullptr;
    }
};

AppWindow::AppWindow() : impl_(new Impl()) {}

AppWindow::~AppWindow() {
    delete impl_;
}

int AppWindow::Run(HINSTANCE instance, int show_command) {
    return impl_->Run(instance, show_command);
}

}  // namespace win_ui
