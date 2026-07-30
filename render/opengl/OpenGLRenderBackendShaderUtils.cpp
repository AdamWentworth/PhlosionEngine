#include "engine/render/opengl/OpenGLRenderBackendShaderUtils.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace opengl_backend_shader_utils {

unsigned int compileShader(GLenum type, const char* source) {
    const unsigned int shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(
            static_cast<std::size_t>(std::max(logLength, 1)),
            '\0');
        GLsizei written = 0;
        glGetShaderInfoLog(
            shader,
            static_cast<GLsizei>(log.size()),
            &written,
            log.data());
        std::cerr << "[OpenGL][Shader] compile failed type="
                  << static_cast<unsigned int>(type)
                  << " log=" << log.c_str() << '\n';
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
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(
            static_cast<std::size_t>(std::max(logLength, 1)),
            '\0');
        GLsizei written = 0;
        glGetProgramInfoLog(
            program,
            static_cast<GLsizei>(log.size()),
            &written,
            log.data());
        std::cerr << "[OpenGL][Shader] link failed log="
                  << log.c_str() << '\n';
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace opengl_backend_shader_utils

