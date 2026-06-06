# tools/sandbox — day-one bring-up exe (console subsystem so stdout is visible).
# M0.3: window + message loop. M2.0: also brings up the renderer (Vulkan or null).
add_executable(sandbox src/main.cpp)
target_link_libraries(sandbox PRIVATE eng::platform eng::render moba_warnings moba_options)
