cmake_minimum_required(VERSION 3.15)
project(bsr-core)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(bsr_core
    include/bsr/core.hpp
    src/core.cpp
)

target_include_directories(bsr_core PUBLIC include)
target_include_directories(bsr_core PRIVATE src internal_include)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
