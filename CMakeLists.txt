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
option(PHLOSION_ENABLE_OPENGL "Build the shelved OpenGL backend and legacy GPU checks" OFF)
option(PHLOSION_BUILD_GPU_TESTS "Build contract tests requiring a working native GPU" OFF)
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
if (PHLOSION_ENABLE_OPENGL)
    find_package(OpenGL REQUIRED)
endif()
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
if(WIN32)
    get_filename_component(_phlosion_kits
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
    file(GLOB _phlosion_dxc_candidates "${_phlosion_kits}/bin/10.*/x64")
    list(SORT _phlosion_dxc_candidates COMPARE NATURAL ORDER DESCENDING)
    find_program(PHLOSION_DXC_EXECUTABLE NAMES dxc.exe
        HINTS "${_phlosion_kits}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
            "$ENV{WindowsSdkVerBinPath}/x64" ${_phlosion_dxc_candidates} REQUIRED)
    get_filename_component(_phlosion_dxc_dir "${PHLOSION_DXC_EXECUTABLE}" DIRECTORY)
    foreach(_dll dxcompiler.dll dxil.dll)
        if(NOT EXISTS "${_phlosion_dxc_dir}/${_dll}")
            message(FATAL_ERROR "DXC runtime missing: ${_phlosion_dxc_dir}/${_dll}")
        endif()
    endforeach()
    set(_phlosion_compiler_destinations
        "${CMAKE_BINARY_DIR}/$<CONFIG>" "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    if(NOT CMAKE_CONFIGURATION_TYPES)
        list(APPEND _phlosion_compiler_destinations "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    list(REMOVE_DUPLICATES _phlosion_compiler_destinations)
    set(_phlosion_compiler_commands)
    foreach(_destination IN LISTS _phlosion_compiler_destinations)
        list(APPEND _phlosion_compiler_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_destination}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_phlosion_dxc_dir}/dxcompiler.dll" "${_phlosion_dxc_dir}/dxil.dll" "${_destination}")
    endforeach()
    add_custom_target(PhlosionShaderCompilerRuntime ${_phlosion_compiler_commands} VERBATIM)
endif()
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
    shadow_moment.vert
    shadow_moment.frag
    contact_filter.frag
    surface_reflection.frag
    probe_relight.frag
    surface_composition.frag
    scene_color.vert
    scene_color.frag
    display_0.comp
    display_1.comp
    display_2.comp
    display_3.comp
    display_4.comp
    display_5.comp
    display_6.comp
    display_7.comp
    display_8.comp
    display_9.comp
    temporal_scene.comp
    screen_feedback.frag
    world_indirect.vert
    world_indirect.frag
)

include(cmake/CompileVulkanShader.cmake)
set(PHLOSION_VULKAN_SHADER_OUTPUTS)
foreach(_format IN ITEMS half feedback)
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/temporal_scene_${_format}.comp.spv")
    set(_format_define "-DTEMPORAL_HALF=1")
    if(_format STREQUAL "feedback")
        set(_format_define "-DTEMPORAL_FEEDBACK=1")
    endif()
    phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/temporal_scene.comp"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" ${_format_define}
        COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/temporal_scene.comp")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()
foreach(_tier RANGE 0 4)
    set(_name "temporal_reflection_${_tier}.comp.spv")
    set(_defs "-DSSR_QUALITY=${_tier}")
    if(_tier EQUAL 4)
        list(APPEND _defs -DSSR_DEPTH_PASS=1)
    endif()
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/${_name}")
    phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/temporal_reflection.comp"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" ${_defs}
        COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/temporal_reflection.comp")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()

foreach(_contact_stage RANGE 0 4)
    set(_contact_qualities 2)
    if(_contact_stage EQUAL 1)
        set(_contact_qualities 0 1 2)
    endif()
    foreach(_contact_quality IN LISTS _contact_qualities)
        set(_contact_dirs 5 10 15)
        set(_contact_steps 3 6 10)
        list(GET _contact_dirs ${_contact_quality} _dirs)
        list(GET _contact_steps ${_contact_quality} _steps)
        set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/contact_compute_${_contact_stage}_${_contact_quality}.comp.spv")
        phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/contact_compute.comp"
            OUTPUT "${_output}"
            OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-DC_STAGE=${_contact_stage}" "-DC_DIRS=${_dirs}" "-DC_STEPS=${_steps}"
            COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/contact_compute.comp")
        list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
    endforeach()
endforeach()

foreach(_shader IN LISTS PHLOSION_VULKAN_SHADER_SOURCES)
    set(_source "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/${_shader}")
    set(_probe_define "")
    if(_shader STREQUAL "surface_composition.frag")
        set(_source "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world.frag")
        set(_probe_define "-DPHLOSION_WORLD_SURFACE_COMPOSITION=1")
    elseif(_shader STREQUAL "probe_relight.frag")
        set(_source "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world.frag")
        set(_probe_define "-DPHLOSION_PROBE_RELIGHT=1")
    endif()
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/${_shader}.spv")
    phlosion_compile_vulkan_shader(SOURCE "${_source}"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" ${_probe_define}
        COMMENT "Compiling Phlosion Vulkan shader ${_shader}")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()

foreach(_shader IN ITEMS world world_indirect)
    set(_source "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/${_shader}.frag")
    set(_output
        "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/${_shader}_dual_source.frag.spv")
    phlosion_compile_vulkan_shader(SOURCE "${_source}"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" -DPHLOSION_VULKAN_DUAL_SOURCE_BLEND=1
        COMMENT "Compiling Phlosion Vulkan dual-source shader ${_shader}.frag")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()

set(_shadow_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/world_shadow.frag.spv")
phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world.frag"
    OUTPUT "${_shadow_output}"
    OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" -DPHLOSION_WORLD_DIRECTIONAL_SHADOW_PASS=1
    COMMENT "Compiling Phlosion Vulkan directional-shadow shader")
list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_shadow_output}")

set(_surface_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/world_surface.frag.spv")
phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world.frag"
    OUTPUT "${_surface_output}"
    OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" -DPHLOSION_WORLD_SURFACE_CAPTURE=1
    COMMENT "Compiling Phlosion surface-data shader")
list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_surface_output}")
set(_motion_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/world_motion.vert.spv")
phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world.vert"
    OUTPUT "${_motion_output}"
    OPTIONS -DPHLOSION_MOTION_VERTEX=1 "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}"
    COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world.vert")
list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_motion_output}")


foreach(_pass RANGE 0 3)
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/indirect_${_pass}.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/indirect.comp"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-DGI_STAGE=${_pass}"
        COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/indirect.comp")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/particle_lighting.glsl"
    "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_indirect.glsl"
    "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_finite_light.glsl")
set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/particle_lighting.comp.spv")
phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/particle_lighting.comp"
    OUTPUT "${_output}"
    OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}"
    COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/particle_lighting.comp")
list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")

foreach(_pass RANGE 0 2)
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/wind_field_${_pass}.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/wind_field.comp"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-DWF_STAGE=${_pass}"
        COMMENT "Compiling Vulkan wind volume pass ${_pass}")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()
foreach(_pass RANGE 0 4)
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/clouds_${_pass}.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/clouds.comp"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-DCL_STAGE=${_pass}"
        COMMENT "Compiling Vulkan cloud pass ${_pass}")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()
foreach(_pass RANGE 0 2)
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/precipitation_${_pass}.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/precipitation.comp"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-DVP_STAGE=${_pass}"
        COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/precipitation.comp")
    list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")
endforeach()


set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/indirect_receiver.comp.spv")
phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/indirect_receiver.comp"
    OUTPUT "${_output}"
    OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}"
    COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/indirect_receiver.comp")
list(APPEND PHLOSION_VULKAN_SHADER_OUTPUTS "${_output}")

foreach(_pass RANGE 0 5)
    set(_output "${PHLOSION_VULKAN_SHADER_OUTPUT_DIR}/clustered_${_pass}.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/clustered.comp"
        OUTPUT "${_output}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-DCLUSTER_PASS=${_pass}"
        COMMENT "Compiling Vulkan shader ${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/clustered.comp")
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

add_library(engine_narrative STATIC src/engine/narrative/SequencePlayer.cpp)
phlosion_apply_settings(engine_narrative)
add_library(Phlosion::Narrative ALIAS engine_narrative)

add_library(engine_core STATIC
    src/engine/render/RendererPolicy.cpp
    src/engine/core/SystemRegistry.cpp
    src/engine/core/StdoutLogger.cpp
    src/engine/core/EventBus.cpp
    src/engine/core/ecs/World.cpp
    src/engine/assets/AsyncAssetLoader.cpp
    src/engine/assets/ResourceBank.cpp
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
target_compile_definitions(engine_core PUBLIC PHLOSION_ENABLE_OPENGL=$<BOOL:${PHLOSION_ENABLE_OPENGL}>)
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
    add_executable(PhlosionRendererPolicyTests tests/TestRendererPolicy.cpp)
    target_link_libraries(PhlosionRendererPolicyTests PRIVATE engine_core)
    phlosion_apply_settings(PhlosionRendererPolicyTests)
    add_test(NAME PhlosionRendererPolicy COMMAND PhlosionRendererPolicyTests)
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
target_compile_definitions(engine_platform PUBLIC PHLOSION_ENABLE_OPENGL=$<BOOL:${PHLOSION_ENABLE_OPENGL}>)
phlosion_apply_settings(engine_platform)

add_library(engine_sdl_replay STATIC src/engine/input/SdlInputReplay.cpp)
target_link_libraries(engine_sdl_replay PUBLIC engine_platform nlohmann_json::nlohmann_json)
phlosion_apply_settings(engine_sdl_replay)

add_library(engine_render STATIC
    src/engine/render/D3D12RenderBackend.cpp
    src/engine/render/d3d12/D3D12RenderBackendPipelineCompile.cpp
    src/engine/render/d3d12/D3D12PipelinePreparation.cpp
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
    src/engine/render/d3d12/D3D12RenderBackendTemporal.cpp
    src/engine/render/d3d12/D3D12RenderBackendIndirectLighting.cpp
    src/engine/render/d3d12/D3D12RenderBackendRainMap.cpp
    src/engine/render/d3d12/D3D12RenderBackendPrecipitation.cpp
    src/engine/render/d3d12/D3D12RenderBackendClouds.cpp
    src/engine/render/d3d12/D3D12RenderBackendWind.cpp
    src/engine/render/d3d12/D3D12RenderBackendLayered.cpp
    src/engine/render/d3d12/D3D12RenderBackendParticles.cpp
    src/engine/render/d3d12/D3D12RenderBackendClustered.cpp
    src/engine/render/d3d12/D3D12RenderBackendDirectionalShadow.cpp
    src/engine/render/d3d12/D3D12TextureUpload.cpp
    src/engine/render/VulkanRenderBackend.cpp
    src/engine/render/vulkan/VulkanRenderBackendLifecycle.cpp
    src/engine/render/vulkan/VulkanRenderBackendSceneColor.cpp
    src/engine/render/vulkan/VulkanRenderBackendTemporal.cpp
    src/engine/render/vulkan/VulkanRenderBackendIndirectLighting.cpp
    src/engine/render/vulkan/VulkanRenderBackendRainMap.cpp
    src/engine/render/vulkan/VulkanRenderBackendPrecipitation.cpp
    src/engine/render/vulkan/VulkanRenderBackendClouds.cpp
    src/engine/render/vulkan/VulkanRenderBackendWind.cpp
    src/engine/render/vulkan/VulkanRenderBackendLayered.cpp
    src/engine/render/vulkan/VulkanRenderBackendParticles.cpp
    src/engine/render/vulkan/VulkanRenderBackendClustered.cpp
    src/engine/render/vulkan/VulkanRenderBackendDirectionalShadow.cpp
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
    src/engine/render/WorldIndirectLighting.cpp
    src/engine/render/WorldDiffuseProbes.cpp
    src/engine/render/gltf/GltfDiffuseProbeBake.cpp
    src/engine/render/AtmosphereSky.cpp
    src/engine/render/WorldSky.cpp
    src/engine/render/WorldSkyAssets.cpp
    src/engine/render/WorldSunShadows.cpp
    src/engine/render/WorldWindController.cpp
    src/engine/render/WorldWindField.cpp
    src/engine/render/WorldClouds.cpp
    src/engine/render/RainVolume.cpp
    src/engine/render/WorldMaterialProfile.cpp
    src/engine/render/WorldLayeredMaterial.cpp
    src/engine/render/WorldLayerMaterialPackage.cpp
    src/engine/render/WorldLayeredSampling.cpp
    src/engine/render/NeutralPmrem.cpp
    src/engine/render/gltf/ModelFastGltfLoader.cpp
    src/engine/render/gltf/ModelFastGltfLoaderHelpers.cpp
    src/engine/render/gltf/ModelFastGltfMaterial.cpp
    src/engine/render/gltf/ModelFastGltfSceneData.cpp
    src/engine/render/gltf/ModelFastGltfTextures.cpp
    src/engine/render/gltf/GltfSceneAsset.cpp
    src/engine/render/gltf/GltfSceneSession.cpp
    src/engine/render/vulkan/VulkanRenderBackendResourceScopes.cpp
    src/engine/render/d3d12/D3D12RenderBackendResourceScopes.cpp
    src/engine/render/gltf/GltfSceneRenderer.cpp
    src/engine/render/gltf/GltfSceneDrawPlan.cpp
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
if (PHLOSION_ENABLE_OPENGL)
    target_sources(engine_render PRIVATE
        src/engine/render/OpenGLRenderBackend.cpp
        src/engine/render/opengl/OpenGLPixelPresenter.cpp
        src/engine/render/opengl/OpenGLRenderBackendDebugDraw.cpp
        src/engine/render/opengl/OpenGLRenderBackendPipelines.cpp
        src/engine/render/opengl/OpenGLRenderBackendShaderUtils.cpp
        src/engine/render/opengl/OpenGLRenderBackendTextures.cpp
        src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp
        src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp
        src/engine/render/opengl/OpenGLRenderBackendWorldBatchSubmission.cpp
        src/engine/render/opengl/OpenGLRenderBackendSceneColor.cpp
        src/engine/render/opengl/OpenGLRenderBackendDirectionalShadow.cpp
        src/engine/render/opengl/OpenGLRenderBackendWorldCachedMesh.cpp
        src/engine/render/opengl/OpenGLRenderBackendWorldPrewarm.cpp
        src/engine/render/opengl/OpenGLRenderBackendResourceScopes.cpp
    )
    target_link_libraries(engine_render PUBLIC OpenGL::GL)
endif()
target_link_libraries(engine_render
    PUBLIC
        engine_core
        engine_models_core
        engine_platform
        SDL2::SDL2
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
    add_dependencies(engine_render PhlosionShaderCompilerRuntime)
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
        src/engine/editor/VulkanEditorRenderSurface.cpp
    )
    target_link_libraries(engine_editor
        PUBLIC
            Engine
            engine_editor_core
        PRIVATE
            imgui::imgui
    )
    if (PHLOSION_ENABLE_OPENGL)
        target_sources(engine_editor PRIVATE src/engine/editor/OpenGLEditorRenderSurface.cpp)
    endif()
    phlosion_apply_settings(engine_editor)
    add_library(Phlosion::Editor ALIAS engine_editor)

    add_executable(PhlosionEditor
        src/engine/editor/PhlosionEditorMain.cpp
    )
    target_link_libraries(PhlosionEditor
        PRIVATE
            engine_editor
            engine_sdl_replay
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
if(TARGET PhlosionEditor)
    add_dependencies(PhlosionEditor PhlosionModelRuntime PhlosionModelPreviewPlugin)
endif()

if (PHLOSION_BUILD_TESTS)
    add_executable(PhlosionModelInstanceTests tests/TestModelInstance.cpp)
    add_executable(PhlosionWeatherParticleTests tests/TestWeatherParticles.cpp)
    target_include_directories(PhlosionWeatherParticleTests PRIVATE src)
    target_link_libraries(PhlosionWeatherParticleTests PRIVATE glm::glm nlohmann_json::nlohmann_json)
    phlosion_apply_settings(PhlosionWeatherParticleTests)
    add_test(NAME PhlosionWeatherParticleTests COMMAND PhlosionWeatherParticleTests)
    add_executable(PhlosionWeatherCurveTests tests/TestWeatherCurve.cpp)
    target_include_directories(PhlosionWeatherCurveTests PRIVATE src)
    phlosion_apply_settings(PhlosionWeatherCurveTests)
    add_test(NAME PhlosionWeatherCurveTests COMMAND PhlosionWeatherCurveTests)
    add_executable(PhlosionWorldWindTests tests/TestWorldWind.cpp src/engine/render/WorldWindController.cpp src/engine/render/WorldWindField.cpp)
    target_include_directories(PhlosionWorldWindTests PRIVATE src)
    target_link_libraries(PhlosionWorldWindTests PRIVATE glm::glm)
    phlosion_apply_settings(PhlosionWorldWindTests)
    add_test(NAME PhlosionWorldWindTests COMMAND PhlosionWorldWindTests)
    add_executable(PhlosionWorldSkyTests tests/TestWorldSky.cpp src/engine/render/WorldSky.cpp)
    target_include_directories(PhlosionWorldSkyTests PRIVATE src)
    phlosion_apply_settings(PhlosionWorldSkyTests)
    add_test(NAME PhlosionWorldSkyTests COMMAND PhlosionWorldSkyTests)
    add_executable(PhlosionIndirectLightingTests tests/TestIndirectLighting.cpp)
    target_link_libraries(PhlosionIndirectLightingTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionIndirectLightingTests)
    add_test(NAME PhlosionIndirectLighting COMMAND PhlosionIndirectLightingTests)
    add_executable(PhlosionDiffuseProbeTests tests/TestDiffuseProbes.cpp)
    target_link_libraries(PhlosionDiffuseProbeTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionDiffuseProbeTests)
    add_test(NAME PhlosionDiffuseProbes COMMAND PhlosionDiffuseProbeTests)
    add_executable(PhlosionVulkanPipelineCacheTests tests/TestVulkanPipelineCache.cpp)
    target_link_libraries(PhlosionVulkanPipelineCacheTests PRIVATE Engine Vulkan::Vulkan)
    phlosion_apply_settings(PhlosionVulkanPipelineCacheTests)
    add_test(NAME PhlosionVulkanPipelineCache COMMAND PhlosionVulkanPipelineCacheTests)
    add_executable(PhlosionVulkanPipelineLibraryTests tests/TestVulkanPipelineLibrary.cpp)
    target_link_libraries(PhlosionVulkanPipelineLibraryTests PRIVATE engine_core Vulkan::Headers)
    phlosion_apply_settings(PhlosionVulkanPipelineLibraryTests)
    add_test(NAME PhlosionVulkanPipelineLibrary COMMAND PhlosionVulkanPipelineLibraryTests)
    add_executable(PhlosionVulkanTextureKeyTests tests/TestVulkanTextureKey.cpp)
    target_link_libraries(PhlosionVulkanTextureKeyTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionVulkanTextureKeyTests)
    add_test(NAME PhlosionVulkanTextureKey COMMAND PhlosionVulkanTextureKeyTests)
    add_executable(PhlosionWorldTemporalSceneTests tests/TestWorldTemporalScene.cpp)
    target_link_libraries(PhlosionWorldTemporalSceneTests PRIVATE engine_core)
    phlosion_apply_settings(PhlosionWorldTemporalSceneTests)
    add_test(NAME PhlosionWorldTemporalScene COMMAND PhlosionWorldTemporalSceneTests)
    add_executable(PhlosionWorldMotionHistoryTests tests/TestWorldMotionHistory.cpp)
    target_link_libraries(PhlosionWorldMotionHistoryTests PRIVATE engine_core)
    phlosion_apply_settings(PhlosionWorldMotionHistoryTests)
    add_test(NAME PhlosionWorldMotionHistory COMMAND PhlosionWorldMotionHistoryTests)
    add_executable(PhlosionRainVolumeTests tests/TestRainVolume.cpp)
    target_link_libraries(PhlosionRainVolumeTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionRainVolumeTests)
    add_test(NAME PhlosionRainVolume COMMAND PhlosionRainVolumeTests)
    add_executable(PhlosionRainImpactsTests tests/TestRainImpacts.cpp)
    target_link_libraries(PhlosionRainImpactsTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionRainImpactsTests)
    add_test(NAME PhlosionRainImpacts COMMAND PhlosionRainImpactsTests)
    add_executable(PhlosionLocalShadowTests tests/TestLocalShadows.cpp)
    target_link_libraries(PhlosionLocalShadowTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionLocalShadowTests)
    add_test(NAME PhlosionLocalShadows COMMAND PhlosionLocalShadowTests)
    add_executable(PhlosionLiveShadowMomentTests tests/TestLiveShadowMoments.cpp)
    target_link_libraries(PhlosionLiveShadowMomentTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionLiveShadowMomentTests)
    if(PHLOSION_BUILD_GPU_TESTS)
        set(_scheduled_shadow_apis vulkan)
        if(WIN32)
            list(APPEND _scheduled_shadow_apis d3d12)
        endif()
        foreach(_api IN LISTS _scheduled_shadow_apis)
            foreach(_case steady move remove dynamic scene projection source-off replacement masked reset reorder continuous multiview)
                foreach(_mode fresh scheduled)
                    set(_shadow_reference)
                    if(_mode STREQUAL "scheduled")
                        set(_shadow_reference "${CMAKE_CURRENT_BINARY_DIR}/shadow-schedule/${_api}-${_case}-fresh.png")
                    endif()
                    set(_shadow_test "PhlosionShadowSchedule-${_api}-${_case}-${_mode}")
                    add_test(NAME ${_shadow_test}
                        COMMAND PhlosionStaticShadowCacheTests ${_api} ${_case} ${_mode}
                            "${CMAKE_CURRENT_BINARY_DIR}/shadow-schedule/${_api}-${_case}-${_mode}.png" ${_shadow_reference})
                    set_tests_properties(${_shadow_test} PROPERTIES LABELS "gpu;${_api};shadow"
                        RUN_SERIAL TRUE TIMEOUT 1200)
                    if(_mode STREQUAL "fresh")
                        set_tests_properties(${_shadow_test} PROPERTIES FIXTURES_SETUP "shadow-${_api}-${_case}")
                    else()
                        set_tests_properties(${_shadow_test} PROPERTIES FIXTURES_REQUIRED "shadow-${_api}-${_case}")
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endif()
    add_executable(PhlosionWorldShadowScheduleTests tests/TestWorldShadowSchedule.cpp)
    target_link_libraries(PhlosionWorldShadowScheduleTests PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionWorldShadowScheduleTests)
    add_test(NAME PhlosionWorldShadowSchedule COMMAND PhlosionWorldShadowScheduleTests)

    add_executable(PhlosionStaticShadowCacheTests tests/TestStaticShadowCache.cpp)
    target_link_libraries(PhlosionStaticShadowCacheTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionStaticShadowCacheTests)
    add_test(NAME PhlosionStaticShadowCacheContracts COMMAND PhlosionStaticShadowCacheTests)
    add_executable(PhlosionLocalHazeTests tests/TestLocalHaze.cpp)
    target_link_libraries(PhlosionLocalHazeTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionLocalHazeTests)
    add_test(NAME PhlosionLocalHaze COMMAND PhlosionLocalHazeTests)
    add_executable(PhlosionRectLightTests tests/TestRectLights.cpp)
    target_link_libraries(PhlosionRectLightTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionRectLightTests)
    add_test(NAME PhlosionRectLights COMMAND PhlosionRectLightTests)
    add_executable(PhlosionFiniteLightTests tests/TestFiniteLight.cpp)
    target_link_libraries(PhlosionFiniteLightTests PRIVATE engine_core SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionFiniteLightTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionFiniteLightTests)
    set(_finite_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/finite_light")
    set(_finite_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/finite_light.comp.spv")
    target_compile_definitions(PhlosionFiniteLightTests PRIVATE
        PHLOSION_FINITE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_finite_light.glsl"
        PHLOSION_FINITE_FIXTURE="${_finite_fixture}/fixture.glsl"
        PHLOSION_FINITE_SPV="${_finite_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_finite_fixture}/fixture.comp"
        OUTPUT "${_finite_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_finite_fixture}"
        COMMENT "Compiling Vulkan shader ${_finite_fixture}/fixture.comp")
    target_sources(PhlosionFiniteLightTests PRIVATE "${_finite_spv}")
    add_executable(PhlosionLayeredMaterialNative tests/TestLayeredMaterialNative.cpp)
    target_link_libraries(PhlosionLayeredMaterialNative PRIVATE Engine Phlosion::Platform)
    phlosion_apply_settings(PhlosionLayeredMaterialNative)
    add_executable(PhlosionLayerMaterialPackageTests tests/TestLayerMaterialPackage.cpp)
    target_link_libraries(PhlosionLayerMaterialPackageTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionLayerMaterialPackageTests)
    add_test(NAME PhlosionLayerMaterialPackage COMMAND PhlosionLayerMaterialPackageTests)
    add_executable(PhlosionLayeredMaterialDataTests tests/TestLayeredMaterialData.cpp)
    target_link_libraries(PhlosionLayeredMaterialDataTests PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionLayeredMaterialDataTests)
    add_test(NAME PhlosionLayeredMaterialData COMMAND PhlosionLayeredMaterialDataTests)
    add_executable(PhlosionLayeredMaterialTests tests/TestLayeredMaterial.cpp)
    target_link_libraries(PhlosionLayeredMaterialTests PRIVATE Phlosion::Render SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionLayeredMaterialTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionLayeredMaterialTests)
    target_compile_definitions(PhlosionLayeredMaterialTests PRIVATE
        PHLOSION_LAYERED_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_layered_material.glsl"
        PHLOSION_LAYERED_EVALUATOR="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_layered_evaluation.glsl"
        PHLOSION_LAYERED_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/layered"
        PHLOSION_LAYERED_BIN="${CMAKE_CURRENT_BINARY_DIR}/generated")
    foreach(fixture fixture terrain mask priority)
        set(_layered_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/layered-${fixture}.comp.spv")
        phlosion_compile_vulkan_shader(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/layered/${fixture}.comp"
            OUTPUT "${_layered_spv}"
            OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${CMAKE_CURRENT_SOURCE_DIR}/tests/layered"
            COMMENT "Compiling layered material ${fixture} fixture")
        target_sources(PhlosionLayeredMaterialTests PRIVATE "${_layered_spv}")
    endforeach()
    if(PHLOSION_BUILD_GPU_TESTS)
        foreach(api vulkan d3d12)
            if(WIN32 OR NOT api STREQUAL "d3d12")
                add_test(NAME PhlosionLayeredMaterial-${api} COMMAND PhlosionLayeredMaterialTests ${api})
                set_tests_properties(PhlosionLayeredMaterial-${api} PROPERTIES LABELS "gpu;${api}" RUN_SERIAL TRUE TIMEOUT 180)
            endif()
        endforeach()
    endif()
    add_executable(PhlosionFoliageMaterialTests tests/TestFoliageMaterial.cpp)
    target_link_libraries(PhlosionFoliageMaterialTests PRIVATE Phlosion::Render SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionFoliageMaterialTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionFoliageMaterialTests)
    set(_foliage_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/foliage")
    set(_foliage_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/foliage.comp.spv")
    target_compile_definitions(PhlosionFoliageMaterialTests PRIVATE
        PHLOSION_FINITE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_finite_light.glsl"
        PHLOSION_FOLIAGE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_foliage.glsl"
        PHLOSION_FOLIAGE_FIXTURE="${_foliage_fixture}/fixture.glsl"
        PHLOSION_FOLIAGE_GEOMETRY="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_foliage_geometry.glsl"
        PHLOSION_FOLIAGE_PROFILE="${_foliage_fixture}/profile.glsl"
        PHLOSION_FOLIAGE_TERRAIN="${_foliage_fixture}/terrain.glsl"
        PHLOSION_FOLIAGE_DEPTH="${_foliage_fixture}/depth.glsl"
        PHLOSION_FOLIAGE_DEPTH_SPV="${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-depth.comp.spv"
        PHLOSION_FOLIAGE_TERRAIN_SPV="${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-terrain.comp.spv"
        PHLOSION_FOLIAGE_PROFILE_SPV="${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-profile.comp.spv"
        PHLOSION_FOLIAGE_SPV="${_foliage_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_foliage_fixture}/fixture.comp"
        OUTPUT "${_foliage_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_foliage_fixture}"
        COMMENT "Compiling foliage material fixture")
    target_sources(PhlosionFoliageMaterialTests PRIVATE "${_foliage_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_foliage_fixture}/profile.comp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-profile.comp.spv"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_foliage_fixture}"
        COMMENT "Compiling foliage profile/billboard fixture")
    target_sources(PhlosionFoliageMaterialTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-profile.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${_foliage_fixture}/terrain.comp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-terrain.comp.spv"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_foliage_fixture}"
        COMMENT "Compiling foliage terrain/tint fixture")
    target_sources(PhlosionFoliageMaterialTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-terrain.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${_foliage_fixture}/depth.comp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-depth.comp.spv"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_foliage_fixture}"
        COMMENT "Compiling foliage depth/projection fixture")
    target_sources(PhlosionFoliageMaterialTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/foliage-depth.comp.spv")


    if(PHLOSION_BUILD_GPU_TESTS)
        foreach(api vulkan d3d12)
            if(WIN32 OR NOT api STREQUAL "d3d12")
                add_test(NAME PhlosionFoliageMaterial-${api} COMMAND PhlosionFoliageMaterialTests ${api})
                set_tests_properties(PhlosionFoliageMaterial-${api} PROPERTIES LABELS "gpu;${api}" RUN_SERIAL TRUE TIMEOUT 180)
            endif()
        endforeach()
    endif()
    add_executable(PhlosionNativeDirectTests tests/TestNativeDirect.cpp)
    target_link_libraries(PhlosionNativeDirectTests PRIVATE Phlosion::Render SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionNativeDirectTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionNativeDirectTests)
    add_test(NAME PhlosionNativeDirectCpu COMMAND PhlosionNativeDirectTests cpu)
    set(_native_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_direct")
    set(_native_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/native_direct.comp.spv")
    target_compile_definitions(PhlosionNativeDirectTests PRIVATE
        PHLOSION_FINITE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_finite_light.glsl"
        PHLOSION_NATIVE_FIXTURE="${_native_fixture}/fixture.glsl"
        PHLOSION_NATIVE_SPV="${_native_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_native_fixture}/fixture.comp"
        OUTPUT "${_native_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_native_fixture}"
        COMMENT "Compiling Vulkan shader ${_native_fixture}/fixture.comp")
    target_sources(PhlosionNativeDirectTests PRIVATE "${_native_spv}")
    if(PHLOSION_BUILD_GPU_TESTS)
        foreach(api vulkan d3d12)
            if(WIN32 OR NOT api STREQUAL "d3d12")
                add_test(NAME PhlosionNativeDirect-${api} COMMAND PhlosionNativeDirectTests ${api})
                set_tests_properties(PhlosionNativeDirect-${api} PROPERTIES LABELS "gpu;${api}" RUN_SERIAL TRUE TIMEOUT 180)
            endif()
        endforeach()
    endif()
    add_executable(PhlosionLocalDirectTests tests/TestLocalDirect.cpp)
    target_link_libraries(PhlosionLocalDirectTests PRIVATE Phlosion::Render SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionLocalDirectTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionLocalDirectTests)
    add_test(NAME PhlosionLocalDirectTranslation COMMAND PhlosionLocalDirectTests translation)
    set(_direct_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/local_direct")
    set(_direct_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/local_direct.comp.spv")
    target_compile_definitions(PhlosionLocalDirectTests PRIVATE
        PHLOSION_FINITE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_finite_light.glsl"
        PHLOSION_DIRECT_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_local_direct.glsl"
        PHLOSION_DIRECT_INPUTS="${_direct_fixture}/inputs.glsl"
        PHLOSION_DIRECT_REFERENCE="${_direct_fixture}/reference.glsl"
        PHLOSION_DIRECT_FIXTURE="${_direct_fixture}/fixture.glsl"
        PHLOSION_DIRECT_SPV="${_direct_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_direct_fixture}/fixture.comp"
        OUTPUT "${_direct_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_direct_fixture}"
        COMMENT "Compiling Vulkan shader ${_direct_fixture}/fixture.comp")
    target_sources(PhlosionLocalDirectTests PRIVATE "${_direct_spv}")
    if(PHLOSION_BUILD_GPU_TESTS)
        add_test(NAME PhlosionLocalDirectVulkan COMMAND PhlosionLocalDirectTests vulkan)
        set_tests_properties(PhlosionLocalDirectVulkan PROPERTIES LABELS "gpu;vulkan" RUN_SERIAL TRUE TIMEOUT 180)
        if(WIN32)
            add_test(NAME PhlosionLocalDirectD3D12 COMMAND PhlosionLocalDirectTests d3d12)
            set_tests_properties(PhlosionLocalDirectD3D12 PROPERTIES LABELS "gpu;d3d12" RUN_SERIAL TRUE TIMEOUT 180)
        endif()
    endif()

    add_executable(PhlosionPlacedProbeTests tests/TestPlacedProbes.cpp)
    target_link_libraries(PhlosionPlacedProbeTests PRIVATE Phlosion::Render Phlosion::Platform SDL2::SDL2)
    phlosion_apply_settings(PhlosionPlacedProbeTests)
    add_executable(PhlosionProbeSurfaceCacheTests tests/TestProbeSurfaceCache.cpp)
    target_include_directories(PhlosionProbeSurfaceCacheTests PRIVATE src)
    target_link_libraries(PhlosionProbeSurfaceCacheTests PRIVATE glm::glm)
    phlosion_apply_settings(PhlosionProbeSurfaceCacheTests)
    add_test(NAME PhlosionProbeSurfaceCache COMMAND PhlosionProbeSurfaceCacheTests)
    add_executable(PhlosionClusteredLightingTests tests/TestClusteredLighting.cpp)
    target_link_libraries(PhlosionClusteredLightingTests PRIVATE Phlosion::Render Phlosion::Platform SDL2::SDL2)
    phlosion_apply_settings(PhlosionClusteredLightingTests)

    if(PHLOSION_BUILD_GPU_TESTS)
        set(_cluster_apis vulkan)
        if(WIN32)
            list(APPEND _cluster_apis d3d12)
        endif()
        foreach(_api IN LISTS _cluster_apis)
            add_test(NAME PhlosionStoredSurfaceReference_${_api}
                COMMAND PhlosionClusteredLightingTests ${_api} stored-reference
                    "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-stored-reference.png")
            add_test(NAME PhlosionStoredSurfaceConsumer_${_api}
                COMMAND PhlosionClusteredLightingTests ${_api} stored
                    "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-stored.png" "-"
                    "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-stored-reference.png")
            set_tests_properties(PhlosionStoredSurfaceConsumer_${_api} PROPERTIES
                DEPENDS PhlosionStoredSurfaceReference_${_api})
            foreach(_placed IN ITEMS stable partial published narrow evict relocate cancel quality paused priority channels global oriented energy gradient global-cancel)
                set(_placed_reference)
                if(_placed STREQUAL "partial")
                    set(_placed_reference "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-placed-stable.png")
                endif()
                add_test(NAME PhlosionPlacedProbe-${_api}-${_placed}
                    COMMAND PhlosionPlacedProbeTests ${_api} ${_placed}
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-placed-${_placed}.png" ${_placed_reference})
                set_tests_properties(PhlosionPlacedProbe-${_api}-${_placed}
                    PROPERTIES LABELS "gpu;${_api};probe" RUN_SERIAL TRUE TIMEOUT 1200)
            endforeach()
            set_tests_properties(PhlosionPlacedProbe-${_api}-stable PROPERTIES FIXTURES_SETUP "placed-${_api}")
            set_tests_properties(PhlosionPlacedProbe-${_api}-partial PROPERTIES FIXTURES_REQUIRED "placed-${_api}")
            foreach(_relight IN ITEMS relight relight-wet relight-emission relight-haze)
                add_test(NAME PhlosionRelight-${_api}-${_relight}-fresh
                    COMMAND PhlosionClusteredLightingTests ${_api} "${_relight}-fresh"
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_relight}-fresh.png")
                add_test(NAME PhlosionRelight-${_api}-${_relight}
                    COMMAND PhlosionClusteredLightingTests ${_api} ${_relight}
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_relight}.png" "-"
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_relight}-fresh.png")
                set_tests_properties(PhlosionRelight-${_api}-${_relight}-fresh PROPERTIES FIXTURES_SETUP "relight-${_api}-${_relight}")
                set_tests_properties(PhlosionRelight-${_api}-${_relight} PROPERTIES FIXTURES_REQUIRED "relight-${_api}-${_relight}")
                set_tests_properties(PhlosionRelight-${_api}-${_relight} PhlosionRelight-${_api}-${_relight}-fresh
                    PROPERTIES LABELS "gpu;${_api};probe" RUN_SERIAL TRUE TIMEOUT 1200)
            endforeach()

            foreach(_probe_case probe probe-abort probe-small probe-flight)
                add_test(NAME PhlosionProbeSurface-${_api}-${_probe_case}-fresh
                    COMMAND PhlosionClusteredLightingTests ${_api} "${_probe_case}-fresh"
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_probe_case}-fresh.png")
                add_test(NAME PhlosionProbeSurface-${_api}-${_probe_case}
                    COMMAND PhlosionClusteredLightingTests ${_api} ${_probe_case}
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_probe_case}.png" "-"
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_probe_case}-fresh.png")
                set_tests_properties(PhlosionProbeSurface-${_api}-${_probe_case}-fresh
                    PROPERTIES FIXTURES_SETUP "probe-${_api}-${_probe_case}")
                set_tests_properties(PhlosionProbeSurface-${_api}-${_probe_case}
                    PROPERTIES FIXTURES_REQUIRED "probe-${_api}-${_probe_case}")
                set_tests_properties(PhlosionProbeSurface-${_api}-${_probe_case}
                    PhlosionProbeSurface-${_api}-${_probe_case}-fresh
                    PROPERTIES LABELS "gpu;${_api};probe" RUN_SERIAL TRUE TIMEOUT 1200)
            endforeach()
            foreach(_fog_case flag-off channel-off receiver-off)
                add_test(NAME PhlosionLocalHazeParticipation-${_api}-${_fog_case}
                    COMMAND PhlosionLocalHazeTests ${_api} ${_fog_case}
                        "${CMAKE_CURRENT_BINARY_DIR}/haze-tests/${_api}-${_fog_case}.png")
                set_tests_properties(PhlosionLocalHazeParticipation-${_api}-${_fog_case}
                    PROPERTIES LABELS "gpu;${_api};haze" RUN_SERIAL TRUE TIMEOUT 1200)
            endforeach()
            foreach(_probe_mode oracle flags)
                set(_probe_reference)
                if(_probe_mode STREQUAL "flags")
                    set(_probe_reference "-" "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-probe-oracle.png")
                endif()
                add_test(NAME PhlosionProbeParticipation-${_api}-${_probe_mode}
                    COMMAND PhlosionClusteredLightingTests ${_api} "probe-${_probe_mode}-relight"
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-probe-${_probe_mode}.png" ${_probe_reference})
                set_tests_properties(PhlosionProbeParticipation-${_api}-${_probe_mode}
                    PROPERTIES LABELS "gpu;${_api};probe" RUN_SERIAL TRUE TIMEOUT 1200)
                if(_probe_mode STREQUAL "oracle")
                    set_tests_properties(PhlosionProbeParticipation-${_api}-${_probe_mode}
                        PROPERTIES FIXTURES_SETUP "probe-participation-${_api}")
                else()
                    set_tests_properties(PhlosionProbeParticipation-${_api}-${_probe_mode}
                        PROPERTIES FIXTURES_REQUIRED "probe-participation-${_api}")
                endif()
            endforeach()
            foreach(_feature channels diffuse specular disabled consumers transparent)
                foreach(_mode oracle forward cluster)
                    if(_feature STREQUAL "transparent" AND _mode STREQUAL "cluster")
                        continue()
                    endif()
                    set(_feature_reference)
                    if(_mode STREQUAL "forward")
                        set(_feature_previous oracle)
                    elseif(_mode STREQUAL "cluster")
                        set(_feature_previous forward)
                    endif()
                    if(NOT _mode STREQUAL "oracle")
                        set(_feature_reference "-" "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_feature}-${_feature_previous}.png")
                    endif()
                    set(_feature_test "PhlosionLightParticipation-${_api}-${_feature}-${_mode}")
                    add_test(NAME ${_feature_test}
                        COMMAND PhlosionClusteredLightingTests ${_api} "${_feature}-${_mode}"
                            "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_feature}-${_mode}.png" ${_feature_reference})
                    set_tests_properties(${_feature_test} PROPERTIES LABELS "gpu;${_api};clustered"
                        RUN_SERIAL TRUE TIMEOUT 1200 FIXTURES_SETUP "light-${_api}-${_feature}-${_mode}")
                    if(NOT _mode STREQUAL "oracle")
                        set_tests_properties(${_feature_test} PROPERTIES FIXTURES_REQUIRED "light-${_api}-${_feature}-${_feature_previous}")
                    endif()
                endforeach()
            endforeach()
            foreach(_native native native-channels native-diffuse native-specular native-disabled native-transparent)
                set(_native_reference "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_native}-forward.png")
                foreach(_mode forward cluster)
                    set(_native_args)
                    if(_mode STREQUAL "cluster")
                        set(_native_args "-" "${_native_reference}")
                    endif()
                    set(_native_test "PhlosionNativeLight-${_api}-${_native}-${_mode}")
                    add_test(NAME ${_native_test}
                        COMMAND PhlosionClusteredLightingTests ${_api} "${_native}-${_mode}"
                            "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_native}-${_mode}.png" ${_native_args})
                    set_tests_properties(${_native_test} PROPERTIES LABELS "gpu;${_api};clustered"
                        RUN_SERIAL TRUE TIMEOUT 1200)
                    if(_mode STREQUAL "forward")
                        set_tests_properties(${_native_test} PROPERTIES FIXTURES_SETUP "native-${_api}-${_native}")
                    else()
                        set_tests_properties(${_native_test} PROPERTIES FIXTURES_REQUIRED "native-${_api}-${_native}")
                    endif()
                endforeach()
            endforeach()
            foreach(_case cluster forward consumer ltc metal masked unsupported off extent ortho moving)
                set(_cluster_reference)
                if(_case STREQUAL "consumer" OR _case STREQUAL "forward")
                    set(_cluster_reference "-" "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-cluster.png")
                endif()
                add_test(NAME PhlosionClustered-${_api}-${_case}
                    COMMAND PhlosionClusteredLightingTests ${_api} ${_case}
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/${_api}-${_case}.png" ${_cluster_reference})
                if(_case STREQUAL "cluster")
                    set_tests_properties(PhlosionClustered-${_api}-${_case}
                        PROPERTIES FIXTURES_SETUP "cluster-reference-${_api}")
                elseif(_cluster_reference)
                    set_tests_properties(PhlosionClustered-${_api}-${_case}
                        PROPERTIES FIXTURES_REQUIRED "cluster-reference-${_api}")
                endif()
                set_tests_properties(PhlosionClustered-${_api}-${_case}
                    PROPERTIES LABELS "gpu;${_api};clustered" RUN_SERIAL TRUE TIMEOUT 1200)
            endforeach()
            if(_api STREQUAL "vulkan")
                foreach(_case scene-cluster scene-consumer)
                    set(_cluster_reference)
                    if(_case STREQUAL "scene-consumer")
                        set(_cluster_reference "-" "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/vulkan-scene-cluster.png")
                    endif()
                    add_test(NAME PhlosionClustered-vulkan-${_case}
                        COMMAND PhlosionClusteredLightingTests vulkan ${_case}
                            "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/vulkan-${_case}.png" ${_cluster_reference})
                    set_tests_properties(PhlosionClustered-vulkan-${_case}
                        PROPERTIES LABELS "gpu;vulkan;clustered" RUN_SERIAL TRUE TIMEOUT 1200)
                endforeach()
                set_tests_properties(PhlosionClustered-vulkan-scene-cluster PROPERTIES FIXTURES_SETUP cluster-scene-reference)
                set_tests_properties(PhlosionClustered-vulkan-scene-consumer PROPERTIES FIXTURES_REQUIRED cluster-scene-reference)
            endif()
            foreach(_case empty blocked pcf border self range-edge clear)
                add_test(NAME PhlosionClusteredShadow-${_api}-${_case}
                    COMMAND PhlosionLiveShadowMomentTests ${_api} cluster-${_case}
                        "${CMAKE_CURRENT_BINARY_DIR}/clustered-tests/shadow-${_api}-${_case}.png")
                set_tests_properties(PhlosionClusteredShadow-${_api}-${_case}
                    PROPERTIES LABELS "gpu;${_api};clustered" RUN_SERIAL TRUE TIMEOUT 1200)
            endforeach()
        endforeach()
    endif()
    add_executable(PhlosionSurfaceBufferTests tests/TestSurfaceBuffer.cpp)
    target_link_libraries(PhlosionSurfaceBufferTests PRIVATE engine_core SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionSurfaceBufferTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionSurfaceBufferTests)
    set(_surface_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/surface_buffer")
    set(_surface_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/surface_buffer.comp.spv")
    target_compile_definitions(PhlosionSurfaceBufferTests PRIVATE
        PHLOSION_SURFACE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_surface.glsl"
        PHLOSION_SURFACE_FIXTURE="${_surface_fixture}/fixture.glsl"
        PHLOSION_SURFACE_PARTICIPATION="${_surface_fixture}/participation.glsl"
        PHLOSION_SURFACE_PARTICIPATION_SPV="${CMAKE_CURRENT_BINARY_DIR}/generated/surface-participation.comp.spv"
        PHLOSION_SURFACE_SPV="${_surface_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_surface_fixture}/fixture.comp"
        OUTPUT "${_surface_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_surface_fixture}"
        COMMENT "Compiling Vulkan shader ${_surface_fixture}/fixture.comp")
    target_sources(PhlosionSurfaceBufferTests PRIVATE "${_surface_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_surface_fixture}/participation.comp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/surface-participation.comp.spv"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_surface_fixture}"
        COMMENT "Compiling surface participation contract")
    target_sources(PhlosionSurfaceBufferTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/surface-participation.comp.spv")
    set(_foliage_surface_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/surface_buffer_foliage.comp.spv")
    target_compile_definitions(PhlosionSurfaceBufferTests PRIVATE
        PHLOSION_SURFACE_FOLIAGE="${CMAKE_CURRENT_SOURCE_DIR}/tests/surface_buffer/foliage.glsl"
        PHLOSION_SURFACE_FOLIAGE_SPV="${_foliage_surface_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/surface_buffer/foliage.comp"
        OUTPUT "${_foliage_surface_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${CMAKE_CURRENT_SOURCE_DIR}/tests/surface_buffer"
        COMMENT "Compiling composed foliage surface contract")
    target_sources(PhlosionSurfaceBufferTests PRIVATE "${_foliage_surface_spv}")


    target_compile_definitions(PhlosionSurfaceBufferTests PRIVATE
        PHLOSION_RECEIVER_BINDINGS="${_surface_fixture}/receiver_bindings.glsl"
        PHLOSION_RECEIVER_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_indirect.glsl"
        PHLOSION_RECEIVER_FIXTURE="${_surface_fixture}/receiver.glsl"
        PHLOSION_RECEIVER_SPV="${CMAKE_CURRENT_BINARY_DIR}/generated/surface-receiver.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${_surface_fixture}/receiver.comp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/surface-receiver.comp.spv"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_surface_fixture}"
        COMMENT "Compiling shared GI receiver contract")
    target_sources(PhlosionSurfaceBufferTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/surface-receiver.comp.spv")

    target_compile_definitions(PhlosionSurfaceBufferTests PRIVATE
        PHLOSION_RECEIVER_ENTRY="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/indirect_receiver_entry.glsl"
        PHLOSION_RECEIVER_PRODUCER_BINDINGS="${_surface_fixture}/receiver_producer_bindings.glsl"
        PHLOSION_RECEIVER_PRODUCER="${_surface_fixture}/receiver_producer.glsl"
        PHLOSION_RECEIVER_PRODUCER_SPV="${CMAKE_CURRENT_BINARY_DIR}/generated/surface-receiver-producer.comp.spv")
    phlosion_compile_vulkan_shader(SOURCE "${_surface_fixture}/receiver_producer.comp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/surface-receiver-producer.comp.spv"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_surface_fixture}"
        COMMENT "Compiling two-sided GI receiver producer contract")
    target_sources(PhlosionSurfaceBufferTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/surface-receiver-producer.comp.spv")

    if(PHLOSION_BUILD_GPU_TESTS)
        add_test(NAME PhlosionSurfaceBufferVulkan COMMAND PhlosionSurfaceBufferTests vulkan)
        set_tests_properties(PhlosionSurfaceBufferVulkan PROPERTIES LABELS "gpu;vulkan" RUN_SERIAL TRUE TIMEOUT 120)
        if(WIN32)
            add_test(NAME PhlosionSurfaceBufferD3D12 COMMAND PhlosionSurfaceBufferTests d3d12)
            set_tests_properties(PhlosionSurfaceBufferD3D12 PROPERTIES LABELS "gpu;d3d12" RUN_SERIAL TRUE TIMEOUT 120)
        endif()
    endif()
    add_executable(PhlosionShadowMomentTests tests/TestShadowMoments.cpp)
    target_link_libraries(PhlosionShadowMomentTests PRIVATE engine_core SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionShadowMomentTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionShadowMomentTests)
    set(_moment_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/shadow_moments")
    set(_moment_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/shadow_moments.comp.spv")
    target_compile_definitions(PhlosionShadowMomentTests PRIVATE
        PHLOSION_MOMENT_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_shadow_moments.glsl"
        PHLOSION_FINITE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_finite_light.glsl"
        PHLOSION_MOMENT_FIXTURE="${_moment_fixture}/fixture.glsl"
        PHLOSION_MOMENT_SPV="${_moment_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_moment_fixture}/fixture.comp"
        OUTPUT "${_moment_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_moment_fixture}"
        COMMENT "Compiling Vulkan shader ${_moment_fixture}/fixture.comp")
    target_sources(PhlosionShadowMomentTests PRIVATE "${_moment_spv}")
    add_executable(PhlosionSourceShadowMomentTests tests/TestSourceShadowMoments.cpp)
    target_link_libraries(PhlosionSourceShadowMomentTests PRIVATE engine_core SDL2::SDL2 glad::glad Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(PhlosionSourceShadowMomentTests PRIVATE d3d12 d3dcompiler dxgi)
    endif()
    phlosion_apply_settings(PhlosionSourceShadowMomentTests)
    set(_source_moment_fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/shadow_source")
    set(_source_moment_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/shadow_source.comp.spv")
    target_compile_definitions(PhlosionSourceShadowMomentTests PRIVATE
        PHLOSION_MOMENT_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_shadow_moments.glsl"
        PHLOSION_FINITE_CORE="${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_finite_light.glsl"
        PHLOSION_MOMENT_FIXTURE="${_source_moment_fixture}/fixture.glsl"
        PHLOSION_MOMENT_SPV="${_source_moment_spv}")
    phlosion_compile_vulkan_shader(SOURCE "${_source_moment_fixture}/fixture.comp"
        OUTPUT "${_source_moment_spv}"
        OPTIONS "-I${PHLOSION_VULKAN_SHADER_SOURCE_DIR}" "-I${_source_moment_fixture}"
        COMMENT "Compiling Vulkan shader ${_source_moment_fixture}/fixture.comp")
    target_sources(PhlosionSourceShadowMomentTests PRIVATE "${_source_moment_spv}")
    target_link_libraries(PhlosionModelInstanceTests PRIVATE Phlosion::Models)
    phlosion_apply_settings(PhlosionModelInstanceTests)
    add_test(NAME PhlosionModelInstance COMMAND PhlosionModelInstanceTests)
endif()

add_library(Phlosion::Runtime ALIAS engine_runtime)
add_executable(PhlosionIndirectBake EXCLUDE_FROM_ALL tools/render/IndirectBake.cpp)
target_link_libraries(PhlosionIndirectBake PRIVATE Engine)
phlosion_apply_settings(PhlosionIndirectBake)

add_executable(PhlosionDiffuseProbeBake EXCLUDE_FROM_ALL tools/render/DiffuseProbeBake.cpp)
target_link_libraries(PhlosionDiffuseProbeBake PRIVATE Engine)
phlosion_apply_settings(PhlosionDiffuseProbeBake)
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
    add_executable(PhlosionScreenFeedbackTests tests/TestScreenFeedback.cpp)
    target_link_libraries(PhlosionScreenFeedbackTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionScreenFeedbackTests)
    add_executable(PhlosionPlanarReflectionTests tests/TestPlanarReflection.cpp)
    target_link_libraries(PhlosionPlanarReflectionTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionPlanarReflectionTests)
    add_test(NAME PhlosionPlanarProjection COMMAND PhlosionPlanarReflectionTests)
    add_executable(PhlosionParticleLightingContracts tests/TestParticleLightingContracts.cpp)
    target_link_libraries(PhlosionParticleLightingContracts PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionParticleLightingContracts)
    add_test(NAME PhlosionParticleLightingContracts COMMAND PhlosionParticleLightingContracts)
    add_executable(PhlosionParticleLightingNative tests/TestParticleLightingNative.cpp)
    target_link_libraries(PhlosionParticleLightingNative PRIVATE Engine Phlosion::Platform)
    phlosion_apply_settings(PhlosionParticleLightingNative)
    add_executable(PhlosionPrecipitationContracts tests/TestPrecipitationContracts.cpp)
    target_link_libraries(PhlosionPrecipitationContracts PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionPrecipitationContracts)
    add_test(NAME PhlosionPrecipitationContracts COMMAND PhlosionPrecipitationContracts)
    add_executable(PhlosionWetSurfaceContracts tests/TestWetSurfacesContracts.cpp)
    target_link_libraries(PhlosionWetSurfaceContracts PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionWetSurfaceContracts)
    add_test(NAME PhlosionWetSurfaceContracts COMMAND PhlosionWetSurfaceContracts)
    add_executable(PhlosionWetSurfaceNative tests/TestWetSurfacesNative.cpp)
    target_link_libraries(PhlosionWetSurfaceNative PRIVATE Engine Phlosion::Platform)
    phlosion_apply_settings(PhlosionWetSurfaceNative)
    add_executable(PhlosionFogComposition tests/TestFogComposition.cpp)
    target_link_libraries(PhlosionFogComposition PRIVATE Engine Phlosion::Platform)
    phlosion_apply_settings(PhlosionFogComposition)
    add_executable(PhlosionPrecipitationNative tests/TestPrecipitationNative.cpp)
    target_link_libraries(PhlosionPrecipitationNative PRIVATE Engine Phlosion::Platform)
    phlosion_apply_settings(PhlosionPrecipitationNative)
    add_executable(PhlosionIndirectNativeTests tests/TestIndirectNative.cpp)
    target_link_libraries(PhlosionIndirectNativeTests PRIVATE Engine Phlosion::Platform)
    phlosion_apply_settings(PhlosionIndirectNativeTests)
    add_executable(PhlosionIndirectCompositionTests tests/TestIndirectComposition.cpp)
    target_link_libraries(PhlosionIndirectCompositionTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionIndirectCompositionTests)
    add_executable(PhlosionWorldTemporalReflectionNativeTests tests/TestWorldTemporalReflectionNative.cpp)
    target_link_libraries(PhlosionWorldTemporalReflectionNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldTemporalReflectionNativeTests)
    add_executable(PhlosionTemporalPolicyNativeTests tests/TestWorldTemporalPolicyNative.cpp)
    target_link_libraries(PhlosionTemporalPolicyNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionTemporalPolicyNativeTests)
    add_executable(PhlosionWorldSunShadowTests tests/TestWorldSunShadows.cpp src/engine/render/WorldSunShadows.cpp)
    target_include_directories(PhlosionWorldSunShadowTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(PhlosionWorldSunShadowTests PRIVATE glm::glm)
    phlosion_apply_settings(PhlosionWorldSunShadowTests)
    add_test(NAME PhlosionWorldSunShadows COMMAND PhlosionWorldSunShadowTests)
    add_executable(PhlosionWorldSunNativeTests tests/TestWorldSunNative.cpp)
    target_link_libraries(PhlosionWorldSunNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldSunNativeTests)
    add_executable(PhlosionWorldWindNativeTests tests/TestWorldWindNative.cpp)
    target_link_libraries(PhlosionWorldWindNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldWindNativeTests)
    add_executable(PhlosionWorldCloudNativeTests tests/TestWorldCloudNative.cpp)
    target_link_libraries(PhlosionWorldCloudNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldCloudNativeTests)
    add_executable(PhlosionWorldSkyNativeTests tests/TestWorldSkyNative.cpp)
    target_link_libraries(PhlosionWorldSkyNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldSkyNativeTests)
    add_executable(PhlosionWorldDisplayNativeTests tests/TestWorldDisplayNative.cpp)
    target_link_libraries(PhlosionWorldDisplayNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldDisplayNativeTests)
    if(PHLOSION_BUILD_GPU_TESTS)
        set(_display_apis vulkan)
        if(WIN32)
            list(APPEND _display_apis d3d12)
        endif()
        foreach(_api IN LISTS _display_apis)
            add_test(NAME PhlosionWorldDisplay-${_api} COMMAND PhlosionWorldDisplayNativeTests ${_api}
                "${CMAKE_CURRENT_BINARY_DIR}/artifacts/display-${_api}")
            set_tests_properties(PhlosionWorldDisplay-${_api} PROPERTIES RUN_SERIAL TRUE TIMEOUT 180 LABELS "gpu;${_api};display"
                FAIL_REGULAR_EXPRESSION "Validation Error;D3D12 validation.*severity=[01]")
        endforeach()
    endif()

    add_executable(PhlosionWorldTemporalNativeTests tests/TestWorldTemporalNative.cpp)
    target_link_libraries(PhlosionWorldTemporalNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldTemporalNativeTests)
    add_executable(PhlosionWorldMotionProducersNativeTests tests/TestWorldMotionProducersNative.cpp)
    target_link_libraries(PhlosionWorldMotionProducersNativeTests PRIVATE Phlosion::Models Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldMotionProducersNativeTests)
    add_executable(PhlosionWorldMotionNativeTests tests/TestWorldMotionNative.cpp)
    target_link_libraries(PhlosionWorldMotionNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionWorldMotionNativeTests)
    add_executable(PhlosionSurfacePassTests tests/TestSurfacePass.cpp)
    target_link_libraries(PhlosionSurfacePassTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionSurfacePassTests)
    add_executable(PhlosionPreparedViewTests tests/TestPreparedWorldViews.cpp)
    target_link_libraries(PhlosionPreparedViewTests PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionPreparedViewTests)
    add_test(NAME PhlosionPreparedViews COMMAND PhlosionPreparedViewTests)
    add_executable(PhlosionWorldMaterialFrameTests tests/TestWorldMaterialFrame.cpp)
    target_link_libraries(PhlosionWorldMaterialFrameTests PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionWorldMaterialFrameTests)
    add_test(NAME PhlosionWorldMaterialFrame COMMAND PhlosionWorldMaterialFrameTests)

    add_executable(PhlosionWorldDeformationStorageTests tests/TestWorldDeformationStorage.cpp)
    target_link_libraries(PhlosionWorldDeformationStorageTests PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionWorldDeformationStorageTests)
    add_test(NAME PhlosionWorldDeformationStorage COMMAND PhlosionWorldDeformationStorageTests)
    if(WIN32)
        add_library(PhlosionDeformationModuleFixture SHARED tests/WorldDeformationModuleFixture.cpp)
        target_link_libraries(PhlosionDeformationModuleFixture PRIVATE Phlosion::Render)
        phlosion_apply_settings(PhlosionDeformationModuleFixture)
        add_executable(PhlosionWorldDeformationModuleTests tests/TestWorldDeformationModule.cpp)
        target_link_libraries(PhlosionWorldDeformationModuleTests PRIVATE Phlosion::Render)
        phlosion_apply_settings(PhlosionWorldDeformationModuleTests)
        add_dependencies(PhlosionWorldDeformationModuleTests PhlosionDeformationModuleFixture)
        add_test(NAME PhlosionWorldDeformationModule COMMAND PhlosionWorldDeformationModuleTests $<TARGET_FILE:PhlosionDeformationModuleFixture>)
    endif()


    add_executable(PhlosionWorldCommandRecordingTests tests/TestWorldCommandRecording.cpp)
    target_link_libraries(PhlosionWorldCommandRecordingTests PRIVATE Phlosion::Render Vulkan::Vulkan)
    phlosion_apply_settings(PhlosionWorldCommandRecordingTests)
    add_test(NAME PhlosionWorldCommandRecording COMMAND PhlosionWorldCommandRecordingTests)
    add_executable(PhlosionAssetLoadingTests tests/TestAssetLoading.cpp)
    target_link_libraries(PhlosionAssetLoadingTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionAssetLoadingTests)
    add_test(NAME PhlosionAssetLoadingContracts COMMAND PhlosionAssetLoadingTests cpu)
    set_tests_properties(PhlosionAssetLoadingContracts PROPERTIES TIMEOUT 45)
    add_executable(PhlosionGltfPreparationNativeTests tests/TestGltfPreparationNative.cpp)
    target_link_libraries(PhlosionGltfPreparationNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionGltfPreparationNativeTests)
    add_executable(PhlosionPreparedViewNativeTests tests/TestPreparedViewNative.cpp)
    target_link_libraries(PhlosionPreparedViewNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionPreparedViewNativeTests)
    add_executable(PhlosionEnvironmentPacketTests tests/TestEnvironmentPacketCache.cpp)
    target_link_libraries(PhlosionEnvironmentPacketTests PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionEnvironmentPacketTests)
    add_test(NAME PhlosionEnvironmentPackets COMMAND PhlosionEnvironmentPacketTests)
    add_executable(PhlosionMaterialBindingTests tests/TestMaterialBindingNative.cpp)
    target_link_libraries(PhlosionMaterialBindingTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionMaterialBindingTests)
    if(WIN32 AND PHLOSION_BUILD_GPU_TESTS)
        foreach(api IN ITEMS d3d12 vulkan)
            foreach(mode IN ITEMS reuse retire)
                add_test(NAME PhlosionMaterialBinding-${api}-${mode} COMMAND PhlosionMaterialBindingTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/material-binding-tests/${api}-${mode}.png")
                set_tests_properties(PhlosionMaterialBinding-${api}-${mode} PROPERTIES
                    LABELS "gpu;${api}" RUN_SERIAL TRUE TIMEOUT 300)
            endforeach()
        endforeach()
    endif()
    add_executable(PhlosionSurfaceCompositionTests tests/TestSurfaceComposition.cpp)
    target_link_libraries(PhlosionSurfaceCompositionTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionSurfaceCompositionTests)
    add_executable(PhlosionSurfaceDepthReuseTests tests/TestSurfaceDepthReuse.cpp)
    target_link_libraries(PhlosionSurfaceDepthReuseTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionSurfaceDepthReuseTests)
    add_executable(PhlosionContactComputeNativeTests tests/TestContactComputeNative.cpp)
    target_link_libraries(PhlosionContactComputeNativeTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionContactComputeNativeTests)
    add_executable(PhlosionContactVisibilityTests tests/TestContactVisibility.cpp)
    target_link_libraries(PhlosionContactVisibilityTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionContactVisibilityTests)
    add_test(NAME PhlosionContactVisibilityContracts COMMAND PhlosionContactVisibilityTests)
    add_executable(PhlosionRenderPhaseProfileTests tests/TestRenderPhaseProfile.cpp)
    target_link_libraries(PhlosionRenderPhaseProfileTests PRIVATE Phlosion::Render)
    phlosion_apply_settings(PhlosionRenderPhaseProfileTests)
    add_test(NAME PhlosionRenderPhaseProfileContracts COMMAND PhlosionRenderPhaseProfileTests)
    add_executable(PhlosionLocalReflectionTests tests/TestLocalReflection.cpp)
    target_link_libraries(PhlosionLocalReflectionTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionLocalReflectionTests)
    add_test(NAME PhlosionLocalReflectionContracts COMMAND PhlosionLocalReflectionTests)
    add_executable(PhlosionScreenReflectionTests tests/TestScreenReflection.cpp)
    target_link_libraries(PhlosionScreenReflectionTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionScreenReflectionTests)
    add_test(NAME PhlosionScreenReflectionContracts COMMAND PhlosionScreenReflectionTests)
    add_executable(PhlosionReflectionResponseTests tests/TestReflectionResponse.cpp)
    target_link_libraries(PhlosionReflectionResponseTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionReflectionResponseTests)
    add_test(NAME PhlosionReflectionResponseContracts COMMAND PhlosionReflectionResponseTests)
    if(PHLOSION_BUILD_GPU_TESTS)
        foreach(api vulkan d3d12)
            if(api STREQUAL "d3d12" AND NOT WIN32)
                continue()
            endif()
            foreach(mode energy energy254 grazing grazing254 wet wet254 grazing-wet grazing-wet254 direct direct254
                    authored authored254 grazing-authored grazing-authored254 wet-authored wet-authored254
                    grazing-wet-authored grazing-wet-authored254 authored-weight authored-weight254
                    grazing-authored-weight grazing-authored-weight254 authored-occluded authored-occluded254
                    grazing-authored-occluded grazing-authored-occluded254 direct-authored direct-authored254)
                add_test(NAME PhlosionReflectionResponse-${api}-${mode} COMMAND PhlosionReflectionResponseTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/reflection-response-tests/${api}-${mode}.png")
                set_tests_properties(PhlosionReflectionResponse-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
                    LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
            add_test(NAME PhlosionTemporalReflection-${api}-secondary COMMAND PhlosionWorldTemporalReflectionNativeTests ${api} secondary)
            set_tests_properties(PhlosionTemporalReflection-${api}-secondary PROPERTIES RUN_SERIAL TRUE TIMEOUT 300
                FAIL_REGULAR_EXPRESSION "Validation Error;D3D12 validation.*severity=[01]"
                LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            foreach(mode opaque masked transparent forward motion extent authority late reset mixed depth-reset cached wet wet-emission probe-switch planar-opaque planar-masked planar-transparent planar-motion planar-extent planar-cached planar-wet-emission planar-coplanar)
                add_test(NAME PhlosionSurfaceComposition-${api}-${mode} COMMAND PhlosionSurfaceCompositionTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/surface-composition-tests/${api}-${mode}")
                set_tests_properties(PhlosionSurfaceComposition-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 300
                    LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
            foreach(mode opaque masked transparent forward motion extent occluded reset)
                add_test(NAME PhlosionSurfaceDepthReuse-${api}-${mode} COMMAND PhlosionSurfaceDepthReuseTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/surface-depth-reuse-tests/${api}-${mode}")
                set_tests_properties(PhlosionSurfaceDepthReuse-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 300
                    LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
            foreach(mode surface contact pbr254 direct emission masked reset glossy rough away disoccluded camera-cut motion)
                add_test(NAME PhlosionContactVisibility-${api}-${mode} COMMAND PhlosionContactVisibilityTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/contact-visibility-tests/${api}-${mode}")
                set_tests_properties(PhlosionContactVisibility-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
                    LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
            foreach(mode surface contact)
                add_test(NAME PhlosionContactVisibility-${api}-${mode}-odd COMMAND PhlosionContactVisibilityTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/contact-visibility-tests/${api}-${mode}-odd" 641 481)
                set_tests_properties(PhlosionContactVisibility-${api}-${mode}-odd PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
                    LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
            foreach(mode mirror pbr254 rough reset source-off camera outside partial seam blocked masked parallax moving removed
                receiver-grazing receiver-grazing-low receiver-hidden
                grazing-view0 grazing-view1 grazing-view2 grazing-view3 grazing-view4 grazing-view5 grazing-view6 grazing-view7 grazing-view8
                handoff0 handoff1 handoff2 handoff3 handoff4 handoff5 handoff6 handoff7 handoff8 motion0 motion1 motion2 motion3 motion4 motion5 motion6 motion7 motion8 face0 face1 face2 face3 face4 face5)
                add_test(NAME PhlosionLocalReflection-${api}-${mode} COMMAND PhlosionLocalReflectionTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/local-reflection-tests/${api}-${mode}")
                set_tests_properties(PhlosionLocalReflection-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
                    LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
            foreach(mode mirror pbr254 rough matte occluded masked reset source-off camera offscreen edge square thin0 thin1 thin2 thin3 thin4 thin5)
                add_test(NAME PhlosionScreenReflection-${api}-${mode} COMMAND PhlosionScreenReflectionTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/screen-reflection-tests/${api}-${mode}")
                set_tests_properties(PhlosionScreenReflection-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
                    LABELS "gpu;${api}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
        endforeach()
    endif()
    if (PHLOSION_BUILD_GPU_TESTS)
        add_executable(PhlosionVulkanSubmissionTests tests/TestVulkanSubmission.cpp)
        target_link_libraries(PhlosionVulkanSubmissionTests PRIVATE Engine Vulkan::Vulkan)
        phlosion_apply_settings(PhlosionVulkanSubmissionTests)
        add_test(NAME PhlosionVulkanSubmission COMMAND PhlosionVulkanSubmissionTests)
        set_tests_properties(PhlosionVulkanSubmission PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
            LABELS "gpu;vulkan" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    add_executable(PhlosionSceneRadianceTests tests/TestSceneRadiance.cpp)
    target_link_libraries(PhlosionSceneRadianceTests PRIVATE Phlosion::Render Phlosion::Platform)
    phlosion_apply_settings(PhlosionSceneRadianceTests)
    set(_feedback_test_apis)
    if (PHLOSION_BUILD_GPU_TESTS)
        list(APPEND _feedback_test_apis vulkan)
        if (PHLOSION_ENABLE_OPENGL)
            list(APPEND _feedback_test_apis opengl)
        endif()
        if (WIN32)
            list(APPEND _feedback_test_apis d3d12)
        endif()
    endif()
    foreach(api IN LISTS _feedback_test_apis)
        if(NOT api STREQUAL "opengl")
            foreach(scenario steady move remove dynamic scene projection source-off replacement masked reset)
                foreach(mode cached fresh refresh)
                    add_test(NAME PhlosionStaticShadowCache-${api}-${scenario}-${mode}
                        COMMAND PhlosionStaticShadowCacheTests ${api} ${scenario} ${mode}
                        "${CMAKE_CURRENT_BINARY_DIR}/static-shadow-cache-tests/${api}-${scenario}-${mode}.png")
                    set_tests_properties(PhlosionStaticShadowCache-${api}-${scenario}-${mode}
                        PROPERTIES RUN_SERIAL TRUE TIMEOUT 240 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
                endforeach()
            endforeach()
        endif()
        add_test(NAME PhlosionFiniteLight-${api} COMMAND PhlosionFiniteLightTests ${api})
        set_tests_properties(PhlosionFiniteLight-${api} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
        add_test(NAME PhlosionShadowMoments-${api} COMMAND PhlosionShadowMomentTests ${api})
        set_tests_properties(PhlosionShadowMoments-${api} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
        add_test(NAME PhlosionSourceShadowMoments-${api} COMMAND PhlosionSourceShadowMomentTests ${api})
        set_tests_properties(PhlosionSourceShadowMoments-${api} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
        foreach(mode blocked empty clear pcf border self range-edge)
            add_test(NAME PhlosionLiveMoments-${api}-${mode} COMMAND PhlosionLiveShadowMomentTests ${api} ${mode}
                "${CMAKE_CURRENT_BINARY_DIR}/live-moment-tests/${api}-${mode}.png")
            set_tests_properties(PhlosionLiveMoments-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 240 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endforeach()
        if(NOT api STREQUAL "opengl")
            foreach(kind spot native)
                set(_shadow_control "${CMAKE_CURRENT_BINARY_DIR}/source-shadow-tests/${api}-${kind}-control.png")
                add_test(NAME PhlosionShadowControl-${api}-${kind} COMMAND PhlosionLiveShadowMomentTests
                    ${api} ${kind}-unshadowed "${_shadow_control}")
                set_tests_properties(PhlosionShadowControl-${api}-${kind} PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
                    LABELS "gpu;${api};shadow" FIXTURES_SETUP shadow-control-${api}-${kind}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "Validation Error;D3D12 validation.*severity=[01]")
                foreach(mode blocked empty clear self sharp-border soft-border extra-border faded skin skin-clear cutout transparent)
                    add_test(NAME PhlosionSourceShadow-${api}-${kind}-${mode} COMMAND PhlosionLiveShadowMomentTests
                        ${api} ${kind}-${mode} "${CMAKE_CURRENT_BINARY_DIR}/source-shadow-tests/${api}-${kind}-${mode}.png" "${_shadow_control}")
                    set_tests_properties(PhlosionSourceShadow-${api}-${kind}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 240
                        LABELS "gpu;${api};shadow" FIXTURES_REQUIRED shadow-control-${api}-${kind}
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "Validation Error;D3D12 validation.*severity=[01]")
                endforeach()
            endforeach()
        endif()
        if(TARGET PhlosionLocalHazeTests)
            foreach(mode moments-on moments-blocked moments-both moments-reduced-on moments-reduced-blocked
                         on off zero-density source-off sky back blocked foreground blue both odd edge
                         reduced-on reduced-off reduced-zero-density reduced-source-off reduced-sky
                         reduced-back reduced-blocked reduced-foreground reduced-blue reduced-both reduced-odd reduced-edge reduced-repeat)
                add_test(NAME PhlosionLocalHaze-${api}-${mode} COMMAND PhlosionLocalHazeTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/local-haze-tests/${api}-${mode}.png")
                set_tests_properties(PhlosionLocalHaze-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 180
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
        endif()
        if(NOT api STREQUAL "opengl")
            foreach(mode bounce direct emission)
                add_test(NAME PhlosionIndirectComposition-${api}-${mode} COMMAND PhlosionIndirectCompositionTests ${api} ${mode}
                    "${CMAKE_CURRENT_BINARY_DIR}/indirect-composition-tests/${api}-${mode}.png")
                set_tests_properties(PhlosionIndirectComposition-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 180
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            endforeach()
        endif()
        set(_planar_modes on off offscreen reversed dry clipped occluded reset extent-change emissive emissive-rough emissive-off)
        if(NOT api STREQUAL "opengl")
            list(APPEND _planar_modes haze)
        endif()
        foreach(mode IN LISTS _planar_modes)
            add_test(NAME PhlosionPlanarReflection-${api}-${mode} COMMAND PhlosionPlanarReflectionTests ${api} ${mode}
                "${CMAKE_CURRENT_BINARY_DIR}/planar-reflection-tests/${api}-${mode}.png")
            set_tests_properties(PhlosionPlanarReflection-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 180
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endforeach()
        foreach(mode ramp bloom feedback)
            add_test(NAME PhlosionSceneRadiance-${api}-${mode} COMMAND PhlosionSceneRadianceTests ${api} ${mode}
                "${CMAKE_CURRENT_BINARY_DIR}/scene-radiance-tests/${api}-${mode}.png")
            set_tests_properties(PhlosionSceneRadiance-${api}-${mode} PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endforeach()
        add_test(NAME PhlosionScreenFeedback-${api} COMMAND PhlosionScreenFeedbackTests ${api}
            "${CMAKE_CURRENT_BINARY_DIR}/screen-feedback-tests/${api}.png")
        set_tests_properties(PhlosionScreenFeedback-${api} PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME PhlosionScreenFeedbackMultiview-${api} COMMAND PhlosionScreenFeedbackTests ${api}
            "${CMAKE_CURRENT_BINARY_DIR}/screen-feedback-tests/${api}-multiview.png" multiview)
        set_tests_properties(PhlosionScreenFeedbackMultiview-${api} PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    target_link_libraries(PhlosionAudioMixerTests PRIVATE Phlosion::Audio)
    phlosion_apply_settings(PhlosionAudioMixerTests)
    add_test(NAME PhlosionAudioMixerContracts COMMAND PhlosionAudioMixerTests)
    set_tests_properties(PhlosionAudioMixerContracts PROPERTIES TIMEOUT 120)

    if (PHLOSION_BUILD_GPU_TESTS AND PHLOSION_ENABLE_OPENGL)
        add_executable(PhlosionPixelPresenterTests tests/TestOpenGLPixelPresenter.cpp)
        target_link_libraries(PhlosionPixelPresenterTests PRIVATE Phlosion::Render Phlosion::Platform)
        phlosion_apply_settings(PhlosionPixelPresenterTests)
        add_test(NAME PhlosionPixelPresenterContracts COMMAND PhlosionPixelPresenterTests)
        set_tests_properties(PhlosionPixelPresenterContracts PROPERTIES TIMEOUT 120 LABELS "gpu;opengl")
    endif()
    add_executable(PhlosionSdlAudioTests tests/TestSdlAudioOutput.cpp)
    target_link_libraries(PhlosionSdlAudioTests PRIVATE Phlosion::AudioSdl)
    phlosion_apply_settings(PhlosionSdlAudioTests)
    add_test(NAME PhlosionSdlAudioContracts COMMAND PhlosionSdlAudioTests)
    set_tests_properties(PhlosionSdlAudioContracts PROPERTIES TIMEOUT 120)

    add_executable(PhlosionCameraTests tests/TestExplorationCamera.cpp)
    add_executable(PhlosionNarrativeTests tests/TestNarrativeSequence.cpp)
    target_link_libraries(PhlosionNarrativeTests PRIVATE Phlosion::Narrative)
    phlosion_apply_settings(PhlosionNarrativeTests)
    add_test(NAME PhlosionNarrativeContracts COMMAND PhlosionNarrativeTests)
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

    add_executable(PhlosionResourceBankTests tests/TestResourceBank.cpp)
    target_link_libraries(PhlosionResourceBankTests PRIVATE Phlosion::Core)
    add_test(NAME PhlosionResourceBankContracts COMMAND PhlosionResourceBankTests)
    set_tests_properties(PhlosionResourceBankContracts PROPERTIES TIMEOUT 45)

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
    add_test(NAME PhlosionShaderBuildDependencies COMMAND ${CMAKE_COMMAND}
        "-DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-build-contract"
        "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
        "-DTEST_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DPHLOSION_GLSLANG_VALIDATOR=${PHLOSION_GLSLANG_VALIDATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckShaderBuildDependencies.cmake")
    set_tests_properties(PhlosionShaderBuildDependencies PROPERTIES TIMEOUT 120)
    add_test(NAME PhlosionMaterialComposition COMMAND ${CMAKE_COMMAND}
        -DPHLOSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/material-composition-contract
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckMaterialComposition.cmake)
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
    if (TARGET SceneFixtureEditorPlugin)
        find_program(PHLOSION_POWERSHELL NAMES powershell pwsh)
        if (PHLOSION_POWERSHELL)
            add_test(
                NAME PhlosionSceneFixtureHarness
                COMMAND "${PHLOSION_POWERSHELL}" -NoProfile
                    -ExecutionPolicy Bypass
                    -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_fixture/test_fixture_harness.ps1"
                    -EngineRoot "${CMAKE_CURRENT_SOURCE_DIR}"
                    -GuardExe "$<TARGET_FILE:SceneFixtureGuard>"
                    -RuntimeExe "$<TARGET_FILE:SceneFixtureRuntime>"
                    -EditorExe "$<TARGET_FILE:PhlosionEditor>"
                    -PluginDll "$<TARGET_FILE:SceneFixtureEditorPlugin>")
            set_tests_properties(
                PhlosionSceneFixtureHarness PROPERTIES TIMEOUT 300)
        endif()
    endif()
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/shadow_moment_filter.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_shadow_moments.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/shadow_moment_filter.glsl" PHLOSION_MOMENT_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_shadow_moments.glsl" PHLOSION_MOMENT_CORE)
string(REPLACE "#include \"world_shadow_moments.glsl\"" "${PHLOSION_MOMENT_CORE}" PHLOSION_MOMENT_SOURCE "${PHLOSION_MOMENT_SOURCE}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ShadowMomentSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/ShadowMomentSource.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/temporal_scene_common.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/temporal_scene_common.glsl" PHLOSION_TEMPORAL_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldTemporalSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldTemporalSource.h" @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/display_common.glsl" PHLOSION_DISPLAY_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/display_aces.glsl" PHLOSION_DISPLAY_ACES_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/display_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/display_aces.glsl")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldDisplaySource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldDisplaySource.h" @ONLY)


# One atmosphere shader source is compiled into GL/HLSL and included by Vulkan.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/scene_bloom.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/scene_bloom.glsl" PHLOSION_SCENE_BLOOM_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SceneBloomSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/SceneBloomSource.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_atmosphere.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_atmosphere.glsl" PHLOSION_ATMOSPHERE_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_sky_model.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_sky_model.glsl" PHLOSION_SKY_MODEL_SOURCE)
string(REPLACE "#include \"world_sky_model.glsl\"" "${PHLOSION_SKY_MODEL_SOURCE}" PHLOSION_ATMOSPHERE_SOURCE "${PHLOSION_ATMOSPHERE_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_cloud_integration.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_cloud_integration.glsl" PHLOSION_CLOUD_INTEGRATION_SOURCE)
string(REPLACE "#include \"world_cloud_integration.glsl\"" "${PHLOSION_CLOUD_INTEGRATION_SOURCE}" PHLOSION_ATMOSPHERE_SOURCE "${PHLOSION_ATMOSPHERE_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_wet_surface.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_wet_surface.glsl" PHLOSION_WET_SURFACE_SOURCE)
string(REPLACE "#include \"world_wet_surface.glsl\"" "${PHLOSION_WET_SURFACE_SOURCE}" PHLOSION_ATMOSPHERE_SOURCE "${PHLOSION_ATMOSPHERE_SOURCE}")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_fog.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_fog.glsl" PHLOSION_FOG_SAMPLE_SOURCE)
string(REPLACE "#include \"world_fog.glsl\"" "${PHLOSION_FOG_SAMPLE_SOURCE}" PHLOSION_ATMOSPHERE_SOURCE "${PHLOSION_ATMOSPHERE_SOURCE}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldAtmosphereSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldAtmosphereSource.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_lighting.glsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_planar.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_planar.glsl" PHLOSION_PLANAR_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldPlanarSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldPlanarSource.h" @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_lighting.glsl" PHLOSION_LIGHTING_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_indirect.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_indirect.glsl" PHLOSION_INDIRECT_CONSUMER_SOURCE)
string(REPLACE "#include \"world_indirect.glsl\"" "${PHLOSION_INDIRECT_CONSUMER_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_local_direct.glsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_foliage.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_foliage.glsl" PHLOSION_FOLIAGE_SOURCE)
string(REPLACE "#include \"world_foliage.glsl\"" "${PHLOSION_FOLIAGE_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_local_direct.glsl" PHLOSION_LOCAL_DIRECT_SOURCE)
string(REPLACE "#include \"world_local_direct.glsl\"" "${PHLOSION_LOCAL_DIRECT_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_surface.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_surface.glsl" PHLOSION_SURFACE_SOURCE)
string(REPLACE "#include \"world_surface.glsl\"" "${PHLOSION_SURFACE_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_contact.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_contact.glsl" PHLOSION_CONTACT_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_contact_filter.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_contact_filter.glsl" PHLOSION_CONTACT_FILTER_SOURCE)
string(REPLACE "#include \"world_contact_filter.glsl\"" "${PHLOSION_CONTACT_FILTER_SOURCE}" PHLOSION_CONTACT_SOURCE "${PHLOSION_CONTACT_SOURCE}")
string(REPLACE "#include \"world_contact.glsl\"" "${PHLOSION_CONTACT_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_screen_reflection.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_screen_reflection.glsl" PHLOSION_SCREEN_REFLECTION_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_local_reflection.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_local_reflection.glsl" PHLOSION_LOCAL_REFLECTION_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_probe_kernel.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_probe_kernel.glsl" PHLOSION_PROBE_KERNEL_SOURCE)
string(REPLACE "#include \"world_probe_kernel.glsl\"" "${PHLOSION_PROBE_KERNEL_SOURCE}" PHLOSION_LOCAL_REFLECTION_SOURCE "${PHLOSION_LOCAL_REFLECTION_SOURCE}")
string(REPLACE "#include \"world_local_reflection.glsl\"" "${PHLOSION_LOCAL_REFLECTION_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
string(REPLACE "#include \"world_screen_reflection.glsl\"" "${PHLOSION_SCREEN_REFLECTION_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_finite_light.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_finite_light.glsl" PHLOSION_FINITE_LIGHT_SOURCE)
string(REPLACE "#include \"world_finite_light.glsl\"" "${PHLOSION_FINITE_LIGHT_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/indirect_compute.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/indirect_compute.glsl" PHLOSION_INDIRECT_SOURCE)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/precipitation_lighting.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/precipitation_lighting.glsl" PHLOSION_PRECIPITATION_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_indirect.glsl" _precipitation_indirect)
string(REPLACE "#include \"world_indirect.glsl\"" "${_precipitation_indirect}" PHLOSION_PRECIPITATION_SOURCE "${PHLOSION_PRECIPITATION_SOURCE}")
string(REPLACE "#include \"world_finite_light.glsl\"" "${PHLOSION_FINITE_LIGHT_SOURCE}" PHLOSION_PRECIPITATION_SOURCE "${PHLOSION_PRECIPITATION_SOURCE}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/particle_lighting.glsl" PHLOSION_PARTICLE_LIGHTING_SOURCE)
string(REPLACE "#include \"world_indirect.glsl\"" "${_precipitation_indirect}" PHLOSION_PARTICLE_LIGHTING_SOURCE "${PHLOSION_PARTICLE_LIGHTING_SOURCE}")
string(REPLACE "#include \"world_finite_light.glsl\"" "${PHLOSION_FINITE_LIGHT_SOURCE}" PHLOSION_PARTICLE_LIGHTING_SOURCE "${PHLOSION_PARTICLE_LIGHTING_SOURCE}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_sun_shadow.glsl" PHLOSION_WORLD_SUN_SHADOW_SOURCE)
foreach(_sun_consumer PHLOSION_PRECIPITATION_SOURCE PHLOSION_INDIRECT_SOURCE)
    string(REPLACE "#include \"world_sun_shadow.glsl\"" "${PHLOSION_WORLD_SUN_SHADOW_SOURCE}" ${_sun_consumer} "${${_sun_consumer}}")
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldParticleLightingSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldParticleLightingSource.h" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldPrecipitationSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldPrecipitationSource.h" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldIndirectSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldIndirectSource.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/indirect_receiver_entry.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_indirect.glsl" PHLOSION_INDIRECT_RECEIVER_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/indirect_receiver_entry.glsl" PHLOSION_INDIRECT_RECEIVER_ENTRY)
string(APPEND PHLOSION_INDIRECT_RECEIVER_SOURCE "${PHLOSION_INDIRECT_RECEIVER_ENTRY}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldIndirectReceiverSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldIndirectReceiverSource.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_clustered_consumer.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_clustered_consumer.glsl" PHLOSION_CLUSTERED_CONSUMER_SOURCE)
string(REPLACE "#include \"world_clustered_consumer.glsl\"" "${PHLOSION_CLUSTERED_CONSUMER_SOURCE}" PHLOSION_LIGHTING_SOURCE "${PHLOSION_LIGHTING_SOURCE}")
set(PHLOSION_LAYERED_SOURCE "")
foreach(_layer_source world_layered_material.glsl world_layered_evaluation.glsl world_layered_sampling.glsl)
    set(_layer_path "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/${_layer_source}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_layer_path}")
    file(READ "${_layer_path}" _layer_text)
    string(REGEX REPLACE "#include [^\n]+\n" "" _layer_text "${_layer_text}")
    string(APPEND PHLOSION_LAYERED_SOURCE "\n${_layer_text}")
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldLayeredSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldLayeredSource.h" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldLightingSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldLightingSource.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_shadow.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_shadow.glsl" PHLOSION_WORLD_SHADOW_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_sun_shadow.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_sun_shadow.glsl" PHLOSION_WORLD_SUN_SHADOW_SOURCE)
string(REPLACE "#include \"world_sun_shadow.glsl\"" "${PHLOSION_WORLD_SUN_SHADOW_SOURCE}" PHLOSION_WORLD_SHADOW_SOURCE "${PHLOSION_WORLD_SHADOW_SOURCE}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldShadowSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldShadowSource.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_volume.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_volume.glsl" PHLOSION_WORLD_VOLUME_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldVolumeSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldVolumeSource.h" @ONLY)

target_include_directories(engine_render PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/temporal_reflection_common.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/temporal_reflection_common.glsl" PHLOSION_TEMPORAL_REFLECTION_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldTemporalReflectionSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldTemporalReflectionSource.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/clustered_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/clustered_entry.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/clustered_common.glsl" PHLOSION_CLUSTERED_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/clustered_entry.glsl" PHLOSION_CLUSTERED_ENTRY)
string(REPLACE "#include \"world_finite_light.glsl\"" "${PHLOSION_FINITE_LIGHT_SOURCE}" PHLOSION_CLUSTERED_SOURCE "${PHLOSION_CLUSTERED_SOURCE}")
string(REPLACE "#include \"world_shadow.glsl\"" "${PHLOSION_WORLD_SHADOW_SOURCE}" PHLOSION_CLUSTERED_SOURCE "${PHLOSION_CLUSTERED_SOURCE}")
string(REPLACE "#include \"world_local_direct.glsl\"" "${PHLOSION_LOCAL_DIRECT_SOURCE}" PHLOSION_CLUSTERED_SOURCE "${PHLOSION_CLUSTERED_SOURCE}")
string(APPEND PHLOSION_CLUSTERED_SOURCE "${PHLOSION_CLUSTERED_ENTRY}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldClusteredSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldClusteredSource.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_probe_relight.glsl")
file(READ "${PHLOSION_VULKAN_SHADER_SOURCE_DIR}/world_probe_relight.glsl" PHLOSION_PROBE_RELIGHT_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldProbeRelightSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldProbeRelightSource.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_contact_compute.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_contact_compute.glsl" WORLD_CONTACT_COMPUTE_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldContactComputeSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldContactComputeSource.h" @ONLY)

if(WIN32)
    add_executable(PhlosionShaderCook tools/render/ShaderCook.cpp)
    target_link_libraries(PhlosionShaderCook PRIVATE Engine)
    phlosion_apply_settings(PhlosionShaderCook)
endif()

if(PHLOSION_BUILD_TESTS)
    add_executable(PhlosionShaderPreparationTests tests/TestShaderPreparation.cpp)
    target_link_libraries(PhlosionShaderPreparationTests PRIVATE Engine)
    phlosion_apply_settings(PhlosionShaderPreparationTests)
    add_test(NAME PhlosionShaderPreparation COMMAND PhlosionShaderPreparationTests)
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_clouds.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_clouds.glsl" PHLOSION_CLOUD_SOURCE)
string(REPLACE "#include \"world_sky_model.glsl\"" "${PHLOSION_SKY_MODEL_SOURCE}" PHLOSION_CLOUD_SOURCE "${PHLOSION_CLOUD_SOURCE}")
string(REPLACE "#include \"world_cloud_integration.glsl\"" "${PHLOSION_CLOUD_INTEGRATION_SOURCE}" PHLOSION_CLOUD_SOURCE "${PHLOSION_CLOUD_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_fog.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_fog.glsl" PHLOSION_FOG_SAMPLE_SOURCE)
string(REPLACE "#include \"world_fog.glsl\"" "${PHLOSION_FOG_SAMPLE_SOURCE}" PHLOSION_CLOUD_SOURCE "${PHLOSION_CLOUD_SOURCE}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldCloudSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldCloudSource.h" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_wind_field.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_wind_field.glsl" PHLOSION_WIND_FIELD_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_wind_sample.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_wind_sample.glsl" PHLOSION_WIND_SAMPLE_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_foliage_geometry.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/world_foliage_geometry.glsl" PHLOSION_FOLIAGE_GEOMETRY_SOURCE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldWindSource.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/WorldWindSource.h" @ONLY)
