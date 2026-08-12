if (NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PHLOSION_ROOT is required")
endif()

set(_shader_sources
    "${PHLOSION_ROOT}/assets/shaders/vulkan/world_material.glsl"
    "${PHLOSION_ROOT}/src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp"
    "${PHLOSION_ROOT}/src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp")

set(_violations "")
foreach(_file IN LISTS _shader_sources)
    file(READ "${_file}" _content)

    # Decoders commonly expand a two-channel normal map into RGBA with a
    # constant blue byte. Both observed sentinel values must reconstruct Z;
    # treating blue=255 as authored Z flattens the mapped surface.
    if (NOT _content MATCHES "[.]z[ \t\r\n]*<=[ \t\r\n]*[(]1[.]5f?[ \t]*/[ \t]*255[.]0f?[)]")
        list(APPEND _violations "${_file}: missing blue=0 packed-normal sentinel")
    endif()
    if (NOT _content MATCHES "[.]z[ \t\r\n]*>=[ \t\r\n]*[(]253[.]5f?[ \t]*/[ \t]*255[.]0f?[)]")
        list(APPEND _violations "${_file}: missing blue=255 packed-normal sentinel")
    endif()
endforeach()

if (_violations)
    list(JOIN _violations "\n  " _formatted)
    message(FATAL_ERROR "Packed-normal backend contract failed:\n  ${_formatted}")
endif()

message(STATUS "Packed XY normals reconstruct Z consistently across all backends")
