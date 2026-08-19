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
            "normalDetailDelta"
            "geometricHalfLambert")
        string(FIND "${SOURCE_TEXT}" "${FORBIDDEN_TOKEN}" TOKEN_OFFSET)
        if(NOT TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Z-A IkCharacter duplicates mapped-normal darkening with '${FORBIDDEN_TOKEN}' in ${SOURCE_PATH}")
        endif()
    endforeach()
endforeach()

message(STATUS "Z-A IkCharacter single normal-response contract verified")
