cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0144 NEW)

project(geminimywiiu C CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
# Tells pkg-config to pull all static dependencies including Brotli, SSL, and zlib
pkg_check_modules(CURL REQUIRED libcurl)

add_executable(geminimywiiu
    src/main.cpp
)

target_include_directories(geminimywiiu PRIVATE
    ${CURL_STATIC_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    /opt/devkitpro/wut/include
)

target_link_directories(geminimywiiu PRIVATE
    /opt/devkitpro/wut/lib
    ${CURL_STATIC_LIBRARY_DIRS}
    ${CURL_LIBRARY_DIRS}
)

# -lwut already contains all nn::* symbols including swkbd!
target_link_libraries(geminimywiiu PRIVATE
    -lwut
    ${CURL_STATIC_LIBRARIES}
    ${CURL_LIBRARIES}
    -lbrotlidec -lbrotlicommon
    -lmbedtls -lmbedx509 -lmbedcrypto -lz
)

# 1. Convert raw executable into a Cafe OS .rpx binary
wut_create_rpx(geminimywiiu)

# 2. Package the .rpx into an Aroma .wuhb bundle
wut_create_wuhb(geminimywiiu
    NAME "geminimywiiu"
    AUTHOR "Logan"
)
