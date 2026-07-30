// GLResource.h

#pragma once
#include <glad/glad.h>

// RAII wrapper for a Vertex Array Object
class VertexArray {
public:
    VertexArray() {
        glGenVertexArrays(1, &ID);
    }
    ~VertexArray() {
        glDeleteVertexArrays(1, &ID);
    }
    // Disable copy semantics
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    // Enable move semantics
    VertexArray(VertexArray&& other) noexcept : ID(other.ID) {
        other.ID = 0;
    }
    VertexArray& operator=(VertexArray&& other) noexcept {
        if (this != &other) {
            glDeleteVertexArrays(1, &ID);
            ID = other.ID;
            other.ID = 0;
        }
        return *this;
    }
    GLuint getID() const { return ID; }
private:
    GLuint ID = 0;
};

// RAII wrapper for a general OpenGL Buffer Object (e.g. VBO, EBO)
class BufferObject {
public:
    // target: GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, etc.
    BufferObject(GLenum target_) : target(target_) {
        glGenBuffers(1, &ID);
    }
    ~BufferObject() {
        glDeleteBuffers(1, &ID);
    }
    // Disable copy semantics
    BufferObject(const BufferObject&) = delete;
    BufferObject& operator=(const BufferObject&) = delete;
    // Enable move semantics
    BufferObject(BufferObject&& other) noexcept : ID(other.ID), target(other.target) {
        other.ID = 0;
    }
    BufferObject& operator=(BufferObject&& other) noexcept {
        if (this != &other) {
            glDeleteBuffers(1, &ID);
            ID = other.ID;
            target = other.target;
            other.ID = 0;
        }
        return *this;
    }
    GLuint getID() const { return ID; }
    GLenum getTarget() const { return target; }
private:
    GLuint ID = 0;
    GLenum target;
};
