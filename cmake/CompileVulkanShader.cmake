# Compiler-emitted includes are the authoritative build dependencies.
function(phlosion_compile_vulkan_shader)
    cmake_parse_arguments(PV "" "SOURCE;OUTPUT;COMMENT" "OPTIONS" ${ARGN})
    if(PV_UNPARSED_ARGUMENTS OR NOT PV_SOURCE OR NOT PV_OUTPUT)
        message(FATAL_ERROR "Vulkan shader requires SOURCE, OUTPUT and compiler OPTIONS")
    endif()
    get_filename_component(_directory "${PV_OUTPUT}" DIRECTORY)
    add_custom_command(OUTPUT "${PV_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_directory}"
        COMMAND "${PHLOSION_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
            ${PV_OPTIONS} --depfile "${PV_OUTPUT}.d"
            -o "${PV_OUTPUT}" "${PV_SOURCE}"
        DEPENDS "${PV_SOURCE}" "${PHLOSION_GLSLANG_VALIDATOR}"
        DEPFILE "${PV_OUTPUT}.d"
        COMMENT "${PV_COMMENT}" VERBATIM)
endfunction()
