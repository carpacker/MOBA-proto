# eng_core — leaf module: arenas, containers, log, assert, handle.h, sim_config.h,
# test harness (Phase 1). M0.2: placeholder TU so the lib + link graph exist.
add_library(eng_core STATIC src/core.cpp)
add_library(eng::core ALIAS eng_core)

target_include_directories(eng_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_core PRIVATE moba_warnings moba_options)
