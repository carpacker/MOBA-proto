# eng_render — the ONLY module that sees Vulkan (ADR-0004/0006). Raw Vulkan in
# src/vk/ (Phase 2). M0.2: placeholder TU; depends on core/math/platform.
add_library(eng_render STATIC src/render.cpp)
add_library(eng::render ALIAS eng_render)

target_include_directories(eng_render
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_render
    PUBLIC  eng::core eng::math eng::platform
    PRIVATE moba_warnings moba_options)

# Vulkan HEADERS only — the loader is hand-loaded (ADR-0004, §3.2). Soft for the
# M0.2 spine; becomes REQUIRED at Phase 2.
if(TARGET Vulkan::Headers)
    target_link_libraries(eng_render PRIVATE Vulkan::Headers)
endif()
