# eng_core — leaf foundation: assertions, memory (arenas/allocator); containers,
# handle.h, sim_config.h, log, test harness arrive across Phase 1. Depends on
# nothing (the OS page backend is injected into virtual arenas, ADR-0005).
add_library(eng_core STATIC
    src/core.cpp
    src/core_assert.cpp
    src/mem/arena.cpp)
add_library(eng::core ALIAS eng_core)

target_include_directories(eng_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_core PRIVATE moba_warnings moba_options)
