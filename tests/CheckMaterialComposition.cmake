cmake_minimum_required(VERSION 3.22)
include("${PHLOSION_ROOT}/cmake/ExpandMaterialSource.cmake")
file(MAKE_DIRECTORY "${TEST_ROOT}")

if(DEFINED REJECT_CASE)
    if(REJECT_CASE STREQUAL "missing")
        set(_source "#include \"phlosion/nonexistent.glsl\"\n")
    elseif(REJECT_CASE STREQUAL "traversal")
        set(_source "#include \"material/../outside.glsl\"\n")
    elseif(REJECT_CASE STREQUAL "unknown_hook")
        set(_source "#include \"material/nonexistent.glsl\"\n")
    elseif(REJECT_CASE STREQUAL "cycle")
        file(MAKE_DIRECTORY "${TEST_ROOT}/material")
        file(WRITE "${TEST_ROOT}/material/loop.glsl" "#include \"material/loop.glsl\"\n")
        set(_source "#include \"material/loop.glsl\"\n")
    endif()
    phlosion_expand_material_source("${_source}" "${TEST_ROOT}" _result)
    message(FATAL_ERROR "Invalid fragment was unexpectedly accepted")
endif()

set(_source "#include \"phlosion/opengl_surface.glsl\"\n")
phlosion_expand_material_source("${_source}" "${TEST_ROOT}/defaults" _default)
if(NOT _default MATCHES "float dielectricSpecular = 0.04;" OR
   NOT _default MATCHES "environmentWetMaterial\\(" OR
   _default MATCHES "#include \"(phlosion|material)/")
    message(FATAL_ERROR "Default surface is incomplete without an extension")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/override/material")
file(WRITE "${TEST_ROOT}/override/material/opengl_surface_parameters.glsl"
    "            float dielectricSpecular = 0.08;\n")
phlosion_expand_material_source("${_source}" "${TEST_ROOT}/override" _override)
string(REPLACE "float dielectricSpecular = 0.04;" "float dielectricSpecular = 0.08;"
    _expected "${_default}")
if(NOT _override STREQUAL _expected)
    message(FATAL_ERROR "Surface override changed unrelated engine behavior")
endif()

foreach(_fragment IN ITEMS surface_lighting.glsl d3d12_surface.hlsl
        opengl_output.glsl d3d12_output.hlsl world_output.glsl world_indirect_output.glsl)
    phlosion_expand_material_source("#include \"phlosion/${_fragment}\"\n"
        "${TEST_ROOT}/defaults" _result)
    if(_result STREQUAL "" OR _result MATCHES "#include \"(phlosion|material)/")
        message(FATAL_ERROR "Incomplete default fragment: ${_fragment}")
    endif()
endforeach()

phlosion_expand_material_source("#include \"world_environment.glsl\"\n"
    "${TEST_ROOT}" _native)
if(NOT _native STREQUAL "#include \"world_environment.glsl\"\n")
    message(FATAL_ERROR "Native compiler include was changed")
endif()
foreach(_case IN ITEMS missing traversal unknown_hook cycle)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DPHLOSION_ROOT=${PHLOSION_ROOT}"
        "-DTEST_ROOT=${TEST_ROOT}/reject-${_case}" "-DREJECT_CASE=${_case}"
        -P "${CMAKE_CURRENT_LIST_FILE}" RESULT_VARIABLE _exit OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(_exit EQUAL 0 OR NOT _err MATCHES "(Missing|Invalid|Cyclic) material fragment")
        message(FATAL_ERROR "Expected explicit fragment rejection for ${_case}: ${_out}${_err}")
    endif()
endforeach()
message(STATUS "Material composition defaults, overrides and invalid inputs passed")
