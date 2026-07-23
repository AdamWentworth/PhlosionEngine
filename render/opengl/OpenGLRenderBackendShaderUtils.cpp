#include "engine/render/opengl/OpenGLRenderBackendShaderUtils.h"

namespace opengl_backend_shader_utils {

unsigned int compileShader(GLenum type, const char* source) {
    const unsigned int shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int linkProgram(unsigned int vs, unsigned int fs) {
    if (vs == 0 || fs == 0) return 0;
    const unsigned int program = glCreateProgram();
    if (program == 0) return 0;
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace opengl_backend_shader_utils

