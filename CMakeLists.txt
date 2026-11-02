cmake_minimum_required(VERSION 3.13)
project(geminimywiiu C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find portlibs packages
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Executable target
add_executable(geminimywiiu
    src/main.cpp
)

# Link Wii U libraries, software keyboard, curl, and static crypto/compression
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

# Package into .rpx and .wuhb bundle for Aroma
wut_create_rpx(geminimywiiu)
wut_create_wuhb(geminimywiiu
    NAME "geminimywiiu"
    AUTHOR "Logan"
    VERSION "1.0.0"
)
