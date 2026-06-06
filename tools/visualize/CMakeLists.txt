# tools/visualize — headless BMP dumps of the fixed-point math (fix_sin/cos/sqrt vs
# libm), so the determinism suite's numbers become images you can open. The fix API is
# header-only (inline + LUT), so this links eng::math mainly for its include path.
add_executable(visualize src/main.cpp)
target_link_libraries(visualize PRIVATE eng::math moba_warnings moba_options)
