// Shader.h
#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <unordered_map>

class Shader {
public:
    // Existing constructor (used across the codebase)
    Shader(const char* vertexPath, const char* fragmentPath);

    // NEW: convenience overload for std::string paths.
    // Delegates to the existing const char* constructor so behavior is unchanged.
    Shader(const std::string& vertexPath, const std::string& fragmentPath)
        : Shader(vertexPath.c_str(), fragmentPath.c_str()) {}

    ~Shader();

    void use() const;
    GLuint getID() const { return ID; }

    // Uniform setters
    void setUniform(const std::string &name, float value) const;
    void setUniform(const std::string &name, int value) const;              // NEW
    void setUniform(const std::string &name, const glm::mat4 &matrix) const;
    void setUniform(const std::string &name, const glm::vec3 &vec) const;
    void setUniform(const std::string &name, const glm::vec2 &vec) const;   // NEW

private:
    GLuint ID;
    std::string loadSource(const char* filePath);
    GLuint compileShader(GLenum type, const char* source);

    mutable std::unordered_map<std::string, GLint> uniformLocationCache;
    GLint getUniformLocation(const std::string &name) const;
};

#endif
