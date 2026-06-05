# eng_platform — the OS seam (ADR-0005). Win32 impl lives in src/win32/ (M0.3+).
# M0.2: placeholder TU; depends on core (PUBLIC, so the include graph is exercised).
add_library(eng_platform STATIC src/platform.cpp)
add_library(eng::platform ALIAS eng_platform)

target_include_directories(eng_platform
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_platform
    PUBLIC  eng::core
    PRIVATE moba_warnings moba_options)
