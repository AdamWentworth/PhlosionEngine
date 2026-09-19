cmake_minimum_required(VERSION 3.22)
project(PhlosionEngine VERSION 0.1.0 LANGUAGES CXX)

# RelWithDebInfo backs the Development editor: optimization, symbols and assertions.
# Keep the release CRT/iterator ABI so optimized engine and game agree.
string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_phlosion_tests_default ON)
    set(_phlosion_editor_default ON)
else()
    set(_phlosion_tests_default OFF)
    set(_phlosion_editor_default OFF)
endif()

set(_phlosion_werror_default OFF)
if (DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "" AND NOT "$ENV{CI}" STREQUAL "0")
    set(_phlosion_werror_default ON)
endif()

option(PHLOSION_BUILD_TESTS "Build Phlosion Engine contract tests" ${_phlosion_tests_default})
option(PHLOSION_BUILD_EDITOR "Build the native Phlosion Editor and UI framework" ${_phlosion_editor_default})
option(PHLOSION_ENABLE_WARNINGS "Enable compiler warnings for engine sources" ON)
option(PHLOSION_WARNINGS_AS_ERRORS "Treat engine warnings as errors" ${_phlosion_werror_default})
option(PHLOSION_VERBOSE_STARTUP "Enable verbose startup and model-load logging" OFF)
option(PHLOSION_OPTIMIZE_RENDER_HOTPATHS_IN_DEBUG
    "Compile selected renderer hot paths with optimization in Debug"
    ON)
option(PHLOSION_OPTIMIZE_D3D12_SHADERS_IN_DEBUG
    "Compile D3D12 shaders with optimization plus debug information in Debug"
    ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
if (PHLOSION_BUILD_EDITOR)
    find_package(imgui CONFIG REQUIRED)
endif()

if (TARGET sol2::sol2)
    set(PHLOSION_SOL2_TARGET sol2::sol2)
elseif (TARGET sol2)
    set(PHLOSION_SOL2_TARGET sol2)
else()
    message(FATAL_ERROR "sol2 was found without a supported CMake target.")
endif()

if (TARGET SDL2_ttf::SDL2_ttf)
    set(PHLOSION_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf)
elseif (TARGET SDL2_ttf::SDL2_ttf-static)
    set(PHLOSION_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf-static)
else()
    message(FATAL_ERROR "SDL2_ttf was found without a supported CMake target.")
endif()

find_program(PHLOSION_GLSLANG_VALIDATOR
    NAMES glslangValidator glslangValidator.exe
    HINTS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/glslang"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_HOST_TRIPLET}/tools/glslang"
    REQUIRED
)

set(PHLOSION_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PHLOSION_VULKAN_SHADER_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan")
set(PHLOSION_VULKAN_SHADER_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/vulkan")
set(PHLOSION_VULKAN_SHADER_SOURCES
    debug.vert
    debug.frag
    sprite.vert
    sprite.frag
    world.vert
    world.frag
    scene_color.vert
    scene_color.frag
    world_indirect.vert
    world_indirect.frag
)

set(PHLOSION_VULKAN_SHADER_OUTPUTS)
foreach(_shader IN LISTS PHLOSION_VULKAN_SHADER_SOURCES)
    set(_source "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/${_shader}")
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/${_shader}.spv")
    set(_shader_dependencies "${_source}")
    if (_shader STREQUAL "world.frag")
        list(APPEND _shader_dependencies
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_material.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_tail_fire.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_environment.glsl")
    elseif (_shader STREQUAL "world_indirect.vert")
        list(APPEND _shader_dependencies
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_indirect_state.glsl")
    elseif (_shader STREQUAL "world_indirect.frag")
        list(APPEND _shader_dependencies
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_indirect_state.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_material.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_tail_fire.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_environment.glsl")
    endif()
    add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}"
        COMMAND "${PHLOSION_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
            "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}"
            -o "${_output}" "${_source}"
        DEPENDS ${_shader_dependencies}
        COMMENT "Compiling Phlosion Vulkan shader ${_shader}"
        VERBATIM
    )
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()

foreach(_shader IN ITEMS world world_indirect)
    set(_source "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/${_shader}.frag")
    set(_output
        "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/${_shader}_dual_source.frag.spv")
    set(_shader_dependencies
        "${_source}"
        "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_material.glsl"
        "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_tail_fire.glsl"
        "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_environment.glsl")
    if (_shader STREQUAL "world_indirect")
        list(APPEND _shader_dependencies
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_indirect_state.glsl")
    endif()
    add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}"
        COMMAND "${PHLOSION_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
            "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}"
            -DPHLOSION_VULKAN_DUAL_SOURCE_BLEND=1
            -o "${_output}" "${_source}"
        DEPENDS ${_shader_dependencies}
        COMMENT "Compiling Phlosion Vulkan dual-source shader ${_shader}.frag"
        VERBATIM
    )
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()

add_custom_target(phlosion_vulkan_shaders
    DEPENDS ${PHLOSION_VULKAN_SHADER_OUTPUTS})

function(phlosion_apply_settings target_name)
    target_include_directories(${target_name}
        PUBLIC
            "$<BUILD_INTERFACE:${PHLOSION_SOURCE_ROOT}>"
    )
    target_compile_definitions(${target_name}
        PRIVATE
            PHLOSION_VERBOSE_STARTUP=$<BOOL:${PHLOSION_VERBOSE_STARTUP}>
    )

    if (MSVC)
        target_compile_options(${target_name} PRIVATE /permissive- /Zc:__cplusplus /FS /MP)
        if (PHLOSION_ENABLE_WARNINGS)
            target_compile_options(${target_name} PRIVATE /W4)
            if (PHLOSION_WARNINGS_AS_ERRORS)
                target_compile_options(${target_name} PRIVATE /WX)
            endif()
        endif()
    elseif (PHLOSION_ENABLE_WARNINGS)
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if (PHLOSION_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(engine_core STATIC
    src/engine/core/SystemRegistry.cpp
    src/engine/core/StdoutLogger.cpp
    src/engine/core/EventBus.cpp
    src/engine/core/ecs/World.cpp
    src/engine/assets/phlosion/PhlosionAuthoredScene.cpp
    src/engine/assets/phlosion/PhlosionEnvironmentPatch.cpp
    src/engine/assets/phlosion/PhlosionResourceContainer.cpp
    src/engine/assets/phlosion/PhlosionSceneArchive.cpp
)
target_link_libraries(engine_core
    PUBLIC
        glm::glm
        nlohmann_json::nlohmann_json
)
phlosion_apply_settings(engine_core)

add_library(engine_platform STATIC
    src/engine/platform/Window.cpp
    src/engine/input/SdlKeyMap.cpp
)
target_link_libraries(engine_platform
    PUBLIC
        SDL2::SDL2
        ${PHLOSION_SDL2_TTF_TARGET}
        glm::glm
)
phlosion_apply_settings(engine_platform)

add_library(engine_render STATIC
    src/engine/render/D3D12RenderBackend.cpp
    src/engine/render/d3d12/D3D12RenderBackendPipelineCompile.cpp
    src/engine/render/d3d12/D3D12RenderBackendDebugPipeline.cpp
    src/engine/render/d3d12/D3D12RenderBackendEditor.cpp
    src/engine/render/d3d12/D3D12RenderBackendSpritePipeline.cpp
    src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp
    src/engine/render/d3d12/D3D12RenderBackendDebugDraw.cpp
    src/engine/render/d3d12/D3D12RenderBackendTextures.cpp
    src/engine/render/d3d12/D3D12RenderBackendCachedWorldMeshes.cpp
    src/engine/render/d3d12/D3D12RenderBackendWorldScene.cpp
    src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp
    src/engine/render/d3d12/D3D12RenderBackendWorldDrawEntryPoints.cpp
    src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp
    src/engine/render/d3d12/D3D12RenderBackendSceneColor.cpp
    src/engine/render/d3d12/D3D12TextureUpload.cpp
    src/engine/render/OpenGLRenderBackend.cpp
    src/engine/render/opengl/OpenGLRenderBackendDebugDraw.cpp
    src/engine/render/opengl/OpenGLRenderBackendPipelines.cpp
    src/engine/render/opengl/OpenGLRenderBackendShaderUtils.cpp
    src/engine/render/opengl/OpenGLRenderBackendTextures.cpp
    src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp
    src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp
    src/engine/render/opengl/OpenGLRenderBackendWorldBatchSubmission.cpp
    src/engine/render/opengl/OpenGLRenderBackendSceneColor.cpp
    src/engine/render/opengl/OpenGLRenderBackendWorldCachedMesh.cpp
    src/engine/render/opengl/OpenGLRenderBackendWorldPrewarm.cpp
    src/engine/render/VulkanRenderBackend.cpp
    src/engine/render/vulkan/VulkanRenderBackendLifecycle.cpp
    src/engine/render/vulkan/VulkanRenderBackendSceneColor.cpp
    src/engine/render/vulkan/VulkanRenderBackendDraw.cpp
    src/engine/render/vulkan/VulkanRenderBackendEditor.cpp
    src/engine/render/vulkan/VulkanRenderBackendSprites.cpp
    src/engine/render/vulkan/VulkanGeometryArenaLayout.cpp
    src/engine/render/vulkan/VulkanRenderBackendGeometry.cpp
    src/engine/render/vulkan/VulkanRenderBackendGeometryArena.cpp
    src/engine/render/vulkan/VulkanRenderBackendInstances.cpp
    src/engine/render/vulkan/VulkanRenderBackendIndirectWorldScene.cpp
    src/engine/render/vulkan/VulkanRenderBackendState.cpp
    src/engine/render/vulkan/VulkanWorldIndirectBatch.cpp
    src/engine/render/vulkan/VulkanRenderBackendWorldScene.cpp
    src/engine/render/vulkan/VulkanRenderBackendEnvironment.cpp
    src/engine/render/vulkan/VulkanRenderBackendMaterials.cpp
    src/engine/render/vulkan/VulkanRenderBackendMaterialTable.cpp
    src/engine/render/vulkan/VulkanRenderBackendTextures.cpp
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/Model.cpp
    src/engine/render/ModelAnimation.cpp
    src/engine/render/ModelCache.cpp
    src/engine/render/RendererParityContract.cpp
    src/engine/render/WorldPbrShaderShared.cpp
    src/engine/render/NeutralPmrem.cpp
    src/engine/render/gltf/ModelFastGltfLoader.cpp
    src/engine/render/gltf/ModelFastGltfLoaderHelpers.cpp
    src/engine/render/gltf/ModelFastGltfMaterial.cpp
    src/engine/render/gltf/ModelFastGltfSceneData.cpp
    src/engine/render/gltf/ModelFastGltfTextures.cpp
    src/engine/ui/UIManager.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/BootLoadingView.cpp
    src/engine/utils/Shader.cpp
    src/engine/utils/ShaderCache.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/Log.cpp
    src/engine/utils/LogSink.cpp
    src/engine/utils/stb_image_impl.cpp
    src/engine/utils/stb_image_write_impl.cpp
    src/engine/vfx/ParticleSystem.cpp
)
target_link_libraries(engine_render
    PUBLIC
        engine_core
        engine_platform
        SDL2::SDL2
        OpenGL::GL
        Vulkan::Vulkan
        glad::glad
        glm::glm
        lua
        nlohmann_json::nlohmann_json
        fastgltf::fastgltf
        ${PHLOSION_SOL2_TARGET}
)
add_dependencies(engine_render phlosion_vulkan_shaders)
target_compile_definitions(engine_render
    PRIVATE
        PHLOSION_VULKAN_SHADER_DIR="${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}"
)
if (WIN32)
    target_link_libraries(engine_render PUBLIC d3d12 dxgi d3dcompiler)
endif()
if (PHLOSION_OPTIMIZE_D3D12_SHADERS_IN_DEBUG)
    target_compile_definitions(engine_render
        PRIVATE
            "$<$<CONFIG:Debug>:PHLOSION_OPTIMIZE_D3D12_SHADERS_IN_DEBUG=1>")
endif()
phlosion_apply_settings(engine_render)

add_library(Engine INTERFACE)
target_link_libraries(Engine
    INTERFACE
        engine_core
        engine_platform
        engine_render
)
target_include_directories(Engine
    INTERFACE
        "$<BUILD_INTERFACE:${PHLOSION_SOURCE_ROOT}>"
)
add_library(Phlosion::Engine ALIAS Engine)

add_library(engine_runtime STATIC
    src/engine/runtime/Application.cpp
)
target_link_libraries(engine_runtime PUBLIC Engine)
phlosion_apply_settings(engine_runtime)

add_library(engine_editor_core STATIC
    src/engine/editor/EditorGameplayReload.cpp
    src/engine/editor/EditorGamePreviewRouting.cpp
    src/engine/editor/EditorRendererPreference.cpp
    src/engine/editor/EditorProjectPluginContract.cpp
    src/engine/editor/ProjectDescriptor.cpp
)
target_link_libraries(engine_editor_core
    PUBLIC
        engine_core
        nlohmann_json::nlohmann_json
)
phlosion_apply_settings(engine_editor_core)

if (PHLOSION_BUILD_EDITOR)
    add_library(engine_editor STATIC
        src/engine/editor/D3D12EditorRenderSurface.cpp
        src/engine/editor/EditorShell.cpp
        src/engine/editor/OpenGLEditorRenderSurface.cpp
        src/engine/editor/VulkanEditorRenderSurface.cpp
    )
    target_link_libraries(engine_editor
        PUBLIC
            Engine
            engine_editor_core
        PRIVATE
            imgui::imgui
    )
    phlosion_apply_settings(engine_editor)
    add_library(Phlosion::Editor ALIAS engine_editor)

    add_executable(PhlosionEditor
        src/engine/editor/PhlosionEditorMain.cpp
    )
    target_link_libraries(PhlosionEditor
        PRIVATE
            engine_editor
            OpenGL::GL
            glad::glad
            glm::glm
    )
    target_compile_definitions(PhlosionEditor
        PRIVATE
            PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
    )
    if (WIN32)
        target_link_libraries(PhlosionEditor PRIVATE comdlg32)
    elseif (CMAKE_DL_LIBS)
        target_link_libraries(PhlosionEditor PRIVATE ${CMAKE_DL_LIBS})
    endif()
    set_target_properties(PhlosionEditor PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    phlosion_apply_settings(PhlosionEditor)

    add_executable(PhlosionEditorPluginProbe
        src/engine/editor/EditorProjectPluginProbe.cpp
    )
    target_link_libraries(PhlosionEditorPluginProbe
        PRIVATE
            engine_editor_core
    )
    target_compile_definitions(PhlosionEditorPluginProbe
        PRIVATE
            PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
    )
    if (CMAKE_DL_LIBS)
        target_link_libraries(
            PhlosionEditorPluginProbe PRIVATE ${CMAKE_DL_LIBS})
    endif()
    phlosion_apply_settings(PhlosionEditorPluginProbe)
endif()

add_library(engine_tools_vfx_preview STATIC
    src/engine/tools/vfx_preview/VfxPreviewApp.cpp
)
target_link_libraries(engine_tools_vfx_preview
    PUBLIC
        Engine
        glm::glm
)
phlosion_apply_settings(engine_tools_vfx_preview)

add_library(Phlosion::Core ALIAS engine_core)
add_library(Phlosion::Platform ALIAS engine_platform)
add_library(Phlosion::Render ALIAS engine_render)
add_library(Phlosion::Runtime ALIAS engine_runtime)
add_library(Phlosion::EditorCore ALIAS engine_editor_core)
add_library(Phlosion::VfxPreview ALIAS engine_tools_vfx_preview)

if (PHLOSION_OPTIMIZE_RENDER_HOTPATHS_IN_DEBUG)
    if (MSVC)
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    endif()

    set(_phlosion_hotpath_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/assets/phlosion/PhlosionResourceContainer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/assets/phlosion/PhlosionSceneArchive.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/D3D12RenderBackend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendCachedWorldMeshes.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendTextures.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendWorldDrawEntryPoints.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/d3d12/D3D12RenderBackendWorldScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/d3d12/D3D12TextureUpload.cpp"
    )
    foreach(_source IN LISTS _phlosion_hotpath_sources)
        if (MSVC)
            set_property(SOURCE "${_source}" APPEND PROPERTY COMPILE_OPTIONS
                "$<$<CONFIG:Debug>:/O2>")
            set_source_files_properties("${_source}" PROPERTIES
                MSVC_RUNTIME_CHECKS "")
        else()
            set_property(SOURCE "${_source}" APPEND PROPERTY COMPILE_OPTIONS
                "$<$<CONFIG:Debug>:-O2>")
        endif()
    endforeach()
endif()

if (PHLOSION_BUILD_TESTS)
    include(CTest)
    add_executable(PhlosionEngineTests
        tests/TestMain.cpp
        tests/TestCamera3D.cpp
        tests/TestEditorRendererPreference.cpp
        tests/TestEditorProjectPluginContract.cpp
        tests/TestEditorGamePreviewRouting.cpp
        tests/TestEditorPerformance.cpp
        tests/TestEditorGameplayReload.cpp
        tests/TestProjectDescriptor.cpp
        tests/TestPhlosionAuthoredScene.cpp
        tests/TestPhlosionEnvironmentPatch.cpp
        tests/TestPhlosionResourceContainer.cpp
        tests/TestPhlosionSceneArchive.cpp
    )
    target_link_libraries(PhlosionEngineTests
        PRIVATE
            Phlosion::Core
            Phlosion::EditorCore
            Phlosion::Render
    )
    if (MSVC)
        target_compile_options(PhlosionEngineTests PRIVATE
            /permissive- /Zc:__cplusplus /FS /MP)
    endif()
    add_test(NAME PhlosionEngineContracts COMMAND PhlosionEngineTests)
    add_test(
        NAME PhlosionEngineSemanticBoundary
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckEngineSemanticBoundary.cmake)
    add_test(
        NAME PhlosionPackedNormalContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckPackedNormalContract.cmake)
    add_test(
        NAME PhlosionSvLocalSpecularProbeContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckSvLocalSpecularProbeContract.cmake)
    add_test(
        NAME PhlosionZaLocalReflectionProbeContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckZaLocalReflectionProbeContract.cmake)
    add_test(
        NAME PhlosionZaEyeParallaxContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckZaEyeParallaxContract.cmake)
    add_test(
        NAME PhlosionPlaEyeSpecularContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckPlaEyeSpecularContract.cmake)
    add_test(
        NAME PhlosionZaSceneLightContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckZaSceneLightContract.cmake)
    add_test(
        NAME PhlosionZaIkCharacterShadingContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckZaIkCharacterShadingContract.cmake)
    add_test(
        NAME PhlosionNativeFresnelEffectContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckNativeFresnelEffectContract.cmake)
    add_test(
        NAME PhlosionSvNativeSssEnvironmentContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckSvNativeSssEnvironmentContract.cmake)
    add_test(
        NAME PhlosionMaterialDebugViewContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckMaterialDebugViewContract.cmake)
    add_test(
        NAME PhlosionReviewLightingProfileContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckReviewLightingProfileContract.cmake)
endif()
