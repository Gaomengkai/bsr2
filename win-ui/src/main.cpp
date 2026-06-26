#include "win_ui/app_window.hpp"

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common_controls);

    const HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr)) {
        return 1;
    }

    win_ui::AppWindow app;
    const int result = app.Run(instance, show_command);

    OleUninitialize();
    return result;
}
