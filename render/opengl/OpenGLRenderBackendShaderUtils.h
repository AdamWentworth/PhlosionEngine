#pragma once

#include <glad/glad.h>

namespace opengl_backend_shader_utils {

unsigned int compileShader(GLenum type, const char* source);
unsigned int linkProgram(unsigned int vs, unsigned int fs);

} // namespace opengl_backend_shader_utils

