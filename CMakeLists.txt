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
option(PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE
    "Build the public asset fixture hosts, guard and editor plugin"
    ${_phlosion_tests_default})
option(PHLOSION_BUILD_ANIMATION_FIXTURE
    "Build the animation fixture hosts, guard and editor plugin"
    ${_phlosion_tests_default})
option(PHLOSION_BUILD_MATERIAL_FIXTURE
    "Build the material fixture hosts, guard and editor plugin"
    ${_phlosion_tests_default})
option(PHLOSION_BUILD_SCENE_FIXTURE
    "Build the glTF scene fixture hosts, guard and editor plugin"
    ${_phlosion_tests_default})
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
find_package(Jolt CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
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
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/portable_material.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_material.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_environment.glsl")
    elseif (_shader STREQUAL "world_indirect.vert")
        list(APPEND _shader_dependencies
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_indirect_state.glsl")
    elseif (_shader STREQUAL "world_indirect.frag")
        list(APPEND _shader_dependencies
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/portable_material.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_indirect_state.glsl"
            "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_material.glsl"
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
        "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/portable_material.glsl"
        "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_material.glsl"
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

add_library(engine_collision STATIC
    src/engine/collision/CollisionWorld.cpp
)
target_link_libraries(engine_collision PUBLIC glm::glm PRIVATE Jolt::Jolt)
phlosion_apply_settings(engine_collision)
add_library(Phlosion::Collision ALIAS engine_collision)

add_library(engine_camera STATIC
    src/engine/camera/ExplorationCamera.cpp
    src/engine/camera/BattleCamera.cpp
)
target_link_libraries(engine_camera PUBLIC Phlosion::Collision glm::glm)
phlosion_apply_settings(engine_camera)
add_library(Phlosion::Camera ALIAS engine_camera)

add_library(engine_core STATIC
    src/engine/core/SystemRegistry.cpp
    src/engine/core/StdoutLogger.cpp
    src/engine/core/EventBus.cpp
    src/engine/core/ecs/World.cpp
    src/engine/assets/AsyncAssetLoader.cpp
    src/engine/assets/phlosion/PhlosionAssetGraphLoader.cpp
    src/engine/assets/phlosion/PhlosionArchiveSession.cpp
    src/engine/assets/phlosion/PhlosionAuthoredScene.cpp
    src/engine/assets/phlosion/PhlosionCookedModelPackage.cpp
    src/engine/assets/phlosion/PhlosionEnvironmentPatch.cpp
    src/engine/assets/phlosion/PhlosionMeshAsset.cpp
    src/engine/assets/phlosion/PhlosionResourceContainer.cpp
    src/engine/assets/phlosion/PhlosionSceneArchive.cpp
)
target_link_libraries(engine_core
    PUBLIC
        glm::glm
        nlohmann_json::nlohmann_json
        Threads::Threads
)
phlosion_apply_settings(engine_core)

add_library(engine_image_codecs STATIC
    src/engine/utils/stb_image_impl.cpp
    src/engine/utils/stb_image_write_impl.cpp)
find_path(PHLOSION_STB_INCLUDE_DIR NAMES stb_image.h REQUIRED)
target_include_directories(engine_image_codecs PUBLIC "${PHLOSION_STB_INCLUDE_DIR}")
if (MSVC)
    target_compile_definitions(engine_image_codecs PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
phlosion_apply_settings(engine_image_codecs)
add_library(Phlosion::ImageCodecs ALIAS engine_image_codecs)

add_library(engine_models_core STATIC
    src/engine/render/ModelPoseSampler.cpp
    src/engine/render/AnimationTrackSampler.cpp
    src/engine/models/ModelTracks.cpp
    src/engine/models/PhlosionModelObject.cpp
    src/engine/models/PhlosionTextureDependencyStore.cpp)
target_link_libraries(engine_models_core PUBLIC engine_core PRIVATE KTX::ktx)
phlosion_apply_settings(engine_models_core)
add_library(Phlosion::ModelsCore ALIAS engine_models_core)

if (PHLOSION_BUILD_TESTS)
    add_executable(PhlosionModelsCoreTests tests/TestModelsCore.cpp)
    target_link_libraries(PhlosionModelsCoreTests PRIVATE Phlosion::ModelsCore)
    phlosion_apply_settings(PhlosionModelsCoreTests)
    add_test(NAME PhlosionModelsCore COMMAND PhlosionModelsCoreTests)
endif()

add_library(engine_input STATIC src/engine/input/InputActions.cpp)
target_link_libraries(engine_input PUBLIC glm::glm)
phlosion_apply_settings(engine_input)
add_library(Phlosion::Input ALIAS engine_input)

add_library(engine_ui STATIC
    src/engine/ui/UiFocusNavigation.cpp
    src/engine/ui/UiTextLayout.cpp
)
target_link_libraries(engine_ui PUBLIC Phlosion::Input glm::glm)
phlosion_apply_settings(engine_ui)
add_library(Phlosion::UI ALIAS engine_ui)

add_library(engine_audio STATIC src/engine/audio/AudioMixer.cpp)
target_link_libraries(engine_audio PUBLIC glm::glm)
phlosion_apply_settings(engine_audio)
add_library(Phlosion::Audio ALIAS engine_audio)

add_library(engine_audio_sdl STATIC src/engine/audio/SdlAudioOutput.cpp)
target_link_libraries(engine_audio_sdl PUBLIC Phlosion::Audio SDL2::SDL2)
phlosion_apply_settings(engine_audio_sdl)
add_library(Phlosion::AudioSdl ALIAS engine_audio_sdl)

add_library(engine_platform STATIC
    src/engine/platform/Window.cpp
    src/engine/input/SdlKeyMap.cpp
    src/engine/input/SdlActionInput.cpp
    src/engine/input/SdlGameInput.cpp
)
target_link_libraries(engine_platform
    PUBLIC
        Phlosion::Input
        SDL2::SDL2
        ${PHLOSION_SDL2_TTF_TARGET}
        glm::glm
)
phlosion_apply_settings(engine_platform)

add_library(engine_sdl_replay STATIC src/engine/input/SdlInputReplay.cpp)
target_link_libraries(engine_sdl_replay PUBLIC engine_platform nlohmann_json::nlohmann_json)
phlosion_apply_settings(engine_sdl_replay)

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
    src/engine/render/AnimationBlend.cpp
    src/engine/render/AnimationStateMachine.cpp
    src/engine/render/ModelCacheAnimation.cpp
    src/engine/render/ModelCache.cpp
    src/engine/render/RendererParityContract.cpp
    src/engine/render/WorldPbrShaderShared.cpp
    src/engine/render/WorldMaterialProfile.cpp
    src/engine/render/NeutralPmrem.cpp
    src/engine/render/gltf/ModelFastGltfLoader.cpp
    src/engine/render/gltf/ModelFastGltfLoaderHelpers.cpp
    src/engine/render/gltf/ModelFastGltfMaterial.cpp
    src/engine/render/gltf/ModelFastGltfSceneData.cpp
    src/engine/render/gltf/ModelFastGltfTextures.cpp
    src/engine/render/gltf/GltfSceneAsset.cpp
    src/engine/render/gltf/GltfSceneSession.cpp
    src/engine/render/opengl/OpenGLRenderBackendResourceScopes.cpp
    src/engine/render/vulkan/VulkanRenderBackendResourceScopes.cpp
    src/engine/render/d3d12/D3D12RenderBackendResourceScopes.cpp
    src/engine/render/gltf/GltfSceneRenderer.cpp
    src/engine/ui/UIManager.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/BootLoadingView.cpp
    src/engine/utils/Shader.cpp
    src/engine/utils/ShaderCache.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/Log.cpp
    src/engine/utils/LogSink.cpp
    src/engine/vfx/ParticleSystem.cpp
)
target_link_libraries(engine_render
    PUBLIC
        engine_core
        engine_models_core
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
            engine_sdl_replay
            OpenGL::GL
            glad::glad
            glm::glm
    )
    target_compile_definitions(PhlosionEditor
        PRIVATE
            PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
    )
    if (WIN32)
        set(PHLOSION_EDITOR_ICON_RC
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/branding/phlosion-mark.ico")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/editor/PhlosionEditor.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/PhlosionEditor.rc"
            @ONLY
        )
        target_sources(PhlosionEditor PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/generated/PhlosionEditor.rc")
        target_link_libraries(PhlosionEditor PRIVATE comdlg32)
    elseif (CMAKE_DL_LIBS)
        target_link_libraries(PhlosionEditor PRIVATE ${CMAKE_DL_LIBS})
    endif()
    set_target_properties(PhlosionEditor PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_custom_command(TARGET PhlosionEditor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:PhlosionEditor>/branding"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/editor/branding/phlosion-lockup-cream-300.png"
            "$<TARGET_FILE_DIR:PhlosionEditor>/branding/phlosion-lockup-cream-300.png"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/editor/branding/phlosion-mark-64.png"
            "$<TARGET_FILE_DIR:PhlosionEditor>/branding/phlosion-mark-64.png"
        VERBATIM
    )
    phlosion_apply_settings(PhlosionEditor)

    add_executable(PhlosionEditorPluginProbe
        src/engine/editor/EditorProjectPluginProbe.cpp
    )
    target_link_libraries(PhlosionEditorPluginProbe
        PRIVATE
            engine_editor_core
            SDL2::SDL2
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
target_link_libraries(engine_render PUBLIC Phlosion::ImageCodecs)
add_library(Phlosion::Render ALIAS engine_render)

add_library(engine_models_runtime STATIC
    src/engine/models/WorldIndexedBatches.cpp
    src/engine/models/ModelGeometryCache.cpp
    src/engine/models/ModelMaterials.cpp
    src/engine/models/ModelTransforms.cpp
    src/engine/models/ModelAttachments.cpp
    src/engine/models/ModelInstance.cpp
    src/engine/models/ModelRenderProvider.cpp)
target_link_libraries(engine_models_runtime PUBLIC Phlosion::ModelsCore Phlosion::Render)
phlosion_apply_settings(engine_models_runtime)
add_library(Phlosion::Models ALIAS engine_models_runtime)
target_link_libraries(Engine INTERFACE Phlosion::Models)
add_library(engine_models_editor STATIC src/engine/editor/ModelPreview.cpp)
target_link_libraries(engine_models_editor PUBLIC Phlosion::Models)
phlosion_apply_settings(engine_models_editor)
add_library(Phlosion::ModelPreview ALIAS engine_models_editor)

add_library(PhlosionModelRuntime SHARED EXCLUDE_FROM_ALL tools/models/ModelRuntimePlugin.cpp)
target_link_libraries(PhlosionModelRuntime PRIVATE Phlosion::Models)
phlosion_apply_settings(PhlosionModelRuntime)
add_library(PhlosionModelPreviewPlugin SHARED EXCLUDE_FROM_ALL tools/models/ModelPreviewPlugin.cpp)
target_link_libraries(PhlosionModelPreviewPlugin PRIVATE Phlosion::ModelPreview)
target_compile_definitions(PhlosionModelPreviewPlugin PRIVATE PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>")
phlosion_apply_settings(PhlosionModelPreviewPlugin)
set_target_properties(PhlosionModelRuntime PhlosionModelPreviewPlugin PROPERTIES PREFIX "")

if (PHLOSION_BUILD_TESTS)
    add_executable(PhlosionModelInstanceTests tests/TestModelInstance.cpp)
    target_link_libraries(PhlosionModelInstanceTests PRIVATE Phlosion::Models)
    phlosion_apply_settings(PhlosionModelInstanceTests)
    add_test(NAME PhlosionModelInstance COMMAND PhlosionModelInstanceTests)
endif()

add_library(Phlosion::Runtime ALIAS engine_runtime)
add_library(Phlosion::EditorCore ALIAS engine_editor_core)
add_library(Phlosion::VfxPreview ALIAS engine_tools_vfx_preview)
include(cmake/PhlosionVfx.cmake)

if (PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE OR PHLOSION_BUILD_SCENE_FIXTURE OR PHLOSION_BUILD_TESTS)
    add_library(public_asset_fixture STATIC
        examples/public_asset_fixture/FixtureData.cpp
        examples/public_asset_fixture/FixtureSession.cpp
        examples/public_asset_fixture/FixturePresentation.cpp
    )
    target_link_libraries(public_asset_fixture
        PUBLIC
            Engine
    )
    target_include_directories(public_asset_fixture
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/public_asset_fixture"
    )
    phlosion_apply_settings(public_asset_fixture)

    # Verification support owns image comparison, the guard CLI and the
    # capture harness. The runnable example library never depends on it.
    add_library(public_asset_fixture_verification STATIC
        tools/public_asset_fixture/FixtureImageGuard.cpp
    )
    target_link_libraries(public_asset_fixture_verification
        PUBLIC
            public_asset_fixture
    )
    target_include_directories(public_asset_fixture_verification
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/public_asset_fixture"
    )
    phlosion_apply_settings(public_asset_fixture_verification)
endif()

if (PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE)
    add_executable(PublicAssetFixtureRuntime
        examples/public_asset_fixture/FixtureRuntimeMain.cpp
    )
    target_link_libraries(PublicAssetFixtureRuntime
        PRIVATE
            public_asset_fixture
    )
    phlosion_apply_settings(PublicAssetFixtureRuntime)

    add_executable(PublicAssetFixtureGuard
        tools/public_asset_fixture/FixtureGuardMain.cpp
    )
    target_link_libraries(PublicAssetFixtureGuard
        PRIVATE
            public_asset_fixture_verification
    )
    phlosion_apply_settings(PublicAssetFixtureGuard)

    if (PHLOSION_BUILD_EDITOR)
        add_library(PublicAssetFixtureEditorPlugin MODULE
            examples/public_asset_fixture/FixtureEditorPlugin.cpp
        )
        target_link_libraries(PublicAssetFixtureEditorPlugin
            PRIVATE
                public_asset_fixture
                engine_editor_core
        )
        target_compile_definitions(PublicAssetFixtureEditorPlugin
            PRIVATE
                PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
        )
        set_target_properties(PublicAssetFixtureEditorPlugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "PublicAssetFixtureEditorPlugin"
        )
        phlosion_apply_settings(PublicAssetFixtureEditorPlugin)
    endif()
endif()

if (PHLOSION_BUILD_ANIMATION_FIXTURE OR PHLOSION_BUILD_TESTS)
    add_library(animation_fixture STATIC
        examples/animation_fixture/AnimationFixtureCase.cpp
        examples/animation_fixture/AnimationFixtureRig.cpp
        examples/animation_fixture/AnimationFixturePresentation.cpp
    )
    target_link_libraries(animation_fixture
        PUBLIC
            Engine
    )
    target_include_directories(animation_fixture
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/animation_fixture"
    )
    phlosion_apply_settings(animation_fixture)

    # Verification support owns image comparison, the authored expectations and
    # the guard CLI. The runnable example never depends on it.
    add_library(animation_fixture_verification STATIC
        tools/animation_fixture/AnimationFixtureGuard.cpp
    )
    target_link_libraries(animation_fixture_verification
        PUBLIC
            animation_fixture
    )
    target_include_directories(animation_fixture_verification
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/animation_fixture"
    )
    phlosion_apply_settings(animation_fixture_verification)
endif()

if (PHLOSION_BUILD_ANIMATION_FIXTURE)
    add_executable(AnimationFixtureGuard
        tools/animation_fixture/AnimationFixtureGuardMain.cpp
    )
    target_link_libraries(AnimationFixtureGuard
        PRIVATE
            animation_fixture_verification
    )
    phlosion_apply_settings(AnimationFixtureGuard)

    add_executable(AnimationFixtureRuntime
        examples/animation_fixture/AnimationFixtureRuntimeMain.cpp
    )
    target_link_libraries(AnimationFixtureRuntime
        PRIVATE
            animation_fixture
    )
    phlosion_apply_settings(AnimationFixtureRuntime)

    if (PHLOSION_BUILD_EDITOR)
        add_library(AnimationFixtureEditorPlugin MODULE
            examples/animation_fixture/AnimationFixtureEditorPlugin.cpp
        )
        target_link_libraries(AnimationFixtureEditorPlugin
            PRIVATE
                animation_fixture
                engine_editor_core
        )
        target_compile_definitions(AnimationFixtureEditorPlugin
            PRIVATE
                PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
        )
        set_target_properties(AnimationFixtureEditorPlugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "AnimationFixtureEditorPlugin"
        )
        phlosion_apply_settings(AnimationFixtureEditorPlugin)
    endif()
endif()

if (PHLOSION_BUILD_MATERIAL_FIXTURE OR PHLOSION_BUILD_TESTS)
    add_library(material_fixture STATIC
        examples/material_fixture/MaterialFixtureCase.cpp
        examples/material_fixture/MaterialFixtureAssets.cpp
        examples/material_fixture/MaterialFixturePresentation.cpp
    )
    target_link_libraries(material_fixture
        PUBLIC
            Engine
    )
    target_include_directories(material_fixture
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/material_fixture"
    )
    phlosion_apply_settings(material_fixture)

    # Verification support owns image comparison, the authored expectations and
    # the guard CLI. The runnable example never depends on it.
    add_library(material_fixture_verification STATIC
        tools/material_fixture/MaterialFixtureGuard.cpp
    )
    target_link_libraries(material_fixture_verification
        PUBLIC
            material_fixture
    )
    target_include_directories(material_fixture_verification
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/material_fixture"
    )
    phlosion_apply_settings(material_fixture_verification)
endif()

if (PHLOSION_BUILD_MATERIAL_FIXTURE)
    add_executable(MaterialFixtureGuard
        tools/material_fixture/MaterialFixtureGuardMain.cpp
    )
    target_link_libraries(MaterialFixtureGuard
        PRIVATE
            material_fixture_verification
    )
    phlosion_apply_settings(MaterialFixtureGuard)

    add_executable(MaterialFixtureRuntime
        examples/material_fixture/MaterialFixtureRuntimeMain.cpp
    )
    target_link_libraries(MaterialFixtureRuntime
        PRIVATE
            material_fixture
    )
    phlosion_apply_settings(MaterialFixtureRuntime)

    if (PHLOSION_BUILD_EDITOR)
        add_library(MaterialFixtureEditorPlugin MODULE
            examples/material_fixture/MaterialFixtureEditorPlugin.cpp
        )
        target_link_libraries(MaterialFixtureEditorPlugin
            PRIVATE
                material_fixture
                engine_editor_core
        )
        target_compile_definitions(MaterialFixtureEditorPlugin
            PRIVATE
                PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
        )
        set_target_properties(MaterialFixtureEditorPlugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "MaterialFixtureEditorPlugin"
        )
        phlosion_apply_settings(MaterialFixtureEditorPlugin)
    endif()
endif()

if (PHLOSION_BUILD_SCENE_FIXTURE OR PHLOSION_BUILD_TESTS)
    add_library(scene_fixture STATIC
        examples/scene_fixture/SceneFixtureCase.cpp
        examples/scene_fixture/SceneFixtureAssets.cpp
        examples/scene_fixture/SceneFixturePresentation.cpp
        examples/scene_fixture/SceneFixtureLifecycle.cpp
    )
    target_link_libraries(scene_fixture
        PUBLIC
            Engine
    )
    target_include_directories(scene_fixture
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/scene_fixture"
    )
    phlosion_apply_settings(scene_fixture)

    # Verification support owns the expected-content oracle and the guard CLI.
    # The runnable example library never depends on it.
    add_library(scene_fixture_verification STATIC
        tools/scene_fixture/SceneFixtureGuard.cpp
        tools/scene_fixture/SceneLifecycleGuard.cpp
    )
    target_link_libraries(scene_fixture_verification
        PUBLIC
            scene_fixture
            public_asset_fixture_verification
    )
    target_include_directories(scene_fixture_verification
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/scene_fixture"
    )
    phlosion_apply_settings(scene_fixture_verification)
endif()

if (PHLOSION_BUILD_SCENE_FIXTURE)
    add_executable(SceneFixtureGuard
        tools/scene_fixture/SceneFixtureGuardMain.cpp
    )
    target_link_libraries(SceneFixtureGuard
        PRIVATE
            scene_fixture_verification
    )
    phlosion_apply_settings(SceneFixtureGuard)

    add_executable(SceneFixtureRuntime
        examples/scene_fixture/SceneFixtureRuntimeMain.cpp
    )
    target_link_libraries(SceneFixtureRuntime
        PRIVATE
            scene_fixture
    )
    phlosion_apply_settings(SceneFixtureRuntime)

    if (PHLOSION_BUILD_EDITOR)
        add_library(SceneFixtureEditorPlugin MODULE
            examples/scene_fixture/SceneFixtureEditorPlugin.cpp
        )
        target_link_libraries(SceneFixtureEditorPlugin
            PRIVATE
                scene_fixture
                engine_editor_core
        )
        target_compile_definitions(SceneFixtureEditorPlugin
            PRIVATE
                PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
        )
        set_target_properties(SceneFixtureEditorPlugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "SceneFixtureEditorPlugin"
        )
        phlosion_apply_settings(SceneFixtureEditorPlugin)
    endif()
endif()

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

if (PHLOSION_BUILD_TESTS AND PHLOSION_BUILD_EDITOR)
    add_library(ValidSdlExtensionPlugin MODULE tests/TestEditorSdlExtensionPlugin.cpp)
    target_link_libraries(ValidSdlExtensionPlugin PRIVATE Engine engine_editor_core)
    target_compile_definitions(ValidSdlExtensionPlugin PRIVATE
        PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>"
        PHLOSION_TEST_VALID_SDL_EXTENSION=1)
    phlosion_apply_settings(ValidSdlExtensionPlugin)
    add_library(MalformedSdlExtensionPlugin MODULE tests/TestEditorSdlExtensionPlugin.cpp)
    target_link_libraries(MalformedSdlExtensionPlugin PRIVATE Engine engine_editor_core)
    target_compile_definitions(MalformedSdlExtensionPlugin PRIVATE
        PHLOSION_EDITOR_BUILD_CONFIGURATION="$<CONFIG>")
    phlosion_apply_settings(MalformedSdlExtensionPlugin)
    add_test(NAME PhlosionSdlExtensionValid
        COMMAND PhlosionEditorPluginProbe $<TARGET_FILE:ValidSdlExtensionPlugin>)
    add_test(NAME PhlosionSdlExtensionMalformed
        COMMAND PhlosionEditorPluginProbe $<TARGET_FILE:MalformedSdlExtensionPlugin>)
    set_tests_properties(PhlosionSdlExtensionMalformed PROPERTIES WILL_FAIL TRUE)
    if (PHLOSION_BUILD_SCENE_FIXTURE)
        add_test(NAME PhlosionSdlExtensionAbsent
            COMMAND PhlosionEditorPluginProbe $<TARGET_FILE:SceneFixtureEditorPlugin>)
    endif()
endif()

if (PHLOSION_BUILD_TESTS)
    include(CTest)
    add_executable(PhlosionInputTests tests/TestInputActions.cpp)
    target_link_libraries(PhlosionInputTests PRIVATE Phlosion::Input)
    phlosion_apply_settings(PhlosionInputTests)
    add_test(NAME PhlosionInputContracts COMMAND PhlosionInputTests)
    set_tests_properties(PhlosionInputContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionUiFocusTests tests/TestUiFocusNavigation.cpp)
    target_link_libraries(PhlosionUiFocusTests PRIVATE Phlosion::UI)
    phlosion_apply_settings(PhlosionUiFocusTests)
    add_test(NAME PhlosionUiFocusContracts COMMAND PhlosionUiFocusTests)
    set_tests_properties(PhlosionUiFocusContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionUiTextTests tests/TestUiTextLayout.cpp)
    target_link_libraries(PhlosionUiTextTests PRIVATE Phlosion::UI)
    phlosion_apply_settings(PhlosionUiTextTests)
    add_test(NAME PhlosionUiTextContracts COMMAND PhlosionUiTextTests)
    set_tests_properties(PhlosionUiTextContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionAudioMixerTests tests/TestAudioMixer.cpp)
    target_link_libraries(PhlosionAudioMixerTests PRIVATE Phlosion::Audio)
    phlosion_apply_settings(PhlosionAudioMixerTests)
    add_test(NAME PhlosionAudioMixerContracts COMMAND PhlosionAudioMixerTests)
    set_tests_properties(PhlosionAudioMixerContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionSdlAudioTests tests/TestSdlAudioOutput.cpp)
    target_link_libraries(PhlosionSdlAudioTests PRIVATE Phlosion::AudioSdl)
    phlosion_apply_settings(PhlosionSdlAudioTests)
    add_test(NAME PhlosionSdlAudioContracts COMMAND PhlosionSdlAudioTests)
    set_tests_properties(PhlosionSdlAudioContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionCameraTests tests/TestExplorationCamera.cpp)
    target_link_libraries(PhlosionCameraTests PRIVATE Phlosion::Camera Phlosion::Input)
    phlosion_apply_settings(PhlosionCameraTests)
    add_test(NAME PhlosionCameraContracts COMMAND PhlosionCameraTests)
    set_tests_properties(PhlosionCameraContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionBattleCameraTests tests/TestBattleCamera.cpp)
    target_link_libraries(PhlosionBattleCameraTests PRIVATE Phlosion::Camera)
    phlosion_apply_settings(PhlosionBattleCameraTests)
    add_test(NAME PhlosionBattleCameraContracts COMMAND PhlosionBattleCameraTests)
    set_tests_properties(PhlosionBattleCameraContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionSdlInputTests tests/TestSdlActionInput.cpp)
    target_link_libraries(PhlosionSdlInputTests PRIVATE engine_platform)
    phlosion_apply_settings(PhlosionSdlInputTests)
    add_test(NAME PhlosionSdlInputContracts COMMAND PhlosionSdlInputTests)
    set_tests_properties(PhlosionSdlInputContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionCollisionTests tests/TestCollisionWorld.cpp)
    target_link_libraries(PhlosionCollisionTests PRIVATE Phlosion::Collision)
    phlosion_apply_settings(PhlosionCollisionTests)
    add_test(NAME PhlosionCollisionContracts COMMAND PhlosionCollisionTests)
    set_tests_properties(PhlosionCollisionContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionCharacterTests tests/TestCharacterController.cpp)
    target_link_libraries(PhlosionCharacterTests PRIVATE Phlosion::Collision)
    phlosion_apply_settings(PhlosionCharacterTests)
    add_test(NAME PhlosionCharacterContracts COMMAND PhlosionCharacterTests)
    set_tests_properties(PhlosionCharacterContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionTriggerTests tests/TestCharacterTriggers.cpp)
    target_link_libraries(PhlosionTriggerTests PRIVATE Phlosion::Collision)
    phlosion_apply_settings(PhlosionTriggerTests)
    add_test(NAME PhlosionTriggerContracts COMMAND PhlosionTriggerTests)
    set_tests_properties(PhlosionTriggerContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionEngineTests
        tests/TestMain.cpp
        tests/TestWorldMaterialProfile.cpp
        tests/TestCamera3D.cpp
        tests/TestEditorRendererPreference.cpp
        tests/TestEditorProjectPluginContract.cpp
        tests/TestEditorGamePreviewRouting.cpp
        tests/TestEditorGamePreviewClock.cpp
        tests/TestSdlInputReplay.cpp
        tests/TestEditorAssetBrowser.cpp
        tests/TestEditorPerformance.cpp
        tests/TestEditorGameplayReload.cpp
        tests/TestProjectDescriptor.cpp
        tests/TestPhlosionAuthoredScene.cpp
        tests/TestPhlosionCookedModelPackage.cpp
        tests/TestPhlosionEnvironmentPatch.cpp
        tests/TestPhlosionMeshAsset.cpp
        tests/TestPhlosionResourceContainer.cpp
        tests/TestPhlosionSceneArchive.cpp
        tests/TestAsyncAssetLoader.cpp
        tests/TestPhlosionAssetGraphLoader.cpp
        tests/TestPhlosionArchiveSession.cpp
        tests/TestGltfSceneImport.cpp
        tests/TestGltfMaterialImport.cpp
        tests/TestGltfTextureImport.cpp
        tests/TestGltfImportFixture.cpp
        tests/TestGltfSceneAsset.cpp
        tests/TestGltfSceneSession.cpp
        tests/TestWorldResourceScope.cpp
        tests/TestGltfSceneAssetRenderer.cpp
        tests/TestModelPoseSampler.cpp
        tests/TestAnimationBlend.cpp
        tests/TestAnimationStateMachine.cpp
        tests/TestModelCacheAnimation.cpp
        tests/TestAnimationFixture.cpp
        tests/TestMaterialFixture.cpp
        tests/TestSceneFixture.cpp
        tests/TestSceneLifecycle.cpp
        tests/TestPublicAssetFixture.cpp
        tests/TestPublicAssetFixtureRendering.cpp
    )
    target_link_libraries(PhlosionEngineTests
        PRIVATE
            engine_sdl_replay
            Phlosion::Core
            Phlosion::EditorCore
            Phlosion::Render
            public_asset_fixture
            public_asset_fixture_verification
            animation_fixture
            animation_fixture_verification
            material_fixture
            material_fixture_verification
            scene_fixture
            scene_fixture_verification
    )
    if (MSVC)
        target_compile_options(PhlosionEngineTests PRIVATE
            /permissive- /Zc:__cplusplus /FS /MP)
    endif()
    add_test(NAME PhlosionEngineContracts COMMAND PhlosionEngineTests)
    set_tests_properties(PhlosionEngineContracts PROPERTIES TIMEOUT 120)
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
        NAME PhlosionMaterialDebugViewContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckMaterialDebugViewContract.cmake)
    add_test(
        NAME PhlosionReviewLightingProfileContract
        COMMAND ${CMAKE_COMMAND}
            -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckReviewLightingProfileContract.cmake)
    if (PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE)
        find_program(PHLOSION_POWERSHELL NAMES powershell pwsh)
        if (PHLOSION_POWERSHELL)
            add_test(
                NAME PhlosionPublicAssetFixtureHarness
                COMMAND "${PHLOSION_POWERSHELL}" -NoProfile
                    -ExecutionPolicy Bypass
                    -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_asset_fixture/test_fixture_harness.ps1"
                    -EngineRoot "${CMAKE_CURRENT_SOURCE_DIR}"
                    -GuardExe "$<TARGET_FILE:PublicAssetFixtureGuard>")
            set_tests_properties(
                PhlosionPublicAssetFixtureHarness PROPERTIES TIMEOUT 240)
        endif()
    endif()
    if (PHLOSION_BUILD_ANIMATION_FIXTURE)
        find_program(PHLOSION_POWERSHELL NAMES powershell pwsh)
        if (PHLOSION_POWERSHELL)
            add_test(
                NAME PhlosionAnimationFixtureHarness
                COMMAND "${PHLOSION_POWERSHELL}" -NoProfile
                    -ExecutionPolicy Bypass
                    -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/animation_fixture/test_fixture_harness.ps1"
                    -EngineRoot "${CMAKE_CURRENT_SOURCE_DIR}"
                    -GuardExe "$<TARGET_FILE:AnimationFixtureGuard>")
            set_tests_properties(
                PhlosionAnimationFixtureHarness PROPERTIES TIMEOUT 240)
        endif()
    endif()
    if (PHLOSION_BUILD_MATERIAL_FIXTURE)
        find_program(PHLOSION_POWERSHELL NAMES powershell pwsh)
        if (PHLOSION_POWERSHELL)
            add_test(
                NAME PhlosionMaterialFixtureHarness
                COMMAND "${PHLOSION_POWERSHELL}" -NoProfile
                    -ExecutionPolicy Bypass
                    -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/material_fixture/test_fixture_harness.ps1"
                    -EngineRoot "${CMAKE_CURRENT_SOURCE_DIR}"
                    -GuardExe "$<TARGET_FILE:MaterialFixtureGuard>")
            set_tests_properties(
                PhlosionMaterialFixtureHarness PROPERTIES TIMEOUT 300)
        endif()
    endif()
    if (PHLOSION_BUILD_SCENE_FIXTURE)
        find_program(PHLOSION_POWERSHELL NAMES powershell pwsh)
        if (PHLOSION_POWERSHELL)
            add_test(
                NAME PhlosionSceneFixtureHarness
                COMMAND "${PHLOSION_POWERSHELL}" -NoProfile
                    -ExecutionPolicy Bypass
                    -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_fixture/test_fixture_harness.ps1"
                    -EngineRoot "${CMAKE_CURRENT_SOURCE_DIR}"
                    -GuardExe "$<TARGET_FILE:SceneFixtureGuard>")
            set_tests_properties(
                PhlosionSceneFixtureHarness PROPERTIES TIMEOUT 300)
        endif()
    endif()
endif()
