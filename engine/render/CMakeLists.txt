# eng_render — the ONLY module that sees Vulkan (ADR-0004/0006). Raw Vulkan in src/vk/
# and src/renderer.cpp, behind the renderer.h seam. Hand-loaded loader: links Vulkan
# HEADERS only, never vulkan-1.lib (ADR-0004 §3.2).
add_library(eng_render STATIC src/render.cpp)
add_library(eng::render ALIAS eng_render)

target_include_directories(eng_render
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_render
    PUBLIC  eng::core eng::math eng::platform
    PRIVATE moba_warnings moba_options)

# Real Vulkan backend when the SDK headers are present; otherwise a null backend so the
# renderer.h seam still links (CI has no SDK yet). Becomes a hard requirement once the
# SDK is installed in CI (then drop the null path and make find_package REQUIRED).
if(TARGET Vulkan::Headers)
    target_sources(eng_render PRIVATE src/vk/vk.cpp src/renderer.cpp)
    target_link_libraries(eng_render PRIVATE Vulkan::Headers)
    message(STATUS "eng_render: Vulkan backend (SDK ${Vulkan_VERSION})")
else()
    target_sources(eng_render PRIVATE src/renderer_null.cpp)
    message(STATUS "eng_render: NULL backend (no Vulkan SDK — set VULKAN_SDK to enable)")
endif()
