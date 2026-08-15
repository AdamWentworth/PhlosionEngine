if(NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

set(GL_PATH "${PHLOSION_ROOT}/src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp")
set(D3D_PATH "${PHLOSION_ROOT}/src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp")
set(VK_MATERIAL_PATH "${PHLOSION_ROOT}/assets/shaders/vulkan/world_material.glsl")
set(VK_DIRECT_PATH "${PHLOSION_ROOT}/assets/shaders/vulkan/world.frag")
set(VK_INDIRECT_PATH "${PHLOSION_ROOT}/assets/shaders/vulkan/world_indirect.frag")

file(READ "${GL_PATH}" GL_SOURCE)
foreach(TOKEN IN ITEMS
        "computeMappedNormalFromTexture"
        "uMetallicRoughnessTexture"
        "uMaterialFlipbook1.w"
        "nativeFresnelEffect")
    if(NOT GL_SOURCE MATCHES "${TOKEN}")
        message(FATAL_ERROR "OpenGL FresnelEffect lost token: ${TOKEN}")
    endif()
endforeach()

file(READ "${D3D_PATH}" D3D_SOURCE)
foreach(TOKEN IN ITEMS
        "computeMappedFresnelLayerNormal"
        "gMetalRoughTex"
        "uLightProjectionUvRowU.w"
        "nativeFresnelEffect")
    if(NOT D3D_SOURCE MATCHES "${TOKEN}")
        message(FATAL_ERROR "D3D12 FresnelEffect lost token: ${TOKEN}")
    endif()
endforeach()

file(READ "${VK_MATERIAL_PATH}" VK_MATERIAL_SOURCE)
foreach(TOKEN IN ITEMS
        "layerNormalMap"
        "surfaceControls.w"
        "useMetallicRoughnessMap"
        "evaluateNativeFresnelEffectLayer")
    if(NOT VK_MATERIAL_SOURCE MATCHES "${TOKEN}")
        message(FATAL_ERROR "Vulkan FresnelEffect lost token: ${TOKEN}")
    endif()
endforeach()

foreach(VK_PATH IN ITEMS "${VK_DIRECT_PATH}" "${VK_INDIRECT_PATH}")
    file(READ "${VK_PATH}" VK_SOURCE)
    if(NOT VK_SOURCE MATCHES "evaluateNativeFresnelEffectLayer")
        message(FATAL_ERROR "Vulkan FresnelEffect call is missing from ${VK_PATH}")
    endif()
    if(NOT VK_SOURCE MATCHES "metallicRoughnessTexture")
        message(FATAL_ERROR "Vulkan secondary normal binding is missing from ${VK_PATH}")
    endif()
endforeach()

message(STATUS "Native FresnelEffect primary/secondary normal contract verified")
