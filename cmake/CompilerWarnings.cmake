# moba_warnings — INTERFACE target carrying the project's warning posture.
# Linked PRIVATE to our OWN targets only, NEVER to Vulkan/third-party (ADR-0006, §3.3).
# /WX is opt-in via MOBA_WERROR (lenient locally so iteration isn't blocked; ON in CI/test).

add_library(moba_warnings INTERFACE)

target_compile_options(moba_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4                 # high warning level
        /permissive-        # strict standard conformance
        /Zc:preprocessor    # conforming preprocessor
        /Zc:__cplusplus     # report the real __cplusplus value
        /utf-8              # source + execution charset = UTF-8
        /diagnostics:caret  # point at the exact token
        /wd4201             # allow nameless struct/union (used by the math types)
    >)

if(MOBA_WERROR)
    target_compile_options(moba_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()
