cmake_minimum_required(VERSION 3.15)
project(bsr-win-ui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bsr_win_ui WIN32
    src/app_window.cpp
    src/directory_card.cpp
    src/main.cpp
)

target_include_directories(bsr_win_ui PRIVATE include)

target_link_libraries(bsr_win_ui PRIVATE
    bsr_core
    comctl32
    ole32
    shell32
    uxtheme
)

target_compile_definitions(bsr_win_ui PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
