// dllmain.cpp â€” nutella shader DLL (EGL-only + debug)
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <MinHook.h>
#include <GL/gl.h>

// â”€â”€ Missing GL types/constants â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER             0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER           0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS            0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS               0x8B82
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE             0x812F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                  0x84C0
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER              0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW               0x88E4
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP            0x0005
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING      0x85B5
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM           0x8B8D
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE            0x84E0
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D        0x8069
#endif
#ifndef GL_VIEWPORT
#define GL_VIEWPORT                  0x0BA2
#endif

// â”€â”€ EGL â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef int   EGLint;
typedef unsigned int EGLBoolean;

#define EGL_WIDTH   0x3057
#define EGL_HEIGHT  0x3056

static const char* LOG_PATH = "shader_log.txt";  // written next to the process CWD; change to an absolute path if you need a fixed location

static void log_line(const char* msg) {
    FILE* f = fopen(LOG_PATH, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, msg);
    fclose(f);
}

// â”€â”€ Shared params (must match Python struct: 10f + 2i = 48 bytes) â”€â”€â”€â”€â”€â”€â”€
#pragma pack(push, 1)
struct Params {
    volatile float saturation, contrast, brightness, shadow, warmth;
    volatile float bloom, lift, hdr, ambient, vignette;
    volatile int enabled, fxaa;
};
#pragma pack(pop)

static HANDLE  g_map    = nullptr;
static Params* g_params = nullptr;

static void open_shm() {
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                               0, sizeof(Params), L"Local\\NutellaParams");
    if (!g_map) {
        char b[80]; snprintf(b, sizeof(b), "nutella: CreateFileMapping failed %lu", GetLastError());
        log_line(b);
        return;
    }
    g_params = (Params*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Params));
    if (!g_params) {
        char b[80]; snprintf(b, sizeof(b), "nutella: MapViewOfFile failed %lu", GetLastError());
        log_line(b);
        return;
    }
    char b[128];
    snprintf(b, sizeof(b), "nutella: shm OK ptr=%p size=%d", (void*)g_params, (int)sizeof(Params));
    log_line(b);
}

// â”€â”€ GL proc types â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum);
typedef void   (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const char**, const GLint*);
typedef void   (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint);
typedef void   (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, char*);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void   (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void   (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint);
typedef void   (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, char*);
typedef void   (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint);
typedef GLint  (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const char*);
typedef void   (APIENTRY *PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void   (APIENTRY *PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void   (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void   (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void   (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void   (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void   (APIENTRY *PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);
typedef void   (APIENTRY *PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);

static PFNGLCREATESHADERPROC            p_glCreateShader         = nullptr;
static PFNGLSHADERSOURCEPROC            p_glShaderSource         = nullptr;
static PFNGLCOMPILESHADERPROC           p_glCompileShader        = nullptr;
static PFNGLGETSHADERIVPROC             p_glGetShaderiv          = nullptr;
static PFNGLGETSHADERINFOLOGPROC        p_glGetShaderInfoLog     = nullptr;
static PFNGLCREATEPROGRAMPROC           p_glCreateProgram        = nullptr;
static PFNGLATTACHSHADERPROC            p_glAttachShader         = nullptr;
static PFNGLLINKPROGRAMPROC             p_glLinkProgram          = nullptr;
static PFNGLGETPROGRAMIVPROC            p_glGetProgramiv         = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC       p_glGetProgramInfoLog    = nullptr;
static PFNGLUSEPROGRAMPROC              p_glUseProgram           = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC      p_glGetUniformLocation   = nullptr;
static PFNGLUNIFORM1FPROC               p_glUniform1f            = nullptr;
static PFNGLUNIFORM1IPROC               p_glUniform1i            = nullptr;
static PFNGLACTIVETEXTUREPROC           p_glActiveTexture        = nullptr;
static PFNGLGENVERTEXARRAYSPROC         p_glGenVertexArrays      = nullptr;
static PFNGLBINDVERTEXARRAYPROC         p_glBindVertexArray      = nullptr;
static PFNGLGENBUFFERSPROC              p_glGenBuffers           = nullptr;
static PFNGLBINDBUFFERPROC              p_glBindBuffer           = nullptr;
static PFNGLBUFFERDATAPROC              p_glBufferData           = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC     p_glVertexAttribPointer  = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC p_glEnableVertexAttribArray = nullptr;
static PFNGLDRAWARRAYSPROC              p_glDrawArrays           = nullptr;
static PFNGLVIEWPORTPROC                p_glViewport             = nullptr;

static void* gl_get(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1)
        p = (void*)GetProcAddress(GetModuleHandleA("opengl32.dll"), name);
    return p;
}

static bool load_gl_procs() {
    p_glCreateShader            = (PFNGLCREATESHADERPROC)            gl_get("glCreateShader");
    p_glShaderSource            = (PFNGLSHADERSOURCEPROC)            gl_get("glShaderSource");
    p_glCompileShader           = (PFNGLCOMPILESHADERPROC)           gl_get("glCompileShader");
    p_glGetShaderiv             = (PFNGLGETSHADERIVPROC)             gl_get("glGetShaderiv");
    p_glGetShaderInfoLog        = (PFNGLGETSHADERINFOLOGPROC)        gl_get("glGetShaderInfoLog");
    p_glCreateProgram           = (PFNGLCREATEPROGRAMPROC)           gl_get("glCreateProgram");
    p_glAttachShader            = (PFNGLATTACHSHADERPROC)            gl_get("glAttachShader");
    p_glLinkProgram             = (PFNGLLINKPROGRAMPROC)             gl_get("glLinkProgram");
    p_glGetProgramiv            = (PFNGLGETPROGRAMIVPROC)            gl_get("glGetProgramiv");
    p_glGetProgramInfoLog       = (PFNGLGETPROGRAMINFOLOGPROC)       gl_get("glGetProgramInfoLog");
    p_glUseProgram              = (PFNGLUSEPROGRAMPROC)              gl_get("glUseProgram");
    p_glGetUniformLocation      = (PFNGLGETUNIFORMLOCATIONPROC)      gl_get("glGetUniformLocation");
    p_glUniform1f               = (PFNGLUNIFORM1FPROC)               gl_get("glUniform1f");
    p_glUniform1i               = (PFNGLUNIFORM1IPROC)               gl_get("glUniform1i");
    p_glActiveTexture           = (PFNGLACTIVETEXTUREPROC)           gl_get("glActiveTexture");
    p_glGenVertexArrays         = (PFNGLGENVERTEXARRAYSPROC)         gl_get("glGenVertexArrays");
    p_glBindVertexArray         = (PFNGLBINDVERTEXARRAYPROC)         gl_get("glBindVertexArray");
    p_glGenBuffers              = (PFNGLGENBUFFERSPROC)              gl_get("glGenBuffers");
    p_glBindBuffer              = (PFNGLBINDBUFFERPROC)              gl_get("glBindBuffer");
    p_glBufferData              = (PFNGLBUFFERDATAPROC)              gl_get("glBufferData");
    p_glVertexAttribPointer     = (PFNGLVERTEXATTRIBPOINTERPROC)     gl_get("glVertexAttribPointer");
    p_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC) gl_get("glEnableVertexAttribArray");
    p_glDrawArrays              = (PFNGLDRAWARRAYSPROC)              gl_get("glDrawArrays");
    p_glViewport                = (PFNGLVIEWPORTPROC)                gl_get("glViewport");

    // Diagnostic summary so we can see exactly which procs failed
    char b[256];
    snprintf(b, sizeof(b),
        "nutella: procs shader=%d prog=%d unif=%d u1f=%d active=%d vao=%d buff=%d draw=%d vp=%d",
        p_glCreateShader!=0, p_glCreateProgram!=0, p_glGetUniformLocation!=0,
        p_glUniform1f!=0, p_glActiveTexture!=0, p_glGenVertexArrays!=0,
        p_glGenBuffers!=0, p_glDrawArrays!=0, p_glViewport!=0);
    log_line(b);

    return (p_glCreateShader && p_glUseProgram && p_glGenVertexArrays
            && p_glDrawArrays && p_glActiveTexture && p_glViewport);
}

// â”€â”€ Shader (DEBUG: forced 20% red tint so we can see if draw runs) â”€â”€â”€â”€â”€â”€
static const char* GL_VS =
"#version 100\n"
"attribute vec2 aPos;\n"
"attribute vec2 aUV;\n"
"varying vec2 vUV;\n"
"void main() {\n"
"    vUV = vec2(aUV.x, 1.0 - aUV.y);\n"
"    gl_Position = vec4(aPos, 0.0, 1.0);\n"
"}\n";

static const char* GL_FS =
"#version 100\n"
"precision mediump float;\n"
"uniform sampler2D u_frame;\n"
"uniform float u_saturation, u_contrast, u_brightness, u_shadow;\n"
"uniform float u_warmth, u_lift, u_hdr, u_ambient, u_vignette;\n"
"varying vec2 vUV;\n"
"const vec3 LUMA = vec3(0.3086, 0.6094, 0.0820);\n"
"float L(vec3 c) { return dot(c, LUMA); }\n"
"void main() {\n"
"    vec3 c = texture2D(u_frame, vUV).rgb;\n"
"    c += u_brightness;\n"
"    c = (c - 0.5) * u_contrast + 0.5;\n"
"    float l = L(c);\n"
"    c = mix(vec3(l), c, u_saturation);\n"
"    c.r += u_warmth * 2.0; c.g += u_warmth * 0.6; c.b -= u_warmth * 1.2;\n"
"    if (u_lift > 0.001) c = pow(max(c, vec3(0.0)), vec3(1.0 / (1.0 + u_lift)));\n"
"    if (u_hdr > 0.001) { float h = L(c); c *= (1.0 + u_hdr * (h - 0.35) * 1.15); }\n"
"    if (u_ambient > 0.001) { float a = 1.0 - L(c); c += vec3(0.085, 0.115, 0.19) * u_ambient * a * a; }\n"
"    if (u_vignette > 0.001) c *= 1.0 - u_vignette * 0.85 * smoothstep(0.36, 0.82, distance(vUV, vec2(0.5)));\n"
"    c = c / (1.0 + max(c - 1.0, vec3(0.0)) * 0.85);\n"
"    /* DEBUG TINT: 20%% red so we can visually verify the draw call runs */\n"
"    c.r = min(1.0, c.r + 0.20);\n"
"    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
"}\n";

// â”€â”€ State â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static GLuint g_program = 0;
static GLuint g_tex     = 0;
static GLuint g_vao     = 0;
static GLuint g_vbo     = 0;
static int    g_tex_w   = 0;
static int    g_tex_h   = 0;
static bool   g_ready   = false;
static volatile long g_frames = 0;

static void init_gl() {
    if (!load_gl_procs()) {
        log_line("nutella: [GL] procs load FAILED");
        return;
    }

    GLuint vs = p_glCreateShader(GL_VERTEX_SHADER);
    p_glShaderSource(vs, 1, &GL_VS, nullptr);
    p_glCompileShader(vs);
    GLint ok = 0;
    p_glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char lg[1024] = {0}; GLsizei n = 0;
        p_glGetShaderInfoLog(vs, sizeof(lg), &n, lg);
        log_line("nutella: VS FAILED:"); log_line(lg);
    }

    GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER);
    p_glShaderSource(fs, 1, &GL_FS, nullptr);
    p_glCompileShader(fs);
    ok = 0;
    p_glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char lg[1024] = {0}; GLsizei n = 0;
        p_glGetShaderInfoLog(fs, sizeof(lg), &n, lg);
        log_line("nutella: FS FAILED:"); log_line(lg);
    }

    g_program = p_glCreateProgram();
    p_glAttachShader(g_program, vs);
    p_glAttachShader(g_program, fs);
    p_glLinkProgram(g_program);
    ok = 0;
    p_glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char lg[1024] = {0}; GLsizei n = 0;
        p_glGetProgramInfoLog(g_program, sizeof(lg), &n, lg);
        log_line("nutella: link FAILED:"); log_line(lg);
    } else {
        log_line("nutella: shader compiled + linked OK");
    }

    float quad[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };
    p_glGenVertexArrays(1, &g_vao);
    p_glBindVertexArray(g_vao);
    p_glGenBuffers(1, &g_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    p_glBindVertexArray(0);

    g_ready = true;
    log_line("nutella: init_gl complete");
}

static void ensure_tex(int w, int h) {
    if (g_tex && g_tex_w == w && g_tex_h == h) return;
    if (g_tex) glDeleteTextures(1, &g_tex);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_tex_w = w; g_tex_h = h;
    char b[80]; snprintf(b, sizeof(b), "nutella: tex resized %dx%d", w, h);
    log_line(b);
}

// â”€â”€ Main pass â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void do_pass(int w, int h) {
    if (!g_ready || !g_program || !g_params) return;
    if (w <= 0 || h <= 0) return;
    if (!g_params->enabled) return;

    // Throttle to ~60 FPS
    static DWORD last_ms = 0;
    DWORD now = GetTickCount();
    if (now - last_ms < 15) return;
    last_ms = now;

    // DEBUG: log params every 60 draws
    static int dbg = 0;
    if (++dbg % 60 == 0) {
        char b[256];
        snprintf(b, sizeof(b),
            "nutella: params sat=%.2f con=%.2f bri=%.2f vig=%.2f en=%d ptr=%p",
            g_params->saturation, g_params->contrast, g_params->brightness,
            g_params->vignette, g_params->enabled, (void*)g_params);
        log_line(b);
    }

    ensure_tex(w, h);

    // â”€â”€ Save state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    GLint prev_prog   = 0, prev_vao = 0;
    GLint prev_active = 0, prev_tex = 0;
    GLint prev_vp[4]  = {0,0,0,0};

    glGetIntegerv(GL_CURRENT_PROGRAM,      &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,       &prev_active);
    glGetIntegerv(GL_VIEWPORT,             prev_vp);
    p_glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,   &prev_tex);

    GLboolean e_depth   = glIsEnabled(GL_DEPTH_TEST);
    GLboolean e_blend   = glIsEnabled(GL_BLEND);
    GLboolean e_cull    = glIsEnabled(GL_CULL_FACE);
    GLboolean e_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean e_stencil = glIsEnabled(GL_STENCIL_TEST);

    GLboolean prev_cmask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, prev_cmask);

    // â”€â”€ Copy the back buffer into our texture â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);

    // â”€â”€ Draw fullscreen graded quad â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    p_glViewport(0, 0, w, h);

    p_glUseProgram(g_program);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    p_glUniform1i(p_glGetUniformLocation(g_program, "u_frame"), 0);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_saturation"), g_params->saturation);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_contrast"),   g_params->contrast);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_brightness"), g_params->brightness);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_shadow"),     g_params->shadow);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_warmth"),     g_params->warmth);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_lift"),       g_params->lift);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_hdr"),        g_params->hdr);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_ambient"),    g_params->ambient);
    p_glUniform1f(p_glGetUniformLocation(g_program, "u_vignette"),   g_params->vignette);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    p_glBindVertexArray(g_vao);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // â”€â”€ Restore â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    p_glBindVertexArray(prev_vao);
    p_glUseProgram(prev_prog);

    if (e_depth)   glEnable(GL_DEPTH_TEST);   else glDisable(GL_DEPTH_TEST);
    if (e_blend)   glEnable(GL_BLEND);        else glDisable(GL_BLEND);
    if (e_cull)    glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);
    if (e_scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (e_stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glColorMask(prev_cmask[0], prev_cmask[1], prev_cmask[2], prev_cmask[3]);

    p_glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    p_glActiveTexture(prev_active);
    glBindTexture(GL_TEXTURE_2D, prev_tex);
}

// â”€â”€ EGL hook â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
typedef EGLBoolean (APIENTRY *eglSwapBuffers_t)(EGLDisplay, EGLSurface);
typedef EGLBoolean (APIENTRY *eglQuerySurface_t)(EGLDisplay, EGLSurface, EGLint, EGLint*);

static eglSwapBuffers_t  g_orig_egl  = nullptr;
static eglQuerySurface_t g_egl_query = nullptr;

static EGLBoolean APIENTRY hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    long n = InterlockedIncrement(&g_frames);

    if (n == 1) {
        log_line("nutella: first EGL frame â€” loading GL procs");
        init_gl();
    }

    EGLint w = 0, h = 0;
    if (g_egl_query && surface) {
        g_egl_query(dpy, surface, EGL_WIDTH,  &w);
        g_egl_query(dpy, surface, EGL_HEIGHT, &h);
    }
    if (w <= 0 || h <= 0) {
        GLint vp[4] = {0,0,0,0};
        glGetIntegerv(GL_VIEWPORT, vp);
        w = vp[2]; h = vp[3];
    }

    do_pass((int)w, (int)h);

    if (n <= 3 || n % 300 == 0) {
        char b[128];
        snprintf(b, sizeof(b), "nutella: frame %ld egl %dx%d", n, w, h);
        log_line(b);
    }

    return g_orig_egl(dpy, surface);
}

// â”€â”€ Install â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static bool try_hook(const char* modname, const char* fnname, void* detour, void** orig) {
    HMODULE h = GetModuleHandleA(modname);
    if (!h) h = LoadLibraryA(modname);
    if (!h) {
        char b[128]; snprintf(b, sizeof(b), "nutella: %s not loaded", modname);
        log_line(b);
        return false;
    }
    void* target = (void*)GetProcAddress(h, fnname);
    if (!target) {
        char b[128]; snprintf(b, sizeof(b), "nutella: %s!%s not exported", modname, fnname);
        log_line(b);
        return false;
    }
    if (MH_CreateHook(target, detour, orig) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        char b[128]; snprintf(b, sizeof(b), "nutella: hook %s!%s failed", modname, fnname);
        log_line(b);
        return false;
    }
    char b[160]; snprintf(b, sizeof(b), "nutella: [hook] %s!%s installed", modname, fnname);
    log_line(b);
    return true;
}

DWORD WINAPI boot(LPVOID) {
    log_line("nutella: dll attached");
    open_shm();

    if (MH_Initialize() != MH_OK) {
        log_line("nutella: MH_Initialize failed");
        return 0;
    }

    // Load EGL query
    HMODULE egl = GetModuleHandleA("libEGL.dll");
    if (!egl) egl = LoadLibraryA("libEGL.dll");
    if (egl)
        g_egl_query = (eglQuerySurface_t)GetProcAddress(egl, "eglQuerySurface");

    if (!try_hook("libEGL.dll", "eglSwapBuffers",
                  (void*)hook_eglSwapBuffers, (void**)&g_orig_egl)) {
        try_hook("libOpenglRender.dll", "eglSwapBuffers",
                 (void*)hook_eglSwapBuffers, (void**)&g_orig_egl);
    }

    log_line("nutella: hook phase done (EGL only)");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, boot, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (g_params) UnmapViewOfFile(g_params);
        if (g_map) CloseHandle(g_map);
    }
    return TRUE;
}