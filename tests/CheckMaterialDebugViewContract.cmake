if(NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

set(BACKEND_PATHS
    "${PHLOSION_ROOT}/src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp"
    "${PHLOSION_ROOT}/src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp"
    "${PHLOSION_ROOT}/assets/shaders/vulkan/world.frag"
    "${PHLOSION_ROOT}/assets/shaders/vulkan/world_indirect.frag")

foreach(SOURCE_PATH IN LISTS BACKEND_PATHS)
    file(READ "${SOURCE_PATH}" SOURCE_TEXT)
    if(NOT SOURCE_TEXT MATCHES "Raw base-color texture sample")
        message(FATAL_ERROR "Raw base-color diagnostic is missing from ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES "Authored tint-resolved albedo without lighting")
        message(FATAL_ERROR "Resolved albedo diagnostic is missing from ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES "pbrDebugView < 7\\.5")
        message(FATAL_ERROR "Seven material diagnostics are not mapped in ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES "nativeFresnelEffectBase")
        message(FATAL_ERROR "Resolved albedo omits native Fresnel color factors in ${SOURCE_PATH}")
    endif()
endforeach()

file(READ "${PHLOSION_ROOT}/src/engine/editor/EditorShell.cpp" EDITOR_TEXT)
if(NOT EDITOR_TEXT MATCHES "Raw base-color map" OR
   NOT EDITOR_TEXT MATCHES "Resolved albedo")
    message(FATAL_ERROR "Editor material-view labels do not match the render contract")
endif()

file(READ "${PHLOSION_ROOT}/src/engine/render/IRenderBackend.h" BACKEND_INTERFACE_TEXT)
if(NOT BACKEND_INTERFACE_TEXT MATCHES "std::clamp\\(view, 0, 7\\)")
    message(FATAL_ERROR "Render backend material-debug range does not expose all seven views")
endif()

message(STATUS "Material debug-view contract verified")
