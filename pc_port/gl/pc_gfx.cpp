#include "pc_gfx.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#if defined(_WIN32)
// glibc's backtrace() has no Windows equivalent worth pulling a dependency in
// for; the wild-vertex diagnostic below degrades to its raw dump instead.
#else
#include <execinfo.h>
#endif
#include <chrono>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <memory>

#include "../timing/pc_render_packet.h"
#include "pc_tev_shader.h"
#include "pc_postprocess.h"
#include "../timing/pc_render_phase.h"
#include "../timing/pc_tick_profiler.h"

#include "pc_opengl.h"

// ── GL Function Pointers (Loaded via SDL_GL_GetProcAddress) ──
typedef void (APIENTRYP PFNGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef void (APIENTRYP PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBUFFERDATAPROC) (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (APIENTRYP PFNGLBUFFERSUBDATAPROC) (GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC) (GLenum type);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC) (void);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC) (GLuint program);
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar *name);
typedef GLint (APIENTRYP PFNGLGETATTRIBLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (APIENTRYP PFNGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void (APIENTRYP PFNGLUNIFORM4FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRYP PFNGLUNIFORM2IPROC) (GLint location, GLint v0, GLint v1);
typedef void (APIENTRYP PFNGLUNIFORM4IPROC) (GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
typedef void (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void (APIENTRYP PCGLGENQUERIESPROC) (GLsizei n, GLuint* ids);
typedef void (APIENTRYP PCGLBEGINQUERYPROC) (GLenum target, GLuint id);
typedef void (APIENTRYP PCGLENDQUERYPROC) (GLenum target);
typedef void (APIENTRYP PCGLGETQUERYOBJECTIVPROC) (GLuint id, GLenum pname, GLint* params);
typedef void (APIENTRYP PCGLGETQUERYOBJECTUI64VPROC) (GLuint id, GLenum pname, GLuint64* params);

// Windows' opengl32 exports only OpenGL 1.1, so anything newer has to come
// through SDL_GL_GetProcAddress like the rest. On Linux these two happen to be
// declared by the system header, which is why they were called directly.
static PFNGLACTIVETEXTUREPROC glActiveTexture_ptr = nullptr;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_ptr = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray_ptr = nullptr;
static PFNGLBLENDEQUATIONPROC glBlendEquation_ptr = nullptr;
static PFNGLGENBUFFERSPROC glGenBuffers_ptr = nullptr;
static PFNGLBINDBUFFERPROC glBindBuffer_ptr = nullptr;
static PFNGLBUFFERDATAPROC glBufferData_ptr = nullptr;
static PFNGLBUFFERSUBDATAPROC glBufferSubData_ptr = nullptr;
static PFNGLCREATESHADERPROC glCreateShader_ptr = nullptr;
static PFNGLSHADERSOURCEPROC glShaderSource_ptr = nullptr;
static PFNGLCOMPILESHADERPROC glCompileShader_ptr = nullptr;
static PFNGLCREATEPROGRAMPROC glCreateProgram_ptr = nullptr;
static PFNGLATTACHSHADERPROC glAttachShader_ptr = nullptr;
static PFNGLLINKPROGRAMPROC glLinkProgram_ptr = nullptr;
static PFNGLUSEPROGRAMPROC glUseProgram_ptr = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ptr = nullptr;
static PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation_ptr = nullptr;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_ptr = nullptr;
static PFNGLDELETESHADERPROC glDeleteShader_ptr = nullptr;
static PFNGLDELETEPROGRAMPROC glDeleteProgram_ptr = nullptr;
static PFNGLGETUNIFORMIVPROC glGetUniformiv_ptr = nullptr;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_ptr = nullptr;
typedef void (APIENTRYP PFNGLUNIFORMMATRIX3FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv_ptr = nullptr;
static PFNGLUNIFORM1IPROC glUniform1i_ptr = nullptr;
static PFNGLUNIFORM1FPROC glUniform1f_ptr = nullptr;
static PFNGLUNIFORM4FPROC glUniform4f_ptr = nullptr;
static PFNGLUNIFORM2FPROC glUniform2f_ptr = nullptr;
static PFNGLUNIFORM2IPROC glUniform2i_ptr = nullptr;
static PFNGLUNIFORM4IPROC glUniform4i_ptr = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ptr = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_ptr = nullptr;
static PFNGLGETSHADERIVPROC glGetShaderiv_ptr = nullptr;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ptr = nullptr;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_ptr = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ptr = nullptr;
static PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform_ptr = nullptr;
static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_ptr = nullptr;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_ptr = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_ptr = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ptr = nullptr;
static PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer_ptr = nullptr;
static PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers_ptr = nullptr;
static PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer_ptr = nullptr;
static PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage_ptr = nullptr;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer_ptr = nullptr;
static PCGLGENQUERIESPROC glGenQueries_ptr = nullptr;
static PCGLBEGINQUERYPROC glBeginQuery_ptr = nullptr;
static PCGLENDQUERYPROC glEndQuery_ptr = nullptr;
static PCGLGETQUERYOBJECTIVPROC glGetQueryObjectiv_ptr = nullptr;
static PCGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v_ptr = nullptr;

#include <SDL2/SDL.h>

static void load_gl_functions() {
    glActiveTexture_ptr = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    glGenVertexArrays_ptr = (PFNGLGENVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
    glBindVertexArray_ptr = (PFNGLBINDVERTEXARRAYPROC)SDL_GL_GetProcAddress("glBindVertexArray");
    glBlendEquation_ptr = (PFNGLBLENDEQUATIONPROC)SDL_GL_GetProcAddress("glBlendEquation");
    glGenBuffers_ptr = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    glBindBuffer_ptr = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    glBufferData_ptr = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    glBufferSubData_ptr = (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
    glCreateShader_ptr = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
    glShaderSource_ptr = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
    glCompileShader_ptr = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
    glCreateProgram_ptr = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
    glAttachShader_ptr = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
    glLinkProgram_ptr = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
    glUseProgram_ptr = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    glGetUniformLocation_ptr = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    glGetAttribLocation_ptr = (PFNGLGETATTRIBLOCATIONPROC)SDL_GL_GetProcAddress("glGetAttribLocation");
    glBindAttribLocation_ptr = (PFNGLBINDATTRIBLOCATIONPROC)SDL_GL_GetProcAddress("glBindAttribLocation");
    glDeleteShader_ptr = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
    glDeleteProgram_ptr = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
    glGetUniformiv_ptr = (PFNGLGETUNIFORMIVPROC)SDL_GL_GetProcAddress("glGetUniformiv");
    glUniformMatrix4fv_ptr = (PFNGLUNIFORMMATRIX4FVPROC)SDL_GL_GetProcAddress("glUniformMatrix4fv");
    glUniformMatrix3fv_ptr = (PFNGLUNIFORMMATRIX3FVPROC)SDL_GL_GetProcAddress("glUniformMatrix3fv");
    glUniform1i_ptr = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");
    glUniform1f_ptr = (PFNGLUNIFORM1FPROC)SDL_GL_GetProcAddress("glUniform1f");
    glUniform4f_ptr = (PFNGLUNIFORM4FPROC)SDL_GL_GetProcAddress("glUniform4f");
    glUniform2f_ptr = (PFNGLUNIFORM2FPROC)SDL_GL_GetProcAddress("glUniform2f");
    glUniform2i_ptr = (PFNGLUNIFORM2IPROC)SDL_GL_GetProcAddress("glUniform2i");
    glUniform4i_ptr = (PFNGLUNIFORM4IPROC)SDL_GL_GetProcAddress("glUniform4i");
    glEnableVertexAttribArray_ptr = (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer_ptr = (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
    glGetShaderiv_ptr = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
    glGetShaderInfoLog_ptr = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    glGetProgramiv_ptr = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
    glGetProgramInfoLog_ptr = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    glGetActiveUniform_ptr = (PFNGLGETACTIVEUNIFORMPROC)SDL_GL_GetProcAddress("glGetActiveUniform");
    glGenFramebuffers_ptr = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    glBindFramebuffer_ptr = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D_ptr = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    glCheckFramebufferStatus_ptr = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    glBlitFramebuffer_ptr = (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebuffer");
    glGenRenderbuffers_ptr = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderbuffers");
    glBindRenderbuffer_ptr = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderbuffer");
    glRenderbufferStorage_ptr = (PFNGLRENDERBUFFERSTORAGEPROC)SDL_GL_GetProcAddress("glRenderbufferStorage");
    glFramebufferRenderbuffer_ptr = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    glGenQueries_ptr = (PCGLGENQUERIESPROC)SDL_GL_GetProcAddress("glGenQueries");
    glBeginQuery_ptr = (PCGLBEGINQUERYPROC)SDL_GL_GetProcAddress("glBeginQuery");
    glEndQuery_ptr = (PCGLENDQUERYPROC)SDL_GL_GetProcAddress("glEndQuery");
    glGetQueryObjectiv_ptr = (PCGLGETQUERYOBJECTIVPROC)SDL_GL_GetProcAddress("glGetQueryObjectiv");
    glGetQueryObjectui64v_ptr = (PCGLGETQUERYOBJECTUI64VPROC)SDL_GL_GetProcAddress("glGetQueryObjectui64v");

    // Every pointer above is called without a null check, so a missing entry
    // point crashes the moment that feature is first used -- which can be deep
    // into a level rather than at startup. Windows' opengl32 exports only
    // OpenGL 1.1, so this is where a driver too old for the port shows up.
    // Name what is missing instead of leaving a silent landmine.
    {
        struct Entry { const char* name; const void* ptr; };
        const Entry entries[] = {
            { "glActiveTexture", (const void*)glActiveTexture_ptr },
            { "glBlendEquation", (const void*)glBlendEquation_ptr },
            { "glGenBuffers", (const void*)glGenBuffers_ptr },
            { "glBindBuffer", (const void*)glBindBuffer_ptr },
            { "glBufferData", (const void*)glBufferData_ptr },
            { "glCreateShader", (const void*)glCreateShader_ptr },
            { "glCreateProgram", (const void*)glCreateProgram_ptr },
            { "glUseProgram", (const void*)glUseProgram_ptr },
            { "glUniformMatrix4fv", (const void*)glUniformMatrix4fv_ptr },
        };
        int missing = 0;
        for (const Entry& entry : entries) {
            if (entry.ptr == nullptr) {
                fprintf(stderr, "[PC GX] OpenGL entry point missing: %s\n", entry.name);
                ++missing;
            }
        }
        if (missing != 0) {
            fprintf(stderr, "[PC GX] %d OpenGL entry point(s) unavailable. The driver is "
                            "too old for this port; expect a crash when they are used.\n",
                    missing);
        }
    }
}

// OpenGL uniform calls carry non-trivial validation/dispatch overhead. GX code
// commonly emits the same material state before many small primitives, so keep
// the last value for every location and only cross the driver boundary when it
// actually changes. Locations outside this deliberately generous range fall
// back to the raw call.
constexpr int PC_UNIFORM_CACHE_SIZE = 512;
// A location number only identifies a uniform within one program, and shader
// specialisation means several programs are now in play. Entries therefore
// carry the generation they were written in, and binding a different program
// bumps the generation: stale values are rejected in O(1) instead of clearing
// every table on each switch.
static uint32_t sUniformGeneration = 1;
// Diagnostic escape hatch: PIKMIN_NO_UNIFORM_CACHE=1 sends every write straight
// to the driver. If a symptom disappears with it set, the cache is at fault and
// not the generated shader.
static bool uniform_cache_disabled() {
    static const bool disabled = std::getenv("PIKMIN_NO_UNIFORM_CACHE") != nullptr;
    return disabled;
}
static void invalidate_uniform_cache() {
    if (++sUniformGeneration == 0) sUniformGeneration = 1;
}
template <typename T> struct UniformCacheEntry {
    uint32_t generation = 0;
    T value {};
};
struct Uniform2iValue { GLint x, y; };
struct Uniform4iValue { GLint x, y, z, w; };
struct Uniform4fValue { GLfloat x, y, z, w; };
struct UniformMat4Value { GLfloat value[16]; };
struct UniformMat3Value { GLfloat value[9]; };
static UniformCacheEntry<GLint> sUniform1iCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<GLfloat> sUniform1fCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<Uniform2iValue> sUniform2iCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<Uniform4iValue> sUniform4iCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<Uniform4fValue> sUniform4fCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<UniformMat4Value> sUniformMat4Cache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<UniformMat3Value> sUniformMat3Cache[PC_UNIFORM_CACHE_SIZE];


// Names the exact uniform write that OpenGL rejects. A rejected write leaves
// the shader reading a stale value, which is indistinguishable from a wrong
// shader by looking at the picture alone. Enabled by PIKMIN_GL_CHECK.
static GLuint sCheckProgram = 0;
static bool uniform_check_enabled() {
    static const bool enabled = std::getenv("PIKMIN_GL_CHECK") != nullptr;
    return enabled;
}
static void check_uniform_write(GLint loc, const char* kind) {
    if (!uniform_check_enabled()) return;
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return;
    static int reported = 0;
    if (reported++ > 40) return;
    char name[128] = "?";
    GLint size = 0; GLenum type = 0; GLsizei length = 0;
    // Locations are not indices, so find the active uniform that owns this one.
    GLint count = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(sCheckProgram, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
        char probe[128];
        if (!glGetActiveUniform_ptr) break;
        glGetActiveUniform_ptr(sCheckProgram, i, sizeof(probe), &length, &size, &type, probe);
        if (glGetUniformLocation_ptr(sCheckProgram, probe) == loc) {
            snprintf(name, sizeof(name), "%s", probe);
            break;
        }
    }
    printf("[PC GX uniform] %s write to location %d rejected (0x%04x) in program %u; that location holds '%s'\n",
           kind, loc, unsigned(error), unsigned(sCheckProgram), name);
    fflush(stdout);
}

static void cached_uniform1i(GLint loc, GLint value) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform1i_ptr(loc, value); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform1i_ptr(loc, value); return; }
    auto& entry = sUniform1iCache[loc];
    if (entry.generation == sUniformGeneration && entry.value == value) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform1i_ptr(loc, value);
    check_uniform_write(loc, "1i");
}
static void cached_uniform1f(GLint loc, GLfloat value) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform1f_ptr(loc, value); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform1f_ptr(loc, value); return; }
    auto& entry = sUniform1fCache[loc];
    if (entry.generation == sUniformGeneration && entry.value == value) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform1f_ptr(loc, value);
    check_uniform_write(loc, "1f");
}
static void cached_uniform2i(GLint loc, GLint x, GLint y) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform2i_ptr(loc, x, y); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform2i_ptr(loc, x, y); return; }
    auto& entry = sUniform2iCache[loc];
    const Uniform2iValue value { x, y };
    if (entry.generation == sUniformGeneration && memcmp(&entry.value, &value, sizeof(value)) == 0) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform2i_ptr(loc, x, y);
    check_uniform_write(loc, "2i");
}
static void cached_uniform4i(GLint loc, GLint x, GLint y, GLint z, GLint w) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform4i_ptr(loc, x, y, z, w); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform4i_ptr(loc, x, y, z, w); return; }
    auto& entry = sUniform4iCache[loc];
    const Uniform4iValue value { x, y, z, w };
    if (entry.generation == sUniformGeneration && memcmp(&entry.value, &value, sizeof(value)) == 0) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform4i_ptr(loc, x, y, z, w);
    check_uniform_write(loc, "4i");
}
static void cached_uniform4f(GLint loc, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform4f_ptr(loc, x, y, z, w); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform4f_ptr(loc, x, y, z, w); return; }
    auto& entry = sUniform4fCache[loc];
    const Uniform4fValue value { x, y, z, w };
    if (entry.generation == sUniformGeneration && memcmp(&entry.value, &value, sizeof(value)) == 0) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform4f_ptr(loc, x, y, z, w);
    check_uniform_write(loc, "4f");
}
static void cached_uniform_mat4(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (loc < 0 || !value) return;
    if (uniform_cache_disabled()) { glUniformMatrix4fv_ptr(loc, count, transpose, value); return; }
    if (count != 1 || transpose != GL_FALSE || loc >= PC_UNIFORM_CACHE_SIZE) {
        glUniformMatrix4fv_ptr(loc, count, transpose, value); return;
    }
    auto& entry = sUniformMat4Cache[loc];
    if (entry.generation == sUniformGeneration && memcmp(entry.value.value, value, sizeof(entry.value.value)) == 0) return;
    entry.generation = sUniformGeneration; memcpy(entry.value.value, value, sizeof(entry.value.value));
    glUniformMatrix4fv_ptr(loc, count, transpose, value);
    check_uniform_write(loc, "mat4");
}
static void cached_uniform_mat3(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (loc < 0 || !value) return;
    if (uniform_cache_disabled()) { glUniformMatrix3fv_ptr(loc, count, transpose, value); return; }
    if (count != 1 || transpose != GL_FALSE || loc >= PC_UNIFORM_CACHE_SIZE) {
        glUniformMatrix3fv_ptr(loc, count, transpose, value); return;
    }
    auto& entry = sUniformMat3Cache[loc];
    if (entry.generation == sUniformGeneration && memcmp(entry.value.value, value, sizeof(entry.value.value)) == 0) return;
    entry.generation = sUniformGeneration; memcpy(entry.value.value, value, sizeof(entry.value.value));
    glUniformMatrix3fv_ptr(loc, count, transpose, value);
    check_uniform_write(loc, "mat3");
}

#define glUniform1i_ptr cached_uniform1i
#define glUniform1f_ptr cached_uniform1f
#define glUniform2i_ptr cached_uniform2i
#define glUniform4i_ptr cached_uniform4i
#define glUniform4f_ptr cached_uniform4f
#define glUniformMatrix4fv_ptr cached_uniform_mat4
#define glUniformMatrix3fv_ptr cached_uniform_mat3

// ── State Storage ──
struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    // The current GLSL backend exposes four texture-coordinate varyings.
    // TEX4-TEX7 still have to be consumed from GX display lists, but retaining
    // them in every streamed vertex only inflated the upload by 32 bytes.
    float tex[4][2];
};
static_assert(sizeof(Vertex) == 72, "Keep the streamed GX vertex compact");

static float sProjMatrix[16];
static float sPosMatrix[64][16];
static u32 sCurrentPosMtxId = 0;

// Normal matrices (3x3 rotation part, stored column-major)
static float sNrmMatrix[64][9];

// Texture coordinate generation
struct TexCoordGen {
    bool active = false;
    int type = 1;      // GX_TG_MTX3X4
    int src = 4;       // GX_TG_TEX0
    u32 mtxIdx = 0;
};
static TexCoordGen sTexCoordGen[8];
static float sTexMatrices[64][16];
// Matrix contents change far less often than they are drawn with, so the batch
// key hashes a revision number per slot rather than the floats themselves.
// That removes ~600 of the ~2 KB the key used to walk on every primitive.
static uint32_t sPosMtxGen[64] = {};
static uint32_t sNrmMtxGen[64] = {};
static uint32_t sTexMtxGen[64] = {};
static uint32_t sProjMtxGen = 0;
static bool sTexMtxLoaded[64] = {};

// Lighting state
struct GfxLight {
    float pos[3];    // world position (or half-vector for specular light 7)
    float dir[3];    // direction / half-vector
    float a[3];      // specular attenuation a0,a1,a2 (N.H quadratic curve)
    float color[4];  // RGBA 0-1
    float k[3];      // distance attenuation k0,k1,k2
    bool active;
};
static GfxLight sLights[8] = {};

// TEV swap mode state
struct TevSwapMode {
    GXTevColorChan red;
    GXTevColorChan green;
    GXTevColorChan blue;
    GXTevColorChan alpha;
};
static TevSwapMode sTevSwapModes[4] = {
    { GX_CH_RED,   GX_CH_GREEN, GX_CH_BLUE,  GX_CH_ALPHA },
    { GX_CH_RED,   GX_CH_RED,   GX_CH_RED,   GX_CH_ALPHA },
    { GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA },
    { GX_CH_BLUE,  GX_CH_BLUE,  GX_CH_BLUE,  GX_CH_ALPHA },
};
static GXTevSwapSel sTevRasSwapSel[GX_MAXTEVSTAGE] = {};
static GXTevSwapSel sTevTexSwapSel[GX_MAXTEVSTAGE] = {};

struct GfxChannel {
    bool enabled = false;
    GXColorSrc matSrc = GX_SRC_VTX;      // Hardware GXInit default: vertex colors
    GXColorSrc ambSrc = GX_SRC_REG;
    u32 lightMask = 0;
    GXDiffuseFn diffFn = GX_DF_NONE;
    GXAttnFn attnFn = GX_AF_NONE;
    bool alphaEnabled = false;
    GXColorSrc alphaMatSrc = GX_SRC_VTX;
    GXColorSrc alphaAmbSrc = GX_SRC_REG;
    u32 alphaLightMask = 0;
    GXDiffuseFn alphaDiffFn = GX_DF_NONE;
    GXAttnFn alphaAttnFn = GX_AF_NONE;
    float matColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  // White: multiply-through no-op
    // GXInit initializes both ambient channel registers to black.  In
    // particular COLOR1 is normally specular; a white default makes every
    // specular material fully white even before a light contributes.
    float ambColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
static GfxChannel sChannels[2] = {}; // COLOR0/ALPHA0, COLOR1/ALPHA1

static constexpr GXAttnFn decode_xf_attn_fn(u32 control) {
    const bool bit9  = (control & (1u << 9)) != 0;
    const bool bit10 = (control & (1u << 10)) != 0;
    if (!bit10) return GX_AF_NONE;
    return bit9 ? GX_AF_SPOT : GX_AF_SPEC;
}

static constexpr u32 decode_xf_light_mask(u32 control) {
    return ((control >> 2) & 0x0Fu) | ((control >> 7) & 0xF0u);
}

static void decode_xf_channel_control(GfxChannel& ch, u32 control, bool alpha) {
    const GXColorSrc matSrc = (control & (1u << 0)) ? GX_SRC_VTX : GX_SRC_REG;
    const bool enabled = (control & (1u << 1)) != 0;
    const GXColorSrc ambSrc = (control & (1u << 6)) ? GX_SRC_VTX : GX_SRC_REG;
    const u32 lightMask = decode_xf_light_mask(control);
    const GXDiffuseFn diffFn = GXDiffuseFn((control >> 7) & 0x3);
    const GXAttnFn attnFn = decode_xf_attn_fn(control);
    if (alpha) {
        ch.alphaMatSrc = matSrc;
        ch.alphaEnabled = enabled;
        ch.alphaAmbSrc = ambSrc;
        ch.alphaLightMask = lightMask;
        ch.alphaDiffFn = diffFn;
        ch.alphaAttnFn = attnFn;
    } else {
        ch.matSrc = matSrc;
        ch.enabled = enabled;
        ch.ambSrc = ambSrc;
        ch.lightMask = lightMask;
        ch.diffFn = diffFn;
        ch.attnFn = attnFn;
    }
}

static_assert(decode_xf_light_mask((1u << 2) | (1u << 14)) == 0x81,
              "XF channel light-mask decoding must preserve lights 0 and 7");

static GLuint sVBO = 0;
static size_t sVboCapacity = 16 * 1024 * 1024;
static size_t sVboWriteOffset = 0;
static GLuint sShaderProgram = 0;
static GLuint sNativeFramebuffer = 0;
static GLuint sNativeColorTexture = 0;
static GLuint sNativeDepthStencil = 0;   // renderbuffer, the fallback
static GLuint sNativeDepthTexture = 0;   // sampled by post-process passes
static bool sDepthIsTexture       = false;
static bool sNativeFramebufferReady = false;
static int sRenderWidth = 640;
static int sRenderHeight = 480;
static float sRenderScale = 2.0f / 3.0f;
static bool sRenderScaleSet = false; // true once set by the settings module

// Aspect ratio support.
enum AspectRatioMode {
    ASPECT_AUTO = 0,    // Detect from window
    ASPECT_4_3 = 1,
    ASPECT_16_10 = 2,
    ASPECT_16_9 = 3,
    ASPECT_21_9 = 4,
    ASPECT_COUNT
};
static int sAspectRatioMode = ASPECT_AUTO;
static float sCurrentAspectRatio = 4.0f / 3.0f; // Current active aspect ratio
static GLuint sBoundTextures[8] = {};
static uint64_t sPerfDraws = 0;
static uint64_t sPerfVertices = 0;
static uint64_t sPerfDisplayLists = 0;
static uint64_t sPerfDisplayListBytes = 0;
static uint64_t sPerfTextureUploads = 0;
static uint64_t sPerfFastDraws = 0;
static uint64_t sPerfSourcePrimitives = 0;
static uint64_t sPerfFastPathDraws[4] = {};
static uint64_t sPerfFastPathVertices[4] = {};
static uint64_t sPerfPrimitiveDraws[7] = {};
static bool sPerfStatsEnabled = false;
static constexpr unsigned PC_GPU_QUERY_RING_SIZE = 8;
struct PerfGpuQuery {
    GLuint scene = 0;
    GLuint blit = 0;
    bool pending = false;
};
static PerfGpuQuery sPerfGpuQueries[PC_GPU_QUERY_RING_SIZE] = {};
static unsigned sPerfGpuQueryWrite = 0;
static bool sPerfGpuQueriesReady = false;
static bool sPerfGpuSceneActive = false;
static uint64_t sPerfGpuSceneNs = 0;
static uint64_t sPerfGpuBlitNs = 0;
static uint64_t sPerfGpuSamples = 0;
struct PerfScope {
    const char* name = nullptr;
    uint64_t draws = 0;
    uint64_t vertices = 0;
    u8 pipeline5ReportCount = 0;
    u8 pipeline7ReportCount = 0;
};
static PerfScope sPerfScopes[8] = {};
static int sPerfCurrentScope = -1;
static uint64_t sPerfTevStageDraws[GX_MAXTEVSTAGE + 1] = {};
struct PerfTevPattern { uint64_t key = 0; uint64_t count = 0; };
static PerfTevPattern sPerfTevPatterns[128] = {};
struct PerfTevMultiPattern { uint64_t key = 0; uint64_t count = 0; u8 stages = 0; };
static PerfTevMultiPattern sPerfTevMultiPatterns[128] = {};
// Uniform locations belong to a program, and specialisation means there is
// now one program per TEV configuration instead of a single ubershader. The
// active program's locations live here; switching programs assigns a cached
// set rather than re-querying the driver.
struct ProgramLocations {
	GLint projMtx = -1;
	GLint posMtx = -1;
	GLint materialColor = -1;
	GLint useMaterialRgb = -1;
	GLint useMaterialAlpha = -1;
	GLint alphaComp0 = -1;
	GLint alphaComp1 = -1;
	GLint alphaOp = -1;
	GLint alphaRef0 = -1;
	GLint alphaRef1 = -1;
	GLint numStages = -1;
	GLint fastPath = -1;
	GLint tevPrev = -1;
	GLint tevReg0 = -1;
	GLint tevReg1 = -1;
	GLint tevReg2 = -1;
	GLint konst[4] = {-1, -1, -1, -1};
	GLint tevKonst[GX_MAXTEVSTAGE] = {};
	GLint tevCSel[GX_MAXTEVSTAGE] = {};
	GLint tevASel[GX_MAXTEVSTAGE] = {};
	GLint tevCOps[GX_MAXTEVSTAGE] = {};
	GLint tevAOps[GX_MAXTEVSTAGE] = {};
	GLint tevTexInfo[GX_MAXTEVSTAGE] = {};
	GLint tevSwapTable[4] = {};
	GLint tevSwapSel[GX_MAXTEVSTAGE] = {};
	GLint tex[8] = {};
	GLint nrmMtx = -1;
	GLint numLights = -1;
	GLint lightPos[4] = {};
	GLint lightColor[4] = {};
	GLint lightK[4] = {};
	GLint ambColor = -1;
	GLint chan0En = -1;
	GLint chan1En = -1;
	GLint chan0AttnFn = -1;
	GLint chan1AttnFn = -1;
	GLint numLights1 = -1;
	GLint lightPos1[4] = {};
	GLint lightColor1[4] = {};
	GLint lightK1[4] = {};
	GLint ambColor1 = -1;
	GLint materialColor1 = -1;
	GLint useMaterialRgb1 = -1;
	GLint specHalf1 = -1;   // specular half-vector for channel 1
	GLint specAttn1 = -1;   // specular a[] coefficients
	GLint fogParams = -1;
	GLint fogColour = -1;
	GLint tevChan[GX_MAXTEVSTAGE] = {};
	GLint tcMode[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	GLint tcMtx[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
};

static ProgramLocations sLoc;
// Fixed vertex attribute slots, forced with glBindAttribLocation on every
// program so a single vertex array object describes them all.
enum : GLuint {
    kAttrPosIndex       = 0,
    kAttrColorIndex     = 1,
    kAttrNormalIndex    = 2,
    kAttrTexCoord0Index = 3, // through 6; the Vertex carries four of them
};
static constexpr int kVertexTexCoordCount = 4;
static GLint sAttrNormal = GLint(kAttrNormalIndex);
// Only the four coordinates the streamed Vertex actually carries get a slot;
// the rest stay -1 so nothing enables an array that reads past the struct.
static GLint sAttrTexCoord[8] = {
    GLint(kAttrTexCoord0Index + 0), GLint(kAttrTexCoord0Index + 1),
    GLint(kAttrTexCoord0Index + 2), GLint(kAttrTexCoord0Index + 3),
    -1, -1, -1, -1,
};
static GLint sAttrPos = GLint(kAttrPosIndex);
static GLint sAttrColor = GLint(kAttrColorIndex);

// Ubershader locations, kept so the fallback path can be restored, plus the
// program currently bound and the vertex shader shared by every program.
static ProgramLocations sUberLocations;
static GLuint sCurrentProgram = 0;
static GLuint sSharedVertexShader = 0;
// One generated program per TEV configuration instead of the interpreting
// ubershader. PIKMIN_TEV_SPECIALIZE=0 falls back to the ubershader for the
// whole session; PIKMIN_TEV_MAX_STAGES=N restricts specialisation to materials
// of at most N stages, which is useful for bisecting a suspected bad material.
static bool sSpecialiseShaders = true;
static int sSpecialiseMaxStages = 16;
static bool sSpecialiseFailed = false;
static uint64_t sPerfShaderCompiles = 0;

static std::vector<Vertex> sVertexStream;
static GXPrimitive sCurrentPrimType;
static u16 sExpectedVerts = 0;
static bool sInPrimitive = false;
static bool sHaveVertex = false;
static bool sVerticesPretransformed = false;

// PERF-NATIVE-002 step 1: GL pipeline state that lives outside pc_gfx_end
// (blend, depth, cull, color mask, viewport, scissor, framebuffer, clears) is
// not mirrored in any variable here, so the probe cannot hash it. Each setter
// bumps this instead: a change of epoch is a change of state, which is exactly
// what a batch would have to flush on. The same list is the flush-point
// inventory step 5 needs, so it is worth getting complete now rather than
// rediscovering it as visual corruption later.
static uint64_t sGlStateEpoch = 0;
void pc_gfx_flush_batch(void);
// Called from each GL-state setter *after* its redundancy guard and *before*
// it touches GL, so the pending batch is drawn under the state it was built
// with. Placing it after the guard matters: most of the game's state
// programming is redundant, and flushing on those would defeat batching.
static inline void pc_gfx_note_gl_state_change() {
    pc_gfx_flush_batch();
    ++sGlStateEpoch;
}

// ── Render Packet Capture System ──
// Captures immutable render packets at the GX -> OpenGL boundary.
// These packets can be replayed without re-entering game code.

static PcRenderPacketStore sPacketStore;
static bool sCaptureEnabled = false;
static bool sCaptureActive = false;
static uint64_t sCurrentCaptureSerial = 0;
static std::vector<uint8_t> sCaptureBuffer;
static float sReplayClearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
static float sReplayClearDepth = 1.0f;

void pc_gfx_begin_capture(uint64_t serial) {
    if (!sCaptureEnabled) return;
    sCaptureActive = true;
    sCurrentCaptureSerial = serial;
    sCaptureBuffer.clear();
    sPacketStore.beginAuthoritativeTick(serial);
}

void pc_gfx_end_capture() {
    sCaptureActive = false;
    sPacketStore.endAuthoritativeTick();
}

void pc_gfx_enable_capture(bool enabled) {
    sCaptureEnabled = enabled;
}

bool pc_gfx_is_capture_enabled() {
    return sCaptureEnabled;
}

PcRenderPacketStore& pc_gfx_get_packet_store() {
    return sPacketStore;
}

// Replay captured display list without re-entering game code
void pc_gfx_replay_display_list(const void* list, u32 nbytes) {
    if (!list || nbytes == 0) return;
    // Temporarily disable capture during replay to avoid infinite recursion
    bool wasCapturing = sCaptureActive;
    sCaptureActive = false;
    pc_gfx_call_display_list(list, nbytes);
    sCaptureActive = wasCapturing;
}

// Replay the entire authoritative tick's captured display lists into a cleared
// internal framebuffer and present it, WITHOUT re-entering any game code.
// This demonstrates that replaying the same immutable packet twice is visually
// identical. Returns false if there is nothing to replay.
bool pc_gfx_replay_captured_frame(void) {
    PcRenderPacketStore& store = sPacketStore;
    if (!sCaptureEnabled) return false;

    const uint64_t targetSerial = pc_render_tick_serial();
    if (!store.preparePresentation(targetSerial, 1.0)) return false;

    const PcRenderFrame* frame = store.getPresentationFrame();
    if (!frame || frame->packets.empty()) {
        store.clearPresentation();
        return false;
    }

    if (!sNativeFramebufferReady || !glBindFramebuffer_ptr) return false;

    // Bind the internal framebuffer and clear to the authoritative clear color.
    pc_gfx_flush_batch();
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glClearColor(sReplayClearColor[0], sReplayClearColor[1], sReplayClearColor[2], sReplayClearColor[3]);
    glClearDepth(sReplayClearDepth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Replay all captured display lists without touching game state.
    bool wasCapturing = sCaptureActive;
    sCaptureActive = false;
    for (const auto& packet : frame->packets) {
        if (packet->valid && !packet->displayListPayload.empty()) {
            pc_gfx_call_display_list(packet->displayListPayload.data(),
                                     static_cast<u32>(packet->displayListPayload.size()));
        }
    }
    sCaptureActive = wasCapturing;

    store.clearPresentation();

    // Present the replayed frame to the default framebuffer.
    pc_gfx_present();
    return true;
}

struct VertexArrayState {
    const u8* base = nullptr;
    u8 stride = 0;
};struct VertexFormatState {
    GXCompCnt count = GX_COMPCNT_NULL;
    GXCompType type = GX_F32;
    u8 frac = 0;
};
static GXAttrType sVtxDesc[GX_VA_MAX_ATTR] = {};
static VertexArrayState sVtxArrays[GX_VA_MAX_ATTR];
static VertexFormatState sVtxFormats[GX_MAX_VTXFMT][GX_VA_MAX_ATTR];

// Active texture binding
// A stale vertex array -- one the game set for an earlier model and never
// reprogrammed -- reads plausible indices out of memory that now belongs to
// something else. Recording when each array was last set distinguishes that
// from a genuinely misaligned index, which is the other way to get garbage.
static uint64_t sFrameSerial = 0;
static uint64_t sArraySetFrame[GX_VA_MAX_ATTR] = {};
static uint64_t sArraySetSerial[GX_VA_MAX_ATTR] = {};
static uint64_t sArraySetCounter = 0;

static GLuint sActiveGLTextures[GX_MAX_TEXMAP] = {};
static bool sHasActiveTextures[GX_MAX_TEXMAP] = {};
static bool sUseMaterialRgb = false;
static bool sUseMaterialAlpha = false;
static float sMaterialColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static GXCompare sAlphaComp0 = GX_ALWAYS;
static GXCompare sAlphaComp1 = GX_ALWAYS;
static GXAlphaOp sAlphaOp = GX_AOP_AND;
static float sAlphaRef0 = 0.0f;
static float sAlphaRef1 = 0.0f;
static bool sColorUpdate = true;
static bool sAlphaUpdate = true;
static float sCopyClearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
static float sCopyClearDepth = 1.0f;
static u8 sNumTevStages = 1;

// Per-stage TEV state. Defaults mirror the GX hardware reset state: pass
// rasterized color through to PREV, so nothing renders black before the game
// programs its first material.
struct TevStageState {
	GXTevColorArg colorIn[4] = { GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC };
	GXTevAlphaArg alphaIn[4] = { GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA };
	GXTevOp colorOp = GX_TEV_ADD;
	GXTevBias colorBias = GX_TB_ZERO;
	GXTevScale colorScale = GX_CS_SCALE_1;
	GXBool colorClamp = GX_TRUE;
	GXTevRegID colorOutReg = GX_TEVPREV;
	GXTevOp alphaOp = GX_TEV_ADD;
	GXTevBias alphaBias = GX_TB_ZERO;
	GXTevScale alphaScale = GX_CS_SCALE_1;
	GXBool alphaClamp = GX_TRUE;
	GXTevRegID alphaOutReg = GX_TEVPREV;
	GXTexMapID texMap = GX_TEXMAP_NULL;
	GXTexCoordID texCoord = GX_TEXCOORD_NULL;
	bool textureEnabled = false;
	int rasChannel = 0; // 0/1 select a lit color channel; -1 means COLOR_NULL.
};

// BP TEV combiner register layout. Keep these helpers as the single source of
// truth for display-list decoding; the static assertions guard against the
// operand reversal that previously made the shader compensate for bad state.
static constexpr GXTevColorArg decode_tev_color_a(u32 value) { return GXTevColorArg((value >> 12) & 0xF); }
static constexpr GXTevColorArg decode_tev_color_b(u32 value) { return GXTevColorArg((value >> 8) & 0xF); }
static constexpr GXTevColorArg decode_tev_color_c(u32 value) { return GXTevColorArg((value >> 4) & 0xF); }
static constexpr GXTevColorArg decode_tev_color_d(u32 value) { return GXTevColorArg(value & 0xF); }
static constexpr GXTevAlphaArg decode_tev_alpha_a(u32 value) { return GXTevAlphaArg((value >> 13) & 0x7); }
static constexpr GXTevAlphaArg decode_tev_alpha_b(u32 value) { return GXTevAlphaArg((value >> 10) & 0x7); }
static constexpr GXTevAlphaArg decode_tev_alpha_c(u32 value) { return GXTevAlphaArg((value >> 7) & 0x7); }
static constexpr GXTevAlphaArg decode_tev_alpha_d(u32 value) { return GXTevAlphaArg((value >> 4) & 0x7); }

static_assert(decode_tev_color_a(0x1234) == GXTevColorArg(1));
static_assert(decode_tev_color_b(0x1234) == GXTevColorArg(2));
static_assert(decode_tev_color_c(0x1234) == GXTevColorArg(3));
static_assert(decode_tev_color_d(0x1234) == GXTevColorArg(4));
static_assert(decode_tev_alpha_a(0x29CF) == GXTevAlphaArg(1));
static_assert(decode_tev_alpha_b(0x29CF) == GXTevAlphaArg(2));
static_assert(decode_tev_alpha_c(0x29CF) == GXTevAlphaArg(3));
static_assert(decode_tev_alpha_d(0x29CF) == GXTevAlphaArg(4));

static TevStageState sTevStages[GX_MAXTEVSTAGE];
static float sTevRegisters[4][4] = {
	{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}
};
static float sKonstColors[4][4] = {
	{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}
};
static GXTevKColorSel sKonstColorSel[GX_MAXTEVSTAGE] = {};
static GXTevKAlphaSel sKonstAlphaSel[GX_MAXTEVSTAGE] = {};

static float tev_konst_fraction(int selector) {
    static const float fractions[8] = { 1.0f, 7.0f / 8.0f, 3.0f / 4.0f, 5.0f / 8.0f,
                                        1.0f / 2.0f, 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 8.0f };
    return selector >= 0 && selector < 8 ? fractions[selector] : 1.0f;
}

static void resolve_tev_konst(u8 stage, float out[4]) {
    const int colorSel = static_cast<int>(sKonstColorSel[stage]);
    if (colorSel < 8) {
        out[0] = out[1] = out[2] = tev_konst_fraction(colorSel);
    } else if (colorSel >= 12 && colorSel < 16) {
        const float* color = sKonstColors[colorSel - 12];
        out[0] = color[0]; out[1] = color[1]; out[2] = color[2];
    } else if (colorSel >= 16 && colorSel < 32) {
        const int component = (colorSel - 16) >> 2;
        const int colorIndex = (colorSel - 16) & 3;
        const float value = sKonstColors[colorIndex][component];
        out[0] = out[1] = out[2] = value;
    } else {
        out[0] = out[1] = out[2] = 1.0f;
    }

    const int alphaSel = static_cast<int>(sKonstAlphaSel[stage]);
    if (alphaSel < 8) {
        out[3] = tev_konst_fraction(alphaSel);
    } else if (alphaSel >= 16 && alphaSel < 32) {
        const int component = (alphaSel - 16) >> 2;
        const int colorIndex = (alphaSel - 16) & 3;
        out[3] = sKonstColors[colorIndex][component];
    } else {
        out[3] = 1.0f;
    }
}

// Map of GXTexObj pointers to OpenGL Texture IDs
static std::unordered_map<uintptr_t, GLuint> sTextureCache;

struct PcTextureSignature {
    const void* image = nullptr;
    u16 width = 0;
    u16 height = 0;
    u32 format = 0;
    GXTexWrapMode wrapS = GX_CLAMP;
    GXTexWrapMode wrapT = GX_CLAMP;
    bool indexed = false;
    u32 tlutName = 0;

    bool operator==(const PcTextureSignature& other) const {
        return image == other.image && width == other.width && height == other.height
            && format == other.format && wrapS == other.wrapS && wrapT == other.wrapT
            && indexed == other.indexed && tlutName == other.tlutName;
    }
};
static std::unordered_map<uintptr_t, PcTextureSignature> sTextureSignatures;

struct PcTlut {
    std::vector<u8> rgba;
};
struct PcCiTexture {
    const u8* image = nullptr;
    u16 width = 0;
    u16 height = 0;
    GXCITexFmt format = GX_TF_C4;
    GXTexWrapMode wrapS = GX_CLAMP;
    GXTexWrapMode wrapT = GX_CLAMP;
    u32 tlutName = 0;
};
static std::unordered_map<uintptr_t, PcTlut> sTlutObjects;
static std::unordered_map<u32, PcTlut> sLoadedTluts;
static std::unordered_map<uintptr_t, PcCiTexture> sCiTextures;
static int sDrawableWidth = 640;
static int sDrawableHeight = 480;

// Pikmin renders into the GameCube's 640x480 EFB coordinate space, but we scale
// it to the target aspect ratio. Translate GX's top-left origin to OpenGL's bottom-left.
static void map_gx_rect(float x, float y, float width, float height,
                        GLint& glX, GLint& glY, GLsizei& glWidth, GLsizei& glHeight) {
    const float targetWidth = sNativeFramebufferReady ? float(sRenderWidth) : float(sDrawableWidth);
    const float targetHeight = sNativeFramebufferReady ? float(sRenderHeight) : float(sDrawableHeight);
    
    // Base dimensions match the target aspect ratio (not always 4:3)
    float aspect = sCurrentAspectRatio;
    int baseWidth = int(lroundf(480.0f * aspect));
    int baseHeight = 480;
    
    const float scale = fminf(targetWidth / float(baseWidth), targetHeight / float(baseHeight));
    const float offsetX = (targetWidth - float(baseWidth) * scale) * 0.5f;
    const float offsetY = (targetHeight - float(baseHeight) * scale) * 0.5f;
    
    // Scale from 640x480 GX coordinates to the target aspect ratio
    float scaleX = float(baseWidth) / 640.0f;
    
    glX = (GLint)lroundf(offsetX + x * scaleX * scale);
    glY = (GLint)lroundf(offsetY + (480.0f - y - height) * scale);
    glWidth = (GLsizei)std::max(0L, lroundf(width * scaleX * scale));
    glHeight = (GLsizei)std::max(0L, lroundf(height * scale));
}

// ── GLSL Shaders ──
static const char* vShaderSrc = 
    "#version 140\n"
    "in vec3 aPos;\n"
    "in vec3 aNormal;\n"
    "in vec4 aColor;\n"
    "in vec2 aTexCoord0;\n"
    "in vec2 aTexCoord1;\n"
    "in vec2 aTexCoord2;\n"
    "in vec2 aTexCoord3;\n"
    "uniform mat4 uProjMtx;\n"
    "uniform mat4 uPosMtx;\n"
    "uniform mat3 uNrmMtx;\n"
    "uniform int uNumLights;\n"
    "uniform vec4 uLightPos[4];\n"
    "uniform vec4 uLightColor[4];\n"
    "uniform vec4 uLightK[4];\n"     // distance attenuation k0,k1,k2,enabled
    "uniform vec4 uAmbColor;\n"
    "uniform int uChan0En;\n"
    "uniform int uChan1En;\n"
    "uniform int uChan0AttnFn;\n"  // 0=NONE,1=SPOT,2=SPEC
    "uniform int uChan1AttnFn;\n"
    "uniform int uNumLights1;\n"
    "uniform vec4 uLightPos1[4];\n"
    "uniform vec4 uLightColor1[4];\n"
    "uniform vec4 uLightK1[4];\n"
    "uniform vec4 uAmbColor1;\n"
    "uniform vec4 uSpecHalf1;\n"   // specular half-vector for channel 1
    "uniform vec4 uSpecAttn1;\n"   // specular a0,a1,a2 quadratic coefficients
    "uniform int uTcMode[8];\n"      // 0=direct uv, 1=position, 2=normal
    "uniform mat4 uTcMtx[8];\n"
    "out vec3 vLit0;\n"
    "out vec3 vLit1;\n"
    "out vec4 vColor;\n"
    "out vec2 vTexCoord0;\n"
    "out vec2 vTexCoord1;\n"
    "out vec2 vTexCoord2;\n"
    "out vec2 vTexCoord3;\n"
    "vec3 doLights(vec3 N, vec4 wp, int n, vec4 lp[4], vec4 lc[4], vec4 lk[4]) {\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int i = 0; i < n; i++) {\n"
    "        vec3 Ld = lp[i].xyz - wp.xyz;\n"
    "        float dist = length(Ld);\n"
    "        vec3 L = Ld / max(dist, 0.001);\n"
    "        float diff = max(dot(N, L), 0.0);\n"
    "        if (lk[i].w > 0.5) {\n"
    "            diff *= clamp(lk[i].x + lk[i].y * dist + lk[i].z * dist * dist, 0.0, 1.0);\n"
    "        }\n"
    "        sum += diff * lc[i].rgb;\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "vec2 rawTc(int index) {\n"
    "    if (index == 0) return aTexCoord0;\n"
    "    if (index == 1) return aTexCoord1;\n"
    "    if (index == 2) return aTexCoord2;\n"
    "    if (index == 3) return aTexCoord3;\n"
    "    return aTexCoord3;\n"
    "}\n"
    "vec2 genTc(int slot, vec4 viewPos, vec3 nrm, vec2 uvIn, vec2 tc0, vec2 tc1, vec2 tc2, vec2 tc3) {\n"
    "    int mode = uTcMode[slot];\n"
    "    if (mode == 0) return uvIn;\n"
    "    vec4 src = (mode == 1) ? viewPos\n"
    "             : (mode == 2) ? vec4(nrm, 1.0)\n"
    "             : (mode >= 11) ? vec4((mode == 11) ? tc0 : (mode == 12) ? tc1 : (mode == 13) ? tc2 : tc3, 0.0, 1.0)\n"
    "             : vec4(rawTc(mode - 3), 0.0, 1.0);\n"
    "    return (uTcMtx[slot] * src).xy;\n"
    "}\n"
    "void main() {\n"
    "    vec4 worldPos = uPosMtx * vec4(aPos, 1.0);\n"
    "    gl_Position = uProjMtx * worldPos;\n"
    "    vec3 N = normalize(uNrmMtx * aNormal);\n"
    "    vec3 lit0 = uAmbColor.rgb + doLights(N, worldPos, uNumLights, uLightPos, uLightColor, uLightK);\n"
    "    vec3 spec1 = vec3(0.0);\n"
    "    if (uChan1AttnFn == 2 && uNumLights1 > 0) {\n"
    "        // GX hardware specular: att = clamp(a0 + a1*cosT + a2*cosT^2)\n"
    "        float cosT = max(dot(N, normalize(uSpecHalf1.xyz)), 0.0);\n"
    "        float att = clamp(uSpecAttn1.x + uSpecAttn1.y * cosT + uSpecAttn1.z * cosT * cosT, 0.0, 1.0);\n"
    "        spec1 = att * uLightColor1[0].rgb;\n"
    "    }\n"
    "    // GX_AF_SPEC uses the channel's attenuation function to produce the\n"
    "    // specular term.  Feeding the same light through the diffuse path as\n"
    "    // well double-counts COLOR1 and saturates specular materials white.\n"
    "    vec3 diffuse1 = (uChan1AttnFn == 2)\n"
    "        ? vec3(0.0)\n"
    "        : doLights(N, worldPos, uNumLights1, uLightPos1, uLightColor1, uLightK1);\n"
    "    vec3 lit1 = uAmbColor1.rgb + diffuse1 + spec1;\n"
    "    vLit0 = uChan0En != 0 ? clamp(lit0, 0.0, 1.0) : vec3(-1.0);\n"
    "    vLit1 = uChan1En != 0 ? clamp(lit1, 0.0, 1.0) : vec3(-1.0);\n"
    "    vColor = aColor;\n"
    // GX permits later texgens to use the output of an earlier texgen as
    // their source (GX_TG_TEXCOORD0..6). Evaluate in hardware order instead
    // of falling back to the usually absent raw attribute for that slot.
    "    vec2 tc0 = genTc(0, worldPos, N, aTexCoord0, vec2(0.0), vec2(0.0), vec2(0.0), vec2(0.0));\n"
    "    vec2 tc1 = genTc(1, worldPos, N, aTexCoord1, tc0, vec2(0.0), vec2(0.0), vec2(0.0));\n"
    "    vec2 tc2 = genTc(2, worldPos, N, aTexCoord2, tc0, tc1, vec2(0.0), vec2(0.0));\n"
    "    vec2 tc3 = genTc(3, worldPos, N, aTexCoord3, tc0, tc1, tc2, vec2(0.0));\n"
    "    vTexCoord0 = tc0;\n"
    "    vTexCoord1 = tc1;\n"
    "    vTexCoord2 = tc2;\n"
    "    vTexCoord3 = tc3;\n"
    "}\n";

static const char* fShaderSrc = 
    "#version 140\n"
    "in vec3 vLit0;\n"
    "in vec3 vLit1;\n"
    "in vec4 vColor;\n"
    "in vec2 vTexCoord0;\n"
    "in vec2 vTexCoord1;\n"
    "in vec2 vTexCoord2;\n"
    "in vec2 vTexCoord3;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D uTex0;\n"
    "uniform sampler2D uTex1;\n"
    "uniform sampler2D uTex2;\n"
    "uniform sampler2D uTex3;\n"
    "uniform sampler2D uTex4;\n"
    "uniform sampler2D uTex5;\n"
    "uniform sampler2D uTex6;\n"
    "uniform sampler2D uTex7;\n"
    "uniform int uNumStages;\n"
    "uniform int uFastPath;\n" // 1=texture, 2=raster, 3=texture*raster
    "uniform vec4 uMaterialColor;\n"
    "uniform bool uUseMaterialRgb;\n"
    "uniform bool uUseMaterialAlpha;\n"
    "uniform vec4 uMaterialColor1;\n"
    "uniform bool uUseMaterialRgb1;\n"
    "uniform ivec4 uTevChan[16];\n"
    "uniform vec4 uTevPrev;\n"
    "uniform vec4 uTevReg0;\n"
    "uniform vec4 uTevReg1;\n"
    "uniform vec4 uTevReg2;\n"
    "uniform vec4 uKonst0;\n"
    "uniform vec4 uKonst1;\n"
    "uniform vec4 uKonst2;\n"
    "uniform vec4 uKonst3;\n"
    "uniform vec4 uTevKonst[16];\n"
    "uniform int uAlphaComp0;\n"
    "uniform int uAlphaComp1;\n"
    "uniform int uAlphaOp;\n"
    "uniform float uAlphaRef0;\n"
    "uniform float uAlphaRef1;\n"
    "uniform ivec4 uTevCSel[16];\n"
    "uniform ivec4 uTevASel[16];\n"
    "uniform ivec4 uTevCOps[16];\n"
    "uniform ivec4 uTevAOps[16];\n"
    "uniform ivec4 uTevTexInfo[16];\n"
    "uniform ivec4 uTevSwapTable[4];\n"
    "uniform ivec2 uTevSwapSel[16];\n"
    "bool alphaCompare(float value, float reference, int func) {\n"
    "    if (func == 0) return false;\n"
    "    if (func == 1) return value < reference;\n"
    "    if (func == 2) return abs(value - reference) < (0.5 / 255.0);\n"
    "    if (func == 3) return value <= reference;\n"
    "    if (func == 4) return value > reference;\n"
    "    if (func == 5) return abs(value - reference) >= (0.5 / 255.0);\n"
    "    if (func == 6) return value >= reference;\n"
    "    return true;\n"
    "}\n"
    "vec2 resolveCoord(int coordIdx) {\n"
    "    if (coordIdx == 0) return vTexCoord0;\n"
    "    if (coordIdx == 1) return vTexCoord1;\n"
    "    if (coordIdx == 2) return vTexCoord2;\n"
    "    if (coordIdx == 3) return vTexCoord3;\n"
    "    return vTexCoord3;\n"
    "}\n"
    "vec4 resolveTex(int texIdx, int coordIdx) {\n"
    "    vec2 uv = resolveCoord(coordIdx);\n"
    "    if (texIdx == 0) return texture(uTex0, uv);\n"
    "    if (texIdx == 1) return texture(uTex1, uv);\n"
    "    if (texIdx == 2) return texture(uTex2, uv);\n"
    "    if (texIdx == 3) return texture(uTex3, uv);\n"
    "    if (texIdx == 4) return texture(uTex4, uv);\n"
    "    if (texIdx == 5) return texture(uTex5, uv);\n"
    "    if (texIdx == 6) return texture(uTex6, uv);\n"
    "    return texture(uTex7, uv);\n"
    "}\n"
    "float swapComponent(vec4 value, int component) {\n"
    "    if (component == 0) return value.r;\n"
    "    if (component == 1) return value.g;\n"
    "    if (component == 2) return value.b;\n"
    "    return value.a;\n"
    "}\n"
    "vec4 applySwap(vec4 value, int tableIndex) {\n"
    "    ivec4 table = uTevSwapTable[clamp(tableIndex, 0, 3)];\n"
    "    return vec4(swapComponent(value, table.x), swapComponent(value, table.y),\n"
    "                swapComponent(value, table.z), swapComponent(value, table.w));\n"
    "}\n"
    "float packTevCompare(vec3 value, int components) {\n"
    "    vec3 q = floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5);\n"
    "    if (components == 1) return q.r;\n"
    "    if (components == 2) return q.r + q.g * 256.0;\n"
    "    return q.r + q.g * 256.0 + q.b * 65536.0;\n"
    "}\n"
    "vec3 compareTevColor(int op, vec3 a, vec3 b, vec3 c, vec3 d) {\n"
    "    bool isEqual = (op & 1) != 0;\n"
    "    if (op >= 14) {\n"
    "        vec3 qa = floor(clamp(a, 0.0, 1.0) * 255.0 + 0.5);\n"
    "        vec3 qb = floor(clamp(b, 0.0, 1.0) * 255.0 + 0.5);\n"
    "        bvec3 pass = isEqual ? equal(qa, qb) : greaterThan(qa, qb);\n"
    "        return d + vec3(pass.x ? c.r : 0.0, pass.y ? c.g : 0.0, pass.z ? c.b : 0.0);\n"
    "    }\n"
    "    int components = (op < 10) ? 1 : (op < 12) ? 2 : 3;\n"
    "    float av = packTevCompare(a, components);\n"
    "    float bv = packTevCompare(b, components);\n"
    "    bool pass = isEqual ? (av == bv) : (av > bv);\n"
    "    return d + (pass ? c : vec3(0.0));\n"
    "}\n"
    "vec4 resolveC(int id, vec4 prev, vec4 c0, vec4 c1, vec4 c2, vec4 konst, vec4 tex, vec4 rast, int stage) {\n"
    "    if (id == 0) return prev;\n"
    "    if (id == 1) return vec4(prev.a, prev.a, prev.a, 1.0);\n"
    "    if (id == 2) return c0;\n"
    "    if (id == 3) return vec4(c0.a, c0.a, c0.a, 1.0);\n"
    "    if (id == 4) return c1;\n"
    "    if (id == 5) return vec4(c1.a, c1.a, c1.a, 1.0);\n"
    "    if (id == 6) return c2;\n"
    "    if (id == 7) return vec4(c2.a, c2.a, c2.a, 1.0);\n"
    "    if (id == 8) return tex;\n"
    "    if (id == 9) return vec4(tex.a, tex.a, tex.a, 1.0);\n"
    "    if (id == 10) return rast;\n"
    "    if (id == 11) return vec4(rast.a, rast.a, rast.a, 1.0);\n"
    "    if (id == 12) return vec4(1.0);\n"
    "    if (id == 13) return vec4(0.5);\n"
    "    if (id == 14) return konst;\n"
    "    return vec4(0.0);\n"
    "}\n"
    "float resolveA(int id, vec4 prev, vec4 c0, vec4 c1, vec4 c2, vec4 konst, vec4 tex, vec4 rast, int stage) {\n"
    "    if (id == 0) return prev.a;\n"
    "    if (id == 1) return c0.a;\n"
    "    if (id == 2) return c1.a;\n"
    "    if (id == 3) return c2.a;\n"
    "    if (id == 4) return tex.a;\n"
    "    if (id == 5) return rast.a;\n"
    "    if (id == 6) return konst.a;\n"
    "    return 0.0;\n"
    "}\n"
    "void main() {\n"
    "    vec4 rast0;\n"
    "    vec4 base0 = vec4(uUseMaterialRgb ? uMaterialColor.rgb : vColor.rgb,\n"
    "                      uUseMaterialAlpha ? uMaterialColor.a : vColor.a);\n"
    "    if (vLit0.x < -0.5) {\n"
    "        // Disabling channel lighting does not force vertex color: GX\n"
    "        // still selects the raster value with matSrc.\n"
    "        rast0 = base0;\n"
    "    } else {\n"
    "        rast0 = vec4(clamp(base0.rgb * vLit0, 0.0, 1.0), base0.a);\n"
    "    }\n"
    "    vec4 base1 = vec4(uUseMaterialRgb1 ? uMaterialColor1.rgb : vColor.rgb, base0.a);\n"
    "    vec4 rast1 = vLit1.x < -0.5\n"
    "        ? base1\n"
    "        : vec4(clamp(base1.rgb * vLit1, 0.0, 1.0), base1.a);\n"
    "    if (uFastPath != 0) {\n"
    "        vec4 fastRast = (uTevChan[0].x == 1) ? rast1 : rast0;\n"
    "        vec4 fastCol;\n"
    "        if (uFastPath == 1) fastCol = texture(uTex0, vTexCoord0);\n"
    "        else if (uFastPath == 2) fastCol = fastRast;\n"
    "        else fastCol = texture(uTex0, vTexCoord0) * fastRast;\n"
    "        bool fastTest0 = alphaCompare(fastCol.a, uAlphaRef0, uAlphaComp0);\n"
    "        bool fastTest1 = alphaCompare(fastCol.a, uAlphaRef1, uAlphaComp1);\n"
    "        bool fastPass = (uAlphaOp == 0) ? (fastTest0 && fastTest1) :\n"
    "                        (uAlphaOp == 1) ? (fastTest0 || fastTest1) :\n"
    "                        (uAlphaOp == 2) ? (fastTest0 != fastTest1) : (fastTest0 == fastTest1);\n"
    "        if (!fastPass) discard;\n"
    "        fragColor = fastCol;\n"
    "        return;\n"
    "    }\n"
    // TEVPREV is a real programmable TEV register (BP E0/E1), not an
    // implicit zero value at the beginning of every primitive.
    "    vec4 prev = uTevPrev;\n"
    "    vec4 c0 = uTevReg0;\n"
    "    vec4 c1 = uTevReg1;\n"
    "    vec4 c2 = uTevReg2;\n"
    "    vec4 tex = vec4(1.0);\n"
    "    for (int i = 0; i < uNumStages; i++) {\n"
    "        vec4 konst = uTevKonst[i];\n"
    "        int tIdx = uTevTexInfo[i].x;\n"
    "        int tcIdx = uTevTexInfo[i].y;\n"
    "        vec4 rawTex = (tIdx >= 0) ? resolveTex(tIdx, tcIdx) : vec4(1.0);\n"
    "        tex = applySwap(rawTex, uTevSwapSel[i].y);\n"
    "        vec4 rawRast = (uTevChan[i].x < 0) ? vec4(0.0) : (uTevChan[i].x == 1) ? rast1 : rast0;\n"
    "        vec4 rast = applySwap(rawRast, uTevSwapSel[i].x);\n"
    "        vec4 a = resolveC(uTevCSel[i].x, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        vec4 b = resolveC(uTevCSel[i].y, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        vec4 cc = resolveC(uTevCSel[i].z, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        vec4 d = resolveC(uTevCSel[i].w, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        float bias = (uTevCOps[i].y == 1) ? 0.5 : (uTevCOps[i].y == 2) ? -0.5 : 0.0;\n"
    "        float scl = (uTevCOps[i].z == 1) ? 2.0 : (uTevCOps[i].z == 2) ? 4.0 : (uTevCOps[i].z == 3) ? 0.5 : 1.0;\n"
    "        vec3 cResult;\n"
    "        vec3 cMix = a.rgb * (vec3(1.0) - cc.rgb) + b.rgb * cc.rgb;\n"
    "        int colorOp = uTevCOps[i].x & 0xff;\n"
    "        if (colorOp >= 8) cResult = compareTevColor(colorOp, a.rgb, b.rgb, cc.rgb, d.rgb);\n"
    "        else if (colorOp == 0) cResult = d.rgb + cMix;\n"
    "        else cResult = d.rgb - cMix;\n"
    "        if (colorOp < 8) cResult = (cResult + bias) * scl;\n"
    "        cResult = ((uTevCOps[i].x & 0x100) != 0) ? clamp(cResult, 0.0, 1.0) : clamp(cResult, -4.0, 4.0);\n"
    "        float al = resolveA(uTevASel[i].x, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        float bl = resolveA(uTevASel[i].y, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        float ac = resolveA(uTevASel[i].z, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        float dl = resolveA(uTevASel[i].w, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        float abias = (uTevAOps[i].y == 1) ? 0.5 : (uTevAOps[i].y == 2) ? -0.5 : 0.0;\n"
    "        float ascl = (uTevAOps[i].z == 1) ? 2.0 : (uTevAOps[i].z == 2) ? 4.0 : (uTevAOps[i].z == 3) ? 0.5 : 1.0;\n"
    "        float aResult;\n"
    "        float aMix = al * (1.0 - ac) + bl * ac;\n"
    "        int alphaOp = uTevAOps[i].x & 0xff;\n"
    "        if (alphaOp >= 14) {\n"
    "            float qa = floor(clamp(al, 0.0, 1.0) * 255.0 + 0.5);\n"
    "            float qb = floor(clamp(bl, 0.0, 1.0) * 255.0 + 0.5);\n"
    "            bool pass = ((alphaOp & 1) != 0) ? (qa == qb) : (qa > qb);\n"
    "            aResult = dl + (pass ? ac : 0.0);\n"
    "        } else if (alphaOp == 0) aResult = dl + aMix;\n"
    "        else aResult = dl - aMix;\n"
    "        if (alphaOp < 8) aResult = (aResult + abias) * ascl;\n"
    "        aResult = ((uTevAOps[i].x & 0x100) != 0) ? clamp(aResult, 0.0, 1.0) : clamp(aResult, -4.0, 4.0);\n"
    "        int cReg = uTevCOps[i].w;\n"
    // GX routes the color and alpha combiner outputs independently. Writing a
    // complete vec4 here corrupts the destination register's alpha whenever
    // colorOutReg and alphaOutReg differ, which breaks later A0/A1/A2 inputs.
    "        if (cReg == 0) prev.rgb = cResult;\n"
    "        else if (cReg == 1) c0.rgb = cResult;\n"
    "        else if (cReg == 2) c1.rgb = cResult;\n"
    "        else c2.rgb = cResult;\n"
    "        int aReg = uTevAOps[i].w;\n"
    "        if (aReg == 0) prev.a = aResult;\n"
    "        else if (aReg == 1) c0.a = aResult;\n"
    "        else if (aReg == 2) c1.a = aResult;\n"
    "        else c2.a = aResult;\n"
    "    }\n"
    "    vec4 col = prev;\n"
    "    bool test0 = alphaCompare(col.a, uAlphaRef0, uAlphaComp0);\n"
    "    bool test1 = alphaCompare(col.a, uAlphaRef1, uAlphaComp1);\n"
    "    bool pass = (uAlphaOp == 0) ? (test0 && test1) :\n"
    "                (uAlphaOp == 1) ? (test0 || test1) :\n"
    "                (uAlphaOp == 2) ? (test0 != test1) : (test0 == test1);\n"
    "    if (!pass) discard;\n"
    "    fragColor = col;\n"
    "}\n";

void pc_gfx_set_render_scale(float scale) {
    sRenderScale = std::clamp(scale, 0.25f, 4.0f);
    sRenderScaleSet = true;
    printf("[PC Port] Internal render scale set to %.2f\n", sRenderScale);
}

float pc_gfx_get_render_scale(void) {
    return sRenderScale;
}

static float calculate_aspect_ratio(int mode, float windowAspect) {
    switch (mode) {
        case ASPECT_4_3:   return 4.0f / 3.0f;
        case ASPECT_16_10: return 16.0f / 10.0f;
        case ASPECT_16_9:  return 16.0f / 9.0f;
        case ASPECT_21_9:  return 21.0f / 9.0f;
        case ASPECT_AUTO:
        default:
            return windowAspect;
    }
}

void pc_gfx_set_aspect_ratio_mode(int mode) {
    sAspectRatioMode = std::clamp(mode, 0, (int)ASPECT_COUNT - 1);
}

int pc_gfx_get_aspect_ratio_mode(void) {
    return sAspectRatioMode;
}

float pc_gfx_get_current_aspect_ratio(void) {
    return sCurrentAspectRatio;
}

static void calculate_output_area(int drawableWidth, int drawableHeight,
                                  float renderedAspect, GLint& outX, GLint& outY,
                                  GLint& outWidth, GLint& outHeight) {
    const float windowAspect = float(drawableWidth) / float(drawableHeight);
    if (renderedAspect >= windowAspect) {
        outWidth = drawableWidth;
        outHeight = GLint(lroundf(float(drawableWidth) / renderedAspect));
        outX = 0;
        outY = (drawableHeight - outHeight) / 2;
    } else {
        outHeight = drawableHeight;
        outWidth = GLint(lroundf(float(drawableHeight) * renderedAspect));
        outX = (drawableWidth - outWidth) / 2;
        outY = 0;
    }
}

// Attribute indices are forced to fixed slots for every program so the one
// vertex array object stays valid no matter which specialised program is
// bound. Must be called before linking.

// Startup GL errors are latched and only surface at the first draw, which says
// nothing about where they came from. These checkpoints drain and name the
// stage that raised one, so a stray error is attributable instead of ambient.
static void gl_error_checkpoint(const char* stage) {
    // Read the environment once. Two of these sit in the per-draw path, so a
    // getenv per call meant well over a thousand environment scans per frame.
    static const bool enabled = std::getenv("PIKMIN_GL_CHECK") != nullptr;
    if (!enabled) return;
    for (;;) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) break;
        const char* name = "UNKNOWN";
        switch (error) {
            case GL_INVALID_ENUM: name = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE: name = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: name = "GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY: name = "GL_OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: name = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        printf("[PC GX check] %s raised %s\n", stage, name);
    }
}

static void bind_fixed_attrib_locations(GLuint program) {
    if (!glBindAttribLocation_ptr) return;
    glBindAttribLocation_ptr(program, kAttrPosIndex, "aPos");
    glBindAttribLocation_ptr(program, kAttrColorIndex, "aColor");
    glBindAttribLocation_ptr(program, kAttrNormalIndex, "aNormal");
    for (int i = 0; i < kVertexTexCoordCount; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "aTexCoord%d", i);
        glBindAttribLocation_ptr(program, kAttrTexCoord0Index + i, name);
    }
}

// Resolves every uniform this backend can feed. A specialised program bakes
// most of them away; those come back as -1 and the matching glUniform call is
// ignored by the driver, which is what lets one upload path serve both the
// ubershader and the generated programs.
static void query_program_locations(GLuint program, ProgramLocations& out) {
    out = ProgramLocations();
    invalidate_uniform_cache();
    out.projMtx = glGetUniformLocation_ptr(program, "uProjMtx");
    out.posMtx = glGetUniformLocation_ptr(program, "uPosMtx");
    out.materialColor = glGetUniformLocation_ptr(program, "uMaterialColor");
    out.useMaterialRgb = glGetUniformLocation_ptr(program, "uUseMaterialRgb");
    out.useMaterialAlpha = glGetUniformLocation_ptr(program, "uUseMaterialAlpha");
    out.alphaComp0 = glGetUniformLocation_ptr(program, "uAlphaComp0");
    out.alphaComp1 = glGetUniformLocation_ptr(program, "uAlphaComp1");
    out.alphaOp = glGetUniformLocation_ptr(program, "uAlphaOp");
    out.alphaRef0 = glGetUniformLocation_ptr(program, "uAlphaRef0");
    out.alphaRef1 = glGetUniformLocation_ptr(program, "uAlphaRef1");
    out.numStages = glGetUniformLocation_ptr(program, "uNumStages");
    out.fastPath = glGetUniformLocation_ptr(program, "uFastPath");
    out.tevPrev = glGetUniformLocation_ptr(program, "uTevPrev");
    out.tevReg0 = glGetUniformLocation_ptr(program, "uTevReg0");
    out.tevReg1 = glGetUniformLocation_ptr(program, "uTevReg1");
    out.tevReg2 = glGetUniformLocation_ptr(program, "uTevReg2");
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uKonst%d", i);
        out.konst[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < GX_MAXTEVSTAGE; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "uTevCSel[%d]", i);
        out.tevCSel[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevASel[%d]", i);
        out.tevASel[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevCOps[%d]", i);
        out.tevCOps[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevAOps[%d]", i);
        out.tevAOps[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevTexInfo[%d]", i);
        out.tevTexInfo[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevKonst[%d]", i);
        out.tevKonst[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uTevSwapTable[%d]", i);
        out.tevSwapTable[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < GX_MAXTEVSTAGE; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uTevSwapSel[%d]", i);
        out.tevSwapSel[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < 8; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "uTex%d", i);
        // Query only. The sampler values are written at the end of this
        // function, once the program is actually bound: writing them here sent
        // the new program's locations to whichever program was still current,
        // which GL either rejected or, when that location happened to hold an
        // integer uniform, silently overwrote with a sampler index.
        out.tex[i] = glGetUniformLocation_ptr(program, buf);
    }

    for (int i = 0; i < 8; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "aTexCoord%d", i);
        snprintf(name, sizeof(name), "uTcMode[%d]", i);
        out.tcMode[i] = glGetUniformLocation_ptr(program, name);
        snprintf(name, sizeof(name), "uTcMtx[%d]", i);
        out.tcMtx[i] = glGetUniformLocation_ptr(program, name);
    }
    out.nrmMtx = glGetUniformLocation_ptr(program, "uNrmMtx");
    out.numLights = glGetUniformLocation_ptr(program, "uNumLights");
    out.ambColor = glGetUniformLocation_ptr(program, "uAmbColor");
    out.chan0En = glGetUniformLocation_ptr(program, "uChan0En");
    out.chan1En = glGetUniformLocation_ptr(program, "uChan1En");
    out.chan0AttnFn = glGetUniformLocation_ptr(program, "uChan0AttnFn");
    out.chan1AttnFn = glGetUniformLocation_ptr(program, "uChan1AttnFn");
    out.numLights1 = glGetUniformLocation_ptr(program, "uNumLights1");
    out.ambColor1 = glGetUniformLocation_ptr(program, "uAmbColor1");
    out.materialColor1 = glGetUniformLocation_ptr(program, "uMaterialColor1");
    out.useMaterialRgb1 = glGetUniformLocation_ptr(program, "uUseMaterialRgb1");
    out.specHalf1 = glGetUniformLocation_ptr(program, "uSpecHalf1");
    out.specAttn1 = glGetUniformLocation_ptr(program, "uSpecAttn1");
    out.fogParams = glGetUniformLocation_ptr(program, "uFogParams");
    out.fogColour = glGetUniformLocation_ptr(program, "uFogColour");
    for (int i = 0; i < 16; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uTevChan[%d]", i);
        out.tevChan[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uLightPos[%d]", i);
        out.lightPos[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightColor[%d]", i);
        out.lightColor[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightK[%d]", i);
        out.lightK[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightPos1[%d]", i);
        out.lightPos1[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightColor1[%d]", i);
        out.lightColor1[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightK1[%d]", i);
        out.lightK1[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uLightPos[%d]", i);
        out.lightPos[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightColor[%d]", i);
        out.lightColor[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightK[%d]", i);
        out.lightK[i] = glGetUniformLocation_ptr(program, buf);
    }
    // Sampler bindings are program state, so the program has to be current
    // before they are written. Leaving it out sent every glUniform1i to
    // whichever program happened to be bound: GL rejected them, the samplers
    // kept their default of zero, and every multi-texture stage sampled unit 0.
    glUseProgram_ptr(program);
    invalidate_uniform_cache();
    for (int i = 0; i < 8; i++) {
        if (out.tex[i] >= 0) glUniform1i_ptr(out.tex[i], i);
    }
}

void pc_gfx_init(void) {
    load_gl_functions();

    const GLubyte* glVersion = glGetString(GL_VERSION);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
    gl_error_checkpoint("context creation");
    printf("[PC Port] OpenGL context: %s; GLSL: %s\n",
           glVersion ? reinterpret_cast<const char*>(glVersion) : "unknown",
           glslVersion ? reinterpret_cast<const char*>(glslVersion) : "unknown");
    if (const char* value = std::getenv("PIKMIN_TEV_SPECIALIZE")) {
        sSpecialiseShaders = value[0] != '0';
    }
    if (const char* value = std::getenv("PIKMIN_TEV_MAX_STAGES")) {
        sSpecialiseMaxStages = std::clamp(atoi(value), 0, 16);
    }
    printf("[PC Port] TEV specialisation: %s, up to %d stage(s); the rest use the ubershader.\n",
           sSpecialiseShaders ? "on" : "off", sSpecialiseMaxStages);
    if (const char* value = std::getenv("PIKMIN_RENDER_SCALE")) {
        if (!sRenderScaleSet) {
            sRenderScale = std::clamp(strtof(value, nullptr), 0.25f, 4.0f);
        }
    }

    // Default identity matrices
    for (int i = 0; i < 16; i++) {
        sProjMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    for (int m = 0; m < 64; m++) {
        for (int i = 0; i < 16; i++) {
            sPosMatrix[m][i] = (i % 5 == 0) ? 1.0f : 0.0f;
            sTexMatrices[m][i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
        sTexMtxLoaded[m] = false;
        for (int c = 0; c < 8; c++) {
            sTexCoordGen[c].active = false;
            sTexCoordGen[c].type = 1;
            sTexCoordGen[c].src = 4;
            sTexCoordGen[c].mtxIdx = 0;
        }
    }

    if (!glCreateShader_ptr) {
        printf("[PC Port Error] OpenGL extension loader failed!\n");
        return;
    }

    // Compile Shaders
    GLuint vs = glCreateShader_ptr(GL_VERTEX_SHADER);
    glShaderSource_ptr(vs, 1, &vShaderSrc, NULL);
    glCompileShader_ptr(vs);

    GLuint fs = glCreateShader_ptr(GL_FRAGMENT_SHADER);
    glShaderSource_ptr(fs, 1, &fShaderSrc, NULL);
    glCompileShader_ptr(fs);

    // Report shader compile errors
    auto checkShader = [&](GLuint shader, const char* name) {
        GLint status = 0;
        if (glGetShaderiv_ptr) glGetShaderiv_ptr(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            char log[4096] = { 0 };
            if (glGetShaderInfoLog_ptr) glGetShaderInfoLog_ptr(shader, sizeof(log), NULL, log);
            printf("[PC Port Shader Error] %s failed to compile:\n%s\n", name, log);
            return false;
        }
        return true;
    };
    bool vsOk = checkShader(vs, "vertex shader");
    bool fsOk = checkShader(fs, "fragment shader");
    if (!vsOk || !fsOk) {
        printf("[PC Port Error] Shader compilation failed - rendering will be broken!\n");
    }

    sShaderProgram = glCreateProgram_ptr();
    glAttachShader_ptr(sShaderProgram, vs);
    glAttachShader_ptr(sShaderProgram, fs);
    bind_fixed_attrib_locations(sShaderProgram);
    glLinkProgram_ptr(sShaderProgram);

    {
        GLint status = 0;
        if (glGetProgramiv_ptr) glGetProgramiv_ptr(sShaderProgram, GL_LINK_STATUS, &status);
        if (status != GL_TRUE) {
            char log[4096] = { 0 };
            if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(sShaderProgram, sizeof(log), NULL, log);
            printf("[PC Port Shader Error] Program link failed:\n%s\n", log);
        } else {
            printf("[PC Port] TEV evaluator shaders compiled and linked OK\n");
        }
    }

    gl_error_checkpoint("ubershader link");
    query_program_locations(sShaderProgram, sLoc);
    gl_error_checkpoint("ubershader uniform locations");
    sUberLocations = sLoc;
    sCurrentProgram = sShaderProgram;
    // The vertex shader is identical for every specialised program, so it is
    // compiled once here and reused when linking them.
    sSharedVertexShader = vs;

    gl_error_checkpoint("shader setup");
    glGenBuffers_ptr(1, &sVBO);
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    glBufferData_ptr(GL_ARRAY_BUFFER, sVboCapacity, nullptr, GL_STREAM_DRAW);

    if (glGenFramebuffers_ptr && glBindFramebuffer_ptr && glFramebufferTexture2D_ptr
        && glGenRenderbuffers_ptr && glBindRenderbuffer_ptr && glRenderbufferStorage_ptr
        && glFramebufferRenderbuffer_ptr && glCheckFramebufferStatus_ptr && glBlitFramebuffer_ptr) {
        glGenFramebuffers_ptr(1, &sNativeFramebuffer);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        glGenTextures(1, &sNativeColorTexture);
        glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sRenderWidth, sRenderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   sNativeColorTexture, 0);
        // Depth as a texture rather than a renderbuffer. Nothing samples it
        // yet, but every screen-space effect worth having -- ambient occlusion,
        // depth-based fog, depth of field, outlines -- needs to read it, and a
        // renderbuffer cannot be read. A renderbuffer is still the fallback:
        // the port keeps a GL 2.1 context path for old drivers, and there
        // packed depth-stencil textures are an extension rather than a
        // guarantee. Losing the effects is acceptable; losing the game is not.
        sDepthIsTexture = false;
        glGenTextures(1, &sNativeDepthTexture);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, sRenderWidth, sRenderHeight,
                     0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        // Depth must not be filtered or wrapped: a sample has to be the value
        // written at that pixel, not a blend of its neighbours.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Sampled as a plain value, not as a shadow comparison.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_TEXTURE_2D, sNativeDepthTexture, 0);
        if (glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            sDepthIsTexture = true;
        } else {
            glDeleteTextures(1, &sNativeDepthTexture);
            sNativeDepthTexture = 0;
            glGenRenderbuffers_ptr(1, &sNativeDepthStencil);
            glBindRenderbuffer_ptr(GL_RENDERBUFFER, sNativeDepthStencil);
            glRenderbufferStorage_ptr(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, sRenderWidth, sRenderHeight);
            glFramebufferRenderbuffer_ptr(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, sNativeDepthStencil);
        }
        sNativeFramebufferReady = glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebufferReady ? sNativeFramebuffer : 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        sBoundTextures[0] = 0;
        printf("[PC Port] Scalable render target: %s (scale %.2f), depth: %s\n",
               sNativeFramebufferReady ? "active" : "unavailable", sRenderScale,
               sDepthIsTexture ? "texture (readable)" : "renderbuffer (not readable)");
    }
    glEnableVertexAttribArray_ptr(sAttrPos);
    glVertexAttribPointer_ptr(sAttrPos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    if (sAttrNormal >= 0) {
        glEnableVertexAttribArray_ptr(sAttrNormal);
        glVertexAttribPointer_ptr(sAttrNormal, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nx));
    }
    glEnableVertexAttribArray_ptr(sAttrColor);
    glVertexAttribPointer_ptr(sAttrColor, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    for (int i = 0; i < 8; ++i) {
        if (sAttrTexCoord[i] >= 0) {
            glEnableVertexAttribArray_ptr(sAttrTexCoord[i]);
            const size_t offset = offsetof(Vertex, tex) + size_t(i) * 2 * sizeof(float);
            glVertexAttribPointer_ptr(sAttrTexCoord[i], 2, GL_FLOAT, GL_FALSE,
                                      sizeof(Vertex), reinterpret_cast<void*>(offset));
        }
    }

    gl_error_checkpoint("vertex array setup");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // GX defines clockwise screen-space triangles as front-facing, whereas
    // OpenGL defaults to counter-clockwise.  Matching GX here keeps culling
    // from rejecting every front face in 3D scenes and cinematics.
    glFrontFace(GL_CW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    printf("[PC Port] OpenGL Backend & GLSL Shaders initialized successfully\n");
}

void pc_gfx_perf_scope_begin(const char* name) {
    sPerfCurrentScope = -1;
    if (!sPerfStatsEnabled || !name) return;
    for (int i = 0; i < 8; ++i) {
        if (sPerfScopes[i].name == name || (sPerfScopes[i].name && strcmp(sPerfScopes[i].name, name) == 0)) {
            sPerfCurrentScope = i;
            return;
        }
        if (!sPerfScopes[i].name) {
            sPerfScopes[i].name = name;
            sPerfCurrentScope = i;
            return;
        }
    }
}

void pc_gfx_perf_scope_end(void) {
    sPerfCurrentScope = -1;
}

static bool perf_gpu_queries_supported() {
#ifdef GL_TIME_ELAPSED
    return glGenQueries_ptr && glBeginQuery_ptr && glEndQuery_ptr
        && glGetQueryObjectiv_ptr && glGetQueryObjectui64v_ptr;
#else
    return false;
#endif
}

static void perf_gpu_queries_init() {
    if (sPerfGpuQueriesReady || !perf_gpu_queries_supported()) return;
    GLuint ids[PC_GPU_QUERY_RING_SIZE * 2] = {};
    glGenQueries_ptr(GLsizei(PC_GPU_QUERY_RING_SIZE * 2), ids);
    for (unsigned i = 0; i < PC_GPU_QUERY_RING_SIZE; ++i) {
        sPerfGpuQueries[i].scene = ids[i * 2];
        sPerfGpuQueries[i].blit = ids[i * 2 + 1];
    }
    sPerfGpuQueriesReady = ids[0] != 0;
    if (sPerfGpuQueriesReady)
        fprintf(stderr, "[PERF GPU] asynchronous timer queries enabled\n");
}

static void perf_gpu_queries_poll() {
#ifdef GL_TIME_ELAPSED
    if (!sPerfGpuQueriesReady) return;
    // Never wait for the GPU. Only consume query pairs whose two results are
    // already available, leaving busy slots pending in the ring.
    for (PerfGpuQuery& query : sPerfGpuQueries) {
        if (!query.pending) continue;
        GLint sceneAvailable = GL_FALSE;
        GLint blitAvailable = GL_FALSE;
        glGetQueryObjectiv_ptr(query.scene, GL_QUERY_RESULT_AVAILABLE, &sceneAvailable);
        glGetQueryObjectiv_ptr(query.blit, GL_QUERY_RESULT_AVAILABLE, &blitAvailable);
        if (sceneAvailable != GL_TRUE || blitAvailable != GL_TRUE) continue;
        GLuint64 sceneNs = 0;
        GLuint64 blitNs = 0;
        glGetQueryObjectui64v_ptr(query.scene, GL_QUERY_RESULT, &sceneNs);
        glGetQueryObjectui64v_ptr(query.blit, GL_QUERY_RESULT, &blitNs);
        sPerfGpuSceneNs += uint64_t(sceneNs);
        sPerfGpuBlitNs += uint64_t(blitNs);
        ++sPerfGpuSamples;
        query.pending = false;
    }
#endif
}

static void perf_gpu_scene_begin() {
#ifdef GL_TIME_ELAPSED
    if (!sPerfStatsEnabled) return;
    perf_gpu_queries_init();
    perf_gpu_queries_poll();
    if (!sPerfGpuQueriesReady || sPerfGpuSceneActive) return;
    PerfGpuQuery& query = sPerfGpuQueries[sPerfGpuQueryWrite];
    if (query.pending) return;
    glBeginQuery_ptr(GL_TIME_ELAPSED, query.scene);
    sPerfGpuSceneActive = true;
#endif
}

void pc_gfx_begin_frame(void) {
    using Clock = std::chrono::steady_clock;
    static const bool perfStats = std::getenv("PIKMIN_PERF_STATS") != nullptr;
    sPerfStatsEnabled = perfStats;
    if (sPerfStatsEnabled) {
        perf_gpu_queries_init();
        perf_gpu_queries_poll();
    }
    static Clock::time_point lastFrame;
    static double elapsedMs = 0.0;
    static unsigned frames = 0;
    const Clock::time_point now = Clock::now();
    if (lastFrame.time_since_epoch().count() != 0) {
        elapsedMs += std::chrono::duration<double, std::milli>(now - lastFrame).count();
        ++frames;
    }
    lastFrame = now;
    if (perfStats && frames >= 120) {
        fprintf(stderr,
                "[PERF] %.1f fps %.2f ms | %.0f draws (%.0f source, %.0f fast) %.0f verts %.0f DL (%.2f MiB) %.2f tex uploads/frame\n",
                frames * 1000.0 / elapsedMs, elapsedMs / frames,
                double(sPerfDraws) / frames, double(sPerfSourcePrimitives) / frames,
                double(sPerfFastDraws) / frames,
                double(sPerfVertices) / frames,
                double(sPerfDisplayLists) / frames,
                double(sPerfDisplayListBytes) / frames / (1024.0 * 1024.0),
                double(sPerfTextureUploads) / frames);
        fprintf(stderr, "[PERF TEV] stages");
        for (int i = 0; i <= GX_MAXTEVSTAGE; ++i) {
            if (sPerfTevStageDraws[i])
                fprintf(stderr, " %d=%.0f", i, double(sPerfTevStageDraws[i]) / frames);
        }
        fprintf(stderr, "\n");
        if (sPerfGpuSamples != 0) {
            fprintf(stderr, "[PERF GPU] scene %.2f ms blit %.2f ms (%llu async samples)\n",
                    double(sPerfGpuSceneNs) / double(sPerfGpuSamples) / 1000000.0,
                    double(sPerfGpuBlitNs) / double(sPerfGpuSamples) / 1000000.0,
                    static_cast<unsigned long long>(sPerfGpuSamples));
        } else if (perf_gpu_queries_supported()) {
            fprintf(stderr, "[PERF GPU] results pending; CPU was not stalled\n");
        } else {
            fprintf(stderr, "[PERF GPU] timer queries unavailable on this OpenGL driver\n");
        }
        for (const PerfScope& scope : sPerfScopes) {
            if (scope.name && scope.draws) {
                fprintf(stderr, "[PERF ACTOR] %s %.0f draws %.0f verts\n", scope.name,
                        double(scope.draws) / frames, double(scope.vertices) / frames);
            }
        }
        fprintf(stderr, "[PERF FAST]");
        for (int i = 1; i < 4; ++i) {
            if (sPerfFastPathDraws[i]) {
                fprintf(stderr, " path%d=%.0f draws/%.2f verts", i,
                        double(sPerfFastPathDraws[i]) / frames,
                        double(sPerfFastPathVertices[i]) / double(sPerfFastPathDraws[i]));
            }
        }
        fprintf(stderr, " | prim tri=%.0f strip=%.0f fan=%.0f lines=%.0f line-strip=%.0f points=%.0f quads=%.0f\n",
                double(sPerfPrimitiveDraws[0]) / frames, double(sPerfPrimitiveDraws[1]) / frames,
                double(sPerfPrimitiveDraws[2]) / frames, double(sPerfPrimitiveDraws[3]) / frames,
                double(sPerfPrimitiveDraws[4]) / frames, double(sPerfPrimitiveDraws[5]) / frames,
                double(sPerfPrimitiveDraws[6]) / frames);
        for (int rank = 0; rank < 4; ++rank) {
            int best = -1;
            for (int i = 0; i < 128; ++i) {
                if (sPerfTevPatterns[i].count && (best < 0 || sPerfTevPatterns[i].count > sPerfTevPatterns[best].count))
                    best = i;
            }
            if (best < 0) break;
            const uint64_t key = sPerfTevPatterns[best].key;
            fprintf(stderr,
                    "[PERF TEV1] %.0f draws C=%u,%u,%u,%u A=%u,%u,%u,%u tex=%d tc=%d ras=%d op=%u/%u flags=0x%llx\n",
                    double(sPerfTevPatterns[best].count) / frames,
                    unsigned((key >> 0) & 15), unsigned((key >> 4) & 15),
                    unsigned((key >> 8) & 15), unsigned((key >> 12) & 15),
                    unsigned((key >> 16) & 7), unsigned((key >> 19) & 7),
                    unsigned((key >> 22) & 7), unsigned((key >> 25) & 7),
                    int((key >> 28) & 15) - 1, int((key >> 32) & 15) - 1,
                    int((key >> 36) & 3) - 1, unsigned((key >> 38) & 15),
                    unsigned((key >> 42) & 15),
                    static_cast<unsigned long long>(key >> 46));
            sPerfTevPatterns[best].count = 0;
        }
        for (int rank = 0; rank < 4; ++rank) {
            int best = -1;
            for (int i = 0; i < 128; ++i) {
                if (sPerfTevMultiPatterns[i].count
                    && (best < 0 || sPerfTevMultiPatterns[i].count > sPerfTevMultiPatterns[best].count))
                    best = i;
            }
            if (best < 0) break;
            const PerfTevMultiPattern& pattern = sPerfTevMultiPatterns[best];
            const uint64_t key = pattern.key;
            fprintf(stderr,
                    "[PERF TEVN] %.0f draws stages=%u final C=%u,%u,%u,%u A=%u,%u,%u,%u tex=%d tc=%d ras=%d op=%u/%u flags=0x%llx\n",
                    double(pattern.count) / frames, unsigned(pattern.stages),
                    unsigned((key >> 0) & 15), unsigned((key >> 4) & 15),
                    unsigned((key >> 8) & 15), unsigned((key >> 12) & 15),
                    unsigned((key >> 16) & 7), unsigned((key >> 19) & 7),
                    unsigned((key >> 22) & 7), unsigned((key >> 25) & 7),
                    int((key >> 28) & 15) - 1, int((key >> 32) & 15) - 1,
                    int((key >> 36) & 3) - 1, unsigned((key >> 38) & 15),
                    unsigned((key >> 42) & 15),
                    static_cast<unsigned long long>(key >> 46));
            sPerfTevMultiPatterns[best].count = 0;
        }
        frames = 0; elapsedMs = 0.0;
        sPerfDraws = sPerfFastDraws = sPerfVertices = sPerfDisplayLists = 0;
        sPerfSourcePrimitives = 0;
        sPerfDisplayListBytes = sPerfTextureUploads = 0;
        sPerfGpuSceneNs = sPerfGpuBlitNs = sPerfGpuSamples = 0;
        memset(sPerfTevStageDraws, 0, sizeof(sPerfTevStageDraws));
        memset(sPerfTevPatterns, 0, sizeof(sPerfTevPatterns));
        memset(sPerfTevMultiPatterns, 0, sizeof(sPerfTevMultiPatterns));
        memset(sPerfFastPathDraws, 0, sizeof(sPerfFastPathDraws));
        memset(sPerfFastPathVertices, 0, sizeof(sPerfFastPathVertices));
        memset(sPerfPrimitiveDraws, 0, sizeof(sPerfPrimitiveDraws));
        for (PerfScope& scope : sPerfScopes) {
            scope.draws = 0;
            scope.vertices = 0;
        }
    }

    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (window) SDL_GL_GetDrawableSize(window, &sDrawableWidth, &sDrawableHeight);
    if (sNativeFramebufferReady && sDrawableWidth > 0 && sDrawableHeight > 0) {
        // Calculate aspect ratio for this frame
        float windowAspect = float(sDrawableWidth) / float(sDrawableHeight);
        sCurrentAspectRatio = calculate_aspect_ratio(sAspectRatioMode, windowAspect);
        
        // Scale the exact output viewport instead of first quantising the
        // aspect ratio through a 480-line base. The old calculation turned a
        // native 1920x1080 target into 1919x1080 and forced needless filtering.
        GLint outX, outY, outWidth, outHeight;
        calculate_output_area(sDrawableWidth, sDrawableHeight, sCurrentAspectRatio,
                              outX, outY, outWidth, outHeight);
        const int wantedWidth = std::max(160, int(lroundf(float(outWidth) * sRenderScale)));
        const int wantedHeight = std::max(120, int(lroundf(float(outHeight) * sRenderScale)));
        if (wantedWidth != sRenderWidth || wantedHeight != sRenderHeight) {
            sRenderWidth = wantedWidth;
            sRenderHeight = wantedHeight;
            glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sRenderWidth, sRenderHeight,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            if (sDepthIsTexture) {
                glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, sRenderWidth, sRenderHeight,
                             0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            } else {
                glBindRenderbuffer_ptr(GL_RENDERBUFFER, sNativeDepthStencil);
                glRenderbufferStorage_ptr(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                          sRenderWidth, sRenderHeight);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            sBoundTextures[0] = 0;
            printf("[PC Port] Internal render resolution: %dx%d (aspect %.3f)\n", sRenderWidth, sRenderHeight, sCurrentAspectRatio);
        }
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    }

    if (!sVBO || !glBufferData_ptr) return;
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    // Orphan once per frame. The driver can finish consuming the previous
    // storage asynchronously while the CPU streams the next frame contiguously.
    glBufferData_ptr(GL_ARRAY_BUFFER, sVboCapacity, nullptr, GL_STREAM_DRAW);
    sVboWriteOffset = 0;
    perf_gpu_scene_begin();
}

// ─── Post-processing ───
//
// Runs between the scene render and the blit to the window. The scene target
// is sampled into a second one of the same size, and the blit then reads from
// whichever holds the result.
//
// With nothing switched on this does nothing at all and the blit reads the
// scene directly, exactly as it did before: a fullscreen pass that only copies
// the frame is pure cost, and the port's reference GPU is a GTX 1050.

static PcPostEffects sPostEffects;
static PcPostEffects sPostCompiledFor;
static bool sPostCompiled = false;
static GLuint sPostFramebuffer = 0;
static GLuint sPostColorTexture = 0;
static GLuint sPostProgram = 0;
static GLuint sPostVAO = 0;
static int sPostWidth = 0, sPostHeight = 0;

// Bloom works at half resolution in two ping-pong targets: bright pass into
// the first, blur across into the second, blur down back into the first.
static GLuint sBloomFbo[2] = { 0, 0 };
static GLuint sBloomTex[2] = { 0, 0 };
static GLuint sBrightProgram = 0;
static GLuint sBlurProgram = 0;
static int sBloomWidth = 0, sBloomHeight = 0;

// Ambient occlusion shares the shape of the bloom chain: compute at half
// resolution, then blur the noise out with the same separable program.
static GLuint sAoFbo[2] = { 0, 0 };
static GLuint sAoTex[2] = { 0, 0 };
static GLuint sSsaoProgram = 0;
static GLuint sAoBlurProgram = 0;
static int sAoWidth = 0, sAoHeight = 0;

// The perspective projection's terms, kept for the post-process pass.
//
// Captured here rather than read back at the end of the frame, because by then
// the last projection set is the HUD's orthographic one and unprojecting screen
// pixels with that would place the whole world in the wrong position.
static float sViewInvP00 = 1.0f, sViewInvP11 = 1.0f;
static float sViewNear = 1.0f, sViewFar = 15000.0f;

void pc_gfx_set_post_effects(const PcPostEffects& fx) { sPostEffects = fx; }

static GLuint post_compile(GLenum type, const char* src, const char* what)
{
    GLuint sh = glCreateShader_ptr(type);
    glShaderSource_ptr(sh, 1, &src, nullptr);
    glCompileShader_ptr(sh);
    GLint ok = 0;
    if (glGetShaderiv_ptr) glGetShaderiv_ptr(sh, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetShaderInfoLog_ptr) glGetShaderInfoLog_ptr(sh, sizeof(log), nullptr, log);
        printf("[PC Port] post-process %s failed to compile: %s\n", what, log);
        glDeleteShader_ptr(sh);
        return 0;
    }
    return sh;
}

// Builds the program for the current effect set. Returns false if anything at
// all is missing, and every caller treats that as "no post-processing" rather
// than as a reason to stop drawing.
static bool post_ensure_program()
{
    if (sPostCompiled && sPostCompiledFor == sPostEffects && sPostProgram) return true;

    if (sPostProgram) {
        glDeleteProgram_ptr(sPostProgram);
        sPostProgram = 0;
    }
    sPostCompiled = false;

    if (!glCreateShader_ptr || !glCreateProgram_ptr || !glGenVertexArrays_ptr) return false;

    const std::string frag = pc_post_build_fragment_shader(sPostEffects);
    GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "vertex shader");
    if (!vs) return false;
    GLuint fs = post_compile(GL_FRAGMENT_SHADER, frag.c_str(), "fragment shader");
    if (!fs) { glDeleteShader_ptr(vs); return false; }

    sPostProgram = glCreateProgram_ptr();
    glAttachShader_ptr(sPostProgram, vs);
    glAttachShader_ptr(sPostProgram, fs);
    glLinkProgram_ptr(sPostProgram);
    glDeleteShader_ptr(vs);
    glDeleteShader_ptr(fs);

    GLint linked = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(sPostProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(sPostProgram, sizeof(log), nullptr, log);
        printf("[PC Port] post-process program failed to link: %s\n", log);
        glDeleteProgram_ptr(sPostProgram);
        sPostProgram = 0;
        return false;
    }

    if (!sPostVAO) glGenVertexArrays_ptr(1, &sPostVAO);

    // The bloom chain's two programs do not depend on the settings, only on
    // whether bloom is wanted at all, so they are built once and kept.
    const bool wantsBlur = pc_post_bloom_active(sPostEffects);
    if (pc_post_bloom_active(sPostEffects) && !sBrightProgram && glUniform2f_ptr) {
        const std::string brightSrc = pc_post_build_brightpass_shader();
        const std::string blurSrc   = pc_post_build_blur_shader();
        GLuint bvs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "bloom vertex shader");
        GLuint bfs = bvs ? post_compile(GL_FRAGMENT_SHADER, brightSrc.c_str(), "bright pass") : 0;
        GLuint lvs = bfs ? post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "blur vertex shader") : 0;
        GLuint lfs = lvs ? post_compile(GL_FRAGMENT_SHADER, blurSrc.c_str(), "blur") : 0;
        if (bvs && bfs && lvs && lfs) {
            sBrightProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sBrightProgram, bvs);
            glAttachShader_ptr(sBrightProgram, bfs);
            glLinkProgram_ptr(sBrightProgram);
            sBlurProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sBlurProgram, lvs);
            glAttachShader_ptr(sBlurProgram, lfs);
            glLinkProgram_ptr(sBlurProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) {
                glGetProgramiv_ptr(sBrightProgram, GL_LINK_STATUS, &ok);
                if (ok == GL_TRUE) glGetProgramiv_ptr(sBlurProgram, GL_LINK_STATUS, &ok);
            }
            if (ok != GL_TRUE) {
                printf("[PC Port] bloom programs failed to link; bloom is off\n");
                glDeleteProgram_ptr(sBrightProgram); sBrightProgram = 0;
                glDeleteProgram_ptr(sBlurProgram); sBlurProgram = 0;
            }
        }
        if (bvs) glDeleteShader_ptr(bvs);
        if (bfs) glDeleteShader_ptr(bfs);
        if (lvs) glDeleteShader_ptr(lvs);
        if (lfs) glDeleteShader_ptr(lfs);
    }

    if (pc_post_ssao_active(sPostEffects) && !sSsaoProgram) {
        const std::string ssaoSrc = pc_post_build_ssao_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "ssao vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, ssaoSrc.c_str(), "ssao") : 0;
        if (vs && fs) {
            sSsaoProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sSsaoProgram, vs);
            glAttachShader_ptr(sSsaoProgram, fs);
            glLinkProgram_ptr(sSsaoProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sSsaoProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                printf("[PC Port] ssao program failed to link; ambient occlusion is off\n");
                glDeleteProgram_ptr(sSsaoProgram);
                sSsaoProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }

    if (pc_post_ssao_active(sPostEffects) && !sAoBlurProgram && glUniform2f_ptr) {
        const std::string blurSrc = pc_post_build_ao_blur_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "ao blur vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, blurSrc.c_str(), "ao blur") : 0;
        if (vs && fs) {
            sAoBlurProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sAoBlurProgram, vs);
            glAttachShader_ptr(sAoBlurProgram, fs);
            glLinkProgram_ptr(sAoBlurProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sAoBlurProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                printf("[PC Port] ao blur failed to link; occlusion will be noisy\n");
                glDeleteProgram_ptr(sAoBlurProgram);
                sAoBlurProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }

    // Bloom builds the blur alongside its bright pass. When occlusion is on
    // without it, the blur still has to exist -- it is what removes the noise
    // the rotated sample kernel deliberately introduces.
    if (wantsBlur && !sBlurProgram && glUniform2f_ptr) {
        const std::string blurSrc = pc_post_build_blur_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "blur vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, blurSrc.c_str(), "blur") : 0;
        if (vs && fs) {
            sBlurProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sBlurProgram, vs);
            glAttachShader_ptr(sBlurProgram, fs);
            glLinkProgram_ptr(sBlurProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sBlurProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                glDeleteProgram_ptr(sBlurProgram);
                sBlurProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }

    sPostCompiledFor = sPostEffects;
    sPostCompiled = true;
    // Once per change of settings, not per frame. Says which effects the pass
    // is actually running, which is otherwise only visible by looking hard at
    // the picture.
    printf("[PC Port] Post-process pass:%s%s%s%s\n",
           sPostEffects.fxaa ? " FXAA" : "",
           pc_post_ssao_active(sPostEffects) ? " SSAO" : "",
           pc_post_bloom_active(sPostEffects) ? " bloom" : "",
           sPostEffects.colourGrading ? " colour-grading" : "");
    fflush(stdout);
    return true;
}

// Keeps the destination the same size as the scene target, which the render
// scale changes at runtime.
static bool post_ensure_target()
{
    if (!glGenFramebuffers_ptr || !glBindFramebuffer_ptr || !glFramebufferTexture2D_ptr) return false;
    if (!sPostFramebuffer) {
        glGenFramebuffers_ptr(1, &sPostFramebuffer);
        glGenTextures(1, &sPostColorTexture);
        sPostWidth = sPostHeight = 0;
    }
    if (sPostWidth != sRenderWidth || sPostHeight != sRenderHeight) {
        glBindTexture(GL_TEXTURE_2D, sPostColorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sRenderWidth, sRenderHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sPostFramebuffer);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   sPostColorTexture, 0);
        const bool complete = glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindTexture(GL_TEXTURE_2D, 0);
        sBoundTextures[0] = 0;
        if (!complete) return false;
        sPostWidth = sRenderWidth;
        sPostHeight = sRenderHeight;
    }
    return true;
}

// Half-resolution ping-pong pair for the bloom chain.
static bool bloom_ensure_targets()
{
    const int w = std::max(1, sRenderWidth / 2);
    const int h = std::max(1, sRenderHeight / 2);
    if (!sBloomFbo[0]) {
        glGenFramebuffers_ptr(2, sBloomFbo);
        glGenTextures(2, sBloomTex);
        sBloomWidth = sBloomHeight = 0;
    }
    if (sBloomWidth == w && sBloomHeight == h) return true;
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, sBloomTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // Linear, and clamped: the blur reads between texels on purpose, and
        // wrapping would drag the far edge of the screen into the near one.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sBloomFbo[i]);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sBloomTex[i], 0);
        if (glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindTexture(GL_TEXTURE_2D, 0);
            sBoundTextures[0] = 0;
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    sBloomWidth = w;
    sBloomHeight = h;
    return true;
}

// Bright pass, blur across, blur down. Leaves the result in sBloomTex[0].
static bool bloom_build()
{
    if (!sBrightProgram || !sBlurProgram) return false;
    if (!bloom_ensure_targets()) return false;

    glViewport(0, 0, sBloomWidth, sBloomHeight);
    glBindVertexArray_ptr(sPostVAO);

    // Bright pass: scene -> [0]
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sBloomFbo[0]);
    glUseProgram_ptr(sBrightProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
    if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sBrightProgram, "uScene"), 0);
    if (glUniform1f_ptr) {
        glUniform1f_ptr(glGetUniformLocation_ptr(sBrightProgram, "uThreshold"),
                        sPostEffects.bloomThreshold);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Blur across: [0] -> [1], then down: [1] -> [0]
    glUseProgram_ptr(sBlurProgram);
    const GLint blurSource = glGetUniformLocation_ptr(sBlurProgram, "uSource");
    const GLint blurStep = glGetUniformLocation_ptr(sBlurProgram, "uBlurStep");
    for (int axis = 0; axis < 2; axis++) {
        const int from = axis == 0 ? 0 : 1;
        const int to   = axis == 0 ? 1 : 0;
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sBloomFbo[to]);
        glBindTexture(GL_TEXTURE_2D, sBloomTex[from]);
        if (glUniform1i_ptr) glUniform1i_ptr(blurSource, 0);
        if (glUniform2f_ptr && blurStep >= 0) {
            glUniform2f_ptr(blurStep,
                            axis == 0 ? 1.0f / float(sBloomWidth) : 0.0f,
                            axis == 0 ? 0.0f : 1.0f / float(sBloomHeight));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindVertexArray_ptr(0);
    return true;
}

// Half-resolution pair for occlusion and its denoise blur.
static bool ao_ensure_targets()
{
    const int w = std::max(1, sRenderWidth / 2);
    const int h = std::max(1, sRenderHeight / 2);
    if (!sAoFbo[0]) {
        glGenFramebuffers_ptr(2, sAoFbo);
        glGenTextures(2, sAoTex);
        sAoWidth = sAoHeight = 0;
    }
    if (sAoWidth == w && sAoHeight == h) return true;
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, sAoTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[i]);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sAoTex[i], 0);
        if (glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindTexture(GL_TEXTURE_2D, 0);
            sBoundTextures[0] = 0;
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    sAoWidth = w;
    sAoHeight = h;
    return true;
}

// Occlusion into [0], blurred across into [1] and back down into [0].
static bool ao_build()
{
    if (!sSsaoProgram || !sAoBlurProgram) return false;
    // Depth is the whole input. Without a depth texture there is nothing to
    // compute from, which is exactly the fallback case pc_gfx warned about.
    if (!sDepthIsTexture || !sNativeDepthTexture) return false;
    if (!ao_ensure_targets()) return false;

    glViewport(0, 0, sAoWidth, sAoHeight);
    glBindVertexArray_ptr(sPostVAO);

    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[0]);
    glUseProgram_ptr(sSsaoProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
    if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uDepth"), 0);
    if (glUniform4f_ptr) {
        glUniform4f_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uProjInfo"),
                        sViewInvP00, sViewInvP11, sViewNear, sViewFar);
        // The bias discards the shallow self-occlusion that the faceted
        // derivative normal produces on flat ground.
        glUniform4f_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uAOParams"),
                        sPostEffects.ssaoRadius, sPostEffects.ssaoIntensity, 0.02f, 0.0f);
    }
    if (glUniform2f_ptr) {
        // The neighbour taps that build the normal step one pixel of this
        // target, not of the full-resolution scene.
        glUniform2f_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uTexel"),
                        1.0f / float(sAoWidth), 1.0f / float(sAoHeight));
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glUseProgram_ptr(sAoBlurProgram);
    const GLint blurSource = glGetUniformLocation_ptr(sAoBlurProgram, "uSource");
    const GLint blurDepth = glGetUniformLocation_ptr(sAoBlurProgram, "uDepth");
    const GLint blurStep = glGetUniformLocation_ptr(sAoBlurProgram, "uBlurStep");
    if (glUniform4f_ptr) {
        glUniform4f_ptr(glGetUniformLocation_ptr(sAoBlurProgram, "uProjInfo"),
                        sViewInvP00, sViewInvP11, sViewNear, sViewFar);
    }
    // Depth stays bound on its own unit for the whole blur: both axes weight
    // their taps by it, and rebinding per pass would only cost state changes.
    if (glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        if (glUniform1i_ptr) glUniform1i_ptr(blurDepth, 1);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    for (int axis = 0; axis < 2; axis++) {
        const int from = axis == 0 ? 0 : 1;
        const int to   = axis == 0 ? 1 : 0;
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[to]);
        glBindTexture(GL_TEXTURE_2D, sAoTex[from]);
        if (glUniform1i_ptr) glUniform1i_ptr(blurSource, 0);
        if (glUniform2f_ptr && blurStep >= 0) {
            glUniform2f_ptr(blurStep,
                            axis == 0 ? 1.0f / float(sAoWidth) : 0.0f,
                            axis == 0 ? 0.0f : 1.0f / float(sAoHeight));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindVertexArray_ptr(0);
    return true;
}

// Returns the framebuffer the blit should read from.
static GLuint post_apply()
{
    if (!pc_post_any_enabled(sPostEffects)) return sNativeFramebuffer;
    // An effect that needs depth cannot run when the driver made us fall back
    // to a renderbuffer. Drop the pass rather than sample a texture that is
    // not there.
    if (pc_post_needs_depth(sPostEffects) && !sDepthIsTexture) return sNativeFramebuffer;
    // Any of these failing means no post-processing, never a stopped frame.
    if (!post_ensure_target() || !post_ensure_program()) return sNativeFramebuffer;

    // Bloom runs its own chain first, at half resolution, leaving its result in
    // sBloomTex[0] for the main pass to add in. A failure here is not fatal:
    // the composite simply reads a texture that contributes nothing.
    bool aoReady = false;
    if (pc_post_ssao_active(sPostEffects)) {
        aoReady = ao_build();
    }

    bool bloomReady = false;
    if (pc_post_bloom_active(sPostEffects)) {
        bloomReady = bloom_build();
    }

    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sPostFramebuffer);
    glViewport(0, 0, sRenderWidth, sRenderHeight);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glUseProgram_ptr(sPostProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
    if (glUniform1i_ptr) {
        glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uScene"), 0);
    }
    if (pc_post_needs_depth(sPostEffects) && glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uDepth"), 1);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    if (aoReady && glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, sAoTex[0]);
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uAO"), 3);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    if (bloomReady && glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, sBloomTex[0]);
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uBloom"), 2);
        glActiveTexture_ptr(GL_TEXTURE0);
        if (glUniform1f_ptr) {
            glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uBloomIntensity"),
                            sPostEffects.bloomIntensity);
        }
    } else if (pc_post_bloom_active(sPostEffects) && glUniform1f_ptr) {
        // The shader was built with the composite in it, so silence it rather
        // than sampling a texture that was never filled.
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uBloomIntensity"), 0.0f);
    }
    if (sPostEffects.fxaa && glUniform4f_ptr) {
        glUniform4f_ptr(glGetUniformLocation_ptr(sPostProgram, "uTexelSize"),
                        1.0f / float(sRenderWidth), 1.0f / float(sRenderHeight),
                        float(sRenderWidth), float(sRenderHeight));
    }
    if (sPostEffects.colourGrading && glUniform1f_ptr) {
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uGamma"), sPostEffects.gamma);
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uBrightness"), sPostEffects.brightness);
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uSaturation"), sPostEffects.saturation);
    }

    // The scene leaves vertex arrays and buffers bound. An empty vertex array
    // object detaches all of it, so the pass cannot be disturbed by whatever
    // the last draw was doing; the geometry comes from gl_VertexID.
    glBindVertexArray_ptr(sPostVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray_ptr(0);

    glUseProgram_ptr(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    return sPostFramebuffer;
}

void pc_gfx_present(void) {
    pc_gfx_flush_batch();
#ifdef GL_TIME_ELAPSED
    if (sPerfGpuSceneActive) {
        glEndQuery_ptr(GL_TIME_ELAPSED);
        sPerfGpuSceneActive = false;
    }
#endif
    if (!sNativeFramebufferReady || !glBindFramebuffer_ptr || !glBlitFramebuffer_ptr) return;
    if (sDrawableWidth <= 0 || sDrawableHeight <= 0) return;
#ifdef GL_TIME_ELAPSED
    PerfGpuQuery* gpuQuery = nullptr;
    if (sPerfStatsEnabled && sPerfGpuQueriesReady) {
        PerfGpuQuery& candidate = sPerfGpuQueries[sPerfGpuQueryWrite];
        if (!candidate.pending) {
            gpuQuery = &candidate;
            glBeginQuery_ptr(GL_TIME_ELAPSED, gpuQuery->blit);
        }
    }
#endif
    const GLuint sourceFramebuffer = post_apply();
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sourceFramebuffer);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, 0);
    // Game renders to the target aspect ratio. Display it centered in the window.
    float windowAspect = float(sDrawableWidth) / float(sDrawableHeight);
    sCurrentAspectRatio = calculate_aspect_ratio(sAspectRatioMode, windowAspect);

    GLint outX, outY, outWidth, outHeight;
    calculate_output_area(sDrawableWidth, sDrawableHeight, sCurrentAspectRatio,
                          outX, outY, outWidth, outHeight);

    glDisable(GL_SCISSOR_TEST);
    // Clearing a full 4K backbuffer immediately before covering every pixel
    // with the blit wastes bandwidth. Clear only when letter/pillarbox bars
    // are actually visible.
    if (outX != 0 || outY != 0 || outWidth != sDrawableWidth || outHeight != sDrawableHeight) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    
    glBlitFramebuffer_ptr(0, 0, sRenderWidth, sRenderHeight, outX, outY, outX + outWidth, outY + outHeight,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
#ifdef GL_TIME_ELAPSED
    if (gpuQuery) {
        glEndQuery_ptr(GL_TIME_ELAPSED);
        gpuQuery->pending = true;
        sPerfGpuQueryWrite = (sPerfGpuQueryWrite + 1) % PC_GPU_QUERY_RING_SIZE;
    }
#endif
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glEnable(GL_SCISSOR_TEST);
}

void pc_gfx_set_projection(const Mtx44 mtx, GXProjectionType type) {
    if (mtx && type == GX_PERSPECTIVE) {
        const float p00 = mtx[0][0];
        const float p11 = mtx[1][1];
        const float m22 = mtx[2][2];
        const float m23 = mtx[2][3];
        // C_MTXPerspective sets m22 = -n/(f-n) and m23 = -fn/(f-n), so their
        // ratio is the far plane and the near one follows from either.
        if (p00 != 0.0f && p11 != 0.0f && m22 != 0.0f && m22 != 1.0f) {
            const float f = m23 / m22;
            const float n = -m22 * f / (1.0f - m22);
            if (f > n && n > 0.0f) {
                sViewInvP00 = 1.0f / p00;
                sViewInvP11 = 1.0f / p11;
                sViewNear = n;
                sViewFar = f;
            }
        }
    }
    if (mtx) {
        // Dolphin matrices are indexed [row][column], while OpenGL consumes a
        // column-major float array when transpose is GL_FALSE.
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                sProjMatrix[column * 4 + row] = mtx[row][column];
            }
        }
        ++sProjMtxGen;
    }
}

void pc_gfx_set_current_mtx(u32 id) {
    if (id < 64) sCurrentPosMtxId = id;
}

void pc_gfx_set_viewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ) {
    (void)nearZ; (void)farZ;
    GLint x, y; GLsizei width, height;
    map_gx_rect(xOrig, yOrig, wd, ht, x, y, width, height);
    static GLint lastX = 0, lastY = 0;
    static GLsizei lastWidth = 0, lastHeight = 0;
    if (x == lastX && y == lastY && width == lastWidth && height == lastHeight) return;
    lastX = x; lastY = y; lastWidth = width; lastHeight = height;
    pc_gfx_note_gl_state_change();
    glViewport(x, y, width, height);
}

void pc_gfx_set_scissor(u32 xOrig, u32 yOrig, u32 wd, u32 ht) {
    GLint x, y; GLsizei width, height;
    map_gx_rect((float)xOrig, (float)yOrig, (float)wd, (float)ht, x, y, width, height);
    static GLint lastX = -1, lastY = -1;
    static GLsizei lastWidth = -1, lastHeight = -1;
    if (x == lastX && y == lastY && width == lastWidth && height == lastHeight) return;
    lastX = x; lastY = y; lastWidth = width; lastHeight = height;
    pc_gfx_note_gl_state_change();
    glScissor(x, y, width, height);
    glEnable(GL_SCISSOR_TEST);
}

void pc_gfx_load_pos_mtx(const Mtx mtx, u32 id) {
    if (id < 64 && mtx) {
        float* dst = sPosMatrix[id];
        dst[0] = mtx[0][0]; dst[1] = mtx[1][0]; dst[2] = mtx[2][0]; dst[3] = 0.0f;
        dst[4] = mtx[0][1]; dst[5] = mtx[1][1]; dst[6] = mtx[2][1]; dst[7] = 0.0f;
        dst[8] = mtx[0][2]; dst[9] = mtx[1][2]; dst[10] = mtx[2][2]; dst[11] = 0.0f;
        dst[12] = mtx[0][3]; dst[13] = mtx[1][3]; dst[14] = mtx[2][3]; dst[15] = 1.0f;
        ++sPosMtxGen[id];
    }
}

void pc_gfx_load_nrm_mtx(const Mtx mtx, u32 id) {
    if (id >= 64 || !mtx) return;
    float* d = sNrmMatrix[id];
    d[0] = mtx[0][0]; d[1] = mtx[1][0]; d[2] = mtx[2][0];
    d[3] = mtx[0][1]; d[4] = mtx[1][1]; d[5] = mtx[2][1];
    d[6] = mtx[0][2]; d[7] = mtx[1][2]; d[8] = mtx[2][2];
    ++sNrmMtxGen[id];
}
void pc_gfx_load_tex_mtx(const Mtx mtx, u32 id) {
    if (id >= 64 || !mtx) return;
    float* d = sTexMatrices[id];
    // 3x4 row-major GX matrix -> column-major 4x4
    d[0]  = mtx[0][0]; d[1]  = mtx[1][0]; d[2]  = mtx[2][0]; d[3]  = 0.0f;
    d[4]  = mtx[0][1]; d[5]  = mtx[1][1]; d[6]  = mtx[2][1]; d[7]  = 0.0f;
    d[8]  = mtx[0][2]; d[9]  = mtx[1][2]; d[10] = mtx[2][2]; d[11] = 0.0f;
    d[12] = mtx[0][3]; d[13] = mtx[1][3]; d[14] = mtx[2][3]; d[15] = 1.0f;
    sTexMtxLoaded[id] = true;
    ++sTexMtxGen[id];
}

// ── Texture coordinate generation ──
void pc_gfx_set_tex_coord_gen(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src,
                              u32 matrixIdx) {
    if (coord < 0 || coord >= 8) return;
    sTexCoordGen[coord].active = true;
    sTexCoordGen[coord].type = type;
    sTexCoordGen[coord].src = src;
    sTexCoordGen[coord].mtxIdx = matrixIdx;
}

void pc_gfx_set_z_mode(GXBool compareEnable, GXCompare func, GXBool updateEnable) {
    static bool valid = false;
    static GXBool lastCompare = GX_FALSE, lastUpdate = GX_FALSE;
    static GXCompare lastFunc = GX_NEVER;
    if (valid && lastCompare == compareEnable && lastFunc == func && lastUpdate == updateEnable) return;
    valid = true; lastCompare = compareEnable; lastFunc = func; lastUpdate = updateEnable;
    pc_gfx_note_gl_state_change();
    if (compareEnable) {
        glEnable(GL_DEPTH_TEST);
        GLenum glfunc = GL_LEQUAL;
        switch (func) {
            case GX_NEVER: glfunc = GL_NEVER; break;
            case GX_LESS: glfunc = GL_LESS; break;
            case GX_EQUAL: glfunc = GL_EQUAL; break;
            case GX_LEQUAL: glfunc = GL_LEQUAL; break;
            case GX_GREATER: glfunc = GL_GREATER; break;
            case GX_NEQUAL: glfunc = GL_NOTEQUAL; break;
            case GX_GEQUAL: glfunc = GL_GEQUAL; break;
            case GX_ALWAYS: glfunc = GL_ALWAYS; break;
        }
        glDepthFunc(glfunc);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(updateEnable ? GL_TRUE : GL_FALSE);
}

void pc_gfx_set_blend_mode(GXBlendMode type, GXBlendFactor srcFactor, GXBlendFactor dstFactor, GXLogicOp op) {
    static bool valid = false;
    static GXBlendMode lastType = GX_BM_NONE;
    static GXBlendFactor lastSrc = GX_BL_ZERO, lastDst = GX_BL_ZERO;
    static GXLogicOp lastOp = GX_LO_CLEAR;
    if (valid && lastType == type && lastSrc == srcFactor && lastDst == dstFactor && lastOp == op) return;
    valid = true; lastType = type; lastSrc = srcFactor; lastDst = dstFactor; lastOp = op;
    pc_gfx_note_gl_state_change();
    glDisable(GL_COLOR_LOGIC_OP);
    glBlendEquation_ptr(GL_FUNC_ADD);
    if (type == GX_BM_BLEND) {
        glEnable(GL_BLEND);
        GLenum s = GL_ONE, d = GL_ZERO;
        switch (srcFactor) {
            case GX_BL_ZERO: s = GL_ZERO; break;
            case GX_BL_ONE: s = GL_ONE; break;
            case GX_BL_SRCCOL: s = GL_DST_COLOR; break;
            case GX_BL_INVSRCCOL: s = GL_ONE_MINUS_DST_COLOR; break;
            case GX_BL_SRCALPHA: s = GL_SRC_ALPHA; break;
            case GX_BL_INVSRCALPHA: s = GL_ONE_MINUS_SRC_ALPHA; break;
            case GX_BL_DSTALPHA: s = GL_DST_ALPHA; break;
            case GX_BL_INVDSTALPHA: s = GL_ONE_MINUS_DST_ALPHA; break;
        }
        switch (dstFactor) {
            case GX_BL_ZERO: d = GL_ZERO; break;
            case GX_BL_ONE: d = GL_ONE; break;
            case GX_BL_DSTCOL: d = GL_SRC_COLOR; break;
            case GX_BL_INVDSTCOL: d = GL_ONE_MINUS_SRC_COLOR; break;
            case GX_BL_SRCALPHA: d = GL_SRC_ALPHA; break;
            case GX_BL_INVSRCALPHA: d = GL_ONE_MINUS_SRC_ALPHA; break;
            case GX_BL_DSTALPHA: d = GL_DST_ALPHA; break;
            case GX_BL_INVDSTALPHA: d = GL_ONE_MINUS_DST_ALPHA; break;
        }
        glBlendFunc(s, d);
    } else if (type == GX_BM_SUBTRACT) {
        glEnable(GL_BLEND);
        glBlendEquation_ptr(GL_FUNC_SUBTRACT);
        glBlendFunc(GL_ONE, GL_ONE);
    } else if (type == GX_BM_LOGIC) {
        glDisable(GL_BLEND);
        glEnable(GL_COLOR_LOGIC_OP);
        static const GLenum logicOps[] = {
            GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY, GL_AND_INVERTED, GL_NOOP,
            GL_XOR, GL_OR, GL_NOR, GL_EQUIV, GL_INVERT, GL_OR_REVERSE,
            GL_COPY_INVERTED, GL_OR_INVERTED, GL_NAND, GL_SET
        };
        const unsigned index = static_cast<unsigned>(op);
        glLogicOp(index < sizeof(logicOps) / sizeof(logicOps[0]) ? logicOps[index] : GL_COPY);
    } else {
        glDisable(GL_BLEND);
    }
}

void pc_gfx_set_cull_mode(GXCullMode mode) {
    static bool valid = false;
    static GXCullMode lastMode = GX_CULL_NONE;
    if (valid && lastMode == mode) return;
    valid = true; lastMode = mode;
    pc_gfx_note_gl_state_change();
    if (mode == GX_CULL_NONE) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(mode == GX_CULL_FRONT ? GL_FRONT : GL_BACK);
    }
}

void pc_gfx_set_color_update(GXBool updateEnable) {
    if (sColorUpdate == updateEnable) return;
    sColorUpdate = updateEnable;
    pc_gfx_note_gl_state_change();
    glColorMask(sColorUpdate, sColorUpdate, sColorUpdate, sAlphaUpdate);
}

void pc_gfx_set_alpha_update(GXBool updateEnable) {
    if (sAlphaUpdate == updateEnable) return;
    sAlphaUpdate = updateEnable;
    pc_gfx_note_gl_state_change();
    glColorMask(sColorUpdate, sColorUpdate, sColorUpdate, sAlphaUpdate);
}

void pc_gfx_set_alpha_compare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
    sAlphaComp0 = comp0;
    sAlphaComp1 = comp1;
    sAlphaOp = op;
    sAlphaRef0 = ref0 / 255.0f;
    sAlphaRef1 = ref1 / 255.0f;
}

void pc_gfx_set_chan_ctrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc,
                          u32 lightMask, GXDiffuseFn diffFn, GXAttnFn attnFn) {
    GfxChannel* ch = nullptr;
    if (chan == GX_COLOR0 || chan == GX_ALPHA0 || chan == GX_COLOR0A0) ch = &sChannels[0];
    else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) ch = &sChannels[1];
    if (!ch) return;
    if (chan == GX_COLOR0 || chan == GX_COLOR1 || chan == GX_COLOR0A0 || chan == GX_COLOR1A1) {
        ch->enabled = enable;
        ch->matSrc = matSrc;
        ch->ambSrc = ambSrc;
        ch->lightMask = lightMask;
        ch->diffFn = diffFn;
        ch->attnFn = attnFn;
    }
    if (chan == GX_ALPHA0 || chan == GX_ALPHA1 || chan == GX_COLOR0A0 || chan == GX_COLOR1A1) {
        ch->alphaEnabled = enable;
        ch->alphaMatSrc = matSrc;
        ch->alphaAmbSrc = ambSrc;
        ch->alphaLightMask = lightMask;
        ch->alphaDiffFn = diffFn;
        ch->alphaAttnFn = attnFn;
    }
}

void pc_gfx_set_chan_amb_color(GXChannelID chan, GXColor color) {
    GfxChannel* ch = nullptr;
    if (chan == GX_COLOR0 || chan == GX_ALPHA0 || chan == GX_COLOR0A0) ch = &sChannels[0];
    else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) ch = &sChannels[1];
    if (!ch) return;
    ch->ambColor[0] = color.r / 255.0f;
    ch->ambColor[1] = color.g / 255.0f;
    ch->ambColor[2] = color.b / 255.0f;
    ch->ambColor[3] = color.a / 255.0f;
}

void pc_gfx_set_chan_mat_color(GXChannelID chan, GXColor color) {
    GfxChannel* ch = nullptr;
    if (chan == GX_COLOR0 || chan == GX_ALPHA0 || chan == GX_COLOR0A0) ch = &sChannels[0];
    else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) ch = &sChannels[1];
    if (!ch) return;
    ch->matColor[0] = color.r / 255.0f;
    ch->matColor[1] = color.g / 255.0f;
    ch->matColor[2] = color.b / 255.0f;
    ch->matColor[3] = color.a / 255.0f;
}

// ── Light management ──
void pc_gfx_init_light_pos(void* ltObj, f32 x, f32 y, f32 z) {
    if (!ltObj) return;
    // GXLightObjPriv at +0x28 is lpos[3]
    u8* raw = static_cast<u8*>(ltObj);
    f32* lpos = reinterpret_cast<f32*>(raw + 0x28);
    lpos[0] = x; lpos[1] = y; lpos[2] = z;
}
void pc_gfx_init_light_dir(void* ltObj, f32 x, f32 y, f32 z) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* ldir = reinterpret_cast<f32*>(raw + 0x34);
    ldir[0] = x; ldir[1] = y; ldir[2] = z;
}
void pc_gfx_init_light_color(void* ltObj, GXColor color) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    raw[0x0C] = color.r; raw[0x0D] = color.g; raw[0x0E] = color.b; raw[0x0F] = color.a;
}
void pc_gfx_init_light_attn(void* ltObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* a = reinterpret_cast<f32*>(raw + 0x10);
    a[0] = a0; a[1] = a1; a[2] = a2;
    f32* k = reinterpret_cast<f32*>(raw + 0x1C);
    k[0] = k0; k[1] = k1; k[2] = k2;
}
void pc_gfx_init_light_attn_a(void* ltObj, f32 a0, f32 a1, f32 a2) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* a = reinterpret_cast<f32*>(raw + 0x10);
    a[0] = a0; a[1] = a1; a[2] = a2;
}
void pc_gfx_init_light_attn_k(void* ltObj, f32 k0, f32 k1, f32 k2) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* k = reinterpret_cast<f32*>(raw + 0x1C);
    k[0] = k0; k[1] = k1; k[2] = k2;
}
void pc_gfx_init_specular_dir(void* ltObj, f32 x, f32 y, f32 z) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* ldir = reinterpret_cast<f32*>(raw + 0x34);
    ldir[0] = x; ldir[1] = y; ldir[2] = z;
}
void pc_gfx_load_light(void* ltObj, u32 lightMask) {
    if (!ltObj) return;
    for (int i = 0; i < 8; i++) {
        if (lightMask & (1 << i)) {
            u8* raw = static_cast<u8*>(ltObj);
            f32* lpos = reinterpret_cast<f32*>(raw + 0x28);
            f32* lk   = reinterpret_cast<f32*>(raw + 0x1C);
            u8* col  = raw + 0x0C;
            sLights[i].pos[0] = lpos[0]; sLights[i].pos[1] = lpos[1]; sLights[i].pos[2] = lpos[2];
            sLights[i].k[0] = lk[0]; sLights[i].k[1] = lk[1]; sLights[i].k[2] = lk[2];
            f32* ld = reinterpret_cast<f32*>(raw + 0x34);
            // Half-vector for specular (light 7) is stored directly in the direction field
            // It's already normalized, use it directly
            sLights[i].dir[0] = ld[0];
            sLights[i].dir[1] = ld[1];
            sLights[i].dir[2] = ld[2];
            f32* la = reinterpret_cast<f32*>(raw + 0x10);
            sLights[i].a[0] = la[0]; sLights[i].a[1] = la[1]; sLights[i].a[2] = la[2];
            sLights[i].color[0] = col[0] / 255.0f;
            sLights[i].color[1] = col[1] / 255.0f;
            sLights[i].color[2] = col[2] / 255.0f;
            sLights[i].color[3] = col[3] / 255.0f;
            sLights[i].active = true;
        }
    }
}

void pc_gfx_set_tev_order(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID chan) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].texMap = map;
        sTevStages[stage].texCoord = coord;
        sTevStages[stage].textureEnabled = map >= GX_TEXMAP0 && map < GX_MAX_TEXMAP;
        if (chan == GX_COLOR_NULL || chan == GX_COLOR_ZERO) sTevStages[stage].rasChannel = -1;
        else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) sTevStages[stage].rasChannel = 1;
        else sTevStages[stage].rasChannel = 0;
    }
}

void pc_gfx_set_tev_op(GXTevStageID stage, GXTevMode mode) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        TevStageState& st = sTevStages[stage];
        st.colorOp = st.alphaOp = GX_TEV_ADD;
        st.colorBias = st.alphaBias = GX_TB_ZERO;
        st.colorScale = st.alphaScale = GX_CS_SCALE_1;
        st.colorClamp = st.alphaClamp = GX_TRUE;
        st.colorOutReg = st.alphaOutReg = GX_TEVPREV;
        if (mode == GX_REPLACE) {
            st.colorIn[0] = GX_CC_ZERO; st.colorIn[1] = GX_CC_ZERO;
            st.colorIn[2] = GX_CC_ZERO; st.colorIn[3] = GX_CC_TEXC;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_ZERO;
            st.alphaIn[2] = GX_CA_ZERO; st.alphaIn[3] = GX_CA_TEXA;
        } else if (mode == GX_PASSCLR) {
            st.colorIn[0] = GX_CC_ZERO; st.colorIn[1] = GX_CC_ZERO;
            st.colorIn[2] = GX_CC_ZERO; st.colorIn[3] = GX_CC_RASC;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_ZERO;
            st.alphaIn[2] = GX_CA_ZERO; st.alphaIn[3] = GX_CA_RASA;
            st.texMap = GX_TEXMAP_NULL;
            st.textureEnabled = false;
        } else if (mode == GX_MODULATE) {
            st.colorIn[0] = GX_CC_ZERO; st.colorIn[1] = GX_CC_TEXC;
            st.colorIn[2] = GX_CC_RASC; st.colorIn[3] = GX_CC_ZERO;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_TEXA;
            st.alphaIn[2] = GX_CA_RASA; st.alphaIn[3] = GX_CA_ZERO;
        } else if (mode == GX_DECAL) {
            st.colorIn[0] = GX_CC_RASC; st.colorIn[1] = GX_CC_TEXC;
            st.colorIn[2] = GX_CC_TEXA; st.colorIn[3] = GX_CC_ZERO;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_ZERO;
            st.alphaIn[2] = GX_CA_ZERO; st.alphaIn[3] = GX_CA_RASA;
        } else if (mode == GX_BLEND) {
            st.colorIn[0] = GX_CC_RASC; st.colorIn[1] = GX_CC_ONE;
            st.colorIn[2] = GX_CC_TEXC; st.colorIn[3] = GX_CC_ZERO;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_TEXA;
            st.alphaIn[2] = GX_CA_RASA; st.alphaIn[3] = GX_CA_ZERO;
        }
    }
}

void pc_gfx_set_num_tev_stages(u8 num) {
    sNumTevStages = std::min<u8>(num, GX_MAXTEVSTAGE);
}

void pc_gfx_set_tev_color_in(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b,
                             GXTevColorArg c, GXTevColorArg d) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].colorIn[0] = a;
        sTevStages[stage].colorIn[1] = b;
        sTevStages[stage].colorIn[2] = c;
        sTevStages[stage].colorIn[3] = d;
    }
}

void pc_gfx_set_tev_alpha_in(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b,
                             GXTevAlphaArg c, GXTevAlphaArg d) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].alphaIn[0] = a;
        sTevStages[stage].alphaIn[1] = b;
        sTevStages[stage].alphaIn[2] = c;
        sTevStages[stage].alphaIn[3] = d;
    }
}

void pc_gfx_set_tev_color_op(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                             GXTevScale scale, GXBool clamp, GXTevRegID outReg) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].colorOp = op;
        sTevStages[stage].colorBias = bias;
        sTevStages[stage].colorScale = scale;
        sTevStages[stage].colorClamp = clamp;
        sTevStages[stage].colorOutReg = outReg;
    }
}

void pc_gfx_set_tev_alpha_op(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                             GXTevScale scale, GXBool clamp, GXTevRegID outReg) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].alphaOp = op;
        sTevStages[stage].alphaBias = bias;
        sTevStages[stage].alphaScale = scale;
        sTevStages[stage].alphaClamp = clamp;
        sTevStages[stage].alphaOutReg = outReg;
    }
}

void pc_gfx_set_tev_color(GXTevRegID reg, GXColor color) {
    if (reg < GX_TEVPREV || reg >= GX_MAX_TEVREG) return;
    sTevRegisters[reg][0] = color.r / 255.0f;
    sTevRegisters[reg][1] = color.g / 255.0f;
    sTevRegisters[reg][2] = color.b / 255.0f;
    sTevRegisters[reg][3] = color.a / 255.0f;
#ifdef PC_GFX_TRACE
    {
        static FILE* tf = nullptr;
        if (!tf) tf = fopen("/tmp/opencode/tevreg.log", "a");
        if (tf) { fprintf(tf, "[API] reg=%d u8 -> (%.3f,%.3f,%.3f,%.3f)\n",
                          (int)reg, color.r / 255.0f, color.g / 255.0f,
                          color.b / 255.0f, color.a / 255.0f); fflush(tf); }
    }
#endif
}

void pc_gfx_set_tev_color_s10(GXTevRegID reg, GXColorS10 color) {
    if (reg < GX_TEVPREV || reg >= GX_MAX_TEVREG) return;
    sTevRegisters[reg][0] = color.r / 255.0f;
    sTevRegisters[reg][1] = color.g / 255.0f;
    sTevRegisters[reg][2] = color.b / 255.0f;
    sTevRegisters[reg][3] = color.a / 255.0f;
#ifdef PC_GFX_TRACE
    {
        static FILE* tf = nullptr;
        if (!tf) tf = fopen("/tmp/opencode/tevreg.log", "a");
        if (tf) { fprintf(tf, "[API] reg=%d s10 -> (%.3f,%.3f,%.3f,%.3f)\n",
                          (int)reg, color.r / 255.0f, color.g / 255.0f,
                          color.b / 255.0f, color.a / 255.0f); fflush(tf); }
    }
#endif
}

void pc_gfx_set_tev_kcolor(GXTevKColorID id, GXColor color) {
    if (id < 0 || id > 3) return;
    sKonstColors[id][0] = color.r / 255.0f;
    sKonstColors[id][1] = color.g / 255.0f;
    sKonstColors[id][2] = color.b / 255.0f;
    sKonstColors[id][3] = color.a / 255.0f;
}

void pc_gfx_set_tev_kcolor_sel(GXTevStageID stage, GXTevKColorSel sel) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sKonstColorSel[stage] = sel;
    }
}

void pc_gfx_set_tev_kalpha_sel(GXTevStageID stage, GXTevKAlphaSel sel) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sKonstAlphaSel[stage] = sel;
    }
}

void pc_gfx_set_tev_swap_mode(GXTevStageID stage, GXTevSwapSel rasSel, GXTevSwapSel texSel) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevRasSwapSel[stage] = rasSel;
        sTevTexSwapSel[stage] = texSel;
    }
}

void pc_gfx_set_tev_swap_mode_table(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {
    if (table >= GX_TEV_SWAP0 && table <= GX_TEV_SWAP3) {
        int idx = table - GX_TEV_SWAP0;
        sTevSwapModes[idx].red = red;
        sTevSwapModes[idx].green = green;
        sTevSwapModes[idx].blue = blue;
        sTevSwapModes[idx].alpha = alpha;
    }
}

// ── Texture Decoding & Binding ──
void pc_gfx_init_tex_obj(GXTexObj* obj, void* imagePtr, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap) {
    (void)mipmap;
    if (!obj || !imagePtr || width == 0 || height == 0) return;

    uintptr_t key = (uintptr_t)obj;
    const PcTextureSignature signature {
        imagePtr, width, height, static_cast<u32>(format), wrapS, wrapT, false, 0
    };
    const auto signatureIt = sTextureSignatures.find(key);
    if (signatureIt != sTextureSignatures.end() && signatureIt->second == signature
        && sTextureCache.find(key) != sTextureCache.end()) {
        return;
    }

    GLuint texId = 0;

    auto it = sTextureCache.find(key);
    if (it != sTextureCache.end()) {
        texId = it->second;
    } else {
        glGenTextures(1, &texId);
        sTextureCache[key] = texId;
    }

    pc_gfx_flush_batch();  // about to rebind and rewrite texture unit 0
    glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    sBoundTextures[0] = texId;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS == GX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT == GX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    std::vector<u8> rgba(width * height * 4, 255);
    const u8* source = static_cast<const u8*>(imagePtr);
    auto writePixel = [&](int x, int y, u8 r, u8 g, u8 b, u8 a) {
        if (x >= width || y >= height) return;
        size_t offset = (static_cast<size_t>(y) * width + x) * 4;
        rgba[offset] = r; rgba[offset + 1] = g;
        rgba[offset + 2] = b; rgba[offset + 3] = a;
    };
    auto expand5 = [](u16 value) -> u8 { return (value << 3) | (value >> 2); };
    auto expand6 = [](u16 value) -> u8 { return (value << 2) | (value >> 4); };

    int tileWidth = 1, tileHeight = 1, bytesPerTile = 0;
    switch (format) {
        case GX_TF_I4: tileWidth = 8; tileHeight = 8; bytesPerTile = 32; break;
        case GX_TF_I8: case GX_TF_IA4: tileWidth = 8; tileHeight = 4; bytesPerTile = 32; break;
        case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3:
            tileWidth = 4; tileHeight = 4; bytesPerTile = 32; break;
        case GX_TF_RGBA8: tileWidth = 4; tileHeight = 4; bytesPerTile = 64; break;
        case GX_TF_CMPR: tileWidth = 8; tileHeight = 8; bytesPerTile = 32; break;
        default: break;
    }

    if (bytesPerTile == 0) {
        // El búfer se inicializa a blanco opaco, así que un formato que no se
        // decodifique acaba en pantalla como un cuadrado blanco sólido y sin
        // que nada lo señale. Se avisa una vez por formato para que deje de ser
        // invisible durante la depuración.
        static u32 reportedFormats = 0;
        const u32 formatBit = format < 32 ? (1u << format) : 0x80000000u;
        const bool firstTime = (reportedFormats & formatBit) == 0;
        reportedFormats |= formatBit;

        // Los formatos Z son un caso conocido y distinto: el juego los usa para
        // texturas que se rellenan copiando el framebuffer (mapMgr crea el
        // «internalLightmap» de 320x240 con TEX_FMT_Z8), y en el port GXCopyTex
        // es un stub vacío, así que esos píxeles nunca llegan a escribirse.
        // Decodificarlos leería memoria sin inicializar. El blanco es además el
        // valor neutro para cómo se combinan, así que se conserva.
        const bool isDepthFormat = format == GX_TF_Z8 || format == GX_TF_Z16
                                || format == GX_TF_Z24X8;
        if (firstTime) {
            printf("[PC GX Warning] Formato de textura no decodificado 0x%X (%dx%d): %s\n",
                   static_cast<unsigned>(format), width, height,
                   isDepthFormat
                       ? "formato Z, su origen (GXCopyTex) es un stub; se deja neutro"
                       : "se dibujará transparente en vez de blanco");
            fflush(stdout);
        }
        if (!isDepthFormat) {
            // Transparente estropea mucho menos la escena que un cuadrado
            // blanco opaco encima de ella.
            std::fill(rgba.begin(), rgba.end(), static_cast<u8>(0));
        }
    }

    if (bytesPerTile) {
        size_t tileOffset = 0;
        for (int tileY = 0; tileY < height; tileY += tileHeight) {
            for (int tileX = 0; tileX < width; tileX += tileWidth, tileOffset += bytesPerTile) {
                for (int y = 0; y < tileHeight; ++y) {
                    for (int x = 0; x < tileWidth; ++x) {
                        int index = y * tileWidth + x;
                        u8 r = 255, g = 255, b = 255, a = 255;
                        if (format == GX_TF_I4) {
                            u8 packed = source[tileOffset + index / 2];
                            u8 intensity = ((index & 1) ? packed : packed >> 4) & 0x0f;
                            r = g = b = a = intensity * 17;
                        } else if (format == GX_TF_I8) {
                            r = g = b = a = source[tileOffset + index];
                        } else if (format == GX_TF_IA4) {
                            u8 packed = source[tileOffset + index];
							a = (packed >> 4) * 17; r = g = b = (packed & 0x0f) * 17;
                        } else if (format == GX_TF_IA8) {
							a = source[tileOffset + index * 2];
							r = g = b = source[tileOffset + index * 2 + 1];
                        } else if (format == GX_TF_RGB565 || format == GX_TF_RGB5A3) {
                            u16 value = (source[tileOffset + index * 2] << 8) | source[tileOffset + index * 2 + 1];
                            if (format == GX_TF_RGB565) {
                                r = expand5(value >> 11); g = expand6((value >> 5) & 0x3f); b = expand5(value & 0x1f);
                            } else if (value & 0x8000) {
                                r = expand5((value >> 10) & 0x1f); g = expand5((value >> 5) & 0x1f); b = expand5(value & 0x1f);
                            } else {
                                a = ((value >> 12) & 7) * 255 / 7;
                                r = ((value >> 8) & 15) * 17; g = ((value >> 4) & 15) * 17; b = (value & 15) * 17;
                            }
                        } else if (format == GX_TF_RGBA8) {
                            a = source[tileOffset + index * 2]; r = source[tileOffset + index * 2 + 1];
                            g = source[tileOffset + 32 + index * 2]; b = source[tileOffset + 33 + index * 2];
                        } else if (format == GX_TF_CMPR) {
                            // GameCube CMPR is DXT1 arranged as four 4x4
                            // sub-blocks inside every 8x8 tile. Endpoints are
                            // big-endian and selectors are stored two bits per
                            // pixel, most-significant pair first.
                            const int block = (y / 4) * 2 + (x / 4);
                            const u8* encoded = source + tileOffset + block * 8;
                            const u16 c0 = (u16(encoded[0]) << 8) | encoded[1];
                            const u16 c1 = (u16(encoded[2]) << 8) | encoded[3];
                            u8 colors[4][4] = {};
                            colors[0][0] = expand5(c0 >> 11); colors[0][1] = expand6((c0 >> 5) & 0x3f);
                            colors[0][2] = expand5(c0 & 0x1f); colors[0][3] = 255;
                            colors[1][0] = expand5(c1 >> 11); colors[1][1] = expand6((c1 >> 5) & 0x3f);
                            colors[1][2] = expand5(c1 & 0x1f); colors[1][3] = 255;
                            if (c0 > c1) {
                                for (int channel = 0; channel < 3; ++channel) {
                                    colors[2][channel] = (2 * colors[0][channel] + colors[1][channel]) / 3;
                                    colors[3][channel] = (colors[0][channel] + 2 * colors[1][channel]) / 3;
                                }
                                colors[2][3] = colors[3][3] = 255;
                            } else {
                                for (int channel = 0; channel < 3; ++channel) {
                                    colors[2][channel] = (colors[0][channel] + colors[1][channel]) / 2;
                                }
                                colors[2][3] = 255;
                                colors[3][3] = 0;
                            }
                            const int localX = x & 3;
                            const int localY = y & 3;
                            const int selector = (encoded[4 + localY] >> (6 - localX * 2)) & 3;
                            r = colors[selector][0]; g = colors[selector][1];
                            b = colors[selector][2]; a = colors[selector][3];
                        }
                        writePixel(tileX + x, tileY + y, r, g, b, a);
                    }
                }
            }
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    ++sPerfTextureUploads;
    sTextureSignatures[key] = signature;
}

void pc_gfx_init_tlut_obj(GXTlutObj* obj, void* lut, GXTlutFmt format, u16 numEntries) {
    if (!obj || !lut || numEntries == 0) return;
    PcTlut decoded;
    decoded.rgba.resize(static_cast<size_t>(numEntries) * 4);
    const u8* source = static_cast<const u8*>(lut);
    auto expand5 = [](u16 value) -> u8 { return (value << 3) | (value >> 2); };
    auto expand6 = [](u16 value) -> u8 { return (value << 2) | (value >> 4); };
    for (u16 i = 0; i < numEntries; ++i) {
        const u16 value = (u16(source[i * 2]) << 8) | source[i * 2 + 1];
        u8 r = 255, g = 255, b = 255, a = 255;
        if (format == GX_TL_IA8) {
            r = g = b = u8(value >> 8);
            a = u8(value & 0xFF);
        } else if (format == GX_TL_RGB565) {
            r = expand5(value >> 11);
            g = expand6((value >> 5) & 0x3F);
            b = expand5(value & 0x1F);
        } else if (value & 0x8000) {
            r = expand5((value >> 10) & 0x1F);
            g = expand5((value >> 5) & 0x1F);
            b = expand5(value & 0x1F);
        } else {
            a = u8(((value >> 12) & 0x7) * 255 / 7);
            r = u8(((value >> 8) & 0xF) * 17);
            g = u8(((value >> 4) & 0xF) * 17);
            b = u8((value & 0xF) * 17);
        }
        const size_t offset = static_cast<size_t>(i) * 4;
        decoded.rgba[offset] = r; decoded.rgba[offset + 1] = g;
        decoded.rgba[offset + 2] = b; decoded.rgba[offset + 3] = a;
    }
    sTlutObjects[reinterpret_cast<uintptr_t>(obj)] = std::move(decoded);
}

void pc_gfx_load_tlut(GXTlutObj* obj, u32 tlutName) {
    if (!obj) return;
    auto it = sTlutObjects.find(reinterpret_cast<uintptr_t>(obj));
    if (it != sTlutObjects.end()) sLoadedTluts[tlutName] = it->second;
}

static bool upload_ci_texture(GXTexObj* obj, const PcCiTexture& ci) {
    auto paletteIt = sLoadedTluts.find(ci.tlutName);
    if (!obj || !ci.image || paletteIt == sLoadedTluts.end() || paletteIt->second.rgba.empty()) return false;
    const PcTlut& palette = paletteIt->second;
    std::vector<u8> rgba(static_cast<size_t>(ci.width) * ci.height * 4, 0);
    int tileWidth = ci.format == GX_TF_C4 || ci.format == GX_TF_C8 ? 8 : 4;
    int tileHeight = ci.format == GX_TF_C4 ? 8 : 4;
    int bytesPerTile = 32;
    size_t tileOffset = 0;
    for (int tileY = 0; tileY < ci.height; tileY += tileHeight) {
        for (int tileX = 0; tileX < ci.width; tileX += tileWidth, tileOffset += bytesPerTile) {
            for (int y = 0; y < tileHeight; ++y) {
                for (int x = 0; x < tileWidth; ++x) {
                    const int local = y * tileWidth + x;
                    u32 paletteIndex = 0;
                    if (ci.format == GX_TF_C4) {
                        const u8 packed = ci.image[tileOffset + local / 2];
                        paletteIndex = (local & 1) ? (packed & 0xF) : (packed >> 4);
                    } else if (ci.format == GX_TF_C8) {
                        paletteIndex = ci.image[tileOffset + local];
                    } else {
                        paletteIndex = ((u32(ci.image[tileOffset + local * 2]) << 8)
                                      | ci.image[tileOffset + local * 2 + 1]) & 0x3FFF;
                    }
                    const int dstX = tileX + x, dstY = tileY + y;
                    const size_t paletteOffset = static_cast<size_t>(paletteIndex) * 4;
                    if (dstX >= ci.width || dstY >= ci.height || paletteOffset + 3 >= palette.rgba.size()) continue;
                    const size_t dst = (static_cast<size_t>(dstY) * ci.width + dstX) * 4;
                    memcpy(&rgba[dst], &palette.rgba[paletteOffset], 4);
                }
            }
        }
    }

    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);
    GLuint texId = 0;
    auto textureIt = sTextureCache.find(key);
    if (textureIt == sTextureCache.end()) {
        glGenTextures(1, &texId);
        sTextureCache[key] = texId;
    } else {
        texId = textureIt->second;
    }
    pc_gfx_flush_batch();  // about to rebind and rewrite texture unit 0
    glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    sBoundTextures[0] = texId;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ci.wrapS == GX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ci.wrapT == GX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ci.width, ci.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    ++sPerfTextureUploads;
    return true;
}

void pc_gfx_init_tex_obj_ci(GXTexObj* obj, void* imagePtr, u16 width, u16 height, GXCITexFmt format,
                            GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap, u32 tlutName) {
    (void)mipmap;
    if (!obj || !imagePtr || width == 0 || height == 0) return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);
    const PcTextureSignature signature {
        imagePtr, width, height, static_cast<u32>(format), wrapS, wrapT, true, tlutName
    };
    const auto signatureIt = sTextureSignatures.find(key);
    if (signatureIt != sTextureSignatures.end() && signatureIt->second == signature
        && sTextureCache.find(key) != sTextureCache.end()) {
        return;
    }

    PcCiTexture ci { static_cast<const u8*>(imagePtr), width, height, format, wrapS, wrapT, tlutName };
    sCiTextures[key] = ci;
    if (upload_ci_texture(obj, ci)) {
        sTextureSignatures[key] = signature;
    }
}

void pc_gfx_load_tex_obj(GXTexObj* obj, GXTexMapID id) {
    if (id < GX_TEXMAP0 || id >= GX_MAX_TEXMAP) return;
    if (!obj) {
        sHasActiveTextures[id] = false;
        return;
    }

    uintptr_t key = (uintptr_t)obj;
    auto it = sTextureCache.find(key);
    if (it == sTextureCache.end()) {
        auto ci = sCiTextures.find(key);
        if (ci != sCiTextures.end() && upload_ci_texture(obj, ci->second)) it = sTextureCache.find(key);
    }
    if (it != sTextureCache.end()) {
        sActiveGLTextures[id] = it->second;
        sHasActiveTextures[id] = true;
    } else {
        // Hasta ahora esto era mudo, y una textura que nunca llegó a subirse
        // se manifestaba solo como un artefacto en pantalla.
        static u32 reported = 0;
        if (reported < 8) {
            ++reported;
            printf("[PC GX Warning] Se pidió la textura %p para el mapa %d, "
                   "pero nunca se subió; se dibujará transparente.\n",
                   static_cast<void*>(obj), static_cast<int>(id));
            fflush(stdout);
        }
        sActiveGLTextures[id] = 0;
        sHasActiveTextures[id] = false;
    }
}

// ── Vertex Descriptor & Setup ──
// The game caches which vertex attributes it has already programmed and skips
// redundant GXSetVtxDesc calls (DGXGraphics::setupVtxDesc). That cache assumes
// nothing else touches the descriptor. Recording the history lets a misparsed
// vertex say which calls actually reached us before it.
//
// Off unless asked for. setupVtxDesc programs every attribute for every mesh
// (see the comment on it in dgxGraphics.cpp), so this runs about fifteen times
// per mesh in every frame of a normal session, purely to keep a history that
// only the wild-vertex report ever reads.
bool pc_gfx_gx_diagnostics_enabled(void) {
    static const bool enabled = [] {
        const char* value = getenv("PIKMIN_WILD_VERTS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

struct VtxDescEvent { uint32_t serial; int attr; int type; bool cleared; };
static VtxDescEvent sVtxDescLog[16] = {};
static uint32_t sVtxDescSerial = 0;
static inline void log_vtx_desc(int attr, int type, bool cleared) {
    if (!pc_gfx_gx_diagnostics_enabled()) return;
    sVtxDescLog[sVtxDescSerial % 16] = { sVtxDescSerial, attr, type, cleared };
    ++sVtxDescSerial;
}
void pc_gfx_dump_vtx_desc_history(const char* why) {
    fprintf(stderr, "[PC GX] vtxdesc history (%s), most recent last, %u calls total:\n", why, sVtxDescSerial);
    const uint32_t first = sVtxDescSerial > 16 ? sVtxDescSerial - 16 : 0;
    for (uint32_t i = first; i < sVtxDescSerial; ++i) {
        const VtxDescEvent& e = sVtxDescLog[i % 16];
        if (e.cleared) fprintf(stderr, "    #%u  CLEAR ALL\n", e.serial);
        else fprintf(stderr, "    #%u  attr a%d = %s\n", e.serial, e.attr,
                     e.type == GX_NONE ? "NONE" : e.type == GX_DIRECT ? "DIRECT"
                     : e.type == GX_INDEX8 ? "IDX8" : "IDX16");
    }
}

void pc_gfx_clear_vtx_desc(void) {
    for (int i = 0; i < GX_VA_MAX_ATTR; ++i) sVtxDesc[i] = GX_NONE;
    log_vtx_desc(0, 0, true);
}
void pc_gfx_set_vtx_desc(GXAttr attr, GXAttrType type) {
    if (attr >= 0 && attr < GX_VA_MAX_ATTR) sVtxDesc[attr] = type;
    log_vtx_desc(int(attr), int(type), false);
}
void pc_gfx_set_vtx_attr_fmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    if (fmt >= 0 && fmt < GX_MAX_VTXFMT && attr >= 0 && attr < GX_VA_MAX_ATTR) {
        sVtxFormats[fmt][attr] = { cnt, type, frac };
    }
}
void pc_gfx_set_array(GXAttr attr, void* basePtr, u8 stride) {
    if (attr >= 0 && attr < GX_VA_MAX_ATTR) {
        sVtxArrays[attr] = { static_cast<const u8*>(basePtr), stride };
        sArraySetFrame[attr]  = sFrameSerial;
        sArraySetSerial[attr] = ++sArraySetCounter;
    }
}

static Vertex sCurVertex = {};

// ── Drawing & FIFO Stream Parser ──
void pc_gfx_begin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    (void)vtxfmt;
    sCurrentPrimType = type;
    sExpectedVerts = nverts;
    sVertexStream.clear();
    if (sVertexStream.capacity() < nverts) sVertexStream.reserve(nverts);
    sInPrimitive = true;
    sHaveVertex = false;
    sVerticesPretransformed = false;
    sCurVertex.r = 1.0f;
    sCurVertex.g = 1.0f;
    sCurVertex.b = 1.0f;
    sCurVertex.a = 1.0f;
    sCurVertex.nx = 0.0f;
    sCurVertex.ny = 0.0f;
    sCurVertex.nz = 1.0f;
    for (int i = 0; i < 4; ++i) {
        sCurVertex.tex[i][0] = 0.0f;
        sCurVertex.tex[i][1] = 0.0f;
    }
}
static int sAttrStep = 0;

void pc_gfx_position(f32 x, f32 y, f32 z) {
    if (!sInPrimitive) return;
    if (sHaveVertex) sVertexStream.push_back(sCurVertex);
    sCurVertex.x = x;
    sCurVertex.y = y;
    sCurVertex.z = z;
    sHaveVertex = true;
}

void pc_gfx_color(u8 r, u8 g, u8 b, u8 a) {
    sCurVertex.r = r / 255.0f;
    sCurVertex.g = g / 255.0f;
    sCurVertex.b = b / 255.0f;
    sCurVertex.a = a / 255.0f;
}

void pc_gfx_texcoord(f32 u, f32 v) {
    sCurVertex.tex[0][0] = u;
    sCurVertex.tex[0][1] = v;
}

void pc_gfx_push_f32(f32 val) {
    if (!sInPrimitive) return;

    if (sAttrStep == 0) sCurVertex.x = val;
    else if (sAttrStep == 1) sCurVertex.y = val;
    else if (sAttrStep == 2) {
        sCurVertex.z = val;
        // Push vertex to stream
        sVertexStream.push_back(sCurVertex);
        sAttrStep = -1; // Reset step for next vertex
    }
    sAttrStep++;
}

void pc_gfx_push_u8(u8 val) { (void)val; }
void pc_gfx_push_u16(u16 val) { (void)val; }
void pc_gfx_push_u32(u32 val) { (void)val; }
void pc_gfx_push_s8(s8 val) { (void)val; }
void pc_gfx_push_s16(s16 val) { (void)val; }
void pc_gfx_push_s32(s32 val) { (void)val; }


// ── Specialised TEV programs ──
// The ubershader interprets the TEV configuration per pixel. That
// configuration is constant for a draw, so it belongs in the program: each
// distinct configuration gets its own generated shader with the selectors
// resolved, and the results are cached because a scene reuses a small number
// of materials. The ubershader stays as the fallback and as an A/B reference.

struct TevProgramEntry {
    PcTevShaderKey key;
    uint64_t hash = 0;
    GLuint program = 0;
    ProgramLocations locations;
};

static std::vector<TevProgramEntry> sTevPrograms;
// Consecutive draws almost always share a material, so remember the last
// configuration and skip both the hash and the table scan when it repeats.
// Fog, as the game asks for it. Kept here rather than thrown away in the stub:
// the values are per stage and the game already computes them correctly.
// What the game asked for, and whether the player allows it. Kept apart so a
// toggle takes effect on the next frame drawn rather than on the next time the
// game happens to call setFog.
static bool sFogRequested = false;
static bool sFogAllowed = true;
static float sFogStart = 0.0f, sFogEnd = 0.0f, sFogNear = 0.0f, sFogFar = 0.0f;
static float sFogColour[3] = { 0.0f, 0.0f, 0.0f };

void pc_gfx_set_fog(int enabled, float startZ, float endZ, float nearZ, float farZ,
                    unsigned char r, unsigned char g, unsigned char b)
{
    // A span of zero would divide by nothing and a near/far pair that is not
    // ordered cannot describe a view, so treat either as "no fog" rather than
    // letting it reach the shader.
    sFogRequested = enabled != 0 && endZ != startZ && farZ > nearZ;
    sFogStart = startZ;
    sFogEnd = endZ;
    sFogNear = nearZ;
    sFogFar = farZ;
    sFogColour[0] = r / 255.0f;
    sFogColour[1] = g / 255.0f;
    sFogColour[2] = b / 255.0f;
    // Once, the first time a stage actually asks for fog. Says that the values
    // are arriving and what they are, which is otherwise only answerable by
    // staring at a horizon.
    static bool reported = false;
    if (sFogRequested && !reported) {
        reported = true;
        printf("[PC Port] Fog active: %.0f..%.0f (view %.0f..%.0f) colour %d,%d,%d\n",
               startZ, endZ, nearZ, farZ, r, g, b);
        fflush(stdout);
    }
}

void pc_gfx_set_fog_allowed(int allowed) { sFogAllowed = allowed != 0; }

static PcTevShaderKey sLastTevKey;
static bool sLastTevKeyValid = false;

// Builds the key describing what the fragment stage must compute. Only
// configuration goes in; per-draw values stay as uniforms so materials that
// differ solely by colour still share one program.
static void build_tev_shader_key(PcTevShaderKey& key) {
    key = PcTevShaderKey();
    const int stages = std::clamp<int>(sNumTevStages, 1, GX_MAXTEVSTAGE);
    key.numStages = uint8_t(stages);
    for (int i = 0; i < stages; ++i) {
        const TevStageState& st = sTevStages[i];
        PcTevStageKey& out = key.stages[i];
        for (int arg = 0; arg < 4; ++arg) {
            out.colorIn[arg] = uint8_t(st.colorIn[arg]);
            out.alphaIn[arg] = uint8_t(st.alphaIn[arg]);
        }
        out.colorOp     = uint8_t(st.colorOp);
        out.colorBias   = uint8_t(st.colorBias);
        out.colorScale  = uint8_t(st.colorScale);
        out.colorClamp  = uint8_t(st.colorClamp ? 1 : 0);
        out.colorOutReg = uint8_t(st.colorOutReg);
        out.alphaOp     = uint8_t(st.alphaOp);
        out.alphaBias   = uint8_t(st.alphaBias);
        out.alphaScale  = uint8_t(st.alphaScale);
        out.alphaClamp  = uint8_t(st.alphaClamp ? 1 : 0);
        out.alphaOutReg = uint8_t(st.alphaOutReg);
        const bool textured = st.textureEnabled && st.texMap >= GX_TEXMAP0 && st.texMap < GX_MAX_TEXMAP;
        out.texMap     = textured ? int8_t(st.texMap) : int8_t(-1);
        // The ubershader clamps the varying set it carries to four slots.
        out.texCoord   = uint8_t(std::clamp<int>(int(st.texCoord), 0, 3));
        out.rasChannel = int8_t(st.rasChannel);
        out.rasSwapSel = uint8_t(sTevRasSwapSel[i] & 3);
        out.texSwapSel = uint8_t(sTevTexSwapSel[i] & 3);
    }
    for (int t = 0; t < 4; ++t) {
        key.swapTable[t][0] = uint8_t(sTevSwapModes[t].red);
        key.swapTable[t][1] = uint8_t(sTevSwapModes[t].green);
        key.swapTable[t][2] = uint8_t(sTevSwapModes[t].blue);
        key.swapTable[t][3] = uint8_t(sTevSwapModes[t].alpha);
    }
    key.alphaComp0       = uint8_t(sAlphaComp0);
    key.alphaComp1       = uint8_t(sAlphaComp1);
    key.alphaTestOp      = uint8_t(sAlphaOp);
    key.useMaterialRgb   = uint8_t(sChannels[0].matSrc == GX_SRC_REG ? 1 : 0);
    key.useMaterialAlpha = uint8_t(sChannels[0].alphaMatSrc == GX_SRC_REG ? 1 : 0);
    key.useMaterialRgb1  = uint8_t(sChannels[1].matSrc == GX_SRC_REG ? 1 : 0);
    key.fog              = uint8_t(sFogRequested && sFogAllowed ? 1 : 0);
}

// Compiles and links one specialised program. Returns 0 on failure, which
// sends the caller back to the ubershader rather than dropping the draw.
static GLuint compile_specialised_program(const PcTevShaderKey& key) {
    if (!glCreateShader_ptr || !sSharedVertexShader) return 0;

    const std::string source = pc_tev_build_fragment_source(key);
    const char* sourcePtr = source.c_str();

    GLuint fragment = glCreateShader_ptr(GL_FRAGMENT_SHADER);
    glShaderSource_ptr(fragment, 1, &sourcePtr, nullptr);
    glCompileShader_ptr(fragment);
    GLint status = 0;
    if (glGetShaderiv_ptr) glGetShaderiv_ptr(fragment, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetShaderInfoLog_ptr) glGetShaderInfoLog_ptr(fragment, sizeof(log), nullptr, log);
        fprintf(stderr, "[PC GX] Specialised TEV shader failed to compile:\n%s\nSource:\n%s\n",
                log, source.c_str());
        glDeleteShader_ptr(fragment);
        return 0;
    }

    GLuint program = glCreateProgram_ptr();
    glAttachShader_ptr(program, sSharedVertexShader);
    glAttachShader_ptr(program, fragment);
    bind_fixed_attrib_locations(program);
    glLinkProgram_ptr(program);
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(program, GL_LINK_STATUS, &status);
    glDeleteShader_ptr(fragment);
    if (status != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(program, sizeof(log), nullptr, log);
        fprintf(stderr, "[PC GX] Specialised TEV program failed to link:\n%s\n", log);
        glDeleteProgram_ptr(program);
        return 0;
    }
    ++sPerfShaderCompiles;
    gl_error_checkpoint("specialised program creation");
    if (std::getenv("PIKMIN_DUMP_SHADERS")) {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/pikmin_tev_%llu.frag",
                 (unsigned long long)sPerfShaderCompiles);
        if (FILE* out = fopen(path, "w")) {
            fputs(source.c_str(), out);
            fclose(out);
        }
    }
    return program;
}

// Selects the program for the current TEV state and makes its uniform
// locations active. Falls back to the ubershader whenever generation fails.
static void use_program_for_current_state() {
    if (!sSpecialiseShaders || sSpecialiseFailed
        || int(sNumTevStages) > sSpecialiseMaxStages) {
        if (sCurrentProgram != sShaderProgram) {
            glUseProgram_ptr(sShaderProgram);
            invalidate_uniform_cache();
            sCurrentProgram = sShaderProgram;
            sCheckProgram = sShaderProgram;
            sLoc = sUberLocations;
            sLastTevKeyValid = false;
        }
        return;
    }

    PcTevShaderKey key;
    build_tev_shader_key(key);
    if (sLastTevKeyValid && sCurrentProgram != sShaderProgram && key == sLastTevKey) {
        return;
    }
    const uint64_t hash = pc_tev_hash_key(key);

    for (TevProgramEntry& entry : sTevPrograms) {
        if (entry.hash != hash || !(entry.key == key)) continue;
        if (sCurrentProgram != entry.program) {
            glUseProgram_ptr(entry.program);
            invalidate_uniform_cache();
            sCurrentProgram = entry.program;
            sCheckProgram = entry.program;
            sLoc = entry.locations;
        }
        sLastTevKey = key;
        sLastTevKeyValid = true;
        return;
    }

    const GLuint program = compile_specialised_program(key);
    if (!program) {
        // One failure is treated as a permanent fallback: a configuration this
        // generator cannot express must not be retried for every draw.
        sSpecialiseFailed = true;
        sLastTevKeyValid = false;
        fprintf(stderr, "[PC GX] Falling back to the TEV ubershader for the rest of the session.\n");
        glUseProgram_ptr(sShaderProgram);
        invalidate_uniform_cache();
        sCurrentProgram = sShaderProgram;
        sLoc = sUberLocations;
        return;
    }

    TevProgramEntry entry;
    entry.key = key;
    entry.hash = hash;
    entry.program = program;
    query_program_locations(program, entry.locations);
    sTevPrograms.push_back(entry);

    if (std::getenv("PIKMIN_DUMP_SHADERS")) {
        printf("[PC GX dump] program %llu stages=%d; samplers:",
               (unsigned long long)sPerfShaderCompiles, int(key.numStages));
        for (int i = 0; i < 8; ++i) {
            if (entry.locations.tex[i] < 0) continue;
            GLint bound = -1;
            if (glGetUniformiv_ptr) {
                glGetUniformiv_ptr(program, entry.locations.tex[i], &bound);
            }
            printf(" uTex%d=%d", i, bound);
        }
        // The stages that actually sample, so the two can be compared.
        printf(" | wants:");
        for (int i = 0; i < int(key.numStages); ++i) {
            printf(" s%d:map=%d,tc=%d", i, int(key.stages[i].texMap), int(key.stages[i].texCoord));
        }
        printf("\n");
        fflush(stdout);
    }

    glUseProgram_ptr(program);
    invalidate_uniform_cache();
    sCurrentProgram = program;
    sCheckProgram = program;
    sLoc = entry.locations;
    sLastTevKey = key;
    sLastTevKeyValid = true;
}

void pc_gfx_set_shader_specialisation(bool enabled) {
    pc_gfx_flush_batch();  // changes which program the next batch compiles to
    sSpecialiseShaders = enabled;
}

bool pc_gfx_get_shader_specialisation(void) {
    return sSpecialiseShaders && !sSpecialiseFailed;
}

size_t pc_gfx_get_specialised_program_count(void) {
    return sTevPrograms.size();
}

unsigned int pc_gfx_get_depth_texture(void)
{
    return sDepthIsTexture ? sNativeDepthTexture : 0;
}

unsigned int pc_gfx_get_colour_texture(void)
{
    return sNativeFramebufferReady ? sNativeColorTexture : 0;
}

void pc_gfx_get_render_size(int* width, int* height)
{
    if (width) *width = sRenderWidth;
    if (height) *height = sRenderHeight;
}

// Per-frame submission cost, accumulated across every GX primitive and handed
// to the tick profiler once per frame. Only touched when PIKMIN_TICK_STATS is
// on: three clock reads per draw would otherwise be the very overhead under
// investigation.
static double sSubmitUniformMs = 0.0;
static double sSubmitVboMs     = 0.0;
static double sSubmitDrawMs    = 0.0;
static uint32_t sSubmitDraws   = 0;
static uint64_t sSubmitVerts   = 0;
static uint32_t sSubmitPrims   = 0;  // GX primitives, i.e. draws before batching
// Display-list parse failures this frame. The individual messages report once
// and then go quiet, which hides whether a fault happened at startup or is
// recurring every frame -- the difference between a cosmetic warning and a
// parser that is desynced and drawing garbage.
static uint32_t sDlDesyncs   = 0;
static uint32_t sBadMtxIdx   = 0;
static uint32_t sWildVerts   = 0;
// Captured at the position fetch so a wild vertex can say which of the three
// inputs to `base + index * stride` was wrong.
static u16      sLastPosIndex  = 0;
static const u8* sLastPosBase  = nullptr;
static u8       sLastPosStride = 0;

// The one-shot message said a desync happened but not which list, where in it,
// or what the bytes look like -- which is the difference between "the game
// handed us a buffer that was already garbage" and "the parser drifted part
// way through a valid list". Reports the first few with context, then stops.
static u8 attr_inline_size(GXAttr attr, const VertexFormatState& fmt);
static int sDesyncReports = 0;
static void report_desync(const char* what, const void* list, size_t offset, u32 nbytes) {
    if (sDesyncReports >= 8) return;
    ++sDesyncReports;
    const u8* base = static_cast<const u8*>(list);
    fprintf(stderr, "[PC GX] DESYNC #%d %s at byte %zu/%u  head:", sDesyncReports, what, offset, nbytes);
    for (size_t i = 0; i < 12 && i < nbytes; ++i) fprintf(stderr, " %02X", base[i]);
    fprintf(stderr, "  near:");
    const size_t from = offset > 6 ? offset - 6 : 0;
    for (size_t i = from; i < from + 12 && i < nbytes; ++i) {
        fprintf(stderr, "%s%02X", i == offset ? " >" : " ", base[i]);
    }
    fprintf(stderr, "\n");

    // The stride is what actually went wrong: the head of every list parses,
    // so the parser is consuming the wrong number of bytes per vertex and
    // landing mid-data. Print the descriptor that produced it.
    fprintf(stderr, "[PC GX] DESYNC #%d vtxdesc:", sDesyncReports);
    unsigned stride = 0;
    for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7; ++a) {
        const GXAttrType d = sVtxDesc[a];
        if (d == GX_NONE) continue;
        const char* kind = d == GX_DIRECT ? "direct" : d == GX_INDEX8 ? "idx8"
                         : d == GX_INDEX16 ? "idx16" : "?";
        unsigned bytes = d == GX_DIRECT
                             ? attr_inline_size(static_cast<GXAttr>(a), sVtxFormats[0][a])
                             : (d == GX_INDEX8 ? 1u : 2u);
        stride += bytes;
        fprintf(stderr, " a%d=%s(%u)", a, kind, bytes);
    }
    fprintf(stderr, "  stride=%u bytes\n", stride);
}

// Consecutive draws that share every piece of state a batch would have to
// share. sRunKey is the previous draw's key; a run ends when it differs.
static uint64_t sRunKey        = 0;
static bool     sHaveRunKey    = false;
static uint32_t sRunCurrent    = 0;  // draws in the run being built
static uint32_t sRunLongest    = 0;
static uint32_t sRunCount      = 0;  // runs closed this frame
static uint32_t sRunGlBreaks   = 0;  // of which, broken by GL pipeline state
static uint64_t sRunEpoch      = 0;  // GL epoch the current run was opened at

// Eight bytes per round rather than one. This runs for every GX primitive --
// 25.000 times in a heavy frame -- because the batcher, not just the profiler,
// depends on it: a byte-at-a-time FNV over the ~2 KB of live state measured
// 42 ms/frame and became the bottleneck it was meant to remove.
static inline uint64_t hash_bytes(uint64_t h, const void* data, size_t bytes) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    while (bytes >= 8) {
        uint64_t word;
        memcpy(&word, p, 8);
        h = (h ^ word) * 1099511628211ull;
        h ^= h >> 29;
        p += 8;
        bytes -= 8;
    }
    if (bytes) {
        uint64_t tail = 0;
        memcpy(&tail, p, bytes);
        h = (h ^ tail) * 1099511628211ull;
        h ^= h >> 29;
    }
    return h;
}

// Everything the uniform block below reads, plus the bound textures and the
// primitive class. Deliberately over-inclusive: a key that is too coarse would
// report runs that a real batcher could not merge, which is the one error that
// would make this measurement worse than useless.
static uint64_t compute_batch_state_key() {
    uint64_t h = 14695981039346656037ull;
    h = hash_bytes(h, &sCurrentProgram, sizeof(sCurrentProgram));
    h = hash_bytes(h, &sProjMtxGen, sizeof(sProjMtxGen));
    h = hash_bytes(h, &sVerticesPretransformed, sizeof(sVerticesPretransformed));
    // Only the matrices actually selected: hashing all 64 slots would report a
    // break every time an unrelated model loaded its own matrix.
    if (!sVerticesPretransformed) {
        const u32 id = sCurrentPosMtxId < 64 ? sCurrentPosMtxId : 0;
        const uint64_t mtxRevision = (uint64_t(id) << 40) ^ (uint64_t(sPosMtxGen[id]) << 20)
                                   ^ uint64_t(sNrmMtxGen[id]);
        h = hash_bytes(h, &mtxRevision, sizeof(mtxRevision));
    }
    h = hash_bytes(h, sChannels, sizeof(sChannels));
    // Only the lights the enabled channels actually select: hashing all eight
    // would report a break whenever an unrelated, unreferenced light moved.
    for (int ch = 0; ch < 2; ++ch) {
        if (!sChannels[ch].enabled) continue;
        const u32 mask = sChannels[ch].lightMask;
        for (int i = 0; i < 8; ++i) {
            if (mask & (1u << i)) h = hash_bytes(h, &sLights[i], sizeof(sLights[i]));
        }
    }
    h = hash_bytes(h, &sAlphaComp0, sizeof(sAlphaComp0));
    h = hash_bytes(h, &sAlphaComp1, sizeof(sAlphaComp1));
    h = hash_bytes(h, &sAlphaOp, sizeof(sAlphaOp));
    h = hash_bytes(h, &sAlphaRef0, sizeof(sAlphaRef0));
    h = hash_bytes(h, &sAlphaRef1, sizeof(sAlphaRef1));
    h = hash_bytes(h, sTevRegisters, sizeof(sTevRegisters));
    h = hash_bytes(h, sKonstColors, sizeof(sKonstColors));
    h = hash_bytes(h, &sNumTevStages, sizeof(sNumTevStages));
    const int stages = sNumTevStages < GX_MAXTEVSTAGE ? sNumTevStages : GX_MAXTEVSTAGE;
    for (int i = 0; i < stages; ++i) {
        h = hash_bytes(h, &sTevStages[i], sizeof(sTevStages[i]));
        h = hash_bytes(h, &sTevRasSwapSel[i], sizeof(sTevRasSwapSel[i]));
        h = hash_bytes(h, &sTevTexSwapSel[i], sizeof(sTevTexSwapSel[i]));
    }
    h = hash_bytes(h, sTevSwapModes, sizeof(sTevSwapModes));
    for (int slot = 0; slot < 8; ++slot) {
        h = hash_bytes(h, &sTexCoordGen[slot], sizeof(sTexCoordGen[slot]));
        if (sTexCoordGen[slot].active) {
            const u32 m = sTexCoordGen[slot].mtxIdx < 64 ? sTexCoordGen[slot].mtxIdx : 0;
            const uint64_t texRevision = (uint64_t(m) << 32) ^ uint64_t(sTexMtxGen[m]);
            h = hash_bytes(h, &texRevision, sizeof(texRevision));
        }
    }
    h = hash_bytes(h, sActiveGLTextures, sizeof(sActiveGLTextures));
    h = hash_bytes(h, sHasActiveTextures, sizeof(sHasActiveTextures));
    // Triangles, strips, fans and quads can all become GL_TRIANGLES and so may
    // share a batch; lines and points cannot mix with them or each other.
    int primClass = 0;
    switch (sCurrentPrimType) {
        case GX_LINES: case GX_LINESTRIP: primClass = 1; break;
        case GX_POINTS:                   primClass = 2; break;
        default:                          primClass = 0; break;
    }
    h = hash_bytes(h, &primClass, sizeof(primClass));
    return h;
}

// Called once per draw while PIKMIN_TICK_STATS is on. Splits run breaks by
// cause: a break that a batcher could never avoid (GL pipeline state changed
// outside our reach) counts differently from one caused by material state.
static inline double submit_clock_ms();

// ── Primitive batching (PERF-NATIVE-002) ────────────────────────────────────
//
// The port used to emit one glDrawArrays per GX primitive: ~8.000 draws of ten
// vertices per frame, each preceded by the whole uniform block. Measurement
// (see SIGUIENTE_SESION.md) showed consecutive primitives share state in runs
// of ~20, and 60-80 in the heavy frames, so the draws collapse to a few
// hundred.
//
// The scheme rests on one invariant, and every flush point below exists to
// keep it: BETWEEN OPENING A BATCH AND DRAWING IT, NOTHING MAY TOUCH GL STATE
// OR UNIFORMS. The uniforms for a batch are written once, when it opens; the
// draw happens later, and reads whatever the GPU still holds. So a batch must
// be flushed before any GL mutation, and drawn *before* the next batch's
// uniforms are written. Material state needs no flush of its own: it is
// covered by the state key, which is compared on every primitive.
static std::vector<Vertex> sBatchVerts;
static bool     sBatchOpen = false;
static uint64_t sBatchKey  = 0;
static GLenum   sBatchMode = GL_TRIANGLES;
static uint32_t sBatchPrims = 0;   // primitives merged into the open batch

// Everything becomes GL_TRIANGLES (or GL_LINES) so that consecutive primitives
// of different topologies can still share one draw. Order is preserved
// exactly, and so is winding: a strip's odd triangles keep the swap that
// GL_TRIANGLE_STRIP would have applied, or every other face would flip.
static void append_primitive_to_batch() {
    const std::vector<Vertex>& v = sVertexStream;
    const size_t n = v.size();
    switch (sCurrentPrimType) {
    case GX_TRIANGLES:
        sBatchVerts.insert(sBatchVerts.end(), v.begin(), v.end());
        break;
    case GX_TRIANGLESTRIP:
        for (size_t i = 2; i < n; ++i) {
            if ((i & 1) == 0) {
                sBatchVerts.push_back(v[i - 2]);
                sBatchVerts.push_back(v[i - 1]);
            } else {
                sBatchVerts.push_back(v[i - 1]);
                sBatchVerts.push_back(v[i - 2]);
            }
            sBatchVerts.push_back(v[i]);
        }
        break;
    case GX_TRIANGLEFAN:
        for (size_t i = 2; i < n; ++i) {
            sBatchVerts.push_back(v[0]);
            sBatchVerts.push_back(v[i - 1]);
            sBatchVerts.push_back(v[i]);
        }
        break;
    case GX_QUADS:
        for (size_t i = 0; i + 3 < n; i += 4) {
            sBatchVerts.push_back(v[i]);
            sBatchVerts.push_back(v[i + 1]);
            sBatchVerts.push_back(v[i + 2]);
            sBatchVerts.push_back(v[i]);
            sBatchVerts.push_back(v[i + 2]);
            sBatchVerts.push_back(v[i + 3]);
        }
        break;
    case GX_LINES:
        sBatchVerts.insert(sBatchVerts.end(), v.begin(), v.end());
        break;
    case GX_LINESTRIP:
        for (size_t i = 1; i < n; ++i) {
            sBatchVerts.push_back(v[i - 1]);
            sBatchVerts.push_back(v[i]);
        }
        break;
    case GX_POINTS:
        sBatchVerts.insert(sBatchVerts.end(), v.begin(), v.end());
        break;
    }
}

// PIKMIN_BATCH=0 draws every primitive on its own, exactly as before this
// change. It is the A/B for the failure mode this design can produce: a
// missed flush point corrupts pixels in a way that depends on scene content,
// and comparing the two modes in the same scene says in one step whether a
// visual defect belongs to batching or was already there.
static bool batching_enabled() {
    static const bool enabled = [] {
        const char* value = getenv("PIKMIN_BATCH");
        return !(value != nullptr && value[0] == '0');
    }();
    return enabled;
}

static GLenum batch_mode_for(GXPrimitive prim) {
    switch (prim) {
    case GX_LINES: case GX_LINESTRIP: return GL_LINES;
    case GX_POINTS:                   return GL_POINTS;
    default:                          return GL_TRIANGLES;
    }
}

// Draws whatever has accumulated. Safe to call at any time: a no-op when no
// batch is open, which is what makes it cheap to place at every flush point.
void pc_gfx_flush_batch(void) {
    if (!sBatchOpen || sBatchVerts.empty()) {
        sBatchOpen  = false;
        sBatchPrims = 0;
        sBatchVerts.clear();
        return;
    }

    const bool profiling = pc_tick_profiler_enabled();
    const double t0 = profiling ? submit_clock_ms() : 0.0;

    const size_t vertexBytes = sBatchVerts.size() * sizeof(Vertex);
    if (vertexBytes > sVboCapacity - sVboWriteOffset) {
        if (vertexBytes > sVboCapacity) {
            while (sVboCapacity < vertexBytes) sVboCapacity *= 2;
        }
        // Orphaning is safe here: draws already queued retain the old storage.
        glBufferData_ptr(GL_ARRAY_BUFFER, sVboCapacity, nullptr, GL_STREAM_DRAW);
        sVboWriteOffset = 0;
    }
    glBufferSubData_ptr(GL_ARRAY_BUFFER, sVboWriteOffset, vertexBytes, sBatchVerts.data());

    const double t1 = profiling ? submit_clock_ms() : 0.0;
    if (profiling) sSubmitVboMs += t1 - t0;

    gl_error_checkpoint("batch upload");
    const GLint firstVertex = static_cast<GLint>(sVboWriteOffset / sizeof(Vertex));
    glDrawArrays(sBatchMode, firstVertex, (GLsizei)sBatchVerts.size());
    gl_error_checkpoint("batch draw");

    if (profiling) {
        sSubmitDrawMs += submit_clock_ms() - t1;
        ++sSubmitDraws;
        sSubmitVerts += sBatchVerts.size();
    }
    ++sPerfDraws;
    sPerfVertices += sBatchVerts.size();
    if (sPerfCurrentScope >= 0) {
        ++sPerfScopes[sPerfCurrentScope].draws;
        sPerfScopes[sPerfCurrentScope].vertices += sBatchVerts.size();
    }
    sVboWriteOffset += vertexBytes;

    sBatchVerts.clear();
    sBatchOpen  = false;
    sBatchPrims = 0;
}

static void note_batch_run(uint64_t materialKey, uint64_t epoch) {
    const uint64_t key = hash_bytes(materialKey, &epoch, sizeof(epoch));
    if (sHaveRunKey && key == sRunKey) {
        ++sRunCurrent;
        return;
    }
    if (sHaveRunKey) {
        if (sRunCurrent > sRunLongest) sRunLongest = sRunCurrent;
        ++sRunCount;
        // Attribute the break: did the GL epoch move, or only the material?
        if (epoch != sRunEpoch) ++sRunGlBreaks;
    }
    sRunKey     = key;
    sRunEpoch   = epoch;
    sHaveRunKey = true;
    sRunCurrent = 1;
}

static inline double submit_clock_ms() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void pc_gfx_flush_submit_stats(void) {
    pc_gfx_flush_batch();
    ++sFrameSerial;
    if (!pc_tick_profiler_enabled()) return;
    pc_tick_profiler_record(kPcTickGfxUniforms, sSubmitUniformMs);
    pc_tick_profiler_record(kPcTickGfxVbo, sSubmitVboMs);
    pc_tick_profiler_record(kPcTickGfxDraw, sSubmitDrawMs);
    pc_tick_profiler_record(kPcTickGfxDrawCount, static_cast<double>(sSubmitDraws));
    pc_tick_profiler_record(kPcTickGfxPrimCount, static_cast<double>(sSubmitPrims));
    pc_tick_profiler_record(kPcTickGfxVertsPerDraw,
                            sSubmitDraws ? static_cast<double>(sSubmitVerts) / sSubmitDraws : 0.0);
    sSubmitUniformMs = sSubmitVboMs = sSubmitDrawMs = 0.0;
    // Close the run still open at the frame boundary so it is counted.
    if (sHaveRunKey) {
        if (sRunCurrent > sRunLongest) sRunLongest = sRunCurrent;
        ++sRunCount;
    }
    pc_tick_profiler_record(kPcTickGfxRunLength,
                            sRunCount ? static_cast<double>(sSubmitPrims) / sRunCount : 0.0);
    pc_tick_profiler_record(kPcTickGfxRunLongest, static_cast<double>(sRunLongest));
    pc_tick_profiler_record(kPcTickGfxGlBreakPct,
                            sRunCount ? 100.0 * sRunGlBreaks / sRunCount : 0.0);
    pc_tick_profiler_record(kPcTickGxDlDesync, static_cast<double>(sDlDesyncs));
    pc_tick_profiler_record(kPcTickGxBadMtxIdx, static_cast<double>(sBadMtxIdx));
    pc_tick_profiler_record(kPcTickGxWildVerts, static_cast<double>(sWildVerts));
    sWildVerts = 0;
    sDlDesyncs = 0;
    sBadMtxIdx = 0;
    sSubmitDraws     = 0;
    sSubmitVerts     = 0;
    sSubmitPrims     = 0;
    sHaveRunKey      = false;
    sRunCurrent      = 0;
    sRunLongest      = 0;
    sRunCount        = 0;
    sRunGlBreaks     = 0;
}

void pc_gfx_end(void) {
    if (sInPrimitive && sHaveVertex) {
        sVertexStream.push_back(sCurVertex);
        sHaveVertex = false;
    }
    if (!sInPrimitive || sVertexStream.empty()) {
        sInPrimitive = false;
        return;
    }

    // Capture immutable packet before emitting to OpenGL (disabled - use display list capture)
    // captureCurrentPacket();

    const bool profilingSubmit = pc_tick_profiler_enabled();
    const double submitT0      = profilingSubmit ? submit_clock_ms() : 0.0;

    // The state key decides everything: identical state means this primitive
    // joins the open batch and no GL call is made at all. The hash is charged
    // to gl:uniforms on purpose, so the uniform cost it replaces is never
    // flattered by moving work out of the measured span.
    const uint64_t stateKey  = compute_batch_state_key();
    const GLenum   batchMode = batch_mode_for(sCurrentPrimType);
    ++sSubmitPrims;
    if (profilingSubmit) note_batch_run(stateKey, sGlStateEpoch);

    if (sBatchOpen && batching_enabled() && stateKey == sBatchKey && batchMode == sBatchMode) {
        append_primitive_to_batch();
        ++sBatchPrims;
        sInPrimitive = false;
        sAttrStep = 0;
        if (profilingSubmit) sSubmitUniformMs += submit_clock_ms() - submitT0;
        return;
    }

    // Different state: draw what is queued while the GPU still holds the
    // uniforms it was built with, and only then reprogram for the new batch.
    pc_gfx_flush_batch();

    // Pick the program for this material first: every uniform below is written
    // through sLoc, which describes whichever program is now bound.
    use_program_for_current_state();

    glUniformMatrix4fv_ptr(sLoc.projMtx, 1, GL_FALSE, sProjMatrix);
    static const float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    glUniformMatrix4fv_ptr(sLoc.posMtx, 1, GL_FALSE,
                          sVerticesPretransformed ? identity : sPosMatrix[sCurrentPosMtxId]);
    glUniform4f_ptr(sLoc.materialColor, sChannels[0].matColor[0], sChannels[0].matColor[1],
                    sChannels[0].matColor[2], sChannels[0].matColor[3]);
    glUniform1i_ptr(sLoc.useMaterialRgb, sChannels[0].matSrc == GX_SRC_REG ? 1 : 0);
    glUniform1i_ptr(sLoc.useMaterialAlpha, sChannels[0].alphaMatSrc == GX_SRC_REG ? 1 : 0);
    glUniform1i_ptr(sLoc.alphaComp0, static_cast<int>(sAlphaComp0));
    glUniform1i_ptr(sLoc.alphaComp1, static_cast<int>(sAlphaComp1));
    glUniform1i_ptr(sLoc.alphaOp, static_cast<int>(sAlphaOp));
    glUniform1f_ptr(sLoc.alphaRef0, sAlphaRef0);
    glUniform1f_ptr(sLoc.alphaRef1, sAlphaRef1);
    glUniform4f_ptr(sLoc.tevPrev, sTevRegisters[GX_TEVPREV][0], sTevRegisters[GX_TEVPREV][1],
                   sTevRegisters[GX_TEVPREV][2], sTevRegisters[GX_TEVPREV][3]);
    glUniform4f_ptr(sLoc.tevReg0, sTevRegisters[GX_TEVREG0][0], sTevRegisters[GX_TEVREG0][1],
                   sTevRegisters[GX_TEVREG0][2], sTevRegisters[GX_TEVREG0][3]);
    glUniform4f_ptr(sLoc.tevReg1, sTevRegisters[GX_TEVREG1][0], sTevRegisters[GX_TEVREG1][1],
                   sTevRegisters[GX_TEVREG1][2], sTevRegisters[GX_TEVREG1][3]);
    glUniform4f_ptr(sLoc.tevReg2, sTevRegisters[GX_TEVREG2][0], sTevRegisters[GX_TEVREG2][1],
                   sTevRegisters[GX_TEVREG2][2], sTevRegisters[GX_TEVREG2][3]);
    for (int i = 0; i < 4; i++) {
        glUniform4f_ptr(sLoc.konst[i], sKonstColors[i][0], sKonstColors[i][1],
                        sKonstColors[i][2], sKonstColors[i][3]);
    }
    glUniform1i_ptr(sLoc.numStages, sNumTevStages);
    int fastPath = 0;
    if (sNumTevStages == 1) {
        const TevStageState& st = sTevStages[0];
        const bool simpleOp = st.colorOp == GX_TEV_ADD && st.alphaOp == GX_TEV_ADD
            && st.colorBias == GX_TB_ZERO && st.alphaBias == GX_TB_ZERO
            && st.colorScale == GX_CS_SCALE_1 && st.alphaScale == GX_CS_SCALE_1
            && st.colorOutReg == GX_TEVPREV && st.alphaOutReg == GX_TEVPREV
            && sTevRasSwapSel[0] == GX_TEV_SWAP0 && sTevTexSwapSel[0] == GX_TEV_SWAP0;
        const bool directTexture = st.textureEnabled && st.texMap == GX_TEXMAP0
            && st.texCoord == GX_TEXCOORD0
            && ((st.colorIn[0] == GX_CC_ZERO && st.colorIn[1] == GX_CC_ZERO
                 && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_TEXC)
                || (st.colorIn[0] == GX_CC_TEXC && st.colorIn[1] == GX_CC_ZERO
                    && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_ZERO))
            && ((st.alphaIn[0] == GX_CA_ZERO && st.alphaIn[1] == GX_CA_ZERO
                 && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_TEXA)
                || (st.alphaIn[0] == GX_CA_TEXA && st.alphaIn[1] == GX_CA_ZERO
                    && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_ZERO));
        const bool rasterInD = st.colorIn[0] == GX_CC_ZERO && st.colorIn[1] == GX_CC_ZERO
            && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_RASC
            && st.alphaIn[0] == GX_CA_ZERO && st.alphaIn[1] == GX_CA_ZERO
            && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_RASA;
        // Some original display lists encode the same pass-through as
        // A=RASC, B=C=D=0. With add/bias-zero/scale-one the TEV equation is
        // still exactly RASC (and RASA for alpha).
        const bool rasterInA = st.colorIn[0] == GX_CC_RASC && st.colorIn[1] == GX_CC_ZERO
            && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_ZERO
            && st.alphaIn[0] == GX_CA_RASA && st.alphaIn[1] == GX_CA_ZERO
            && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_ZERO;
        const bool directRaster = !st.textureEnabled && (rasterInD || rasterInA);
        // GX_MODULATE, heavily used by UI glyphs, particles and simple model
        // materials. It is exactly texture * raster for both color and alpha;
        // running the generic TEV loop for every covered pixel is unnecessary.
        const bool modulateTextureRaster = st.textureEnabled && st.texMap == GX_TEXMAP0
            && st.texCoord == GX_TEXCOORD0
            && st.colorIn[0] == GX_CC_ZERO && st.colorIn[1] == GX_CC_TEXC
            && st.colorIn[2] == GX_CC_RASC && st.colorIn[3] == GX_CC_ZERO
            && st.alphaIn[0] == GX_CA_ZERO && st.alphaIn[1] == GX_CA_TEXA
            && st.alphaIn[2] == GX_CA_RASA && st.alphaIn[3] == GX_CA_ZERO;
        if (simpleOp && directTexture) fastPath = 1;
        else if (simpleOp && directRaster) fastPath = 2;
        else if (simpleOp && modulateTextureRaster) fastPath = 3;
    }
    glUniform1i_ptr(sLoc.fastPath, fastPath);
    if (fastPath != 0) {
        ++sPerfFastDraws;
        ++sPerfFastPathDraws[fastPath];
        sPerfFastPathVertices[fastPath] += sVertexStream.size();
    }
    if (sPerfStatsEnabled) {
        sPerfTevStageDraws[std::min<int>(sNumTevStages, GX_MAXTEVSTAGE)]++;
        // One bounded snapshot of each title-scene pipeline is substantially
        // more useful than logging thousands of identical draws. It exposes
        // where CPREV/A1 are produced while keeping the hot path allocation-
        // free and the log small.
        PerfScope* pipelineScope = sPerfCurrentScope >= 0 ? &sPerfScopes[sPerfCurrentScope] : nullptr;
        u8* pipelineReportCount = !pipelineScope ? nullptr
                                : sNumTevStages == 5 ? &pipelineScope->pipeline5ReportCount
                                : sNumTevStages == 7 ? &pipelineScope->pipeline7ReportCount : nullptr;
        if (pipelineReportCount && *pipelineReportCount < 4) {
            const unsigned pipelineIndex = unsigned((*pipelineReportCount)++);
            if (pipelineIndex == 0) {
                fprintf(stderr, "[PERF TEVPIPE%u %s] regs", unsigned(sNumTevStages), pipelineScope->name);
                for (int reg = 0; reg < 4; ++reg) {
                    fprintf(stderr, " r%d=(%.3f,%.3f,%.3f,%.3f)", reg,
                            sTevRegisters[reg][0], sTevRegisters[reg][1],
                            sTevRegisters[reg][2], sTevRegisters[reg][3]);
                }
                fprintf(stderr, "\n");
                for (u8 stage = 0; stage < sNumTevStages; ++stage) {
                    const TevStageState& pipe = sTevStages[stage];
                    fprintf(stderr,
                            "[PERF TEVPIPE%u %s] s%u C=%d,%d,%d,%d->%d A=%d,%d,%d,%d->%d tex=%d/%d/%d ras=%d op=%d/%d bias=%d/%d scale=%d/%d clamp=%d/%d\n",
                            unsigned(sNumTevStages), pipelineScope->name, unsigned(stage),
                            int(pipe.colorIn[0]), int(pipe.colorIn[1]), int(pipe.colorIn[2]), int(pipe.colorIn[3]), int(pipe.colorOutReg),
                            int(pipe.alphaIn[0]), int(pipe.alphaIn[1]), int(pipe.alphaIn[2]), int(pipe.alphaIn[3]), int(pipe.alphaOutReg),
                            pipe.textureEnabled ? 1 : 0, int(pipe.texMap), int(pipe.texCoord), pipe.rasChannel,
                            int(pipe.colorOp), int(pipe.alphaOp), int(pipe.colorBias), int(pipe.alphaBias),
                            int(pipe.colorScale), int(pipe.alphaScale), int(pipe.colorClamp), int(pipe.alphaClamp));
                }
            }

            // Identify what this pipeline actually covers before attributing
            // a visual defect to it. Report projected NDC bounds and the UV
            // ranges consumed by its active texture coordinates. This is
            // diagnostic-only and does not alter material state or pixels.
            float ndcMinX = 1.0e30f, ndcMinY = 1.0e30f;
            float ndcMaxX = -1.0e30f, ndcMaxY = -1.0e30f;
            float uvMin[4][2];
            float uvMax[4][2];
            for (int tc = 0; tc < 4; ++tc) {
                uvMin[tc][0] = uvMin[tc][1] = 1.0e30f;
                uvMax[tc][0] = uvMax[tc][1] = -1.0e30f;
            }
            const float* model = sVerticesPretransformed ? identity : sPosMatrix[sCurrentPosMtxId];
            for (const Vertex& vertex : sVertexStream) {
                const float mx = model[0] * vertex.x + model[4] * vertex.y + model[8] * vertex.z + model[12];
                const float my = model[1] * vertex.x + model[5] * vertex.y + model[9] * vertex.z + model[13];
                const float mz = model[2] * vertex.x + model[6] * vertex.y + model[10] * vertex.z + model[14];
                const float mw = model[3] * vertex.x + model[7] * vertex.y + model[11] * vertex.z + model[15];
                const float cx = sProjMatrix[0] * mx + sProjMatrix[4] * my + sProjMatrix[8] * mz + sProjMatrix[12] * mw;
                const float cy = sProjMatrix[1] * mx + sProjMatrix[5] * my + sProjMatrix[9] * mz + sProjMatrix[13] * mw;
                const float cw = sProjMatrix[3] * mx + sProjMatrix[7] * my + sProjMatrix[11] * mz + sProjMatrix[15] * mw;
                if (fabsf(cw) > 1.0e-8f) {
                    const float nx = cx / cw;
                    const float ny = cy / cw;
                    ndcMinX = std::min(ndcMinX, nx);
                    ndcMaxX = std::max(ndcMaxX, nx);
                    ndcMinY = std::min(ndcMinY, ny);
                    ndcMaxY = std::max(ndcMaxY, ny);
                }
                for (int tc = 0; tc < 4; ++tc) {
                    uvMin[tc][0] = std::min(uvMin[tc][0], vertex.tex[tc][0]);
                    uvMax[tc][0] = std::max(uvMax[tc][0], vertex.tex[tc][0]);
                    uvMin[tc][1] = std::min(uvMin[tc][1], vertex.tex[tc][1]);
                    uvMax[tc][1] = std::max(uvMax[tc][1], vertex.tex[tc][1]);
                }
            }
            fprintf(stderr, "[PERF TEVGEOM%u %s #%u] verts=%zu ndc=(%.3f,%.3f)-(%.3f,%.3f)",
                    unsigned(sNumTevStages), pipelineScope->name, pipelineIndex, sVertexStream.size(),
                    ndcMinX, ndcMinY, ndcMaxX, ndcMaxY);
            bool reportedTc[4] = {};
            for (u8 stage = 0; stage < sNumTevStages; ++stage) {
                const int tc = int(sTevStages[stage].texCoord);
                if (sTevStages[stage].textureEnabled && tc >= 0 && tc < 4 && !reportedTc[tc]) {
                    reportedTc[tc] = true;
                    fprintf(stderr, " uv%d=(%.3f,%.3f)-(%.3f,%.3f)", tc,
                            uvMin[tc][0], uvMin[tc][1], uvMax[tc][0], uvMax[tc][1]);
                }
            }
            fprintf(stderr, "\n");
        }
        if (sNumTevStages == 1) {
            const TevStageState& st = sTevStages[0];
            uint64_t key = uint64_t(st.colorIn[0] & 15)
                | (uint64_t(st.colorIn[1] & 15) << 4)
                | (uint64_t(st.colorIn[2] & 15) << 8)
                | (uint64_t(st.colorIn[3] & 15) << 12)
                | (uint64_t(st.alphaIn[0] & 7) << 16)
                | (uint64_t(st.alphaIn[1] & 7) << 19)
                | (uint64_t(st.alphaIn[2] & 7) << 22)
                | (uint64_t(st.alphaIn[3] & 7) << 25)
                | (uint64_t((int(st.texMap) + 1) & 15) << 28)
                | (uint64_t((int(st.texCoord) + 1) & 15) << 32)
                | (uint64_t((st.rasChannel + 1) & 3) << 36)
                | (uint64_t(st.colorOp & 15) << 38)
                | (uint64_t(st.alphaOp & 15) << 42);
            uint64_t flags = uint64_t(st.colorBias & 3)
                | (uint64_t(st.alphaBias & 3) << 2)
                | (uint64_t(st.colorScale & 3) << 4)
                | (uint64_t(st.alphaScale & 3) << 6)
                | (uint64_t(st.colorClamp ? 1 : 0) << 8)
                | (uint64_t(st.alphaClamp ? 1 : 0) << 9)
                | (uint64_t(st.colorOutReg & 3) << 10)
                | (uint64_t(st.alphaOutReg & 3) << 12);
            key |= flags << 46;
            size_t slot = size_t((key ^ (key >> 33) ^ (key >> 17)) & 127);
            for (size_t probe = 0; probe < 128; ++probe) {
                PerfTevPattern& entry = sPerfTevPatterns[(slot + probe) & 127];
                if (entry.count == 0 || entry.key == key) {
                    entry.key = key;
                    entry.count++;
                    break;
                }
            }
        } else if (sNumTevStages > 1) {
            // The final stage must produce TEVPREV, which is the fragment
            // shader output. Grouping its signature exposes broken multi-stage
            // materials without logging every draw or tying diagnostics to a
            // particular model.
            const TevStageState& st = sTevStages[std::min<int>(sNumTevStages, GX_MAXTEVSTAGE) - 1];
            uint64_t key = uint64_t(st.colorIn[0] & 15)
                | (uint64_t(st.colorIn[1] & 15) << 4)
                | (uint64_t(st.colorIn[2] & 15) << 8)
                | (uint64_t(st.colorIn[3] & 15) << 12)
                | (uint64_t(st.alphaIn[0] & 7) << 16)
                | (uint64_t(st.alphaIn[1] & 7) << 19)
                | (uint64_t(st.alphaIn[2] & 7) << 22)
                | (uint64_t(st.alphaIn[3] & 7) << 25)
                | (uint64_t((int(st.texMap) + 1) & 15) << 28)
                | (uint64_t((int(st.texCoord) + 1) & 15) << 32)
                | (uint64_t((st.rasChannel + 1) & 3) << 36)
                | (uint64_t(st.colorOp & 15) << 38)
                | (uint64_t(st.alphaOp & 15) << 42);
            uint64_t flags = uint64_t(st.colorBias & 3)
                | (uint64_t(st.alphaBias & 3) << 2)
                | (uint64_t(st.colorScale & 3) << 4)
                | (uint64_t(st.alphaScale & 3) << 6)
                | (uint64_t(st.colorClamp ? 1 : 0) << 8)
                | (uint64_t(st.alphaClamp ? 1 : 0) << 9)
                | (uint64_t(st.colorOutReg & 3) << 10)
                | (uint64_t(st.alphaOutReg & 3) << 12);
            key |= flags << 46;
            const u8 stages = std::min<int>(sNumTevStages, GX_MAXTEVSTAGE);
            size_t slot = size_t((key ^ (key >> 33) ^ (key >> 17) ^ stages) & 127);
            for (size_t probe = 0; probe < 128; ++probe) {
                PerfTevMultiPattern& entry = sPerfTevMultiPatterns[(slot + probe) & 127];
                if (entry.count == 0 || (entry.key == key && entry.stages == stages)) {
                    entry.key = key;
                    entry.stages = stages;
                    entry.count++;
                    break;
                }
            }
        }
    }
    for (u8 stage = 0; stage < sNumTevStages && stage < GX_MAXTEVSTAGE; ++stage) {
        const TevStageState& st = sTevStages[stage];
        float konst[4];
        resolve_tev_konst(stage, konst);
        glUniform4f_ptr(sLoc.tevKonst[stage], konst[0], konst[1], konst[2], konst[3]);
        glUniform4i_ptr(sLoc.tevCSel[stage], st.colorIn[0], st.colorIn[1], st.colorIn[2], st.colorIn[3]);
        glUniform4i_ptr(sLoc.tevASel[stage], st.alphaIn[0], st.alphaIn[1], st.alphaIn[2], st.alphaIn[3]);
        int texIdx = (st.textureEnabled && st.texMap >= GX_TEXMAP0 && st.texMap < GX_MAX_TEXMAP)
                         ? static_cast<int>(st.texMap) : -1;
        glUniform4i_ptr(sLoc.tevTexInfo[stage], texIdx, st.texCoord, 0, 0);
        const int packedColorOp = static_cast<int>(st.colorOp) | (st.colorClamp ? 0x100 : 0);
        const int packedAlphaOp = static_cast<int>(st.alphaOp) | (st.alphaClamp ? 0x100 : 0);
        glUniform4i_ptr(sLoc.tevCOps[stage], packedColorOp, st.colorBias, st.colorScale, st.colorOutReg);
        glUniform4i_ptr(sLoc.tevAOps[stage], packedAlphaOp, st.alphaBias, st.alphaScale, st.alphaOutReg);
        if (sLoc.tevChan[stage] >= 0) {
            // GX_COLOR1/GX_COLOR1A1 stages sample the specular channel's raster.
            glUniform4i_ptr(sLoc.tevChan[stage], st.rasChannel, 0, 0, 0);
        }
        // TEV swap mode
        if (sLoc.tevSwapSel[stage] >= 0) {
            int rasSel = (int)sTevRasSwapSel[stage];
            int texSel = (int)sTevTexSwapSel[stage];
            glUniform2i_ptr(sLoc.tevSwapSel[stage], rasSel, texSel);
        }
    }
    // TEV swap mode tables
    for (int t = 0; t < 4; ++t) {
        if (sLoc.tevSwapTable[t] >= 0) {
            glUniform4i_ptr(sLoc.tevSwapTable[t],
                (int)sTevSwapModes[t].red,
                (int)sTevSwapModes[t].green,
                (int)sTevSwapModes[t].blue,
                (int)sTevSwapModes[t].alpha);
        }
    }

    // Only touch texture units referenced by an active TEV stage. Most Pikmin
    // materials use one map; rebinding all eight for every tiny primitive was
    // a substantial driver overhead. Sampler locations are fixed at init.
    bool usedTextureMaps[8] = {};
    for (u8 stage = 0; stage < sNumTevStages && stage < GX_MAXTEVSTAGE; ++stage) {
        const TevStageState& st = sTevStages[stage];
        if (st.textureEnabled && st.texMap >= GX_TEXMAP0 && st.texMap < 8) {
            usedTextureMaps[static_cast<int>(st.texMap)] = true;
        }
    }
    int lastActiveMap = 0;
    for (int map = 0; map < 8; ++map) {
        if (!usedTextureMaps[map]) continue;
        const GLuint wanted = (sHasActiveTextures[map] ? sActiveGLTextures[map] : 0);
        if (sBoundTextures[map] != wanted) {
            glActiveTexture_ptr(GL_TEXTURE0 + map);
            glBindTexture(GL_TEXTURE_2D, wanted);
            sBoundTextures[map] = wanted;
            lastActiveMap = map;
        }
    }
    if (lastActiveMap != 0) glActiveTexture_ptr(GL_TEXTURE0);

    // Upload lighting
    if (sLoc.nrmMtx >= 0) {
        static const float identityNrm[9] = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };
        const float* nrmMtx = sVerticesPretransformed
            ? identityNrm
            : sNrmMatrix[sCurrentPosMtxId < 64 ? sCurrentPosMtxId : 0];
        glUniformMatrix3fv_ptr(sLoc.nrmMtx, 1, GL_FALSE, nrmMtx);
    }
    int numLights = 0;
    float ambR = 1.0f, ambG = 1.0f, ambB = 1.0f, ambA = 1.0f;
    if (sChannels[0].enabled) {
        // Only lit channels contribute lighting; a disabled channel must pass
        // rasterized colors through untouched (matches GX hardware behavior).
        ambR = sChannels[0].ambColor[0];
        ambG = sChannels[0].ambColor[1];
        ambB = sChannels[0].ambColor[2];
        ambA = sChannels[0].ambColor[3];
        u32 mask = sChannels[0].lightMask;
        for (int i = 0; i < 8 && numLights < 4; i++) {
            if (mask & (1u << i)) {
                glUniform4f_ptr(sLoc.lightPos[numLights], sLights[i].pos[0], sLights[i].pos[1], sLights[i].pos[2], 0.0f);
                glUniform4f_ptr(sLoc.lightColor[numLights], sLights[i].color[0], sLights[i].color[1], sLights[i].color[2], sLights[i].color[3]);
                // Distance attenuation applies when the channel's attention
                // function requests it (game uses GX_AF_SPOT with lights).
                const float* k = sLights[i].k;
                bool attnOn = (sChannels[0].attnFn != GX_AF_NONE);
                glUniform4f_ptr(sLoc.lightK[numLights], k[0], k[1], k[2], attnOn ? 1.0f : 0.0f);
                numLights++;
            }
        }
    }
    if (sLoc.numLights >= 0) glUniform1i_ptr(sLoc.numLights, numLights);
    if (sLoc.ambColor >= 0) glUniform4f_ptr(sLoc.ambColor, ambR, ambG, ambB, ambA);
    if (sLoc.chan0En >= 0) glUniform1i_ptr(sLoc.chan0En, sChannels[0].enabled ? 1 : 0);
    if (sLoc.chan0AttnFn >= 0) glUniform1i_ptr(sLoc.chan0AttnFn, (int)sChannels[0].attnFn);

    // Channel 1 (typically specular) feeds stages whose TEV order selects it.
    int numLights1 = 0;
    float a1r = 0.0f, a1g = 0.0f, a1b = 0.0f, a1a = 1.0f;
    if (sChannels[1].enabled) {
        a1r = sChannels[1].ambColor[0];
        a1g = sChannels[1].ambColor[1];
        a1b = sChannels[1].ambColor[2];
        a1a = sChannels[1].ambColor[3];
        u32 mask1 = sChannels[1].lightMask;
        for (int i = 0; i < 8 && numLights1 < 4; i++) {
            if (mask1 & (1u << i)) {
                glUniform4f_ptr(sLoc.lightPos1[numLights1], sLights[i].pos[0], sLights[i].pos[1], sLights[i].pos[2], 0.0f);
                glUniform4f_ptr(sLoc.lightColor1[numLights1], sLights[i].color[0], sLights[i].color[1], sLights[i].color[2], sLights[i].color[3]);
                const float* k1 = sLights[i].k;
                bool attnOn1 = (sChannels[1].attnFn != GX_AF_NONE);
                glUniform4f_ptr(sLoc.lightK1[numLights1], k1[0], k1[1], k1[2], attnOn1 ? 1.0f : 0.0f);
                numLights1++;
            }
        }
    }
    if (sLoc.numLights1 >= 0) glUniform1i_ptr(sLoc.numLights1, numLights1);
    if (sLoc.ambColor1 >= 0) glUniform4f_ptr(sLoc.ambColor1, a1r, a1g, a1b, a1a);
    if (sLoc.chan1En >= 0) glUniform1i_ptr(sLoc.chan1En, sChannels[1].enabled ? 1 : 0);
    if (sLoc.chan1AttnFn >= 0) glUniform1i_ptr(sLoc.chan1AttnFn, (int)sChannels[1].attnFn);
    // Specular half-vector: light 7's dir field (offset 0x34) holds it.
    if (sChannels[1].enabled && sChannels[1].attnFn == GX_AF_SPEC) {
        u32 mask1 = sChannels[1].lightMask;
        for (int i = 7; i < 8; i++) {
            if (mask1 & (1u << i) && sLights[i].active) {
                if (sLoc.specHalf1 >= 0) {
                    glUniform4f_ptr(sLoc.specHalf1, sLights[i].dir[0], sLights[i].dir[1], sLights[i].dir[2], 0.0f);
                }
                if (sLoc.specAttn1 >= 0) {
                    glUniform4f_ptr(sLoc.specAttn1, sLights[i].a[0], sLights[i].a[1], sLights[i].a[2], 0.0f);
                }
                
                break;
            }
        }
    }
    if (sLoc.materialColor1 >= 0)
        glUniform4f_ptr(sLoc.materialColor1, sChannels[1].matColor[0], sChannels[1].matColor[1],
                        sChannels[1].matColor[2], sChannels[1].matColor[3]);
    // Only present in programs built with fog in the key, so the locations are
    // -1 everywhere else and this costs nothing on the draws that have none.
    if (sLoc.fogParams >= 0)
        glUniform4f_ptr(sLoc.fogParams, sFogStart, sFogEnd, sFogNear, sFogFar);
    if (sLoc.fogColour >= 0)
        glUniform4f_ptr(sLoc.fogColour, sFogColour[0], sFogColour[1], sFogColour[2], 1.0f);
    if (sLoc.useMaterialRgb1 >= 0)
        glUniform1i_ptr(sLoc.useMaterialRgb1, sChannels[1].matSrc == GX_SRC_REG ? 1 : 0);

    // Texture coordinate generation for every GX slot. TEX0-TEX7 sources are
    // independent from the destination slot and may use different matrices.
    for (int slot = 0; slot < 8; slot++) {
        int mode = 0;
        if (slot < 8 && sTexCoordGen[slot].active) {
            GXTexGenSrc src = static_cast<GXTexGenSrc>(sTexCoordGen[slot].src);
            if (src == GX_TG_POS) mode = 1;
            else if (src == GX_TG_NRM) mode = 2;
            else if (src >= GX_TG_TEX0 && src <= GX_TG_TEX7)
                mode = 3 + int(src - GX_TG_TEX0);
            else if (src >= GX_TG_TEXCOORD0 && src <= GX_TG_TEXCOORD6)
                mode = 11 + int(src - GX_TG_TEXCOORD0);
        }
        if (sLoc.tcMode[slot] >= 0) glUniform1i_ptr(sLoc.tcMode[slot], mode);
        if (mode != 0 && sLoc.tcMtx[slot] >= 0) {
            u32 mtxIdx = (slot < 8) ? sTexCoordGen[slot].mtxIdx : 0;
            if (mtxIdx >= 64) mtxIdx = 0;
            glUniformMatrix4fv_ptr(sLoc.tcMtx[slot], 1, GL_FALSE, sTexMatrices[mtxIdx]);
        }
    }

    const double submitT1 = profilingSubmit ? submit_clock_ms() : 0.0;
    if (profilingSubmit) sSubmitUniformMs += submitT1 - submitT0;

    switch (sCurrentPrimType) {
        case GX_TRIANGLES: ++sPerfPrimitiveDraws[0]; break;
        case GX_TRIANGLESTRIP: ++sPerfPrimitiveDraws[1]; break;
        case GX_TRIANGLEFAN: ++sPerfPrimitiveDraws[2]; break;
        case GX_LINES: ++sPerfPrimitiveDraws[3]; break;
        case GX_LINESTRIP: ++sPerfPrimitiveDraws[4]; break;
        case GX_POINTS: ++sPerfPrimitiveDraws[5]; break;
        case GX_QUADS: ++sPerfPrimitiveDraws[6]; break;
    }

    gl_error_checkpoint("uniform upload");

    // Uniforms for this state are now on the GPU. Open the batch; the draw
    // happens at the next flush, with these same uniforms still bound.
    sBatchKey   = stateKey;
    sBatchMode  = batchMode;
    sBatchOpen  = true;
    sBatchPrims = 1;
    append_primitive_to_batch();
    if (!batching_enabled()) pc_gfx_flush_batch();

    static bool reportedFirstDraw = false;
    if (!reportedFirstDraw) {
        GLenum error = glGetError();
        printf("[PC Port] First GX draw: %zu vertices, GL status 0x%04x\n",
               sVertexStream.size(), static_cast<unsigned>(error));
        if (error != GL_NO_ERROR) {
            const char* errStr = "UNKNOWN";
            switch (error) {
                case GL_INVALID_ENUM: errStr = "GL_INVALID_ENUM"; break;
                case GL_INVALID_VALUE: errStr = "GL_INVALID_VALUE"; break;
                case GL_INVALID_OPERATION: errStr = "GL_INVALID_OPERATION"; break;
                case GL_STACK_OVERFLOW: errStr = "GL_STACK_OVERFLOW"; break;
                case GL_STACK_UNDERFLOW: errStr = "GL_STACK_UNDERFLOW"; break;
                case GL_OUT_OF_MEMORY: errStr = "GL_OUT_OF_MEMORY"; break;
                case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            }
            printf("[PC Port] GL Error: %s\n", errStr);
        }
        // Check shader program
        GLint linkStatus;
        glGetProgramiv_ptr(sShaderProgram, GL_LINK_STATUS, &linkStatus);
        if (!linkStatus) {
            char log[4096];
            if (glGetProgramInfoLog_ptr) {
                glGetProgramInfoLog_ptr(sShaderProgram, sizeof(log), NULL, log);
            }
            printf("[PC Port] Shader link error: %s\n", log);
        }
        // Check active uniforms
        GLint numUniforms = 0;
        glGetProgramiv_ptr(sShaderProgram, GL_ACTIVE_UNIFORMS, &numUniforms);
        printf("[PC Port] Active uniforms: %d\n", numUniforms);
        for (int i = 0; i < numUniforms; i++) {
            char name[256];
            GLsizei length;
            GLint size;
            GLenum type;
            if (glGetActiveUniform_ptr) {
                glGetActiveUniform_ptr(sShaderProgram, i, sizeof(name), &length, &size, &type, name);
            } else {
                name[0] = '\0';
            }
            printf("[PC Port] Uniform %d: %s (type 0x%04x)\n", i, name, type);
        }
        const Vertex& first = sVertexStream.front();
        printf("[PC Port] First vertex: (%.2f, %.2f, %.2f); projection scale=(%.4f, %.4f), offset=(%.4f, %.4f); view offset=(%.2f, %.2f, %.2f)\n",
               first.x, first.y, first.z, sProjMatrix[0], sProjMatrix[5],
               sProjMatrix[12], sProjMatrix[13], sPosMatrix[sCurrentPosMtxId][12],
               sPosMatrix[sCurrentPosMtxId][13], sPosMatrix[sCurrentPosMtxId][14]);
        reportedFirstDraw = true;
    }

    sInPrimitive = false;
    sAttrStep = 0;
}

static bool read_be_u16(const u8*& cursor, const u8* end, u16& value) {
    if (end - cursor < 2) return false;
    value = (u16(cursor[0]) << 8) | cursor[1];
    cursor += 2;
    return true;
}

static bool read_attribute_index(GXAttrType type, const u8*& cursor, const u8* end, u16& index) {
    if (type == GX_INDEX8) {
        if (cursor >= end) return false;
        index = *cursor++;
        return true;
    }
    if (type == GX_INDEX16) return read_be_u16(cursor, end, index);
    return false;
}

static inline float read_attr_float(const u8* ptr, GXCompType type, u8 frac) {
    if (type == GX_F32) {
        float f;
        memcpy(&f, ptr, sizeof(float));
        return f;
    } else if (type == GX_S16) {
        s16 val;
        memcpy(&val, ptr, sizeof(s16));
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    } else if (type == GX_U16) {
        u16 val;
        memcpy(&val, ptr, sizeof(u16));
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    } else if (type == GX_S8) {
        s8 val = static_cast<s8>(*ptr);
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    } else if (type == GX_U8) {
        u8 val = *ptr;
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    }
    return 0.0f;
}

static inline u8 get_comptype_size(GXCompType type) {
    if (type == GX_F32) return 4;
    if (type == GX_S16 || type == GX_U16) return 2;
    return 1;
}

static inline u8 get_color_size(GXCompType type) {
    if (type == GX_RGB565 || type == GX_RGBA4) return 2;
    if (type == GX_RGB8 || type == GX_RGBA6) return 3;
    return 4; // GX_RGBA8 / GX_RGBX8
}

static inline void read_attr_color(const u8* ptr, GXCompType type, u8& r, u8& g, u8& b, u8& a) {
    if (type == GX_RGBA8 || type == GX_RGBX8) {
        r = ptr[0]; g = ptr[1]; b = ptr[2]; a = ptr[3];
    } else if (type == GX_RGB8 || type == GX_RGBA6) {
        r = ptr[0]; g = ptr[1]; b = ptr[2]; a = 255;
    } else if (type == GX_RGB565) {
        u16 val;
        memcpy(&val, ptr, sizeof(u16));
        r = (val >> 11) & 0x1F; r = (r << 3) | (r >> 2);
        g = (val >> 5) & 0x3F;  g = (g << 2) | (g >> 4);
        b = val & 0x1F;         b = (b << 3) | (b >> 2);
        a = 255;
    } else if (type == GX_RGBA4) {
        u16 val;
        memcpy(&val, ptr, sizeof(u16));
        r = (val >> 12) & 0xF; r = (r << 4) | r;
        g = (val >> 8) & 0xF;  g = (g << 4) | g;
        b = (val >> 4) & 0xF;  b = (b << 4) | b;
        a = val & 0xF;         a = (a << 4) | a;
    } else {
        r = 255; g = 255; b = 255; a = 255;
    }
}

static void transform_position(u8 matrixId, float x, float y, float z,
                               float& outX, float& outY, float& outZ) {
    const float* m = sPosMatrix[matrixId < 64 ? matrixId : 0];
    outX = m[0] * x + m[4] * y + m[8] * z + m[12];
    outY = m[1] * x + m[5] * y + m[9] * z + m[13];
    outZ = m[2] * x + m[6] * y + m[10] * z + m[14];
}

static void transform_normal(u8 matrixId, float x, float y, float z,
                             float& outX, float& outY, float& outZ) {
    const float* m = sNrmMatrix[matrixId < 64 ? matrixId : 0];
    outX = m[0] * x + m[3] * y + m[6] * z;
    outY = m[1] * x + m[4] * y + m[7] * z;
    outZ = m[2] * x + m[5] * y + m[8] * z;
}

// Byte size of one vertex attribute when stored directly in the stream.
static u8 attr_inline_size(GXAttr attr, const VertexFormatState& fmt) {
    if (attr >= GX_VA_PNMTXIDX && attr <= GX_VA_TEX7MTXIDX) return 1;
    if (attr == GX_VA_POS) return fmt.count * get_comptype_size(fmt.type);
    if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
        if (fmt.count == GX_NRM_NBT) return 9 * get_comptype_size(fmt.type);
        return 3 * get_comptype_size(fmt.type); // XYZ and NBT3 both lead with 3 values
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) return get_color_size(fmt.type);
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        return (fmt.count == GX_TEX_ST ? 2 : 1) * get_comptype_size(fmt.type);
    }
    return 0;
}

// ── Embedded GP command stream handlers ──
// Display lists carry more than vertex streams: material state (BP registers),
// vertex descriptors (CP registers), matrices/lighting (XF registers) and even
// nested display lists travel inline. Interpreting them here reproduces how
// the game bakes per-material configuration before its geometry.

static bool read_be_u32(const u8*& cursor, const u8* end, u32& value) {
    if (end - cursor < 4) return false;
    value = (u32(cursor[0]) << 24) | (u32(cursor[1]) << 16) | (u32(cursor[2]) << 8) | cursor[3];
    cursor += 4;
    return true;
}

static inline float sext11_to_float(u32 v) {
    v &= 0x7FF;
    if (v & 0x400) v -= 0x800; // sign extend 11-bit
    return static_cast<float>(static_cast<s32>(v)) / 256.0f;
}

static inline float bits_to_float(u32 bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static void handle_bp_reg(u32 hex) {
    const u32 reg = (hex >> 24) & 0xFF;
    switch (reg) {
    case 0x41: { // PE color mode: blend/logic operation and write masks
        const bool blend = (hex & 0x1) != 0;
        const bool logic = ((hex >> 1) & 0x1) != 0;
        const bool subtract = ((hex >> 11) & 0x1) != 0;
        GXBlendMode mode = subtract ? GX_BM_SUBTRACT : logic ? GX_BM_LOGIC : blend ? GX_BM_BLEND : GX_BM_NONE;
        pc_gfx_set_blend_mode(mode, GXBlendFactor((hex >> 8) & 0x7),
                              GXBlendFactor((hex >> 5) & 0x7), GXLogicOp((hex >> 12) & 0xF));
        pc_gfx_set_color_update(GXBool((hex >> 3) & 0x1));
        pc_gfx_set_alpha_update(GXBool((hex >> 4) & 0x1));
        break;
    }
    case 0xE0 ... 0xE7: { // TEV/Konst colors: RA and BG word pairs
        // GXTevRegID is PREV=0, REG0=1, REG1=2, REG2=3, exactly matching
        // BP pairs E0/E1 through E6/E7. Bit 23 distinguishes 8-bit Konst
        // writes from signed 11-bit TEV-register writes.
        const u32 pairIdx = (reg - 0xE0) >> 1;
        const bool isRaWord = ((reg - 0xE0) & 1) == 0;
        static u32 pendingRa[4] = {};
        if (isRaWord) {
            pendingRa[pairIdx] = hex & 0xFFFFFF;
        } else {
            const u32 ra = pendingRa[pairIdx];
            const u32 bg = hex & 0xFFFFFF;
            if ((ra & 0x800000) != 0 || (bg & 0x800000) != 0) {
                float* dst = sKonstColors[pairIdx];
                dst[0] = float(ra & 0xFF) / 255.0f;
                dst[1] = float((bg >> 12) & 0xFF) / 255.0f;
                dst[2] = float(bg & 0xFF) / 255.0f;
                dst[3] = float((ra >> 12) & 0xFF) / 255.0f;
            } else {
                float* dst = sTevRegisters[pairIdx];
                dst[0] = sext11_to_float(ra & 0x7FF);
                dst[1] = sext11_to_float((bg >> 12) & 0x7FF);
                dst[2] = sext11_to_float(bg & 0x7FF);
                dst[3] = sext11_to_float((ra >> 12) & 0x7FF);
            }
#ifdef PC_GFX_TRACE
            {
                static FILE* tf = nullptr;
                if (!tf) tf = fopen("/tmp/opencode/tevreg.log", "w");
                if (tf) { fprintf(tf, "[BP ] pair%u RA=%06X BG=%06X kind=%s\n",
                                  pairIdx, ra, bg, (ra & 0x800000) ? "konst" : "tev"); fflush(tf); }
            }
#endif
        }
        break;
    }
    case 0x80 ... 0x87: { // TREF: TEV order, two stages per register
        const u32 base = reg - 0x80;
        const u32 s0 = base * 2, s1 = base * 2 + 1;
        if (s0 < GX_MAXTEVSTAGE) {
            sTevStages[s0].texMap = GXTexMapID(hex & 0x7);
            sTevStages[s0].texCoord = GXTexCoordID((hex >> 3) & 0x7);
            sTevStages[s0].textureEnabled = ((hex >> 6) & 1) != 0;
            const u32 ras = (hex >> 7) & 0x7;
            sTevStages[s0].rasChannel = ras == 7 ? -1 : ras == 1 ? 1 : 0;
        }
        if (s1 < GX_MAXTEVSTAGE) {
            sTevStages[s1].texMap = GXTexMapID((hex >> 12) & 0x7);
            sTevStages[s1].texCoord = GXTexCoordID((hex >> 15) & 0x7);
            sTevStages[s1].textureEnabled = ((hex >> 18) & 1) != 0;
            const u32 ras = (hex >> 19) & 0x7;
            sTevStages[s1].rasChannel = ras == 7 ? -1 : ras == 1 ? 1 : 0;
        }
        break;
    }
    case 0xC0 ... 0xDF: { // TEV combiner environment, color+alpha per stage
        const u32 idx = reg - 0xC0;
        const u32 stage = idx >> 1;
        if (stage >= GX_MAXTEVSTAGE) break;
        TevStageState& st = sTevStages[stage];
        if ((idx & 1) == 0) { // color combiner
            st.colorIn[0] = decode_tev_color_a(hex);
            st.colorIn[1] = decode_tev_color_b(hex);
            st.colorIn[2] = decode_tev_color_c(hex);
            st.colorIn[3] = decode_tev_color_d(hex);
            const u32 biasBits = (hex >> 16) & 0x3;
            const u32 opBit = (hex >> 18) & 0x1;
            const u32 scaleBits = (hex >> 20) & 0x3;
            st.colorOp = biasBits == 3 ? GXTevOp(8 + scaleBits * 2 + opBit)
                                       : (opBit ? GX_TEV_SUB : GX_TEV_ADD);
            st.colorBias = biasBits == 3 ? GX_TB_ZERO : GXTevBias(biasBits);
            st.colorClamp = GXBool((hex >> 19) & 0x1);
            st.colorScale = biasBits == 3 ? GX_CS_SCALE_1 : GXTevScale(scaleBits);
            st.colorOutReg = GXTevRegID((hex >> 22) & 0x3);
        } else { // alpha combiner
            // Bits 0..3 are the texture/raster swap selectors, not alpha inputs.
            sTevRasSwapSel[stage] = GXTevSwapSel(hex & 0x3);
            sTevTexSwapSel[stage] = GXTevSwapSel((hex >> 2) & 0x3);
            st.alphaIn[0] = decode_tev_alpha_a(hex);
            st.alphaIn[1] = decode_tev_alpha_b(hex);
            st.alphaIn[2] = decode_tev_alpha_c(hex);
            st.alphaIn[3] = decode_tev_alpha_d(hex);
            const u32 biasBits = (hex >> 16) & 0x3;
            const u32 opBit = (hex >> 18) & 0x1;
            const u32 scaleBits = (hex >> 20) & 0x3;
            st.alphaOp = biasBits == 3 ? GXTevOp(8 + scaleBits * 2 + opBit)
                                       : (opBit ? GX_TEV_SUB : GX_TEV_ADD);
            st.alphaBias = biasBits == 3 ? GX_TB_ZERO : GXTevBias(biasBits);
            st.alphaClamp = GXBool((hex >> 19) & 0x1);
            st.alphaScale = biasBits == 3 ? GX_CS_SCALE_1 : GXTevScale(scaleBits);
            st.alphaOutReg = GXTevRegID((hex >> 22) & 0x3);
        }
        break;
    }
    case 0xF6 ... 0xFD: { // KSEL: konst selection plus two channels of a swap table
        const u32 index = reg - 0xF6;
        const u32 evenStage = index * 2;
        if (evenStage < GX_MAXTEVSTAGE) {
            sKonstColorSel[evenStage] = GXTevKColorSel((hex >> 4) & 0x1F);
            sKonstAlphaSel[evenStage] = GXTevKAlphaSel((hex >> 9) & 0x1F);
        }
        if (evenStage + 1 < GX_MAXTEVSTAGE) {
            sKonstColorSel[evenStage + 1] = GXTevKColorSel((hex >> 14) & 0x1F);
            sKonstAlphaSel[evenStage + 1] = GXTevKAlphaSel((hex >> 19) & 0x1F);
        }
        TevSwapMode& table = sTevSwapModes[index >> 1];
        if ((index & 1) == 0) {
            table.red = GXTevColorChan(hex & 0x3);
            table.green = GXTevColorChan((hex >> 2) & 0x3);
        } else {
            table.blue = GXTevColorChan(hex & 0x3);
            table.alpha = GXTevColorChan((hex >> 2) & 0x3);
        }
        break;
    }
    case 0xF3: { // PE alpha compare
        pc_gfx_set_alpha_compare(GXCompare((hex >> 16) & 0x7), u8(hex & 0xFF),
                                 GXAlphaOp((hex >> 22) & 0x3),
                                 GXCompare((hex >> 19) & 0x7), u8((hex >> 8) & 0xFF));
        break;
    }
    case 0xFE: { // NumChans/NumTex/NumTev packed update
        sNumTevStages = u8((hex >> 10) & 0xF) + 1;
        break;
    }
    default:
        break;
    }
}

static void handle_xf_regs(u32 addrBase, u32 numWords, const u32* words) {
    // Display lists embed whole matrix loads. Position matrices live at
    // XF addresses 12*n (3x4 words each); normal matrices at 0x400 + 9*n
    // (3x3 words each). GX matrix ids are 3*n to match PNMTXIDX values.
    if (addrBase < 0x0180 && numWords == 12 && (addrBase % 12) == 0) {
        const u32 mtxId = addrBase / 4; // 12*n / 4 == 3*n
        if (mtxId < 64) {
            float* dst = sPosMatrix[mtxId];
            // GX stores rows sequentially; OpenGL consumes column-major.
            for (int col = 0; col < 4; ++col) {
                dst[col * 4 + 0] = bits_to_float(words[col]);
                dst[col * 4 + 1] = bits_to_float(words[col + 4]);
                dst[col * 4 + 2] = bits_to_float(words[col + 8]);
            }
            dst[3] = 0.0f; dst[7] = 0.0f; dst[11] = 0.0f; dst[15] = 1.0f;
        }
        return;
    }
    if (addrBase >= 0x0400 && addrBase < 0x0400 + 24 * 9 && numWords == 9 &&
        ((addrBase - 0x0400) % 9) == 0) {
        const u32 mtxId = ((addrBase - 0x0400) / 9) * 3;
        if (mtxId < 64) {
            float* d = sNrmMatrix[mtxId];
            d[0] = bits_to_float(words[0]); d[1] = bits_to_float(words[3]); d[2] = bits_to_float(words[6]);
            d[3] = bits_to_float(words[1]); d[4] = bits_to_float(words[4]); d[5] = bits_to_float(words[7]);
            d[6] = bits_to_float(words[2]); d[7] = bits_to_float(words[5]); d[8] = bits_to_float(words[8]);
        }
        return;
    }
    for (u32 w = 0; w < numWords; ++w) {
        const u32 addr = addrBase + w;
        const u32 hex = words[w];
        switch (addr) {
        case GX_XF_REG_AMBIENT0: case GX_XF_REG_AMBIENT1: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_AMBIENT0];
            ch.ambColor[0] = ((hex >> 24) & 0xFF) / 255.0f;
            ch.ambColor[1] = ((hex >> 16) & 0xFF) / 255.0f;
            ch.ambColor[2] = ((hex >> 8) & 0xFF) / 255.0f;
            ch.ambColor[3] = (hex & 0xFF) / 255.0f;
            break;
        }
        case GX_XF_REG_MATERIAL0: case GX_XF_REG_MATERIAL1: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_MATERIAL0];
            ch.matColor[0] = ((hex >> 24) & 0xFF) / 255.0f;
            ch.matColor[1] = ((hex >> 16) & 0xFF) / 255.0f;
            ch.matColor[2] = ((hex >> 8) & 0xFF) / 255.0f;
            ch.matColor[3] = (hex & 0xFF) / 255.0f;
            break;
        }
        case GX_XF_REG_COLOR0CNTRL: case GX_XF_REG_COLOR1CNTRL: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_COLOR0CNTRL];
            // GXSetChanCtrl packs these XF fields as follows: material source
            // in bit 0, enable in bit 1, ambient source in bit 6, diffuse in
            // bits 7-8, attenuation in bits 9-10 and the two halves of the
            // light mask in bits 2-5 / 11-14.  These display-list writes are
            // used heavily by model materials, so confusing bit 0 with bit 1
            // silently disabled lighting for any material using GX_SRC_REG.
            decode_xf_channel_control(ch, hex, false);
            break;
        }
        case GX_XF_REG_ALPHA0CNTRL: case GX_XF_REG_ALPHA1CNTRL: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_ALPHA0CNTRL];
            decode_xf_channel_control(ch, hex, true);
            break;
        }
        default:
            break;
        }
    }
}

void pc_gfx_call_display_list(const void* list, u32 nbytes) {
    // On its own switch rather than the tick profiler's: tying it to
    // PIKMIN_TICK_STATS would put the cost back exactly when measuring, which
    // is the one time it must not be there. Shares the switch with the vertex
    // descriptor history, which exists only to be printed by this report.
    const bool profilingWildVerts = pc_gfx_gx_diagnostics_enabled();
    static bool reportedUnsupportedCommand = false;
    static bool reportedMalformedVertex = false;
    static bool reportedInvalidMatrix = false;
    if (!list || nbytes == 0) return;

    // Capture display list if capture is active
    if (sCaptureActive) {
        sPacketStore.captureDisplayList(list, nbytes);
    }

    ++sPerfDisplayLists;
    sPerfDisplayListBytes += nbytes;
    const u8* cursor = static_cast<const u8*>(list);
    const u8* end = cursor + nbytes;
    bool pendingTriangleStrip = false;
    auto flushPendingStrip = [&]() {
        if (pendingTriangleStrip) {
            pc_gfx_end();
            pendingTriangleStrip = false;
        }
    };

    while (cursor < end) {
        const u8 command = *cursor++;
        if (command == 0) continue; // 32-byte display-list padding.
        if (command == GX_CMD_NOP) continue;

        // ── Non-draw GP commands: interpret and keep parsing ──
        if (command == GX_CMD_LOAD_BP_REG) {
            flushPendingStrip();
            u32 hex;
            if (!read_be_u32(cursor, end, hex)) break;
            handle_bp_reg(hex);
            continue;
        }
        if (command == GX_CMD_LOAD_XF_REG) {
            flushPendingStrip();
            u32 header;
            if (!read_be_u32(cursor, end, header)) break;
            const u32 numWords = (header >> 16) & 0xFFFF;
            const u32 addrBase = header & 0xFFFF;
#ifdef PC_GFX_TRACE
            {
                static FILE* xf = nullptr;
                if (!xf) xf = fopen("/tmp/opencode/xfdl.log", "w");
                if (xf) {
                    fprintf(xf, "XF num=%u addr=%04X w0=%08X w1=%08X\n", numWords, addrBase,
                            cursor[0] << 24 | cursor[1] << 16 | cursor[2] << 8 | cursor[3],
                            cursor[4] << 24 | cursor[5] << 16 | cursor[6] << 8 | cursor[7]);
                    fflush(xf);
                }
            }
#endif
            if (end - cursor < ptrdiff_t(numWords * 4)) break;
            handle_xf_regs(addrBase, numWords, reinterpret_cast<const u32*>(cursor));
            cursor += numWords * 4;
            continue;
        }
        if (command == GX_CMD_CALL_DL) {
            flushPendingStrip();
            u32 addr, size;
            if (!read_be_u32(cursor, end, addr) || !read_be_u32(cursor, end, size)) break;
            // Nested display lists: the address is relative to a base pointer
            // tracked by the game. For now we log and continue - the primary
            // issue (flat materials) is fixed by XF/BP handlers above.
            continue;
        }
        if (command == GX_CMD_LOAD_CP_REG || command == GX_CMD_INVL_VC ||
            (command >= GX_CMD_LOAD_INDX_A && command <= GX_CMD_LOAD_INDX_D + 0x07)) {
            flushPendingStrip();
            // CP reg: 1 byte address + 4 byte value. Indexed loads: 4 byte
            // header + payload. Both are safe to skip for rendering state we
            // already track globally.
            u32 skip;
            if (!read_be_u32(cursor, end, skip)) break;
            continue;
        }

        const u8 opcode = command & GX_OPCODE_MASK;
        const u8 format = command & GX_VAT_MASK;
        if (opcode < GX_CMD_DRAW_QUADS || opcode > GX_CMD_DRAW_POINTS) {
            ++sDlDesyncs;
            report_desync("unsupported opcode", list,
                          size_t(cursor - static_cast<const u8*>(list) - 1), nbytes);
            flushPendingStrip();
            if (!reportedUnsupportedCommand) {
                fprintf(stderr, "[PC GX] Unsupported display-list command 0x%02X at byte %zu/%u\n",
                        command, size_t(cursor - static_cast<const u8*>(list) - 1), nbytes);
                reportedUnsupportedCommand = true;
            }
            break;
        }

        u16 vertexCount = 0;
        if (!read_be_u16(cursor, end, vertexCount)) break;
        ++sPerfSourcePrimitives;
        const bool continueTriangleStrip = opcode == GX_TRIANGLESTRIP && pendingTriangleStrip;
        if (!continueTriangleStrip) {
            flushPendingStrip();
            pc_gfx_begin(static_cast<GXPrimitive>(opcode), static_cast<GXVtxFmt>(format), vertexCount);
        } else {
            // Join independent strips using degenerate triangles. An extra
            // duplicate for odd-length strips preserves the winding parity of
            // the first real triangle in the following strip.
            const Vertex last = sVertexStream.back();
            if (sVertexStream.size() & 1) sVertexStream.push_back(last);
            sVertexStream.push_back(last);
            sVertexStream.reserve(sVertexStream.size() + vertexCount + 1);
        }
        sVerticesPretransformed = true;

        bool malformed = false;
        for (u16 vertex = 0; vertex < vertexCount && !malformed; ++vertex) {
            const u8* const vertexStart = cursor;
            Vertex v;
            v.x = 0.0f; v.y = 0.0f; v.z = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.r = 1.0f; v.g = 1.0f; v.b = 1.0f; v.a = 1.0f;
            for (int tc = 0; tc < 4; ++tc) {
                v.tex[tc][0] = 0.0f;
                v.tex[tc][1] = 0.0f;
            }
            u8 matrixId = static_cast<u8>(sCurrentPosMtxId);

            for (int attrNumber = GX_VA_PNMTXIDX; attrNumber <= GX_VA_TEX7; ++attrNumber) {
                GXAttr attr = static_cast<GXAttr>(attrNumber);
                GXAttrType desc = sVtxDesc[attr];
                if (desc == GX_NONE) {
                    // NBT replaces the normal attribute in the hardware stream.
                    if (attr == GX_VA_NRM && sVtxDesc[GX_VA_NBT] != GX_NONE) {
                        attr = GX_VA_NBT;
                        desc = sVtxDesc[attr];
                    } else {
                        continue;
                    }
                }

                if (attr <= GX_VA_TEX7MTXIDX) {
                    if (desc != GX_DIRECT || cursor >= end) { malformed = true; break; }
                    const u8 value = *cursor++;
                    if (attr == GX_VA_PNMTXIDX) {
                        matrixId = value;
                        if (matrixId >= 64) ++sBadMtxIdx;
                        if (matrixId >= 64 && !reportedInvalidMatrix) {
                            fprintf(stderr, "[PC GX] Invalid PNMTXIDX %u in display list\n", matrixId);
                            reportedInvalidMatrix = true;
                        }
                    }
                    continue;
                }

                const u8* element = nullptr;
                if (desc == GX_DIRECT) {
                    // Inline vertex data embedded in the display list itself.
                    const u8 inlineSize = attr_inline_size(attr, sVtxFormats[format][attr]);
                    if (inlineSize == 0 || end - cursor < inlineSize) { malformed = true; break; }
                    element = cursor;
                    cursor += inlineSize;
                } else {
                    u16 index = 0;
                    if (!read_attribute_index(desc, cursor, end, index)) {
                        malformed = true;
                        break;
                    }
                    const VertexArrayState& array = sVtxArrays[attr];
                    if (!array.base || array.stride == 0) continue;
                    element = array.base + size_t(index) * array.stride;
                    // Only the wild-vertex report below reads these, and this
                    // is the innermost loop in the whole renderer: three stores
                    // per vertex, hundreds of thousands of vertices a frame.
                    if (profilingWildVerts && attr == GX_VA_POS) {
                        sLastPosIndex  = index;
                        sLastPosBase   = array.base;
                        sLastPosStride = array.stride;
                    }
                }

                if (attr == GX_VA_POS) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 compSize = get_comptype_size(fmtState.type);
                    float x = read_attr_float(element, fmtState.type, fmtState.frac);
                    float y = read_attr_float(element + compSize, fmtState.type, fmtState.frac);
                    float z = (fmtState.count == GX_POS_XYZ) ? read_attr_float(element + 2 * compSize, fmtState.type, fmtState.frac) : 0.0f;
                    
                    transform_position(matrixId, x, y, z, v.x, v.y, v.z);
                    // The map is a few thousand units across; anything beyond
                    // this, or non-finite, did not come from real model data.
                    // Diagnostic, and it runs per vertex, so it must not run at
                    // all unless asked for: left ungated it cost the title
                    // screen more than half its frame rate.
                    if (profilingWildVerts
                        && (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)
                            || fabsf(v.x) > 1.0e6f || fabsf(v.y) > 1.0e6f || fabsf(v.z) > 1.0e6f)) {
                        ++sWildVerts;
                        // Separate the two possible sources: the model data we
                        // read, or the matrix we multiplied it by. Printing
                        // both says which one is garbage without guessing.
                        static int reports = 0;
                        if (reports < 6) {
                            ++reports;
                            const float* m = sPosMatrix[matrixId < 64 ? matrixId : 0];
                            fprintf(stderr,
                                    "[PC GX] WILD #%d mtx=%u in=(%g,%g,%g) out=(%g,%g,%g)\n",
                                    reports, unsigned(matrixId), x, y, z, v.x, v.y, v.z);
                            (void)m;
                            const VertexFormatState& pf = sVtxFormats[format][GX_VA_POS];
                            fprintf(stderr,
                                    "[PC GX] WILD #%d pos idx=%u base=%p stride=%u -> elem=%p (offset %zu)  fmt: count=%d type=%d frac=%u\n",
                                    reports, unsigned(sLastPosIndex), (const void*)sLastPosBase,
                                    unsigned(sLastPosStride),
                                    (const void*)(sLastPosBase + size_t(sLastPosIndex) * sLastPosStride),
                                    size_t(sLastPosIndex) * sLastPosStride,
                                    int(pf.count), int(pf.type), unsigned(pf.frac));
                            fprintf(stderr,
                                    "[PC GX] WILD #%d POS array last set on frame %llu (now %llu, %llu frames ago); set #%llu of %llu total\n",
                                    reports,
                                    (unsigned long long)sArraySetFrame[GX_VA_POS],
                                    (unsigned long long)sFrameSerial,
                                    (unsigned long long)(sFrameSerial - sArraySetFrame[GX_VA_POS]),
                                    (unsigned long long)sArraySetSerial[GX_VA_POS],
                                    (unsigned long long)sArraySetCounter);
                            // The array and the format are right, so the
                            // remaining suspect is the layout we are stepping
                            // through. Print the descriptor and the actual
                            // stream bytes for this vertex so the true stride
                            // can be read off by hand.
                            fprintf(stderr, "[PC GX] WILD #%d vtxdesc:", reports);
                            unsigned wstride = 0;
                            for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7; ++a) {
                                const GXAttrType d = sVtxDesc[a];
                                if (d == GX_NONE) continue;
                                const char* kind = d == GX_DIRECT ? "direct" : d == GX_INDEX8 ? "idx8"
                                                 : d == GX_INDEX16 ? "idx16" : "?";
                                unsigned bytes = d == GX_DIRECT
                                    ? attr_inline_size(static_cast<GXAttr>(a), sVtxFormats[format][a])
                                    : (d == GX_INDEX8 ? 1u : 2u);
                                wstride += bytes;
                                fprintf(stderr, " a%d=%s(%u)", a, kind, bytes);
                            }
                            fprintf(stderr, "  stride=%u  vat=%u  vert %u/%u at list byte %zu/%u\n",
                                    wstride, unsigned(format), unsigned(vertex), unsigned(vertexCount),
                                    size_t(vertexStart - static_cast<const u8*>(list)), nbytes);
                            fprintf(stderr, "[PC GX] WILD #%d stream:", reports);
                            for (int b = 0; b < 20 && vertexStart + b < end; ++b) {
                                fprintf(stderr, " %02X", vertexStart[b]);
                            }
                            fprintf(stderr, "\n");
                            if (reports == 1) {
                                pc_gfx_dump_vtx_desc_history("first wild vertex");
                                // The same list parses correctly at 30 Hz, so
                                // the descriptor is not wrong for the mesh --
                                // this list is reaching us from a draw path
                                // that never programmed one. Name that path
                                // instead of inferring it.
#if defined(_WIN32)
                                fprintf(stderr, "[PC GX] wild draw call path: "
                                                "unavailable on this platform\n");
#else
                                void* frames[16];
                                const int n = backtrace(frames, 16);
                                char** names = backtrace_symbols(frames, n);
                                fprintf(stderr, "[PC GX] wild draw call path:\n");
                                for (int f = 0; f < n; ++f) {
                                    fprintf(stderr, "    %s\n", names ? names[f] : "?");
                                }
                                free(names);
#endif
                            }
                            fprintf(stderr, "[PC GX] WILD #%d raw:", reports);
                            for (int b = 0; b < 12; ++b) fprintf(stderr, " %02X", element[b]);
                            fprintf(stderr, "\n");
                        }
                    }
                } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 r, g, b, a;
                    read_attr_color(element, fmtState.type, r, g, b, a);
                    if (attr == GX_VA_CLR0) {
                        v.r = r / 255.0f;
                        v.g = g / 255.0f;
                        v.b = b / 255.0f;
                        v.a = a / 255.0f;
                    }
                } else if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 compSize = get_comptype_size(fmtState.type);
                    // Every normal encoding (XYZ, NBT, NBT3) stores the normal
                    // vector in the first three slots; NBT/NBT3 just append
                    // binormal/tangent data we can safely ignore.
                    v.nx = read_attr_float(element, fmtState.type, fmtState.frac);
                    v.ny = read_attr_float(element + compSize, fmtState.type, fmtState.frac);
                    v.nz = read_attr_float(element + 2 * compSize, fmtState.type, fmtState.frac);
                } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 compSize = get_comptype_size(fmtState.type);
                    const int tc = int(attr) - int(GX_VA_TEX0);
                    // The parser must consume all eight GX attributes, while
                    // the current shader backend only exposes TEX0-TEX3.
                    if (tc < 4) {
                        v.tex[tc][0] = read_attr_float(element, fmtState.type, fmtState.frac);
                        v.tex[tc][1] = (fmtState.count == GX_TEX_ST)
                            ? read_attr_float(element + compSize, fmtState.type, fmtState.frac) : 0.0f;
                    }
                }
            }

            // Display-list vertices are transformed to view space on the CPU
            // so that a primitive can contain several PNMTXIDX values.  Their
            // normals must follow the same per-vertex matrix; applying one
            // global normal matrix later gives skinned joints unrelated light
            // directions (notably Olimar/Pikmin heads and articulated ships).
            float nx, ny, nz;
            transform_normal(matrixId, v.nx, v.ny, v.nz, nx, ny, nz);
            v.nx = nx;
            v.ny = ny;
            v.nz = nz;

            if (continueTriangleStrip && vertex == 0) sVertexStream.push_back(v);
            sVertexStream.push_back(v);
        }

        if (malformed) {
            ++sDlDesyncs;
            report_desync("truncated vertex", list,
                          size_t(cursor - static_cast<const u8*>(list)), nbytes);
            if (!reportedMalformedVertex) {
                fprintf(stderr, "[PC GX] Malformed vertex stream at byte %zu/%u (format %u)\n",
                        size_t(cursor - static_cast<const u8*>(list)), nbytes, format);
                reportedMalformedVertex = true;
            }
            sInPrimitive = false;
            sVertexStream.clear();
            pendingTriangleStrip = false;
            break;
        }
        if (opcode == GX_TRIANGLESTRIP) pendingTriangleStrip = true;
        else pc_gfx_end();
    }
    flushPendingStrip();
}

void pc_gfx_copy_disp(void* dest, GXBool clear) {
    pc_gfx_note_gl_state_change();
    (void)dest;
    if (clear) {
        const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(sCopyClearColor[0], sCopyClearColor[1], sCopyClearColor[2], sCopyClearColor[3]);
        glClearDepth(sCopyClearDepth);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
    }
}

void pc_gfx_set_copy_clear(GXColor color, u32 clearZ) {
    sCopyClearColor[0] = color.r / 255.0f;
    sCopyClearColor[1] = color.g / 255.0f;
    sCopyClearColor[2] = color.b / 255.0f;
    sCopyClearColor[3] = color.a / 255.0f;
    sCopyClearDepth = (clearZ & 0x00ffffffu) / 16777215.0;
    // Keep the replay clear in sync with the authoritative clear color.
    sReplayClearColor[0] = sCopyClearColor[0];
    sReplayClearColor[1] = sCopyClearColor[1];
    sReplayClearColor[2] = sCopyClearColor[2];
    sReplayClearColor[3] = sCopyClearColor[3];
    sReplayClearDepth    = sCopyClearDepth;
}
