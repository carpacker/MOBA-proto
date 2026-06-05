# add_shader_library(TARGET SOURCES a.vert b.frag ...) — GLSL -> SPIR-V offline
# via glslc, with -MD/-MF depfiles so editing an included .glsl retriggers dependents
# (ADR-0008, §3.4). Output .spv lands in ${CMAKE_BINARY_DIR}/shaders, located at
# runtime via the MOBA_SHADER_DIR compile def. SPIR-V is loaded directly through the
# platform file API, NOT wrapped in the asset system.
#
# Requires the Vulkan SDK (Vulkan_GLSLC_EXECUTABLE). Not called by the M0.2 spine
# (no shaders yet); it fails loudly if invoked without glslc.

function(add_shader_library TARGET)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "add_shader_library(${TARGET}): glslc not found. "
                            "Install the Vulkan SDK (find_package(Vulkan)).")
    endif()
    set(out_dir "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    set(spv_files "")
    foreach(src ${ARG_SOURCES})
        get_filename_component(name "${src}" NAME)
        set(spv "${out_dir}/${name}.spv")
        add_custom_command(OUTPUT "${spv}"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE}
                    $<$<CONFIG:Debug>:-g> $<$<NOT:$<CONFIG:Debug>>:-O>
                    --target-env=vulkan1.3 -MD -MF "${spv}.d"
                    "${CMAKE_SOURCE_DIR}/${src}" -o "${spv}"
            DEPENDS "${CMAKE_SOURCE_DIR}/${src}"
            DEPFILE "${spv}.d"
            VERBATIM)
        list(APPEND spv_files "${spv}")
    endforeach()
    add_custom_target(${TARGET} ALL DEPENDS ${spv_files})
    set_target_properties(${TARGET} PROPERTIES SHADER_OUTPUT_DIR "${out_dir}")
endfunction()
