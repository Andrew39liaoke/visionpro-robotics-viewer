cmake_minimum_required(VERSION 3.16)

project(d435i_webrtc_streamer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(realsense2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
)

add_executable(d435i-streamer src/main.cpp)

target_link_libraries(d435i-streamer PRIVATE
    realsense2::realsense2
    PkgConfig::GSTREAMER
)

target_compile_options(d435i-streamer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

install(TARGETS d435i-streamer RUNTIME DESTINATION bin)
