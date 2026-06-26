#pragma once

#include <windows.h>

namespace win_ui {

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    int Run(HINSTANCE instance, int show_command);

private:
    class Impl;
    Impl* impl_;
};

}  // namespace win_ui
