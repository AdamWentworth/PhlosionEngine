if(NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

set(GL_PATH "${PHLOSION_ROOT}/src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp")
set(D3D_PATH "${PHLOSION_ROOT}/src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp")
set(VK_PATH "${PHLOSION_ROOT}/assets/shaders/vulkan/world_material.glsl")

foreach(SOURCE_PATH IN ITEMS "${GL_PATH}" "${D3D_PATH}" "${VK_PATH}")
    file(READ "${SOURCE_PATH}" SOURCE_TEXT)
    if(NOT SOURCE_TEXT MATCHES "sampleSvLocalSpecularProbe")
        message(FATAL_ERROR "SV local-probe sampler is missing from ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES "atlasSize.*atlasSize.y.*3|atlasWidth.*atlasHeight.*3")
        message(FATAL_ERROR "SV packed-cube atlas qualification is missing from ${SOURCE_PATH}")
    endif()
endforeach()

file(READ "${GL_PATH}" GL_SOURCE)
file(READ "${D3D_PATH}" D3D_SOURCE)
file(READ "${VK_PATH}" VK_SOURCE)
if(NOT GL_SOURCE MATCHES "decodeSvLocalProbeHalf" OR
   NOT GL_SOURCE MATCHES "texelFetch")
    message(FATAL_ERROR "OpenGL lost exact packed-half reconstruction")
endif()
if(NOT D3D_SOURCE MATCHES "f16tof32" OR
   NOT D3D_SOURCE MATCHES "probeTexture.GetDimensions")
    message(FATAL_ERROR "D3D12 lost exact packed-half reconstruction")
endif()
if(NOT VK_SOURCE MATCHES "decodeSvLocalProbeHalf" OR
   NOT VK_SOURCE MATCHES "texelFetch")
    message(FATAL_ERROR "Vulkan lost exact packed-half reconstruction")
endif()

message(STATUS "SV local specular probe contract verified")
