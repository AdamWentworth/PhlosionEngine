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
    if(NOT SOURCE_TEXT MATCHES "applyReviewLightingProfile")
        message(FATAL_ERROR "Review-lighting composite transform is missing from ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES "reviewAlbedo")
        message(FATAL_ERROR "Review-lighting resolved-albedo input is missing from ${SOURCE_PATH}")
    endif()
endforeach()

file(READ
    "${PHLOSION_ROOT}/assets/shaders/vulkan/world_material.glsl"
    VULKAN_SHARED_TEXT)
if(NOT VULKAN_SHARED_TEXT MATCHES "decodeReviewLightingProfile" OR
   NOT VULKAN_SHARED_TEXT MATCHES "shadowFloor" OR
   NOT VULKAN_SHARED_TEXT MATCHES "grazingSurface")
    message(FATAL_ERROR "Vulkan review-lighting profile definitions are incomplete")
endif()

file(READ "${PHLOSION_ROOT}/src/engine/editor/EditorShell.cpp" EDITOR_TEXT)
foreach(LABEL
        "Source Bridge"
        "Neutral Studio"
        "Albedo-biased"
        "Grazing Check")
    if(NOT EDITOR_TEXT MATCHES "${LABEL}")
        message(FATAL_ERROR "Editor review-lighting label is missing: ${LABEL}")
    endif()
endforeach()

file(READ
    "${PHLOSION_ROOT}/src/engine/editor/PhlosionEditorMain.cpp"
    EDITOR_MAIN_TEXT)
if(NOT EDITOR_MAIN_TEXT MATCHES "--asset-preview-lighting=")
    message(FATAL_ERROR "Hidden captures cannot select a review-lighting profile")
endif()

message(STATUS "Review-lighting profile contract verified")
