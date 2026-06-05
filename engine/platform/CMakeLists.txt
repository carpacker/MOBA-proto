# eng_platform — the OS seam (ADR-0005). Win32 impl in src/win32/.
add_library(eng_platform STATIC src/win32/win32_platform.cpp)
add_library(eng::platform ALIAS eng_platform)

target_include_directories(eng_platform
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_platform
    PUBLIC  eng::core
    PRIVATE moba_warnings moba_options
            user32 gdi32 winmm)   # window/input, GDI, timeBeginPeriod
