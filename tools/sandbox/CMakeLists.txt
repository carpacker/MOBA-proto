# tools/sandbox — day-one bring-up exe (console subsystem so stdout is visible).
# M0.3: window + message loop. Grows to "window + Vulkan clear" in Phase 2.
add_executable(sandbox src/main.cpp)
target_link_libraries(sandbox PRIVATE eng::platform moba_warnings moba_options)
