if(NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

set(GL_PATH "${PHLOSION_ROOT}/src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp")
set(D3D_PATH "${PHLOSION_ROOT}/src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp")
set(VK_PATH "${PHLOSION_ROOT}/assets/shaders/vulkan/world_material.glsl")

foreach(SOURCE_PATH IN ITEMS "${GL_PATH}" "${D3D_PATH}" "${VK_PATH}")
    file(READ "${SOURCE_PATH}" SOURCE_TEXT)
    foreach(REQUIRED_TOKEN IN ITEMS
            "shadowingGiGain"
            "combinedShadowAmount"
            "shadowAmount * shadowingGiGain"
            "packed shadowSpec RGB is not a multiplicative"
            "normalDotHalf - specularOffset"
            "shadowProcessArea"
            "rimShape")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Z-A IkCharacter shading token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
    foreach(FORBIDDEN_TOKEN IN ITEMS
            "shadowTint"
            "normalDetailDelta"
            "geometricHalfLambert")
        string(FIND "${SOURCE_TEXT}" "${FORBIDDEN_TOKEN}" TOKEN_OFFSET)
        if(NOT TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Z-A IkCharacter duplicates mapped-normal darkening with '${FORBIDDEN_TOKEN}' in ${SOURCE_PATH}")
        endif()
    endforeach()
endforeach()

file(READ "${GL_PATH}" GL_TEXT)
file(READ "${D3D_PATH}" D3D_TEXT)
file(READ "${VK_PATH}" VK_TEXT)
string(REGEX MATCH
    "vec3[ \t\r\n]+shaded[ \t\r\n]*=[ \t\r\n]*mix\\([ \t\r\n]*albedo,[ \t\r\n]*shadowSpec\\.rgb,"
    GL_ABSOLUTE_SHADOW_MATCH "${GL_TEXT}")
string(REGEX MATCH
    "float3[ \t\r\n]+shaded[ \t\r\n]*=[ \t\r\n]*lerp\\([ \t\r\n]*albedo,[ \t\r\n]*shadowSpec\\.rgb,"
    D3D_ABSOLUTE_SHADOW_MATCH "${D3D_TEXT}")
string(REGEX MATCH
    "vec3[ \t\r\n]+shaded[ \t\r\n]*=[ \t\r\n]*mix\\([ \t\r\n]*sourceAlbedo,[ \t\r\n]*shadowSpec\\.rgb,"
    VK_ABSOLUTE_SHADOW_MATCH "${VK_TEXT}")
if(NOT GL_ABSOLUTE_SHADOW_MATCH OR
   NOT D3D_ABSOLUTE_SHADOW_MATCH OR
   NOT VK_ABSOLUTE_SHADOW_MATCH)
    message(FATAL_ERROR
        "Z-A IkCharacter must interpolate albedo to its absolute packed shadow color")
endif()

message(STATUS "Z-A IkCharacter absolute-shadow and single-normal-response contract verified")
