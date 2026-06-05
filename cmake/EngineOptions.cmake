# moba_options — the ONE flag string for the whole project (ADR-0009, §3.3).
# No exceptions, no RTTI; per-config defines; explicit Release whole-program opt.
# Linked PRIVATE to every one of our targets.

add_library(moba_options INTERFACE)

# No RTTI, no exceptions (ADR-0009). _HAS_EXCEPTIONS=0 stops the STL dragging in
# exception machinery under /EHs-c-.
target_compile_options(moba_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/GR- /EHs-c->)

target_compile_definitions(moba_options INTERFACE
    _HAS_EXCEPTIONS=0
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Debug>:MOBA_DEBUG=1>
    $<$<CONFIG:RelWithDebInfo>:MOBA_DEV=1>
    $<$<CONFIG:Release>:MOBA_RELEASE=1>)

# Release whole-program optimization must be explicit AND uniform: CMake's default
# MSVC Release has neither /GL nor /LTCG, and /GL requires matching /LTCG at link
# (and at the static-lib archive step) or LTO silently disengages / fails to link.
target_compile_options(moba_options INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL>)
target_link_options(moba_options INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG>)
set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG"
    CACHE STRING "" FORCE)

# ASan lives in a dedicated DebugASan config, added with the arena allocator (M1.0)
# once arenas have poison hooks — see ARCHITECTURE §3.3. Not wired in the M0.2 spine.
