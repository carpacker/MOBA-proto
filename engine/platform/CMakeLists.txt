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

# Vulkan surface creation (vkCreateWin32SurfaceKHR) lives in the platform (ADR-0005).
# Headers only, and only when the SDK is present — the headless/CI build stays
# Vulkan-free (platform_vk_create_surface is then simply not compiled).
if(TARGET Vulkan::Headers)
    target_link_libraries(eng_platform PRIVATE Vulkan::Headers)
    target_compile_definitions(eng_platform PRIVATE MOBA_HAVE_VULKAN=1)
endif()
