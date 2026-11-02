cmake_minimum_required(VERSION 3.13)
project(geminimywiiu C CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(wut REQUIRED)
find_package(CURL REQUIRED)
find_package(MbedTLS REQUIRED)

add_executable(geminimywiiu
    src/main.cpp
)

target_include_directories(geminimywiiu PRIVATE
    ${WUT_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(geminimywiiu PRIVATE
    wut::core
    ${CURL_LIBRARIES}
    ${MBEDTLS_LIBRARIES}
    -lssl -lcrypto -lz
)

# Bundle into WUHB for Aroma
wut_create_wuhb(geminimywiiu
    NAME "geminimywiiu"
    AUTHOR "Logan"
    ICON "${CMAKE_SOURCE_DIR}/icon.png"
    TV_IMAGE "${CMAKE_SOURCE_DIR}/tv.png"
    DRC_IMAGE "${CMAKE_SOURCE_DIR}/drc.png"
)
