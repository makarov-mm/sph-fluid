// WGL implementation of gl::loadFunctions().
#include "gl.hpp"

#include <cstdint>

namespace {

PROC resolve(const char* name) {
    PROC proc = wglGetProcAddress(name);
    // Some drivers report failure with small sentinel values instead of nullptr.
    const auto value = reinterpret_cast<std::intptr_t>(proc);
    if (value == 0 || value == 1 || value == 2 || value == 3 || value == -1) {
        static const HMODULE opengl32 = GetModuleHandleW(L"opengl32.dll");
        proc = opengl32 ? GetProcAddress(opengl32, name) : nullptr;
    }
    return proc;
}

// Function pointer casts go through void(*)() to keep -Wcast-function-type quiet.
template <class Fn>
Fn functionCast(PROC proc) {
    return reinterpret_cast<Fn>(reinterpret_cast<void (*)()>(proc));
}

}  // namespace

std::expected<void, std::string> gl::loadFunctions() {
#define SPH_GL_LOAD(ret, name, params)                                  \
    name = functionCast<ret(APIENTRY*) params>(resolve("gl" #name));    \
    if (!name) return std::unexpected(std::string("gl" #name));
    SPH_GL_FUNCTIONS(SPH_GL_LOAD)
#undef SPH_GL_LOAD
    return {};
}
