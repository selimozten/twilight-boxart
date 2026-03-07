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

    # --- macOS .app bundle ---
    set(APP_NAME "twilight-boxart")
    set(APP_DISPLAY_NAME "Twilight BoxArt")
    set(APP_BUNDLE_ID "com.selimozten.twilight-boxart")
    set(APP_EXECUTABLE "twilight-boxart")

    configure_file(${CMAKE_SOURCE_DIR}/macos/Info.plist.in
                   ${CMAKE_BINARY_DIR}/Info.plist @ONLY)

    add_custom_target(app-bundle ALL
        DEPENDS twilight-boxart
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/Twilight BoxArt.app"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Twilight BoxArt.app/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Twilight BoxArt.app/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/twilight-boxart"
                "${CMAKE_BINARY_DIR}/Twilight BoxArt.app/Contents/MacOS/twilight-boxart"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/Info.plist"
                "${CMAKE_BINARY_DIR}/Twilight BoxArt.app/Contents/Info.plist"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/macos/app.icns"
                "${CMAKE_BINARY_DIR}/Twilight BoxArt.app/Contents/Resources/app.icns"
        COMMENT "Creating Twilight BoxArt.app bundle"
    )
elseif(WIN32)
    target_link_libraries(twilight-boxart ole32 shell32 winmm gdi32 opengl32)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(twilight-boxart Threads::Threads m dl)
endif()
