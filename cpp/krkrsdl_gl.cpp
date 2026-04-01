#include "tjsCommHead.h"
#include "eventCallbackFun.h"
#if _KRKRSDL3_GL
#include <GL/glew.h>
#else
#include <GLES3/gl32.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL2/SDL.h>

#include <thread>
#include <mutex>
#include <unordered_set>
#include <cstdlib>

extern std::thread::id TVPMainThreadID;
extern std::vector<SDL_Sprite*> renderTexture;
extern std::mutex sdlRenderMtx;

namespace krkrsdl3
{
// base
static std::unordered_set<std::string> sTVPGLExtensions;
void fetchGLInfo()
{
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);

    SDL_Log("OpenGL Vendor    : %s\n", vendor);
    SDL_Log("OpenGL Renderer  : %s\n", renderer);
    SDL_Log("OpenGL Version   : %s\n", version);
    SDL_Log("GLSL Version     : %s\n", glslVersion);

    GLint numExtensions;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    SDL_Log("Supported Extensions (%d):\n", numExtensions);
    for (int i = 0; i < numExtensions; i++)
    {
        const GLubyte* ext = glGetStringi(GL_EXTENSIONS, i);
        sTVPGLExtensions.emplace(std::string((const char*)ext));
        SDL_Log("  %s\n", ext);
    }
}
bool checkGLExtension(const std::string& extname)
{
    return sTVPGLExtensions.find(extname) != sTVPGLExtensions.end();
}

// 全部都在这里搞定
static GLuint krkrsdl3_program = 0, krkrsdl3_vao = 0, krkrsdl3_vbo = 0, krkrsdl3_ebo = 0;
#if _KRKRSDL3_GL
const char* vertexShaderSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;

uniform vec2 windowSize;
uniform vec2 texture_Position;
uniform vec2 texture_Size;

void main()
{
    vec2 pixelPos = texture_Position + vec2(texture_Size.x * aPos.x, texture_Size.y * aPos.y);
    vec2 ndcPos = pixelPos * 2.0 - 1.0;

    gl_Position = vec4(ndcPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";
const char* fragmentShaderSrc = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D texture1;

void main()
{
    FragColor = texture(texture1, TexCoord);
}
)";
#else
const char* vertexShaderSrc = R"(#version 320 es
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;

uniform vec2 windowSize;
uniform vec2 texture_Position;
uniform vec2 texture_Size;

void main()
{
    vec2 pixelPos = texture_Position + vec2(texture_Size.x * aPos.x, texture_Size.y * aPos.y);
    vec2 ndcPos = pixelPos * 2.0 - 1.0;

    gl_Position = vec4(ndcPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";
const char* fragmentShaderSrc = R"(#version 320 es
out mediump vec4 FragColor;
in mediump vec2 TexCoord;
uniform sampler2D texture1;

void main()
{
    FragColor = texture(texture1, TexCoord);
}
)";
#endif
GLuint compileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        SDL_Log("Shader compile error: %s", log);
    }
    return shader;
}
GLuint createProgram()
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success)
    {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        SDL_Log("Program link error: %s", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// 一些基础函数
void SDL_GL_BaseSet(int w, int h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);
    if (krkrsdl3_program == 0 || glIsProgram(krkrsdl3_program) != GL_TRUE)
    {
        // Create shader program
        krkrsdl3_program = createProgram();
        glGenVertexArrays(1, &krkrsdl3_vao);
        glGenBuffers(1, &krkrsdl3_vbo);
        glGenBuffers(1, &krkrsdl3_ebo);
        // VBO
        glBindVertexArray(krkrsdl3_vao);
        glBindBuffer(GL_ARRAY_BUFFER, krkrsdl3_vbo);
        // 顶点数据
        float vertices[] = {
            // 位置          // 纹理坐标
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, // 左下
            1.0f, 0.0f, 0.0f, 1.0f, 1.0f, // 右下
            1.0f, 1.0f, 0.0f, 1.0f, 0.0f, // 右上
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f, // 左上
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        // EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, krkrsdl3_ebo);
        unsigned int indices[] = {
            0, 1, 2, // 第一个三角形
            2, 3, 0  // 第二个三角形
        };
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        // 位置属性
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        // 纹理坐标属性
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    glUseProgram(krkrsdl3_program);
    glViewport(0, 0, w, h);
    glBindVertexArray(krkrsdl3_vao);
}
// 绘制函数
void SDL_GL_DrawTexture(SDL_Sprite* sp, int w, int h)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)sp->texture);
    glUniform1i(glGetUniformLocation(krkrsdl3_program, "texture1"), 0);
    glUniform2f(glGetUniformLocation(krkrsdl3_program, "windowSize"), w, h);
    float currScale = std::min(((float)w) / sp->width, ((float)h) / sp->height);
    sp->scale = currScale;
    float scaledW = currScale * sp->width;
    float scaledH = currScale * sp->height;
    float xPos = (w - scaledW) / 2.0;
    sp->xPos = xPos;
    float yPos = (h - scaledH) / 2.0;
#if !_KRKRSDL3_GL
    // Portrait mode: apply TAPIR_PORTRAIT_TOP_OFFSET (0=top, 50=center, 100=bottom)
    // Default is 50 (center) when env var is not set.
    float remainingY = (float)h - scaledH;
    if (remainingY > 1.0f) {
        int pct = 50; // default: center
        const char *env = getenv("TAPIR_PORTRAIT_TOP_OFFSET");
        if (env) pct = atoi(env);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        yPos = remainingY * pct / 100.0f;
    }
#endif
    sp->yPos = yPos;
    glUniform2f(glGetUniformLocation(krkrsdl3_program, "texture_Position"), xPos / w, yPos / h);
    glUniform2f(glGetUniformLocation(krkrsdl3_program, "texture_Size"), scaledW / w, scaledH / h);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

// 创建opengl素材
void SDL_GL_CreateTexture(SDL_Sprite& sp)
{
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sp.width, sp.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    sp.texture = texture;
}

// 素材加入渲染
void SDL_GL_JoinTexture(SDL_Sprite* sp)
{
    renderTexture.push_back(sp);
}

// 更新素材
void SDL_GL_UpdateTexture(SDL_Sprite* sp, uint8_t* buff, int width, int height, int pitch)
{
    glBindTexture(GL_TEXTURE_2D, sp->texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buff);
    glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// 素材离开渲染
void SDL_GL_DepartTexture(SDL_Sprite* sp)
{
    for (size_t i = 0; i < renderTexture.size(); i++)
    {
        if (renderTexture.at(i)->texture == sp->texture)
        {
            renderTexture.erase(renderTexture.begin() + i);
            break;
        }
    }
}

// 清理素材
void SDL_GL_DestroyTexture(SDL_Sprite* sp)
{
    GLuint grp[1] = {(GLuint)sp->texture};
    glDeleteTextures(1, grp);

    sp->texture = 0;
}

} // namespace krkrsdl3
