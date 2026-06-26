cmake_minimum_required(VERSION 3.15)
project(bsr-mac-ui LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

add_executable(bsr_mac_ui MACOSX_BUNDLE
    src/main.mm
)

target_link_libraries(bsr_mac_ui PRIVATE
    bsr_core
    ${APPKIT_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
)

target_compile_options(bsr_mac_ui PRIVATE
    -fobjc-arc
)
