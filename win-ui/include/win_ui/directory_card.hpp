#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace win_ui {

class DirectoryCard {
public:
    struct Selection {
        enum class Kind {
            None,
            Directory,
            Files,
        };

        Kind kind = Kind::None;
        std::filesystem::path directory;
        std::vector<std::filesystem::path> files;
    };

    DirectoryCard();
    ~DirectoryCard();

    DirectoryCard(const DirectoryCard&) = delete;
    DirectoryCard& operator=(const DirectoryCard&) = delete;

    bool Create(HWND parent,
                HINSTANCE instance,
                int control_id,
                const std::wstring& title,
                const std::wstring& placeholder,
                HFONT font);
    void Destroy();

    void SetBounds(const RECT& bounds);
    void SetSelection(const Selection& selection);
    void SetDirectoryPath(const std::filesystem::path& path);
    void SetFilePaths(const std::vector<std::filesystem::path>& paths);
    void SetPath(const std::filesystem::path& path);
    void ClearPath();
    const Selection& selection() const;
    const std::filesystem::path& path() const;
    bool HasPath() const;
    bool HasSelection() const;
    void SetEnabled(bool enabled);
    void SetDropHighlighted(bool highlighted);
    void SetFont(HFONT font);
    HWND hwnd() const;

    void SetOnChoose(std::function<void()> callback);
    void SetOnSelectionChanged(std::function<void(const Selection&)> callback);
    void SetOnPathChanged(std::function<void(const std::filesystem::path&)> callback);

private:
    class DropTarget;

    HWND parent_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND choose_button_ = nullptr;
    int control_id_ = 0;
    std::wstring title_;
    std::wstring placeholder_;
    Selection selection_;
    HFONT font_ = nullptr;
    bool mouse_hot_ = false;
    bool tracking_mouse_ = false;
    bool drop_highlighted_ = false;
    bool drop_registered_ = false;
    std::function<void()> on_choose_;
    std::function<void(const Selection&)> on_selection_changed_;
    std::function<void(const std::filesystem::path&)> on_path_changed_;
    DropTarget* drop_target_ = nullptr;

    static ATOM RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    void Layout();
    void Paint(HDC dc);
    void StartMouseTracking();
    void SetMouseHot(bool hot);
    void NotifyChooseRequested();
    void NotifySelectionChanged();
    void NotifyPathChanged();
    std::wstring DisplayText() const;
    std::wstring SelectionSummary() const;
    bool IsInteractive() const;

    friend class DropTarget;
};

}  // namespace win_ui
