#include "win_ui/app_window.hpp"

#include "bsr/core.hpp"
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
    HWND run_button_ = nullptr;
    HWND status_bar_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_brush_ = nullptr;
    DirectoryCard subtitle_card_;
    DirectoryCard video_card_;
    bool running_ = false;

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
        window_ = CreateWindowExW(
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
            info->ptMinTrackSize.x = kWindowMinWidth;
            info->ptMinTrackSize.y = kWindowMinHeight;
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
        font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        background_brush_ = CreateSolidBrush(RGB(255, 255, 255));

        hint_label_ = CreateWindowExW(
            0,
            L"STATIC",
            L"将文件夹拖到下方卡片中，或点击卡片右侧的“浏览...”选择目录。",
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

        SendMessageW(copy_checkbox_, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(hint_label_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(copy_checkbox_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(run_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

        subtitle_card_.Create(
            window_,
            instance_,
            kIdSubtitleCard,
            L"字幕目录",
            L"拖入字幕文件夹",
            font_);
        video_card_.Create(
            window_,
            instance_,
            kIdVideoCard,
            L"视频目录",
            L"拖入视频文件夹",
            font_);

        subtitle_card_.SetOnChoose([this]() {
            ChooseDirectory(subtitle_card_, L"已选择字幕目录");
        });
        video_card_.SetOnChoose([this]() {
            ChooseDirectory(video_card_, L"已选择视频目录");
        });
        subtitle_card_.SetOnPathChanged([this](const std::filesystem::path&) {
            SetStatus(L"已填充字幕目录");
            UpdateRunAvailability();
        });
        video_card_.SetOnPathChanged([this](const std::filesystem::path&) {
            SetStatus(L"已填充视频目录");
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
        const int content_width = client_rect.right - (kMargin * 2);
        const int bottom_actions_top = client_rect.bottom - status_height - kMargin - kRunButtonHeight;

        int y = kMargin;
        MoveWindow(hint_label_, kMargin, y, content_width, kTopHintHeight, TRUE);

        y += kTopHintHeight + 14;
        subtitle_card_.SetBounds(RECT{kMargin, y, kMargin + content_width, y + kCardHeight});

        y += kCardHeight + kCardSpacing;
        video_card_.SetBounds(RECT{kMargin, y, kMargin + content_width, y + kCardHeight});

        const int action_top = std::max(y + kCardHeight + kBottomSectionGap, bottom_actions_top);
        MoveWindow(copy_checkbox_, kMargin, action_top + 6, 200, kCheckboxHeight, TRUE);
        MoveWindow(
            run_button_,
            client_rect.right - kMargin - kRunButtonWidth,
            action_top,
            kRunButtonWidth,
            kRunButtonHeight,
            TRUE);
    }

    LRESULT OnCommand(int control_id, int notification) {
        if (notification != BN_CLICKED) {
            return 0;
        }

        switch (control_id) {
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
        return !running_ && subtitle_card_.HasPath() && video_card_.HasPath();
    }

    void SetRunning(bool running) {
        running_ = running;
        subtitle_card_.SetEnabled(!running);
        video_card_.SetEnabled(!running);
        EnableWindow(copy_checkbox_, !running);
        UpdateRunAvailability();
    }

    void UpdateRunAvailability() {
        EnableWindow(run_button_, ReadyToRun() ? TRUE : FALSE);
    }

    void RunRename() {
        const std::filesystem::path subtitle_path = subtitle_card_.path();
        const std::filesystem::path video_path = video_card_.path();
        const bool will_copy = SendMessageW(copy_checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;

        if (subtitle_path.empty() || video_path.empty()) {
            ShowError(L"请先选择字幕目录和视频目录。");
            SetStatus(L"缺少目录");
            return;
        }
        if (!std::filesystem::exists(subtitle_path) || !std::filesystem::is_directory(subtitle_path)) {
            ShowError(L"字幕目录不存在。");
            SetStatus(L"字幕目录不存在");
            return;
        }
        if (!std::filesystem::exists(video_path) || !std::filesystem::is_directory(video_path)) {
            ShowError(L"视频目录不存在。");
            SetStatus(L"视频目录不存在");
            return;
        }

        SetRunning(true);
        SetStatus(L"正在处理...");

        try {
            const std::vector<std::filesystem::path> created =
                bsr::core::rename_subtitles(video_path, subtitle_path, will_copy);

            if (created.empty()) {
                SetStatus(L"没有找到可匹配的字幕");
            } else {
                SetStatus(L"完成 " + std::to_wstring(created.size()) + L" 个文件");
            }
        } catch (const std::exception& ex) {
            ShowError(utf8_to_wide(ex.what()));
            SetStatus(L"处理失败");
        }

        SetRunning(false);
    }

    void Cleanup() {
        subtitle_card_.Destroy();
        video_card_.Destroy();

        if (background_brush_ != nullptr) {
            DeleteObject(background_brush_);
            background_brush_ = nullptr;
        }
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
