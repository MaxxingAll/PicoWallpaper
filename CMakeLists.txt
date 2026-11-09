cmake_minimum_required(VERSION 3.20)
project(PicoWallpaper VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "PicoWallpaper builds on Windows 10/11 only")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "PicoWallpaper requires the Visual Studio C++ toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(PicoWallpaper WIN32
    src/main.cpp
    src/core/config.cpp
    src/core/playback_clock.cpp
    src/decoder/mf_decoder.cpp
    src/decoder/mf_video_format.cpp
    src/platform/win32/desktop_injector.cpp
    src/platform/win32/d3d11_presenter.cpp
    src/platform/win32/monitors.cpp
    src/platform/win32/power_governor.cpp
    src/platform/win32/startup.cpp
    src/platform/win32/tray_icon.cpp
    src/app.rc
)

target_include_directories(PicoWallpaper PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(PicoWallpaper PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    PICOWP_VERSION="${PROJECT_VERSION}"
)

target_compile_options(PicoWallpaper PRIVATE
    /GR- /EHsc /W4 /utf-8
    "$<$<CONFIG:Release>:/O1;/Oi;/Gy;/GL>"
)
target_link_options(PicoWallpaper PRIVATE
    "$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF;/INCREMENTAL:NO;/MAP:$<TARGET_FILE_DIR:PicoWallpaper>/PicoWallpaper.map>"
)

target_link_libraries(PicoWallpaper PRIVATE
    d3d11
    dxgi
    mfplat
    mfreadwrite
    mfuuid
    dwmapi
    user32
    shell32
    ole32
    uuid
    wtsapi32
    advapi32
)

set_target_properties(PicoWallpaper PROPERTIES
    OUTPUT_NAME "PicoWallpaper"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(PicoWallpaperTests
        tests/regression_tests.cpp
        src/core/config.cpp
        src/decoder/mf_video_format.cpp
        src/platform/win32/d3d11_presenter.cpp
        src/platform/win32/monitors.cpp
        src/platform/win32/startup.cpp
    )
    target_include_directories(PicoWallpaperTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(PicoWallpaperTests PRIVATE
        UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_compile_options(PicoWallpaperTests PRIVATE /W4 /EHsc /utf-8)
    target_link_libraries(PicoWallpaperTests PRIVATE d3d11 dxgi mfplat mfuuid ole32 advapi32 shell32 user32)
    add_test(NAME regression COMMAND PicoWallpaperTests)
endif()
