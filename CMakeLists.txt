cmake_minimum_required(VERSION 3.16)
project(twilight-boxart VERSION 2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find raylib on system, or fetch it automatically
find_package(raylib QUIET)
if(NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)
endif()

# Find libcurl (must be installed on the system)
find_package(CURL REQUIRED)

add_executable(twilight-boxart src/main.c src/crawler.c src/platform.c)
target_include_directories(twilight-boxart PRIVATE src)
target_link_libraries(twilight-boxart raylib CURL::libcurl)

if(APPLE)
    target_link_libraries(twilight-boxart
        "-framework Cocoa"
        "-framework IOKit"
        "-framework OpenGL"
        "-framework CoreVideo"
    )
elseif(WIN32)
    target_link_libraries(twilight-boxart ole32 shell32 winmm gdi32 opengl32)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(twilight-boxart Threads::Threads m dl)
endif()
