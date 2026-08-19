if(NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

set(GL_PATH
    "${PHLOSION_ROOT}/src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp")
set(D3D_PATH
    "${PHLOSION_ROOT}/src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp")
set(VK_DIRECT_PATH
    "${PHLOSION_ROOT}/assets/shaders/vulkan/world.frag")
set(VK_INDIRECT_PATH
    "${PHLOSION_ROOT}/assets/shaders/vulkan/world_indirect.frag")
set(VK_MATERIAL_PATH
    "${PHLOSION_ROOT}/assets/shaders/vulkan/world_material.glsl")

foreach(SOURCE_PATH IN ITEMS
        "${GL_PATH}"
        "${D3D_PATH}"
        "${VK_DIRECT_PATH}"
        "${VK_INDIRECT_PATH}")
    file(READ "${SOURCE_PATH}" SOURCE_TEXT)
    foreach(REQUIRED_TOKEN IN ITEMS
            "nativePlainEye"
            "nativeSeparateEyeCoat")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "PLA eye token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
endforeach()

file(READ "${GL_PATH}" GL_TEXT)
foreach(REQUIRED_TOKEN IN ITEMS
        "nativeEyeClearCoat && uMaterialRect1.w < -0.5"
        "nativeSeparateEyeCoat ? eyeSurfaceNormal : n")
    string(FIND "${GL_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
    if(TOKEN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "OpenGL PLA eye-specular token '${REQUIRED_TOKEN}' is missing")
    endif()
endforeach()

file(READ "${D3D_PATH}" D3D_TEXT)
foreach(REQUIRED_TOKEN IN ITEMS
        "nativeEyeMode && uProjectedShadowRowY.w < -0.5f"
        "normalScale * 0.8f"
        "if (nativeSeparateEyeCoat)")
    string(FIND "${D3D_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
    if(TOKEN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "D3D12 PLA eye-specular token '${REQUIRED_TOKEN}' is missing")
    endif()
endforeach()

foreach(SOURCE_PATH IN ITEMS "${VK_DIRECT_PATH}" "${VK_INDIRECT_PATH}")
    file(READ "${SOURCE_PATH}" SOURCE_TEXT)
    foreach(REQUIRED_TOKEN IN ITEMS
            "nativePlainEye"
            "pbrFactors.x * 0.8"
            "? -2.0"
            "nativeSeparateEyeCoat ? 0.0 : 1.0")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Vulkan PLA eye-specular token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
endforeach()

file(READ "${VK_MATERIAL_PATH}" VK_MATERIAL_TEXT)
string(FIND
    "${VK_MATERIAL_TEXT}"
    "dielectricSpecularIntensity < -1.5"
    VK_SENTINEL_OFFSET)
if(VK_SENTINEL_OFFSET EQUAL -1)
    message(FATAL_ERROR
        "Vulkan PLA sparse-highlight sentinel is missing")
endif()

message(STATUS "PLA eye dielectric-specular parity contract verified")
