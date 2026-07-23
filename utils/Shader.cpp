// src/engine/utils/Shader.cpp
#include "Shader.h"

#include "engine/utils/Log.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include <glm/gtc/type_ptr.hpp>

// --------------------
// Include expansion helpers (local to this .cpp)
// --------------------

static std::string readTextFileOrThrow(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        const std::string msg = std::string("Shader file open failed: ") + path;
        LOG_ERROR_T("SHADER", msg);
        throw std::runtime_error(msg);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string getDirectory(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash + 1);
}

static bool isProjectAbsolutePath(const std::string& inc) {
    // Treat includes starting with "assets/" as project-rooted (run dir).
    return inc.rfind("assets/", 0) == 0;
}

static bool isFilesystemAbsolutePath(const std::string& inc) {
    // POSIX + Windows drive letter
    if (!inc.empty() && (inc[0] == '/' || inc[0] == '\\')) return true;
    if (inc.size() >= 3 && std::isalpha((unsigned char)inc[0]) && inc[1] == ':' &&
        (inc[2] == '\\' || inc[2] == '/')) return true;
    return false;
}

static std::string resolveIncludePath(const std::string& includeName, const std::string& parentFilePath) {
    if (isFilesystemAbsolutePath(includeName) || isProjectAbsolutePath(includeName)) {
        return includeName;
    }
    return getDirectory(parentFilePath) + includeName;
}

static std::string expandIncludesRecursive(
    const std::string& source,
    const std::string& parentFilePath,
    std::unordered_set<std::string>& includeGuard
) {
    std::stringstream input(source);
    std::stringstream output;
    std::string line;

    while (std::getline(input, line)) {
        std::string trimmed = line;
        // very light trim (left)
        while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) trimmed.erase(trimmed.begin());

        if (trimmed.rfind("#include", 0) == 0) {
            auto firstQuote = trimmed.find('"');
            auto lastQuote  = trimmed.find_last_of('"');
            if (firstQuote == std::string::npos || lastQuote == std::string::npos || lastQuote <= firstQuote) {
                const std::string msg = std::string("Malformed #include in ") + parentFilePath + ": " + line;
                LOG_ERROR_T("SHADER", msg);
                throw std::runtime_error(msg);
            }
            const std::string includeName = trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            const std::string includePath = resolveIncludePath(includeName, parentFilePath);

            if (includeGuard.count(includePath)) {
                // Prevent include cycles; skip duplicate.
                continue;
            }
            includeGuard.insert(includePath);

            const std::string includedText = readTextFileOrThrow(includePath);
            output << "\n// --- begin include: " << includePath << " ---\n";
            output << expandIncludesRecursive(includedText, includePath, includeGuard);
            output << "\n// --- end include: " << includePath << " ---\n";
        } else {
            output << line << "\n";
        }
    }
    return output.str();
}

static bool supportsDualSourceBlendOpenGL() {
    if (GLAD_GL_VERSION_4_0) return true;
    if (glad_glGetStringi == nullptr) return false;
    GLint extensionCount = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (GLint i = 0; i < extensionCount; ++i) {
        const GLubyte* ext = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
        if (!ext) continue;
        if (std::string_view(reinterpret_cast<const char*>(ext)) == "GL_ARB_blend_func_extended") {
            return true;
        }
    }
    return false;
}

// --------------------
// Shader implementation
// --------------------

Shader::Shader(const char* vertexPath, const char* fragmentPath)
    : ID(0) {

    const std::string vPath = vertexPath ? vertexPath : "";
    const std::string fPath = fragmentPath ? fragmentPath : "";

    if (vPath.empty() || fPath.empty()) {
        const std::string msg = "Shader constructor received empty path(s).";
        LOG_ERROR_T("SHADER", msg);
        throw std::runtime_error(msg);
    }

    std::unordered_set<std::string> guard;
    const std::string vertexSourceRaw   = readTextFileOrThrow(vPath);
    const std::string fragmentSourceRaw = readTextFileOrThrow(fPath);

    const std::string vertexCode   = expandIncludesRecursive(vertexSourceRaw, vPath, guard);

    // reset include guard for fragment so shared includes can still expand independently
    guard.clear();
    const std::string fragmentCode = expandIncludesRecursive(fragmentSourceRaw, fPath, guard);

    GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexCode.c_str());
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentCode.c_str());

    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    if (supportsDualSourceBlendOpenGL() &&
        fragmentCode.find("FragBlendAlpha") != std::string::npos &&
        glad_glBindFragDataLocationIndexed != nullptr) {
        glBindFragDataLocationIndexed(ID, 0u, 0u, "FragColor");
        glBindFragDataLocationIndexed(ID, 0u, 1u, "FragBlendAlpha");
    }
    glLinkProgram(ID);

    GLint success = 0;
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024]{0};
        glGetProgramInfoLog(ID, 1024, nullptr, infoLog);

        glDeleteShader(vertex);
        glDeleteShader(fragment);
        glDeleteProgram(ID);
        ID = 0;

        const std::string msg = std::string("Program link failed (") + vPath + ", " + fPath + "): " + infoLog;
        LOG_ERROR_T("SHADER", msg);
        throw std::runtime_error(msg);
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader::~Shader() {
    if (ID != 0) {
        glDeleteProgram(ID);
        ID = 0;
    }
}

void Shader::use() const {
    glUseProgram(ID);
}

std::string Shader::loadSource(const char* filePath) {
    // Kept for compatibility with existing code; now fail-fast.
    return readTextFileOrThrow(filePath ? std::string(filePath) : std::string());
}

GLuint Shader::compileShader(GLenum type, const char* source) {
    if (!source || source[0] == '\0') {
        const std::string msg = "Shader compile called with empty source.";
        LOG_ERROR_T("SHADER", msg);
        throw std::runtime_error(msg);
    }

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024]{0};
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        glDeleteShader(shader);

        const std::string msg =
            std::string(type == GL_VERTEX_SHADER ? "Vertex" : "Fragment") +
            std::string(" shader compilation failed: ") + infoLog;
        LOG_ERROR_T("SHADER", msg);
        throw std::runtime_error(msg);
    }
    return shader;
}

GLint Shader::getUniformLocation(const std::string &name) const {
    auto it = uniformLocationCache.find(name);
    if (it != uniformLocationCache.end()) {
        return it->second;
    }

    GLint location = glGetUniformLocation(ID, name.c_str());
    if (location == -1) {
        LOG_WARN_T("SHADER", std::string("Uniform '") + name + "' not found.");
    }

    uniformLocationCache[name] = location;
    return location;
}

void Shader::setUniform(const std::string &name, float value) const {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string &name, const glm::mat4 &matrix) const {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
}

void Shader::setUniform(const std::string &name, const glm::vec3 &vec) const {
    glUniform3f(getUniformLocation(name), vec.x, vec.y, vec.z);
}

void Shader::setUniform(const std::string &name, int value) const {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string &name, const glm::vec2 &vec) const {
    glUniform2f(getUniformLocation(name), vec.x, vec.y);
}
