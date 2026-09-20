if (NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

file(GLOB_RECURSE _engine_boundary_files
    LIST_DIRECTORIES false
    "${PHLOSION_ROOT}/src/*"
    "${PHLOSION_ROOT}/assets/shaders/*")
list(APPEND _engine_boundary_files
    "${PHLOSION_ROOT}/CMakeLists.txt")

set(_violations "")
foreach(_file IN LISTS _engine_boundary_files)
    file(READ "${_file}" _content)
    string(TOLOWER "${_content}" _lower)
    if (_lower MATCHES "pokemon|autochess|gamefreak|lgpe|route[ _-]*1|pac_|evaluatefieldgroundsurface|evaluatefieldcliffsurface|fieldtreeshader|fieldgrassshader|fieldgroundshader|fieldcliffshader|gastly|charmander|tailfire|ikcharacter|nativesss|nativefresneleffect|nativeeyeclearcoat|zaui|zaik|zasourcestage|svlocalprobe|svlocalspecularprobe|zalocalreflectionprobe|z-a")
        list(APPEND _violations "${_file}")
    endif()
    if (_lower MATCHES "(^|[^a-z0-9_])(boardrenderer|battlefeed|healthbarrenderer|healthbardata|growlvfx|scratchvfx|combatdecision|shopms|roundms|combatms|movementluams|sessionbackdroptilesenabled)([^a-z0-9_]|$)"
            OR _lower MATCHES "enginegrowl|enginescratch")
        list(APPEND _violations "${_file}")
    endif()
endforeach()

foreach(_removed_path IN ITEMS
    "${PHLOSION_ROOT}/src/engine/assets/lgpe"
    "${PHLOSION_ROOT}/src/engine/ui/Card.h"
    "${PHLOSION_ROOT}/src/engine/ui/Card.cpp"
    "${PHLOSION_ROOT}/src/engine/render/BoardRenderer.h"
    "${PHLOSION_ROOT}/src/engine/render/BoardRenderer.cpp"
    "${PHLOSION_ROOT}/src/engine/ui/BattleFeed.h"
    "${PHLOSION_ROOT}/src/engine/ui/BattleFeed.cpp"
    "${PHLOSION_ROOT}/src/engine/ui/HealthBarRenderer.h"
    "${PHLOSION_ROOT}/src/engine/ui/HealthBarRenderer.cpp"
    "${PHLOSION_ROOT}/src/engine/ui/HealthBarData.h")
    if (EXISTS "${_removed_path}")
        list(APPEND _violations "${_removed_path}")
    endif()
endforeach()

# Optional editor-tool implementations belong in PhlosionPackages. The Engine
# retains only package ABI/data/action contracts and extension routing.
set(_editor_shell
    "${PHLOSION_ROOT}/src/engine/editor/EditorShell.cpp")
file(READ "${_editor_shell}" _editor_shell_content)
string(TOLOWER "${_editor_shell_content}" _editor_shell_lower)
if (_editor_shell_lower MATCHES
    "terrain tile editor|flatten \+ tidy|terrainauthoringmode|drawterrainprefab")
    list(APPEND _violations "${_editor_shell}")
endif()

if (_violations)
    list(JOIN _violations "\n  " _formatted)
    message(FATAL_ERROR
        "Game-specific code crossed the Phlosion Engine boundary:\n  ${_formatted}")
endif()

message(STATUS
    "Phlosion semantic boundary is clean; project material implementations are external")
