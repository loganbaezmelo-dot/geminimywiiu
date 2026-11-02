cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0144 NEW)

project(geminimywiiu C CXX)

set(CMAKE_CXX_STANDARD 17)

# Point cmake to wut's custom cmake modules
list(APPEND CMAKE_MODULE_PATH "/opt/devkitpro/wut/share/wut/cmake")

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

add_executable(geminimywiiu
    src/main.cpp
)

target_include_directories(geminimywiiu PRIVATE
    ${CURL_INCLUDE_DIRS}
    /opt/devkitpro/wut/include
)

target_link_directories(geminimywiiu PRIVATE
    /opt/devkitpro/wut/lib
    ${CURL_LIBRARY_DIRS}
)

# Link wut, curl, and networking/crypto libs
target_link_libraries(geminimywiiu PRIVATE
    -lwut
    ${CURL_LIBRARIES}
    -lmbedtls -lmbedx509 -lmbedcrypto -lz
)

# Package into WUHB
wut_create_wuhb(geminimywiiu
    NAME "geminimywiiu"
    AUTHOR "Logan"
)
