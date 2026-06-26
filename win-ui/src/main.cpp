#include "win_ui/app_window.hpp"

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>

namespace {

void enable_high_dpi_support() {
    using SetDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto set_context = reinterpret_cast<SetDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_context != nullptr &&
            set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }

    if (HMODULE shcore = LoadLibraryW(L"Shcore.dll")) {
        auto set_awareness = reinterpret_cast<SetProcessDpiAwarenessFn>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (set_awareness != nullptr && SUCCEEDED(set_awareness(2))) {
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }

    SetProcessDPIAware();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common_controls);

    enable_high_dpi_support();

    const HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr)) {
        return 1;
    }

    win_ui::AppWindow app;
    const int result = app.Run(instance, show_command);

    OleUninitialize();
    return result;
}
