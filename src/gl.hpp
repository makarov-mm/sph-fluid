#pragma once
// OpenGL 3.3 entry points, loaded at runtime so the project needs no GLAD/GLEW.
// GL 1.1 functions (glClear, glDrawArrays, ...) come straight from the system
// header and library; everything newer lives in namespace gl as function pointers.

#ifdef _WIN32
#include "win32.hpp"
#endif
#include <GL/gl.h>

#include <charconv>
#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef GL_VERSION_1_5
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
#endif
#ifndef GL_VERSION_2_0
using GLchar = char;
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif

// X(return type, name without the gl prefix, parameter list)
#define SPH_GL_FUNCTIONS(X)                                                                                    \
    X(GLuint, CreateShader, (GLenum type))                                                                     \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths))   \
    X(void, CompileShader, (GLuint shader))                                                                    \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))                                         \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* log))                  \
    X(void, DeleteShader, (GLuint shader))                                                                     \
    X(GLuint, CreateProgram, ())                                                                               \
    X(void, AttachShader, (GLuint program, GLuint shader))                                                     \
    X(void, LinkProgram, (GLuint program))                                                                     \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))                                       \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* log))                \
    X(void, DeleteProgram, (GLuint program))                                                                   \
    X(void, UseProgram, (GLuint program))                                                                      \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                                         \
    X(void, Uniform1f, (GLint location, GLfloat v0))                                                           \
    X(void, Uniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))                                   \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))      \
    X(void, GenVertexArrays, (GLsizei n, GLuint* arrays))                                                      \
    X(void, BindVertexArray, (GLuint array))                                                                   \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                                             \
    X(void, GenBuffers, (GLsizei n, GLuint* buffers))                                                          \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                                        \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))                      \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))                \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                                                 \
    X(void, EnableVertexAttribArray, (GLuint index))                                                           \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, \
                                  const void* pointer))

namespace gl {

#define SPH_GL_DECLARE(ret, name, params) inline ret(APIENTRY* name) params = nullptr;
SPH_GL_FUNCTIONS(SPH_GL_DECLARE)
#undef SPH_GL_DECLARE

// Resolves every function above for the current context (platform specific).
// On failure returns the name of the first missing function.
std::expected<void, std::string> loadFunctions();

// Checks that the current context provides at least OpenGL major.minor.
inline std::expected<void, std::string> requireVersion(int major, int minor) {
    const auto* raw = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const std::string_view version = raw ? raw : "";
    int haveMajor = 0, haveMinor = 0;
    const char* const end = version.data() + version.size();
    auto [next, ec] = std::from_chars(version.data(), end, haveMajor);
    if (ec == std::errc{} && next != end && *next == '.') std::from_chars(next + 1, end, haveMinor);
    if (haveMajor > major || (haveMajor == major && haveMinor >= minor)) return {};

    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    return std::unexpected(std::format("OpenGL {}.{} is required, but the driver provides {} ({}).", major, minor,
                                       raw ? raw : "an unknown version", renderer ? renderer : "unknown renderer"));
}

// Owning handle for a GL object name; deletes it on destruction.
template <void (*Delete)(GLuint)>
class Object {
public:
    Object() = default;
    explicit Object(GLuint id) noexcept : id_(id) {}
    Object(Object&& other) noexcept : id_(std::exchange(other.id_, 0)) {}
    Object& operator=(Object&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }
    ~Object() { reset(); }

    GLuint get() const noexcept { return id_; }
    void reset() noexcept {
        if (id_ != 0) Delete(id_);
        id_ = 0;
    }

private:
    GLuint id_ = 0;
};

inline void deleteShader(GLuint id) { DeleteShader(id); }
inline void deleteProgram(GLuint id) { DeleteProgram(id); }
inline void deleteBuffer(GLuint id) { DeleteBuffers(1, &id); }
inline void deleteVertexArray(GLuint id) { DeleteVertexArrays(1, &id); }

using Shader = Object<&deleteShader>;
using Program = Object<&deleteProgram>;
using Buffer = Object<&deleteBuffer>;
using VertexArray = Object<&deleteVertexArray>;

inline Buffer createBuffer() {
    GLuint id = 0;
    GenBuffers(1, &id);
    return Buffer{id};
}

inline VertexArray createVertexArray() {
    GLuint id = 0;
    GenVertexArrays(1, &id);
    return VertexArray{id};
}

}  // namespace gl
