if(NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

set(GL_PATH "${PHLOSION_ROOT}/src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp")
set(D3D_PATH "${PHLOSION_ROOT}/src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp")
set(VK_PATH "${PHLOSION_ROOT}/assets/shaders/vulkan/world_material.glsl")

foreach(SOURCE_PATH IN ITEMS "${GL_PATH}" "${D3D_PATH}" "${VK_PATH}")
    file(READ "${SOURCE_PATH}" SOURCE_TEXT)
    foreach(REQUIRED_TOKEN IN ITEMS
            "sourceSceneShadowVisibility"
            "sourceSceneShadowBypass"
            "effectiveDirectShadowVisibility"
            "shadowedWrappedLambert"
            "sourceSceneShadowBypass * sourceSceneShadowBypass"
            "biasedLambert * effectiveDirectShadowVisibility"
            "shadowedWrappedLambert - authoredShadowShift"
            "max(directDiffuse"
            "zaIkLocalReflectionDirection"
            "reflect(-viewDirection, mappedNormal)")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Z-A scene-light token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
    if(NOT SOURCE_TEXT MATCHES
            "sourceSceneShadowVisibility = 1\\.0[f]*")
        message(FATAL_ERROR
            "Z-A unavailable scene-shadow boundary is not neutral in ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES
            "sourceSceneShadowBypass = 0\\.0[f]*")
        message(FATAL_ERROR
            "Z-A unavailable scene-shadow bypass is not neutral in ${SOURCE_PATH}")
    endif()
    if(SOURCE_TEXT MATCHES
            "shadowProcessDomain = (clamp|saturate)\\([^\\n]*wrappedLambert - authoredShadowShift")
        message(FATAL_ERROR
            "Z-A ShadowingShift still bypasses the scene-shadow stage in ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES
            "zaIkLocalReflectionDirection[^}]*return reflect\\(-viewDirection, mappedNormal\\)")
        message(FATAL_ERROR
            "Z-A local-reflection direction changed in ${SOURCE_PATH}")
    endif()
endforeach()

message(STATUS "Z-A scene-light staging contract verified")
