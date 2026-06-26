cmake_minimum_required(VERSION 3.21)
project(Bsr)

include(CTest)

add_subdirectory(core)
if(WIN32)
    add_subdirectory(win-ui)
endif()

