# Embed the descriptor's policy so packaged runtimes do not need a source path.
function(phlosion_project_renderers target descriptor)
    file(READ "${descriptor}" _project)
    string(JSON _rendering GET "${_project}" rendering)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${descriptor}")
    set(_header "${CMAKE_CURRENT_BINARY_DIR}/generated/ProjectRendererPolicy.h")
    file(WRITE "${_header}" "#pragma once\n#include \"engine/render/RendererPolicy.h\"\ninline const engine::render::RendererPolicy& projectRendererPolicy() {\n    static const auto policy = engine::render::parseRendererPolicy(R\"policy({\"rendering\":${_rendering}})policy\");\n    return policy;\n}\n")
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
endfunction()
