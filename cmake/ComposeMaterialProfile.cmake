include("${CMAKE_CURRENT_LIST_DIR}/ExpandMaterialSource.cmake")

function(phlosion_compose_material_profile)
    cmake_parse_arguments(PM "" "TARGET;OUTPUT_DIRECTORY;SOURCE_DIRECTORY;FIELD_SOURCE_DIR" "" ${ARGN})
    if(PM_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown material composition arguments: ${PM_UNPARSED_ARGUMENTS}")
    endif()
    if (NOT PM_TARGET OR NOT PM_OUTPUT_DIRECTORY OR NOT PM_SOURCE_DIRECTORY)
        message(FATAL_ERROR "Material composition requires target, output and source directory")
    endif()
    get_filename_component(_material_engine_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(_material_output "${PM_OUTPUT_DIRECTORY}")
    file(MAKE_DIRECTORY "${_material_output}")
    set(_material_templates "${_material_engine_root}/assets/shaders/vulkan")
    file(GLOB _material_engine_includes CONFIGURE_DEPENDS "${_material_templates}/*.glsl")
    file(GLOB_RECURSE _material_sources CONFIGURE_DEPENDS
        "${PM_SOURCE_DIRECTORY}/*.glsl"
        "${PM_SOURCE_DIRECTORY}/*.hlsl"
        "${_material_engine_root}/assets/shaders/materials/*.glsl"
        "${_material_engine_root}/assets/shaders/materials/*.hlsl")
    if(PM_FIELD_SOURCE_DIR)
        file(GLOB_RECURSE _field_sources CONFIGURE_DEPENDS
            "${PM_FIELD_SOURCE_DIR}/*.glsl" "${PM_FIELD_SOURCE_DIR}/*.hlsl")
        list(APPEND _material_sources ${_field_sources})
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_material_sources})

    foreach(_api IN ITEMS opengl d3d12 world world_indirect)
        if (_api STREQUAL "d3d12")
            set(_extension hlsl)
        else()
            set(_extension glsl)
        endif()
        foreach(_part IN ITEMS declarations evaluation)
            if (_api MATCHES "^world")
                set(_field_name "project_${_api}_${_part}.glsl")
                set(_output_name "project_${_api}_${_part}.glsl")
            else()
                set(_field_name "field_${_api}_${_part}.${_extension}")
                set(_output_name "${_api}_${_part}.${_extension}")
            endif()
            set(_field "")
            if (PM_FIELD_SOURCE_DIR)
                file(READ "${PM_FIELD_SOURCE_DIR}/${_field_name}" _field)
            endif()
            file(READ "${PM_SOURCE_DIRECTORY}/${_api}_${_part}.${_extension}" _profile)
            string(TOUPPER "${_part}" _part_upper)
            string(REPLACE "__PROJECT_FIELD_${_part_upper}__" "${_field}" _composed "${_profile}")
            phlosion_expand_material_source("${_composed}" "${PM_SOURCE_DIRECTORY}" _composed)
            file(CONFIGURE OUTPUT "${_material_output}/${_output_name}" CONTENT "${_composed}" @ONLY)
        endforeach()
        if (_api MATCHES "^world")
            configure_file("${PM_SOURCE_DIRECTORY}/${_api}_vertex_evaluation.glsl"
                "${_material_output}/project_${_api}_vertex_evaluation.glsl" COPYONLY)
        else()
            foreach(_part IN ITEMS declarations evaluation)
                configure_file("${PM_SOURCE_DIRECTORY}/${_api}_vertex_${_part}.${_extension}"
                    "${_material_output}/${_api}_vertex_${_part}.${_extension}" COPYONLY)
            endforeach()
        endif()
    endforeach()
    file(GLOB _material_helpers CONFIGURE_DEPENDS
        "${PM_SOURCE_DIRECTORY}/*.glsl" "${PM_SOURCE_DIRECTORY}/*.hlsl")
    foreach(_source IN LISTS _material_helpers)
        get_filename_component(_name "${_source}" NAME)
        if (_name MATCHES "^(opengl|d3d12|world|world_indirect)_(vertex_)?(declarations|evaluation)\\.(glsl|hlsl)$")
            continue()
        endif()
        file(READ "${_source}" _helper)
        phlosion_expand_material_source("${_helper}" "${PM_SOURCE_DIRECTORY}" _helper)
        file(CONFIGURE OUTPUT "${_material_output}/${_name}" CONTENT "${_helper}" @ONLY)
    endforeach()

    set(_material_outputs)
    foreach(_variant IN ITEMS world world_indirect)
        foreach(_stage IN ITEMS vert frag)
            if (_stage STREQUAL "frag")
                set(_blends standard dual_source)
            else()
                set(_blends standard)
            endif()
            foreach(_blend IN LISTS _blends)
                set(_defines -DPHLOSION_PROJECT_MATERIAL=1)
                set(_name "${_variant}")
                if (_blend STREQUAL "dual_source")
                    list(APPEND _defines -DPHLOSION_VULKAN_DUAL_SOURCE_BLEND=1)
                    string(APPEND _name "_dual_source")
                endif()
                set(_output "${_material_output}/${_name}.${_stage}.spv")
                add_custom_command(OUTPUT "${_output}"
                    COMMAND "${PHLOSION_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
                        "-I${_material_templates}" "-I${_material_output}" ${_defines}
                        -o "${_output}" "${_material_templates}/${_variant}.${_stage}"
                    DEPENDS "${_material_templates}/${_variant}.${_stage}" ${_material_engine_includes} ${_material_sources}
                    COMMENT "Compiling game world material ${_name}.${_stage}" VERBATIM)
                list(APPEND _material_outputs "${_output}")
            endforeach()
        endforeach()
    endforeach()
    set(_shadow_output "${_material_output}/world_shadow.frag.spv")
    add_custom_command(OUTPUT "${_shadow_output}"
        COMMAND "${PHLOSION_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
            "-I${_material_templates}" "-I${_material_output}"
            -DPHLOSION_PROJECT_MATERIAL=1 -DPHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS=1
            -o "${_shadow_output}" "${_material_templates}/world.frag"
        DEPENDS "${_material_templates}/world.frag" ${_material_engine_includes} ${_material_sources}
        COMMENT "Compiling game world directional-shadow material" VERBATIM)
    list(APPEND _material_outputs "${_shadow_output}")
    add_custom_target(${PM_TARGET} DEPENDS ${_material_outputs})

endfunction()
