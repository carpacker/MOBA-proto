# eng_math — leaf module: fix.h (Q16.16), rng.h, vec/mat/quat/geom (Phase 1).
# M0.2: placeholder TU.
add_library(eng_math STATIC src/math.cpp)
add_library(eng::math ALIAS eng_math)

target_include_directories(eng_math
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_math PRIVATE moba_warnings moba_options)
