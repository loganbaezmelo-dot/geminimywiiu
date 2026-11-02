cmake_minimum_required(VERSION 3.13)

# Point CMake directly to wut's installed CMake config files
list(APPEND CMAKE_PREFIX_PATH "/opt/devkitpro/wut" "/opt/devkitpro/portlibs/wiiu")

project(geminimywiiu C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Discover WUT targets (wut::whb, wut::coreinit, wut::vpad)
find_package(wut REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(geminimywiiu
    src/main.cpp
)

target_link_libraries(geminimywiiu
    PRIVATE
        wut::whb
        wut::coreinit
        wut::vpad
        -lnn_swkbd
        CURL::libcurl
        brotlidec
        brotlicommon
        ssl
        crypto
        z
        Threads::Threads
)

wut_create_rpx(geminimywiiu)
wut_create_wuhb(geminimywiiu
    NAME "geminimywiiu"
    AUTHOR "Logan"
    VERSION "1.0.0"
)
