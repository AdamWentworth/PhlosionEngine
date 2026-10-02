# Engine-owned VFX. The runtime has no editor or portable-library dependency.
add_library(engine_vfx_core STATIC
    src/engine/vfx/effects/shared/SharedAuthoredBatchVFX.cpp
    src/engine/vfx/runtime/shared/SharedAuthoredVfxHelpers.cpp)
target_link_libraries(engine_vfx_core PUBLIC Phlosion::Render)
phlosion_apply_settings(engine_vfx_core)
add_library(Phlosion::VfxCore ALIAS engine_vfx_core)

add_library(engine_vfx STATIC
    src/engine/vfx/runtime/EffectHost.cpp
    src/engine/vfx/runtime/shared/SharedAuthoredVfxSubmission.cpp
    src/engine/vfx/runtime/shared/SharedAuthoredVfxBridge.cpp
    src/engine/vfx/runtime/shared/SharedAuthoredVfxBatches.cpp)
target_link_libraries(engine_vfx PUBLIC Phlosion::VfxCore)
phlosion_apply_settings(engine_vfx)
add_library(Phlosion::Vfx ALIAS engine_vfx)
target_link_libraries(Engine INTERFACE Phlosion::Vfx)

add_library(engine_vfx_authoring STATIC
    src/engine/vfx/preview/shared/SharedAuthoredVfxRenderer.cpp)
target_link_libraries(engine_vfx_authoring PUBLIC Phlosion::Vfx Phlosion::VfxPreview)
phlosion_apply_settings(engine_vfx_authoring)
add_library(Phlosion::VfxAuthoring ALIAS engine_vfx_authoring)

option(PHLOSION_BUILD_VFX_LIBRARY_BRIDGE "Build the optional VFXLibrary playback bridge" OFF)
if (PHLOSION_BUILD_VFX_LIBRARY_BRIDGE)
    get_filename_component(_phlosion_vfx_library_default
        "${CMAKE_CURRENT_SOURCE_DIR}/../../VFX/VFXLibrary" ABSOLUTE)
    set(PHLOSION_VFX_LIBRARY_SOURCE_DIR "${_phlosion_vfx_library_default}" CACHE PATH
        "Independent VFXLibrary source checkout")
    if (NOT TARGET VFXLibrary::Runtime)
        if (NOT EXISTS "${PHLOSION_VFX_LIBRARY_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "PHLOSION_BUILD_VFX_LIBRARY_BRIDGE requires VFXLibrary::Runtime or PHLOSION_VFX_LIBRARY_SOURCE_DIR pointing to VFXLibrary")
        endif()
        add_subdirectory("${PHLOSION_VFX_LIBRARY_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/vfx-library")
    endif()
    add_library(engine_vfx_library_bridge STATIC
        src/engine/vfx/portable/PortableEffect.cpp
        src/engine/vfx/portable/PortableAudio.cpp)
    target_link_libraries(engine_vfx_library_bridge PUBLIC Phlosion::Vfx VFXLibrary::Runtime Phlosion::Audio)
    phlosion_apply_settings(engine_vfx_library_bridge)
    add_library(Phlosion::VfxLibraryBridge ALIAS engine_vfx_library_bridge)
endif()

if (PHLOSION_BUILD_TESTS)
    add_executable(PhlosionEffectHostTests tests/TestEffectHost.cpp)
    target_link_libraries(PhlosionEffectHostTests PRIVATE Phlosion::Vfx)
    phlosion_apply_settings(PhlosionEffectHostTests)
    add_test(NAME PhlosionEffectHost COMMAND PhlosionEffectHostTests)
    add_executable(PhlosionAuthoredVfxTests tests/TestAuthoredVfx.cpp)
    target_link_libraries(PhlosionAuthoredVfxTests PRIVATE Phlosion::Vfx)
    phlosion_apply_settings(PhlosionAuthoredVfxTests)
    add_test(NAME PhlosionAuthoredVfx COMMAND PhlosionAuthoredVfxTests
        "${CMAKE_CURRENT_BINARY_DIR}/vfx-test-fixture")
endif()
