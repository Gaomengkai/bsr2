#pragma once

#include <windows.h>

#include <string>

namespace win_ui {

struct AdvancedOptionsState {
    bool add_suffix_enabled = false;
    std::wstring extra_suffix;
};

class AdvancedOptionsDialog {
public:
    bool ShowModal(HINSTANCE instance, HWND owner, AdvancedOptionsState& state);

private:
    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND description_label_ = nullptr;
    HWND add_suffix_checkbox_ = nullptr;
    HWND suffix_edit_ = nullptr;
    HWND suffix_hint_label_ = nullptr;
    HWND ok_button_ = nullptr;
    HWND cancel_button_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_brush_ = nullptr;
    UINT current_dpi_ = 96;
    bool accepted_ = false;
    bool finished_ = false;
    AdvancedOptionsState working_state_;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    bool RegisterWindowClass() const;
    bool CreateDialogWindow();
    void RefreshFonts();
    void Layout();
    void UpdateControlState();
    void CenterOverOwner() const;
    void Finish(bool accepted);
    std::wstring NormalizedSuffixText() const;
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    void Cleanup();
};

}  // namespace win_ui
