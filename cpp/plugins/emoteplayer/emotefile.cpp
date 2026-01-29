#include "emotefile.h"

#include "Log.h"
#include "TVPStorage.h"
#include "UtilStreams.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

#include "xp3filter.h"

#include <zlib.h>
#include <SDL3/SDL.h>

#include <sstream>
#include <algorithm>

#pragma once

#define GLM_ASSERT_VALID(matrix) \
    do \
    { \
        const glm::mat4& m = (matrix); \
        for (int i = 0; i < 4; ++i) \
        { \
            for (int j = 0; j < 4; ++j) \
            { \
                assert(!std::isnan(m[i][j]) && "矩阵包含NaN值"); \
                assert(!std::isinf(m[i][j]) && "矩阵包含无穷大值"); \
            } \
        } \
    } while (0)

struct EMoteCTX
{
    tjs_uint32 key[4];
    tjs_uint32 v;
    tjs_uint32 count;
};

inline void init_emote_ctx(EMoteCTX* ctx, const tjs_uint32 key[4])
{
    ctx->key[0] = key[0];
    ctx->key[1] = key[1];
    ctx->key[2] = key[2];
    ctx->key[3] = key[3];
    ctx->v = 0;
    ctx->count = 0;
}

inline void emote_decrypt(EMoteCTX* ctx, tjs_uint8* data, tjs_uint32 size)
{
    for (tjs_uint32 i = 0; i < size; i++)
    {
        if (!ctx->v)
        {
            tjs_uint32 b = ctx->key[3];
            tjs_uint32 a = ctx->key[0] ^ (ctx->key[0] << 0xB);
            ctx->key[0] = ctx->key[1];
            ctx->key[1] = ctx->key[2];
            tjs_uint32 c = a ^ b ^ ((a ^ (b >> 0xB)) >> 8);
            ctx->key[2] = b;
            ctx->key[3] = c;
            ctx->v = c;
            ctx->count = 4;
        }

        data[i] ^= static_cast<tjs_uint8>(ctx->v);
        ctx->v >>= 8;
        ctx->count--;
    }
}

// lzfs.dll 大概也只有emote会用
namespace Lz4Stream
{

struct Lz4FrameInfo
{
    int BlockSize = 0;
    bool IndependentBlocks = false;
    bool HasBlockChecksum = false;
    bool HasContentLength = false;
    bool HasContentChecksum = false;
    bool HasDictionary = false;
    long OriginalLength = 0;
    int DictionaryId = 0;

    Lz4FrameInfo(tTJSBinaryStream* input)
    {
        // flags
        input->SetPosition(4);
        tjs_uint8 flags = input->ReadI8LE();
        int version = flags >> 6;
        if (version != 1)
            throw "Invalid LZ4 compressed stream.";
        IndependentBlocks = 0 != (flags & 0x20);
        HasBlockChecksum = 0 != (flags & 0x10);
        HasContentLength = 0 != (flags & 8);
        HasContentChecksum = 0 != (flags & 4);
        HasDictionary = 0 != (flags & 1);

        // blocksize
        tjs_int code = input->ReadI8LE();
        switch ((code >> 4) & 7)
        {
            case 4:
                BlockSize = 0x10000;
                break;
            case 5:
                BlockSize = 0x40000;
                break;
            case 6:
                BlockSize = 0x100000;
                break;
            case 7:
                BlockSize = 0x400000;
                break;
            default:
                throw "Invalid LZ4 compressed stream.";
        }

        // other
        if (HasContentLength)
        {
            tjs_uint64 length = (tjs_uint64)input->ReadI32LE();
            tjs_uint32 lengthex = input->ReadI32LE();
            length |= (tjs_uint64)lengthex << 32;
            OriginalLength = length;
        }
        if (HasDictionary)
        {
            DictionaryId = input->ReadI32LE();
        }
        input->ReadI8LE(); // skip descriptor checksum
    }
};

static const int MinMatch = 4;
static const int LastLiterals = 5;
static const int MFLimit = 12;
static const int MatchLengthBits = 4;
static const int MatchLengthMask = 0xF;
static const int RunMask = 0xF;
static void CopyOverlapped(tjs_uint8* data, int src, int dst, int count)
{
    if (count <= 0)
        return;

    if (dst > src)
    {
        while (count > 0)
        {
            int chunk = std::min(dst - src, count);
            memcpy(data + dst, data + src, chunk);
            dst += chunk;
            count -= chunk;
        }
    }
    else
    {
        memcpy(data + dst, data + src, count);
    }
}
static int DecompressBlock(tjs_uint8* block,
                           tjs_int32 block_size,
                           tjs_uint8* output,
                           tjs_int32 output_size)
{
    int src = 0;
    int iend = block_size;

    int dst = 0;
    int oend = output_size;

    for (;;)
    {
        /* get literal length */
        int token = block[src++];
        int length = token >> MatchLengthBits;
        if (RunMask == length)
        {
            int n;
            do
            {
                n = block[src++];
                length += n;
            } while ((src < iend - RunMask) && (0xFF == n));
            if (dst + length < dst || src + length < src) // overflow detection
                throw "Invalid LZ4 compressed stream.";
        }

        /* copy literals */
        int copy_end = dst + length;
        if ((copy_end > oend - MFLimit) || (src + length > iend - (3 + LastLiterals)))
        {
            if ((src + length != iend) || copy_end > oend)
                throw "Invalid LZ4 compressed stream.";
            memcpy(output + dst, block + src,length);
            src += length;
            dst += length;
            break;
        }
        memcpy(output + dst, block + src, length);
        src += length;
        dst = copy_end;

        /* get offset */
        int offset = 0;
        tjs_uint16 recOffset = 0;
        memcpy(&recOffset, block + src, 2);
        offset = recOffset;
        src += 2;
        int match = dst - offset;
        if (match < 0)
            throw "Invalid LZ4 compressed stream.";

        /* get matchlength */
        length = token & MatchLengthMask;
        if (MatchLengthMask == length)
        {
            int n;
            do
            {
                n = block[src++];
                if (src > iend - LastLiterals)
                    throw "Invalid LZ4 compressed stream.";
                length += n;
            } while (0xFF == n);
            if (dst + length < dst) // overflow detection
                throw "Invalid LZ4 compressed stream.";
        }
        length += MinMatch;

        /* copy match within block */
        CopyOverlapped(output, match, dst, length);
        dst += length;
    }
    return dst; // number of output bytes decoded
}
}

tTJSBinaryStream* GetLz4Stream(tTJSBinaryStream* orgStream)
{
    // 获取头信息
    Lz4Stream::Lz4FrameInfo info(orgStream);
    tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, info.OriginalLength);
    // 一个Block一个Block地获取数据
    bool m_eof = false;
    tjs_int m_dataBuffSize = info.BlockSize;
    tjs_uint8* m_dataBuff = new tjs_uint8[m_dataBuffSize];
    memset(m_dataBuff, 0, m_dataBuffSize);
    tjs_int m_data_size = 0;
    tjs_int m_blockBuffSize = 0;
    tjs_uint8* m_blockBuff = NULL;
    while (!m_eof)
    {
        // 获取下一个block
        tjs_int32 block_size = 0;
        if (4 != orgStream->Read(&block_size, 4))
            throw "EndOfStreamException";
        if (0 == block_size)
        {
            m_eof = true;
            m_data_size = 0;
            if (info.HasContentChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }
        else if (block_size < 0)
        {
            m_data_size = block_size & 0x7FFFFFFF;
            if (m_dataBuff == NULL || m_data_size > m_dataBuffSize)
            {
                if (m_dataBuff != NULL)
                    delete[] m_dataBuff;
                m_dataBuff = new tjs_uint8[m_data_size];
                m_dataBuffSize = m_data_size;
                memset(m_dataBuff, 0, m_dataBuffSize);
            }
            m_data_size = orgStream->Read(m_dataBuff, m_data_size);
            if (info.HasBlockChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }
        else
        {
            tjs_int32 m_block_size = block_size;
            if (m_blockBuff == NULL || m_block_size > m_blockBuffSize)
            {
                if (m_blockBuff != NULL)
                    delete[] m_blockBuff;
                m_blockBuff = new tjs_uint8[m_block_size];
                m_blockBuffSize = m_block_size;
                memset(m_blockBuff, 0, m_blockBuffSize);
            }
            if (m_block_size != orgStream->Read(m_blockBuff, m_block_size))
                throw "EndOfStreamException";
            m_data_size = Lz4Stream::DecompressBlock(m_blockBuff, m_block_size, m_dataBuff,
                                                        m_dataBuffSize);
            if (info.HasBlockChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }

        // 写入数据
        _stream->Write(m_dataBuff, m_data_size);
    }
    // 删除原始并返回
    if (m_dataBuff != NULL)
        delete[] m_dataBuff;
    if (m_blockBuff != NULL)
        delete[] m_blockBuff;
    delete orgStream;
    return _stream;
}


using namespace PSB;

namespace emoteplayer
{
#pragma region glprogram

    static GLuint emotenodeprogram = 0;
    static GLuint emotenodeVAO = 0;
#if _KRKRSDL3_GL
    static const char* vertexShaderSrc = R"(
            #version 430 core
            layout (location = 0) in vec2 aPos;

            void main()
            {
                gl_Position = vec4(aPos.xy, 0.0, 1.0);
            }
            )";
    static const char* tessControlShaderSrc = R"(
            #version 430 core
            layout (vertices = 16) out;

            void main()
            {
                gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

                if (gl_InvocationID == 0)
                {
                    gl_TessLevelInner[0] = 8.0;
                    gl_TessLevelInner[1] = 8.0;
                    gl_TessLevelOuter[0] = 16.0;
                    gl_TessLevelOuter[1] = 16.0;
                    gl_TessLevelOuter[2] = 16.0;
                    gl_TessLevelOuter[3] = 16.0;
                }
            }
            )";
    static const char* tessEvaluationShaderSrc = R"(
            #version 430 core
            layout(quads, equal_spacing, ccw) in;

            out vec2 tessCoord;

            // 最大渲染深度64，应该是够用了
            uniform int surfaceCount;
            uniform mat4 transforms[64];
            uniform vec2 controlPoints[64][16];

            float B0(float t) { return (1.0 - t) * (1.0 - t) * (1.0 - t); }
            float B1(float t) { return 3.0 * t * (1.0 - t) * (1.0 - t); }
            float B2(float t) { return 3.0 * t * t * (1.0 - t); }
            float B3(float t) { return t * t * t; }
            vec2 bezierSurface(vec2 uv, int idx) {
                float u = uv.x;
                float v = uv.y;

                vec2 result = vec2(0.0);

                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        int index = i * 4 + j;

                        // 计算基函数乘积
                        float bu = 0.0;
                        float bv = 0.0;

                        if (i == 0) bu = B0(u);
                        else if (i == 1) bu = B1(u);
                        else if (i == 2) bu = B2(u);
                        else if (i == 3) bu = B3(u);

                        if (j == 0) bv = B0(v);
                        else if (j == 1) bv = B1(v);
                        else if (j == 2) bv = B2(v);
                        else if (j == 3) bv = B3(v);

                        float basis = bu * bv;
                        vec2 controlPoint = controlPoints[idx][index];
                        result += controlPoint * basis;
                    }
                }

                return result;
            }

            void main(void)
            {
                float u = gl_TessCoord.x;
                float v = gl_TessCoord.y;

                vec4 lastPt = vec4(u, v, 0, 0);
                for (int i = 0; i < surfaceCount; i++) {
                    if (i > 0) {
                        lastPt = vec4(1.0 - (lastPt.y / 2.0 + 0.5), 0.5 + lastPt.x / 2.0, 0, 0);
                    }
                    vec2 position = bezierSurface(lastPt.xy, i);
                    lastPt = transforms[i] * vec4(position.xy, 0, 1);
                }
                gl_Position = lastPt * vec4(1.0, -1.0, 1.0, 1.0);

                tessCoord = vec2(gl_TessCoord.y, gl_TessCoord.x);
            }
            )";
    static const char* fragmentShaderSrc = R"(
            #version 430 core
            out vec4 FragColor;
            in vec2 tessCoord;
            uniform sampler2D texture1;
            uniform bool enableMask = false;
            uniform vec2 viewportSize;
            uniform sampler2D maskTexture;
            uniform float opa = 1.0;
            void main()
            {
                vec4 maskColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);
                if (enableMask) {
                    vec2 normalizedCoord = gl_FragCoord.xy / viewportSize;
                    maskColor = texture(maskTexture, normalizedCoord);
                }

                vec4 color = texture(texture1, tessCoord);
                if (enableMask && maskColor.a < 0.5) {
                    discard;
                } else {
                    color.a = color.a * opa;
                    FragColor = vec4(color.rgba);
                }
            }
        )";
#else
    static const char* vertexShaderSrc = R"(#version 320 es
            layout (location = 0) in vec2 aPos;

            void main()
            {
                gl_Position = vec4(aPos.xy, 0.0, 1.0);
            }
            )";
    static const char* tessControlShaderSrc = R"(#version 320 es
            layout (vertices = 16) out;

            void main()
            {
                gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

                if (gl_InvocationID == 0)
                {
                    gl_TessLevelInner[0] = 8.0;
                    gl_TessLevelInner[1] = 8.0;
                    gl_TessLevelOuter[0] = 16.0;
                    gl_TessLevelOuter[1] = 16.0;
                    gl_TessLevelOuter[2] = 16.0;
                    gl_TessLevelOuter[3] = 16.0;
                }
            }
            )";
    static const char* tessEvaluationShaderSrc = R"(#version 320 es
            layout(quads, equal_spacing, ccw) in;

            out vec2 tessCoord;

            // 最大渲染深度20，应该是够用了?
            uniform int surfaceCount;
            uniform mat4 transforms[20];
            uniform vec2 controlPoints[20][16];

            float B0(float t) { return (1.0 - t) * (1.0 - t) * (1.0 - t); }
            float B1(float t) { return 3.0 * t * (1.0 - t) * (1.0 - t); }
            float B2(float t) { return 3.0 * t * t * (1.0 - t); }
            float B3(float t) { return t * t * t; }
            vec2 bezierSurface(vec2 uv, int idx) {
                float u = uv.x;
                float v = uv.y;

                vec2 result = vec2(0.0);

                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        int index = i * 4 + j;

                        // 计算基函数乘积
                        float bu = 0.0;
                        float bv = 0.0;

                        if (i == 0) bu = B0(u);
                        else if (i == 1) bu = B1(u);
                        else if (i == 2) bu = B2(u);
                        else if (i == 3) bu = B3(u);

                        if (j == 0) bv = B0(v);
                        else if (j == 1) bv = B1(v);
                        else if (j == 2) bv = B2(v);
                        else if (j == 3) bv = B3(v);

                        float basis = bu * bv;
                        vec2 controlPoint = controlPoints[idx][index];
                        result += controlPoint * basis;
                    }
                }

                return result;
            }

            void main(void)
            {
                float u = gl_TessCoord.x;
                float v = gl_TessCoord.y;

                vec4 lastPt = vec4(u, v, 0, 0);
                for (int i = 0; i < surfaceCount; i++) {
                    if (i > 0) {
                        lastPt = vec4(1.0 - (lastPt.y / 2.0 + 0.5), 0.5 + lastPt.x / 2.0, 0.0, 0.0);
                    }
                    vec2 position = bezierSurface(lastPt.xy, i);
                    lastPt = transforms[i] * vec4(position.xy, 0, 1);
                }
                gl_Position = lastPt * vec4(1.0, -1.0, 1.0, 1.0);

                tessCoord = vec2(gl_TessCoord.y, gl_TessCoord.x);
            }
            )";
    static const char* fragmentShaderSrc = R"(#version 320 es
            out mediump vec4 FragColor;
            in mediump vec2 tessCoord;
            uniform sampler2D texture1;
            uniform bool enableMask;
            uniform mediump vec2 viewportSize;
            uniform sampler2D maskTexture;
            uniform mediump float opa;
            void main()
            {
                mediump vec4 maskColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);
                if (enableMask) {
                    mediump vec2 normalizedCoord = gl_FragCoord.xy / viewportSize;
                    maskColor = texture(maskTexture, normalizedCoord);
                }

                mediump vec4 color = texture(texture1, tessCoord);
                if (enableMask && maskColor.a < 0.5) {
                    discard;
                } else {
                    color.a = color.a * opa;
                    FragColor = vec4(color.rgba);
                }
            }
        )";
#endif
    void checkGLError(const char* location)
    {
        GLenum error = glGetError();
        while (error != GL_NO_ERROR)
        {
            const char* errorStr = "";
            switch (error)
            {
                case GL_INVALID_ENUM:
                    errorStr = "GL_INVALID_ENUM";
                    break;
                case GL_INVALID_VALUE:
                    errorStr = "GL_INVALID_VALUE";
                    break;
                case GL_INVALID_OPERATION:
                    errorStr = "GL_INVALID_OPERATION";
                    break;
                case GL_INVALID_FRAMEBUFFER_OPERATION:
                    errorStr = "GL_INVALID_FRAMEBUFFER_OPERATION";
                    break;
                case GL_OUT_OF_MEMORY:
                    errorStr = "GL_OUT_OF_MEMORY";
                    break;
            }
            SDL_Log("OpenGL ES Error at %s: %s (0x%04X)\n", location, errorStr, error);
            error = glGetError();
        }
    }
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
    GLuint createRenderProgram()
    {
        GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
        GLuint tcs = compileShader(GL_TESS_CONTROL_SHADER, tessControlShaderSrc);
        GLuint tes = compileShader(GL_TESS_EVALUATION_SHADER, tessEvaluationShaderSrc);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, tcs);
        glAttachShader(prog, tes);
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
        glDeleteShader(tcs);
        glDeleteShader(tes);
        glDeleteShader(fs);
        return prog;
    }
    GLuint createEmptyTexture(int width, int height)
    {
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        return texture;
    }
    GLuint createEmptyDepthTexture(int width, int height)
    {
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT,
                     GL_UNSIGNED_INT, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return texture;
    }
    GLuint createFBO(GLuint texture, GLuint depthtexture)
    {
        GLuint result;
        glGenFramebuffers(1, &result);
        glBindFramebuffer(GL_FRAMEBUFFER, result);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthtexture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            SDL_Log("Framebuffer不完整!");
        }

        return result;
    }
    GLfloat default_control_points[32] = {
        0.000000f, 0.000000f, 0.333333f, 0.000000f, 0.666667f, 0.000000f, 1.000000f, 0.000000f,
        0.000000f, 0.333333f, 0.333333f, 0.333333f, 0.666667f, 0.333333f, 1.000000f, 0.333333f,
        0.000000f, 0.666667f, 0.333333f, 0.666667f, 0.666667f, 0.666667f, 1.000000f, 0.666667f,
        0.000000f, 1.000000f, 0.333333f, 1.000000f, 0.666667f, 1.000000f, 1.000000f, 1.000000f};
    void glBaseSet()
    {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClearDepthf(-1.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (emotenodeprogram == 0 || glIsProgram(emotenodeprogram) != GL_TRUE)
        {
            // 程序
            emotenodeprogram = createRenderProgram();
            // array
            glGenVertexArrays(1, &emotenodeVAO);
        }
        glUseProgram(emotenodeprogram);
        glBindVertexArray(emotenodeVAO);
    }
    void glBaseSetWithoutClear()
    {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClearDepthf(-1.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        if (emotenodeprogram == 0 || glIsProgram(emotenodeprogram) != GL_TRUE)
        {
            // 程序
            emotenodeprogram = createRenderProgram();
            // array
            glGenVertexArrays(1, &emotenodeVAO);
        }
        glUseProgram(emotenodeprogram);
        glBindVertexArray(emotenodeVAO);
    }

    #pragma endregion

#pragma region Base

    emoteframe::emoteframe(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // time type
        filePtr->parseReal(time, _rootData["time"]);
        int64_t tmp;
        filePtr->parseNumber(tmp, _rootData["type"]);
        type = static_cast<int8_t>(tmp);
        // content
        auto it = _rootData.find("content");
        if (it != _rootData.end())
        {
            hasContent = true;
            filePtr->updateSyncTime(time);
            filePtr->parseObject(_rootData, it->second);
            // coord angle s z zcc
            it = _rootData.find("coord");
            if (it != _rootData.end())
            {
                std::vector<uint32_t> _listOffset;
                filePtr->parseList(_listOffset, it->second);
                if (_listOffset.size() == 3)
                {
                    filePtr->parseReal(coordX, _listOffset[0]);
                    filePtr->parseReal(coordY, _listOffset[1]);
                    filePtr->parseReal(coordZ, _listOffset[2]);
                    filePtr->updateZMax(coordZ);
                }
            }
            it = _rootData.find("angle");
            if (it != _rootData.end())
            {
                filePtr->parseReal(angle, it->second);
            }
            it = _rootData.find("sx");
            if (it != _rootData.end())
            {
                filePtr->parseReal(sx, it->second);
            }
            it = _rootData.find("sy");
            if (it != _rootData.end())
            {
                filePtr->parseReal(sy, it->second);
            }
            it = _rootData.find("zx");
            if (it != _rootData.end())
            {
                filePtr->parseReal(zx, it->second);
            }
            it = _rootData.find("zy");
            if (it != _rootData.end())
            {
                filePtr->parseReal(zy, it->second);
            }
            it = _rootData.find("ox");
            if (it != _rootData.end())
            {
                filePtr->parseReal(ox, it->second);
            }
            it = _rootData.find("oy");
            if (it != _rootData.end())
            {
                filePtr->parseReal(oy, it->second);
            }
            it = _rootData.find("zcc");
            if (it != _rootData.end())
            {
                std::map<std::string, uint32_t> zcc_map;
                if (filePtr->parseObject(zcc_map, it->second))
                {
                    haszcc = true;
                    // c
                    std::vector<uint32_t> zcc_list;
                    filePtr->parseList(zcc_list, zcc_map["c"]);
                    if (zcc_list.size() == 2)
                    {
                        for (int i = 0; i < 2; i++)
                            filePtr->parseReal(zcc_c[i], zcc_list[i]);
                    }
                    // x
                    zcc_list.clear();
                    filePtr->parseList(zcc_list, zcc_map["x"]);
                    if (zcc_list.size() == 4)
                    {
                        for (int i = 0; i < 4; i++)
                            filePtr->parseReal(zcc_x[i], zcc_list[i]);
                    }
                    // y
                    zcc_list.clear();
                    filePtr->parseList(zcc_list, zcc_map["y"]);
                    if (zcc_list.size() == 4)
                    {
                        for (int i = 0; i < 4; i++)
                            filePtr->parseReal(zcc_y[i], zcc_list[i]);
                    }
                }
            }
            // ccc
            it = _rootData.find("ccc");
            if (it != _rootData.end())
            {
                std::map<std::string, uint32_t> ccc_map;
                if (filePtr->parseObject(ccc_map, it->second))
                {
                    hasccc = true;
                    // c
                    std::vector<uint32_t> ccc_list;
                    filePtr->parseList(ccc_list, ccc_map["c"]);
                    if (ccc_list.size() == 2)
                    {
                        for (int i = 0; i < 2; i++)
                            filePtr->parseReal(ccc_c[i], ccc_list[i]);
                    }
                    // x
                    ccc_list.clear();
                    filePtr->parseList(ccc_list, ccc_map["x"]);
                    if (ccc_list.size() == 4)
                    {
                        for (int i = 0; i < 4; i++)
                            filePtr->parseReal(ccc_x[i], ccc_list[i]);
                    }
                    // y
                    ccc_list.clear();
                    filePtr->parseList(ccc_list, ccc_map["y"]);
                    if (ccc_list.size() == 4)
                    {
                        for (int i = 0; i < 4; i++)
                            filePtr->parseReal(ccc_y[i], ccc_list[i]);
                    }
                }
            }
            // src
            if (filePtr->isKrkr)
            {
                filePtr->parseString(src, _rootData["src"]);
            }
            else
            {
                it = _rootData.find("src");
                if (it == _rootData.end()) // 说明此时是layout
                {
                    src = "layout";
                }
                else // 否则按照旧规则直接拼接吧
                {
                    std::string tmp;
                    // 初始为src，然后再看看是否为motion
                    src = "src/";
                    it = _rootData.find("motion");
                    if (it != _rootData.end())
                    {
                        src = "motion/";
                        // 获取timeoffset
                        std::map<std::string, uint32_t> tmpMap;
                        if (filePtr->parseObject(tmpMap, it->second))
                        {
                            it = tmpMap.find("timeOffset");
                            if (it != tmpMap.end())
                            {
                                filePtr->parseReal(timeOffset, it->second);
                            }
                        }
                    }
                    filePtr->parseString(tmp, _rootData["src"]);
                    // 还有一个blank情形
                    if (strcmp(tmp.c_str(), "blank") == 0)
                    {
                        src = "";
                    }
                    src += tmp;
                    filePtr->parseString(tmp, _rootData["icon"]);
                    src += "/" + tmp;
                    src.erase(std::remove(src.begin(), src.end(), '\0'), src.end());
                }
            }
            // mask
            filePtr->parseNumber(mask, _rootData["mask"]);
            // bm
            it = _rootData.find("bm");
            if (it != _rootData.end())
            {
                filePtr->parseNumber(bm, it->second);
            }
            // opa
            it = _rootData.find("opa");
            if (it != _rootData.end())
            {
                filePtr->parseReal(opa, it->second);
            }
            // mesh
            auto it = _rootData.find("mesh");
            if (it != _rootData.end())
            {
                filePtr->parseObject(_rootData, it->second);
                // bp
                std::vector<uint32_t> meshOffset;
                filePtr->parseList(meshOffset, _rootData["bp"]);
                if (meshOffset.size() == 32)
                {
                    hasbp = true;
                    for (int i = 0; i < 32; i++)
                        filePtr->parseReal(bp[i], meshOffset[i]);
                }
                // cc
                auto it = _rootData.find("cc");
                if (it != _rootData.end())
                {
                    if (filePtr->parseObject(_rootData, it->second))
                    {
                        hascc = true;
                        // c
                        meshOffset.clear();
                        filePtr->parseList(meshOffset, _rootData["c"]);
                        if (meshOffset.size() == 2)
                        {
                            for (int i = 0; i < 2; i++)
                                filePtr->parseReal(cc_c[i], meshOffset[i]);
                        }
                        // x
                        meshOffset.clear();
                        filePtr->parseList(meshOffset, _rootData["x"]);
                        if (meshOffset.size() == 4)
                        {
                            for (int i = 0; i < 4; i++)
                                filePtr->parseReal(cc_x[i], meshOffset[i]);
                        }
                        // y
                        meshOffset.clear();
                        filePtr->parseList(meshOffset, _rootData["y"]);
                        if (meshOffset.size() == 4)
                        {
                            for (int i = 0; i < 4; i++)
                                filePtr->parseReal(cc_y[i], meshOffset[i]);
                        }
                    }
                }
            }
        }
    }
    emoteframe::~emoteframe()
    {
    }

    emotenode::emotenode(emotemotion* rootmotion,
                         emotenode* parent,
                         std::vector<emotenode*>& nodeList,
                         emotefile* filePtr,
                         uint32_t startOffset)
      : _parent(parent),
        _rootmotion(rootmotion),
        _filePtr(filePtr)
    {
        // add to mgn ss
        nodeList.push_back(this);
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // meshInfo
        int64_t tmp = 0;
        auto it = _rootData.find("meshCombine");
        if (it != _rootData.end())
        {
            filePtr->parseNumber(tmp, it->second);
            meshCombine = static_cast<uint8_t>(tmp);
        }
        it = _rootData.find("meshDivision");
        if (it != _rootData.end())
        {
            filePtr->parseNumber(tmp, it->second);
            meshDivision = static_cast<uint8_t>(tmp);
        }
        it = _rootData.find("meshTransform");
        if (it != _rootData.end())
        {
            filePtr->parseNumber(tmp, it->second);
            meshTransform = static_cast<uint8_t>(tmp);
        }
        filePtr->parseNumber(tmp, _rootData["type"]);
        type = static_cast<uint8_t>(tmp);
        // mask
        it = _rootData.find("meshSyncChildMask");
        if (it != _rootData.end())
        {
            filePtr->parseNumber(tmp, it->second);
            meshSyncChildMask = static_cast<uint32_t>(tmp);
        }
        it = _rootData.find("inheritMask");
        if (it != _rootData.end())
        {
            filePtr->parseNumber(tmp, it->second);
            inheritMask = static_cast<uint32_t>(tmp);
        }
        // var
        it = _rootData.find("parameterize");
        if (it != _rootData.end())
        {
            if (filePtr->parseNumber(tmp, it->second))
            {
                isParameterize = true;
                parameterIdx = static_cast<int32_t>(tmp);
            }
        }
        // label
        filePtr->parseString(label, _rootData["label"]);
        // frameList
        std::vector<uint32_t> _tmpList;
        filePtr->parseList(_tmpList, _rootData["frameList"]);
        for (size_t i = 0; i < _tmpList.size(); i++)
        {
            emoteframe* tmp = new emoteframe(filePtr, _tmpList.at(i));
            frameList.push_back(tmp);
        }
        // children
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["children"]);
        for (size_t i = 0; i < _tmpList.size(); i++)
        {
            emotenode* tmp = new emotenode(_rootmotion, this, nodeList, filePtr, _tmpList.at(i));
            children.push_back(tmp);
        }
        // refmask List
        _tmpList.clear();
        it = _rootData.find("stencilCompositeMaskLayerList");
        if (it != _rootData.end())
        {
            filePtr->parseList(_tmpList, it->second);
            for (size_t i = 0; i < _tmpList.size(); i++)
            {
                std::string refName;
                filePtr->parseString(refName, _tmpList.at(i));
                stencilCompositeMaskLayerList.push_back(refName);
            }
        }
    }
    emotenode::~emotenode()
    {
        for (auto frame : frameList)
        {
            if (frame != nullptr)
                delete frame;
        }
        for (auto child : children)
        {
            if (child != nullptr)
                delete child;
        }
        if (isIcon && data != nullptr)
            delete data;
        if (selftexture != 0 && glIsTexture(selftexture) == GL_TRUE)
        {
            glDeleteTextures(1, &selftexture);
        }
    }
    void emotenode::checkDrawStatus(float tick, std::vector<emoteRender>& renderList, emotelimit lim)
    {
        // 不绘制进行节点传递
        if (renderList.size() > 0 && renderList.back().type == 0)
        {
            isNeedDraw = false;
            return;
        }

        // 确定时间轴
        if (frameList.size() == 0)
        {
            isNeedDraw = false;
            return;
        }
        frame = nullptr;
        size_t currFrameIdx = -1;
        for (size_t i = 0; i < frameList.size(); i++)
        {
            if (frameList.at(i)->time <= tick)
            {
                frame = frameList.at(i);
                currFrameIdx = i;
            }
            else
                break;
        }
        if (frame == nullptr || !frame->hasContent)
        {
            isNeedDraw = false;
            return;
        }
        nextframe = nullptr;
        if (currFrameIdx >= 0 && currFrameIdx < frameList.size() - 1)
            nextframe = frameList.at(currFrameIdx + 1);
        if (nextframe != nullptr && !nextframe->hasContent)
            nextframe = nullptr;

        // 节点基础信息获取
        isNeedDraw = true;
        isIcon = false;
        isLayout = false;
        emoteicon* tmpic = _filePtr->findsourceByName(frame->src);
        if (tmpic == nullptr)
            emot = _filePtr->findmotionByName(frame->src);
        else
            emot = nullptr;
        if (tmpic != nullptr)
        {
            isIcon = true;

            if (tmpic != ic) // 直接比对ic
            {
                ic = tmpic;
                width = ic->width;
                height = ic->height;
                resizeMainData();
                resizeOpenGL();
                originX = ic->originX;
                originY = ic->originY;
                // 读取像素数据
                _filePtr->readIconTobuffer(data, width * height * 4, width * 4, ic);
                glBindTexture(GL_TEXTURE_2D, selftexture);
#if _KRKRSDL3_GL
                if (_filePtr->colorType == 0)
                {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, width, height, 0, GL_BGRA,
                                 GL_UNSIGNED_BYTE, data);
                }
                else if (_filePtr->colorType == 1)
                {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                                 GL_UNSIGNED_BYTE, data);
                }
                else
                {
                    SDL_Log("unknow colorType");
                }
#else
                if (_filePtr->colorType == 0)
                {
                    // 色彩转化
                    for (size_t i = 0; i < width * height; i++)
                    {
                        uint8_t tmp = data[4 * i];
                        data[4 * i] = data[4 * i + 2];
                        data[4 * i + 2] = tmp;
                    }
                }
                else if (_filePtr->colorType != 1)
                {
                    SDL_Log("unknow colorType");
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             data);
#endif
                glGenerateMipmap(GL_TEXTURE_2D);
            }
            // 设置混色
            currbm = frame->bm;
        }
        else if (emot != nullptr || strcmp(frame->src.c_str(), "layout") == 0 ||
                 strcmp(frame->src.c_str(), "clip") == 0)
        {
            isLayout = true;

            // 直接用父类提供的区域
            if (width != lim.width || height != lim.height)
            {
                width = lim.width;
                height = lim.height;
                originX = lim.originX;
                originY = lim.originY;
            }
        }
        else
        {
            std::istringstream iss(frame->src);
            std::string token;
            std::getline(iss, token, '/');
            if (strcmp(token.c_str(), "blank") == 0)
            {
                std::getline(iss, token, ':');
                int32_t w = std::stoi(token);
                std::getline(iss, token, ':');
                int32_t h = std::stoi(token);
                std::getline(iss, token, ':');
                int32_t ox = std::stoi(token);
                std::getline(iss, token, ':');
                int32_t oy = std::stoi(token);

                if (width != w || height != h)
                {
                    width = w;
                    height = h;
                }
                originX = ox;
                originY = oy;
            }
            else if (strcmp(token.c_str(), "shape") == 0)
            {
                // TODO
                isNeedDraw = false;
                return;
            }
            else
            {
                SDL_Log("source unsupported!!!--->%s", frame->src.c_str());
                isNeedDraw = false;
                return;
            }
        }
    }
    void emotenode::progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim)
    {
        // 参数化时可能改变
        currTick = tick;
        // 对于motion，增加终结机制, 即无法越过selfSyncTime
        if (_filePtr->isMotion && currTick > _rootmotion->selfSyncTime)
            currTick = _rootmotion->selfSyncTime;
        // 再来一个非时间戳节点, 采用时间永驻机制
        if (_filePtr->isMotion && frameList.size() == 2 && frameList.at(0)->type == 2 &&
            frameList.at(1)->type == 0)
            currTick = frameList.at(0)->time;
        // 不会处理，先跳过
        if (type == 12)
        {
            checkDrawStatus(currTick, renderList, lim);
            isNeedDraw = true;
            isLayout = true;
            originX = lim.originX;
            originY = lim.originY;
            width = lim.width;
            height = lim.height;
        }
        // 对于参数节点 进行参数反查来定位tick
        else if (isParameterize)
        {
            if (_rootmotion != nullptr)
            {
                float new_tick = _rootmotion->getTickByIdx(parameterIdx);
                if (new_tick >= 0.0)
                {
                    checkDrawStatus(new_tick, renderList, lim);
                    currTick = new_tick;
                }
                else
                    isNeedDraw = false;
            }
            else
                isNeedDraw = false;
        }
        else
        {
            // 时间戳判断 绘制信息检测
            checkDrawStatus(currTick, renderList, lim);
        }

        // 构建渲染方法
        renderMethod.clear();
        renderMethod = renderList;
        if ((!isNeedDraw && (width == 0 || height == 0)) || type == 7)
        {
            originX = lim.originX;
            originY = lim.originY;
            width = lim.width;
            height = lim.height;
        }
        else
        {
            // 基础参数
            if (nextframe != nullptr &&
                ((frame->type != 2) ||
                 (frame->type == 2 && nextframe->type == 2))) // 存在下一帧则对关键帧进行插值
            {
                // 针对nan/inf情形动态完成刷新
                if (std::isnan(frame->coordX)) frame->coordX = -lim.originX;
                if (std::isnan(frame->coordY)) frame->coordY = -lim.originY;
                if (std::isnan(nextframe->coordX)) nextframe->coordX = -lim.originX;
                if (std::isnan(nextframe->coordY)) nextframe->coordY = -lim.originY;
                if (std::isinf(frame->coordX)) frame->coordX = lim.width-lim.originX;
                if (std::isinf(frame->coordY)) frame->coordY = lim.width-lim.originY;
                if (std::isinf(nextframe->coordX)) nextframe->coordX = lim.height-lim.originX;
                if (std::isinf(nextframe->coordY)) nextframe->coordY = lim.height-lim.originY;

                // 坐标
                currCoordx =
                    (frame->coordX + (nextframe->coordX - frame->coordX) /
                                                  (nextframe->time - frame->time) *
                                                  (currTick - frame->time));
                currCoordy =
                    (frame->coordY + (nextframe->coordY - frame->coordY) /
                                                  (nextframe->time - frame->time) *
                                                  (currTick - frame->time));
                currCoordz =
                    (frame->coordZ + (nextframe->coordZ - frame->coordZ) /
                                                  (nextframe->time - frame->time) *
                                                  (currTick - frame->time));
                // 透明度
                currOpa = (frame->opa + (nextframe->opa - frame->opa) / (nextframe->time - frame->time) *
                                      (currTick - frame->time));
                // 变换参数(太sb了，感觉180才是分界点)
                if (nextframe->angle < 180 && 
                    frame->angle > 180) // 从 小360 到大0
                {
                    currAngle = (frame->angle - 360 +
                                 (nextframe->angle + 360 - frame->angle) /
                                                    (nextframe->time - frame->time) * (currTick - frame->time));
                }
                else if (nextframe->angle > 180 && frame->angle < 180) // 从 大0 到 小360
                {
                    currAngle = (frame->angle +
                                 (nextframe->angle - 360 - frame->angle) /
                                     (nextframe->time - frame->time) * (currTick - frame->time));
                }
                else
                {
                    currAngle = (frame->angle + (nextframe->angle - frame->angle) /
                                                    (nextframe->time - frame->time) *
                                                    (currTick - frame->time));
                }
                currSx = (frame->sx + (nextframe->sx - frame->sx) / (nextframe->time - frame->time) *
                                     (currTick - frame->time));
                currSy = (frame->sy + (nextframe->sy - frame->sy) / (nextframe->time - frame->time) *
                                     (currTick - frame->time));
                currZx = (frame->zx + (nextframe->zx - frame->zx) / (nextframe->time - frame->time) *
                                     (currTick - frame->time));
                currZy = (frame->zy + (nextframe->zy - frame->zy) / (nextframe->time - frame->time) *
                                     (currTick - frame->time));
                currOx =
                    (frame->ox + (nextframe->ox - frame->ox) / (nextframe->time - frame->time) *
                                     (currTick - frame->time));
                currOy =
                    (frame->oy + (nextframe->oy - frame->oy) / (nextframe->time - frame->time) *
                                     (currTick - frame->time));
                // 偏移
                currTimeOffset = (frame->timeOffset + (nextframe->timeOffset - frame->timeOffset) /
                                                          (nextframe->time - frame->time) *
                                                          (currTick - frame->time));
                // 网格参数
                if (frame->hasbp || nextframe->hasbp)
                {
                    isNeedBp = true;
                    for (size_t i = 0; i < 32; i++)
                        currbp[i] = (frame->bp[i] + (nextframe->bp[i] - frame->bp[i]) /
                                                        (nextframe->time - frame->time) *
                                                        (currTick - frame->time));
                }
                else
                    isNeedBp = false;
            }
            else
            {
                // 针对nan/inf情形动态完成刷新
                if (std::isnan(frame->coordX)) frame->coordX = -lim.originX;
                if (std::isnan(frame->coordY)) frame->coordY = -lim.originY;
                if (std::isinf(frame->coordX)) frame->coordX = lim.width-lim.originX;
                if (std::isinf(frame->coordY)) frame->coordY = lim.width-lim.originY;

                // 计算坐标
                currCoordx = frame->coordX;
                currCoordy = frame->coordY;
                currCoordz = frame->coordZ;
                // 透明度
                currOpa = frame->opa;
                // 变换参数
                currAngle = frame->angle;
                currSx = frame->sx, currSy = frame->sy;
                currZx = frame->zx, currZy = frame->zy;
                currOx = frame->ox, currOy = frame->oy;
                // 偏移
                currTimeOffset = frame->timeOffset;
                // 网格参数
                if (frame->hasbp)
                {
                    isNeedBp = true;
                    for (size_t i = 0; i < 32; i++)
                        currbp[i] = frame->bp[i];
                }
                else
                    isNeedBp = false;
            }

            // 有深度信息时，穿透到最顶层
            if (renderMethod.size() > 0 && currCoordz != 0.0)
            {
                if (renderMethod.at(0).type == 3)
                {
                    renderMethod.at(0).matTrans =
                        glm::translate(renderMethod.at(0).matTrans, glm::vec3(0.0, 0.0, currCoordz));
                }
                else
                {
                    glm::mat4 proj = glm::ortho(-renderMethod.at(0).originX,
                                                renderMethod.at(0).width - renderMethod.at(0).originX,
                                                renderMethod.at(0).height - renderMethod.at(0).originY,
                                   -renderMethod.at(0).originY, lim.zMax, -lim.zMax);
                    glm::mat4 last = glm::mat4(1.0f);
                    last = glm::translate(last, glm::vec3(0.0, 0.0, currCoordz));
                    last = last * glm::inverse(proj) * renderMethod.at(0).matTrans;
                    renderMethod.at(0).matTrans = proj * last;
                }
                GLM_ASSERT_VALID(renderMethod.at(0).matTrans);
            }

            // 有模板信息时，穿透到最顶层
            if (renderMethod.size() > 0 && type == 12)
            {
                renderMethod.at(0).hasStencil = true;
                for (auto nodeName : stencilCompositeMaskLayerList)
                {
                    // 让父类去找节点
                    emotenode* tmpNode = _rootmotion->getNodeByName(nodeName);
                    if (tmpNode != nullptr)
                    {
                        renderMethod.at(0).layerNode.push_back(tmpNode);
                    }
                }
            }

            // 是否需要同步父节点变形 玩不明白啊，反正视觉效果还行，就算了吧
            // if ((inheritMask >> 25 & 0x1) != 0x1 && renderMethod.size() > 0 &&
            // renderMethod.back().type == 1)
            //{
            //    emoteRender emt = renderMethod.back();
            //    renderMethod.pop_back();
            //    emt.type = 2;
            //    renderMethod.push_back(emt);
            //}

            // 构建变换矩阵 平移 currCoordx/currCoordy → 缩放 zx/zy → 剪切 sx/sy → 旋转 angle
            glm::mat4 model = glm::mat4(1.0f); // 注:复合顺序是反过来的
            model = glm::translate(model, glm::vec3(currCoordx, currCoordy, 0));
            model = glm::rotate(model, glm::radians(currAngle), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::mat4(1.0f, currSy, 0.0f, 0.0f, currSx, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                              0.0f, 0.0f, 0.0f, 0.0f, 1.0f) *
                    model;
            model = glm::scale(model, glm::vec3(currZx, currZy, 1.0f));

            if (isLayout) // 不绘制，不处理网格数据，只构建变换矩阵
            {
                if (renderMethod.size() > 0 &&
                    renderMethod.back().type == 3) // 如果上一级是layout的话，则进行合并
                {
                    // 构造渲染方法结构
                    emoteRender emt = renderMethod.back();
                    renderMethod.pop_back();
                    emt.type = 3;

                    // 更新矩阵
                    emt.matTrans = emt.matTrans * model;

                    // fbo信息
                    emt.originX = originX;
                    emt.originY = originY;
                    emt.width = width;
                    emt.height = height;
                    emt.label = label;
                    GLM_ASSERT_VALID(emt.matTrans);
                    renderMethod.push_back(emt);
                }
                else
                {
                    // 构造渲染方法结构
                    emoteRender emt;
                    emt.type = 3;

                    // 如果父类没有网格变形，则进行矩阵解耦并外加合并
                    if (renderMethod.size() > 1 && renderMethod.back().type == 2)
                    {
                        glm::mat4 demuxMat =
                            glm::scale(renderMethod.back().matTrans,
                                       glm::vec3(1 / renderMethod.back().width,
                                                 1 / renderMethod.back().height, 1.0f));
                        demuxMat =
                            glm::translate(demuxMat, glm::vec3(renderMethod.back().originX,
                                                               renderMethod.back().originY, 0.0f));
                        emt.matTrans = demuxMat * model;
                        emt.opa *= renderMethod.back().opa;
                        renderMethod.pop_back();
                        // 绘制层解耦
                        originX = renderMethod.back().originX;
                        originY = renderMethod.back().originY;
                        width = renderMethod.back().width;
                        height = renderMethod.back().height;
                        emt.matTrans =
                            glm::inverse(glm::ortho(-originX, width - originX, height - originY,
                                                    -originY, lim.zMax, -lim.zMax)) *
                            emt.matTrans;
                    }
                    else // 正常更新矩阵
                    {
                        emt.matTrans = model;
                        
                    }

                    // fbo信息
                    emt.originX = originX;
                    emt.originY = originY;
                    emt.width = width;
                    emt.height = height;
                    emt.label = label;
                    GLM_ASSERT_VALID(emt.matTrans);
                    renderMethod.push_back(emt);
                }
            }
            else // 需要绘制，构建绘制矩阵和变形参数
            {
                if (renderMethod.size() > 0 &&
                    renderMethod.back().type == 3) // 如果上一级是layout的话，则进行合并
                {
                    // 构造渲染方法结构
                    emoteRender emt = renderMethod.back();
                    renderMethod.pop_back();
                    emt.type = 2;
                    emt.opa *= currOpa;
                    if (isNeedBp)
                    {
                        for (size_t i = 0; i < 32; i++)
                            emt.controlPts[i] = currbp[i];
                        emt.type = 1;
                    }

                    // 更新矩阵
                    glm::mat4 projection =
                        glm::ortho(-lim.originX, lim.width - lim.originX, lim.height - lim.originY,
                                   -lim.originY, lim.zMax, -lim.zMax);
                    model = glm::translate(model,
                                           glm::vec3(-originX - currOx, -originY - currOy, 0.0f));
                    model = glm::scale(model, glm::vec3(width, height, 1.0f));
                    emt.matTrans = projection * emt.matTrans * model;

                    // fbo信息
                    emt.originX = originX;
                    emt.originY = originY;
                    emt.width = width;
                    emt.height = height;
                    emt.label = label;
                    GLM_ASSERT_VALID(emt.matTrans);
                    renderMethod.push_back(emt);
                }
                else
                {
                    // 构造渲染方法结构
                    emoteRender emt;
                    emt.type = 2;
                    emt.opa = currOpa;
                    if (isNeedBp)
                    {
                        for (size_t i = 0; i < 32; i++)
                            emt.controlPts[i] = currbp[i];
                        emt.type = 1;
                    }

                    // 如果父类没有网格变形，则进行矩阵解耦并外加合并
                    if (renderMethod.size() > 0 && renderMethod.back().type == 2)
                    {
                        glm::mat4 demuxMat =
                            glm::scale(renderMethod.back().matTrans,
                                       glm::vec3(1 / renderMethod.back().width,
                                                 1 / renderMethod.back().height, 1.0f));
                        demuxMat =
                            glm::translate(demuxMat, glm::vec3(renderMethod.back().originX,
                                                               renderMethod.back().originY, 0.0f));
                        model = glm::translate(
                            model, glm::vec3(-originX - currOx, -originY - currOy, 0.0f));
                        model = glm::scale(model, glm::vec3(width, height, 1.0f));
                        emt.matTrans = demuxMat * model;
                        emt.opa *= renderMethod.back().opa;
                        renderMethod.pop_back();
                    }
                    else // 正常更新矩阵
                    {
                        glm::mat4 projection =
                            glm::ortho(-lim.originX, lim.width - lim.originX, lim.height - lim.originY, -lim.originY, lim.zMax, -lim.zMax);
                        model = glm::translate(
                            model, glm::vec3(-originX - currOx, -originY - currOy, 0.0f));
                        model = glm::scale(model, glm::vec3(width, height, 1.0f));
                        emt.matTrans = projection * model;
                    }

                    // fbo信息
                    emt.originX = originX;
                    emt.originY = originY;
                    emt.width = width;
                    emt.height = height;
                    emt.label = label;
                    GLM_ASSERT_VALID(emt.matTrans);
                    renderMethod.push_back(emt);
                }
            }
        }

        // 传递给子类
        for (auto ch : children)
        {
            ch->progress(tick, renderMethod, {originX, originY, width, height, lim.zMax});
        }

        // 处理motion情形
        if (emot != nullptr)
        {
            emot->progress(tick + currTimeOffset, renderMethod,
                           {originX, originY, width, height, lim.zMax});
        }
    }
    void emotenode::draw(GLuint targetFbo, emotelimit lim, GLuint exFbo, GLuint exTex)
    {
        //if (frame != nullptr && !frame->src.empty() && ic != nullptr)
        //{
        //    SDL_Log("%s with depth:%f and bm:%d", frame->src.c_str(), getCurrentRenderZ(), currbm);
        //}
        //if (frame != nullptr && strcmp(frame->src.c_str(), "src/tex/0066") == 0)
        //{
        //    cv::Mat rgba(height, width, CV_8UC4, data);
        //    cv::imshow("org", rgba);
        //}
        if (emot != nullptr) // 对于motion直接穿透绘制
        {
            emot->draw(targetFbo, lim, exFbo, exTex);
            return;
        }
        if (!isNeedDraw || !isIcon || renderMethod.size() < 1 || removed)
            return; // 跳过无需绘制的 和 非icon的 和 无method 的节点

        //if (frame != nullptr && !frame->src.empty() && ic != nullptr)
        //{
        //    //SDL_Log("%s with depth:%f and bm:%d with tick:%f", frame->src.c_str(), getCurrentRenderZ(), currbm, currTick);
        //    //cv::Mat rgba(height, width, CV_8UC4, data);
        //    //cv::Mat bgra;
        //    //cv::cvtColor(rgba, bgra, cv::COLOR_RGBA2BGRA);
        //    //cv::imshow(frame->src, bgra);
        //}
        // 提前绘制好蒙版texture
        if (renderMethod.at(0).hasStencil && exFbo != 0) // 进行Stencil过滤 不考虑复合蒙版的情况了
        {
            glBindFramebuffer(GL_FRAMEBUFFER, exFbo);
            glBaseSet();
            for (auto maskLayer : renderMethod.at(0).layerNode)
            {
                if (maskLayer != nullptr)
                    maskLayer->draw(exFbo, lim, 0, 0);
            }
        }

        // clear
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glUseProgram(emotenodeprogram);
        glViewport(0, 0, lim.width, lim.height);
#if _KRKRSDL3_GL
        if (renderMethod.size() > 64)
#else
        if (renderMethod.size() > 24)
#endif
        {
            SDL_Log("render:%s failed!!!", label.c_str());
            return;
        }

        //static tjs_uint8* enoughData = new tjs_uint8[lim.width * lim.height * 4];
        //static GLfloat* enoughDepthData = new GLfloat[lim.width * lim.height];
        //if (frame != nullptr && strcmp(frame->src.c_str(), "src/tex/0066") == 0)
        //{
        //    glReadPixels(0, 0, lim.width, lim.height, GL_RGBA, GL_UNSIGNED_BYTE, enoughData);
        //    cv::Mat rgba(lim.height, lim.width, CV_8UC4, enoughData);
        //    cv::imshow("before", rgba);
        //
        //    glReadPixels(0, 0, lim.width, lim.height, GL_DEPTH_COMPONENT, GL_FLOAT,
        //                 enoughDepthData);
        //    cv::Mat depthMat(height, width, CV_32FC1, enoughDepthData);
        //    cv::Mat normalized;
        //    cv::normalize(depthMat, normalized, 0.0, 1.0, cv::NORM_MINMAX);
        //    cv::Mat displayMat;
        //    normalized.convertTo(displayMat, CV_8UC1, 255.0);
        //    cv::Mat colorMap;
        //    cv::applyColorMap(displayMat, colorMap, cv::COLORMAP_JET);
        //    cv::imshow("depth", colorMap);
        //}

        // bm
        switch (currbm)
        {
            case 0:
            {
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
                glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
                break;
            }
            case 1:
            {
                glBlendFuncSeparate(GL_DST_COLOR, GL_ONE, GL_ZERO, GL_ONE);
                glBlendEquation(GL_FUNC_ADD);
                break;
            }
            case 3:
            {
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
                glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
                break;
            }
            case 4:
            {
                glBlendFuncSeparate(GL_DST_COLOR, GL_ONE, GL_ZERO, GL_ONE);
                glBlendEquation(GL_FUNC_ADD);
                break;
            }
            case 6: // TODO
            {
                return;
            }
            default:
            {
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
                glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
            }
        }
        // renderMethod
        glUniform1i(glGetUniformLocation(emotenodeprogram, "surfaceCount"), renderMethod.size());
        int idxCnt = 0;
        float totalOpa = currOpa;
        for (int32_t i = renderMethod.size() - 1; i >= 0; i--)
        {
            // opa
            totalOpa *= renderMethod.at(i).opa;
            // transform
            char uniformName[32];
            sprintf(uniformName, "transforms[%d]", idxCnt);
            glUniformMatrix4fv(glGetUniformLocation(emotenodeprogram, uniformName), 1, GL_FALSE,
                               glm::value_ptr(renderMethod.at(i).matTrans));
            // controlPoints
            sprintf(uniformName, "controlPoints[%d]", idxCnt);
            if (renderMethod.at(i).type == 1)
                glUniform2fv(glGetUniformLocation(emotenodeprogram, uniformName), 16,
                             renderMethod.at(i).controlPts);
            else
                glUniform2fv(glGetUniformLocation(emotenodeprogram, uniformName), 16,
                             default_control_points);
            idxCnt++;
        }
        // opa
        glUniform1f(glGetUniformLocation(emotenodeprogram, "opa"), totalOpa);
        // texture
        if (renderMethod.at(0).hasStencil && exFbo != 0) // 使用exTex作为蒙版过滤
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, selftexture);
            glUniform1i(glGetUniformLocation(emotenodeprogram, "texture1"), 0);
            glUniform1i(glGetUniformLocation(emotenodeprogram, "enableMask"), true);
            glUniform2f(glGetUniformLocation(emotenodeprogram, "viewportSize"), lim.width, lim.height);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, exTex);
            glUniform1i(glGetUniformLocation(emotenodeprogram, "maskTexture"), 1);
            glDrawArrays(GL_PATCHES, 0, 16);
        }
        else
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, selftexture);
            glUniform1i(glGetUniformLocation(emotenodeprogram, "texture1"), 0);
            glUniform1i(glGetUniformLocation(emotenodeprogram, "enableMask"), false);
            glDrawArrays(GL_PATCHES, 0, 16);
        }

        //if (frame != nullptr && strcmp(frame->src.c_str(), "src/tex/0066") == 0)
        //{
        //  static tjs_uint8* enoughData = new tjs_uint8[lim.width * lim.height * 4];
        //  glReadPixels(0, 0, lim.width, lim.height, GL_RGBA, GL_UNSIGNED_BYTE, enoughData);
        //  cv::Mat rgba(lim.height, lim.width, CV_8UC4, enoughData);
        //  cv::Mat bgra;
        //  cv::cvtColor(rgba, bgra, cv::COLOR_RGBA2BGRA);
        //  cv::imshow(frame->src, bgra);
        //}
    }
    float emotenode::getCurrentRenderZ()
    {
        if (renderMethod.size() > 0)
        {
            return renderMethod.at(0).matTrans[3][2];
        }
        return 0;
    }
    void emotenode::resizeMainData()
    {
        if (data != nullptr)
            delete data;
        int datasize = width * height * 4;
        data = new uint8_t[datasize];
        memset(data, 0, datasize);
    }
    void emotenode::resizeOpenGL()
    {
        if (selftexture != 0 && glIsTexture(selftexture) == GL_TRUE)
        {
            glDeleteTextures(1, &selftexture);
        }
        selftexture = createEmptyTexture(width, height);
    }

    emotemotion::emotemotion(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // time
        filePtr->parseNumber(lastTime, _rootData["lastTime"]);
        filePtr->parseNumber(loopTime, _rootData["loopTime"]);
        // layer
        std::vector<uint32_t> _layer;
        filePtr->parseList(_layer, _rootData["layer"]);
        for (size_t i = 0; i < _layer.size(); i++)
        {
            emotenode* tmp = new emotenode(this, nullptr, nodeList, filePtr, _layer.at(i));
            layer.push_back(tmp);
        }

        // priority
        _layer.clear();
        filePtr->parseList(_layer, _rootData["priority"]);
        if (_layer.size() > 0)
        {
            std::map<std::string, uint32_t> _priority;
            filePtr->parseObject(_priority, _layer.at(0));
            auto it = _priority.find("content");
            if (it != _priority.end())
            {
                _layer.clear();
                filePtr->parseList(_layer, _priority["content"]);
                for (auto idx : _layer)
                {
                    int64_t tmp;
                    filePtr->parseNumber(tmp, idx);
                    priority.push_back(static_cast<int32_t>(tmp));
                }
            }
        }
        // rearrange
        std::vector<emotenode*> result;
        for (auto node_idx = priority.begin(); node_idx != priority.end(); ++node_idx)
        {
            if (*node_idx >= nodeList.size())
                continue;

            result.push_back(nodeList.at(*node_idx));
        }
        nodeList = result;

        // parameter
        _layer.clear();
        filePtr->parseList(_layer, _rootData["parameter"]);
        for (auto varItem : _layer)
        {
            std::map<std::string, uint32_t> varMap;
            filePtr->parseObject(varMap, varItem);
            emoteVar* var = new emoteVar();
            int64_t tmp = 0;
            filePtr->parseNumber(tmp, varMap["rangeBegin"]);
            var->rangeBegin = static_cast<int32_t>(tmp);
            filePtr->parseNumber(tmp, varMap["rangeEnd"]);
            var->rangeEnd = static_cast<int32_t>(tmp);
            filePtr->parseNumber(tmp, varMap["division"]);
            var->division = static_cast<int32_t>(tmp);
            filePtr->parseString(var->id, varMap["id"]);
            parameter.push_back(var);
        }
        // parameter to child
        auto it = _rootData.find("parameterize");
        if (it != _rootData.end())
        {
            int64_t tmp = 0;
            if (filePtr->parseNumber(tmp, it->second))
            {
                isParameterize = true;
                parameterIdx = static_cast<int32_t>(tmp);
            }
        }
        if (isParameterize)
        {
            for (auto itmNode : nodeList)
            {
                if (!itmNode->isParameterize)
                {
                    itmNode->isParameterize = true;
                    itmNode->parameterIdx = parameterIdx;
                }
            }
        }

        // selfSyncTime
        for (auto _node : nodeList)
        {
            for (auto _frame : _node->frameList)
            {
                if (_frame != nullptr && _frame->hasContent)
                {
                    if (_frame->time > selfSyncTime)
                        selfSyncTime = _frame->time;
                }
            }
        }
    }
    emotemotion::~emotemotion()
    {
        for (auto lay : layer)
        {
            if (lay != nullptr)
                delete lay;
        }
        for (auto para : parameter)
        {
            if (para != nullptr)
                delete para;
        }
    }
    float emotemotion::getTickByIdx(int32_t parameterIdx)
    {
        if (parameterIdx >= parameter.size() || parameterIdx < 0)
            return -1.0f;
        float currVal = 0;
        if (_filePtr->getTickByName(parameter.at(parameterIdx)->id, currVal))
        {
            parameter.at(parameterIdx)->currVal = currVal;
        }
        return parameter.at(parameterIdx)->transToTick();
    }
    emotenode* emotemotion::getNodeByName(const std::string& name)
    {
        for (auto node : nodeList)
        {
            if (node == nullptr)
                continue;
            if (strcmp(name.c_str(), node->label.c_str()) == 0) // 仅靠一个label，合理吗？
            {
                return node;
            }
        }
        return nullptr;
    }
    void emotemotion::progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim)
    {
        // 树状便利，构建节点的渲染结构
        for (auto ch : layer)
        {
            renderMethod.clear();
            renderMethod = renderList;
            // 蒙版是否穿过motion，没啥好的依据，就随便猜一个了
            if (renderMethod.size() > 0 && ch->type == 2)
            {
                renderMethod.at(0).hasStencil = false;
                renderMethod.at(0).layerNode.clear();
            }
            ch->progress(tick, renderMethod, lim);
        }
    }
    void emotemotion::draw(GLuint targetFbo, emotelimit lim, GLuint exFbo, GLuint exTex)
    {
        std::vector<emotenode*> result, resultEx;
        if (_filePtr->isMotion)
        {
            // motion则是先处理非motion节点，然后再处理motion节点
            for (auto node_idx = nodeList.rbegin(); node_idx != nodeList.rend(); ++node_idx)
            {
                if ((*node_idx)->emot == nullptr)
                    result.push_back(*node_idx);
                else
                    resultEx.push_back(*node_idx);
            }
        }
        else
        {
            // 先展开所有的motion情形
            std::vector<emotenode*> stack(nodeList.begin(), nodeList.end());
            while (!stack.empty())
            {
                emotenode* current = stack.back();
                stack.pop_back();

                if (current->emot == nullptr)
                {
                    result.push_back(current);
                }
                else
                {
                    for (auto it = current->emot->nodeList.begin();
                         it != current->emot->nodeList.end(); ++it)
                    {
                        stack.push_back(*it);
                    }
                }
            }

            // 再根据lastZ进行校准
            std::stable_sort(result.begin(), result.end(), [](emotenode* a, emotenode* b)
                             { return a->getCurrentRenderZ() < b->getCurrentRenderZ(); });
        }

        // 遍历绘制节点
        for (auto currNode : result)
        {
            if (currNode != nullptr)
            {
                currNode->draw(targetFbo, lim, exFbo, exTex);
            }
        }
        for (auto currNode : resultEx)
        {
            if (currNode != nullptr)
            {
                currNode->draw(targetFbo, lim, exFbo, exTex);
            }
        }
    }

    emoteobject::emoteobject(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // type
        int64_t tmp;
        filePtr->parseNumber(tmp, _rootData["type"]);
        type = static_cast<int8_t>(tmp);
        // motion
        std::map<std::string, uint32_t> _motion;
        filePtr->parseObject(_motion, _rootData["motion"]);
        for (auto _obj : _motion)
        {
            emotemotion* tmp = new emotemotion(filePtr, _obj.second);
            motion.insert(std::pair<std::string, emotemotion*>(_obj.first, tmp));
        }
    }
    emoteobject::~emoteobject()
    {
        for (auto mtn : motion)
        {
            if (mtn.second != nullptr)
                delete mtn.second;
        }
    }
    emoteVar* emoteobject::findVarByName(const std::string& name)
    {
        for (auto mtn : motion)
        {
            for (auto var : mtn.second->parameter)
            {
                if (var->id == name)
                {
                    return var;
                }
            }
        }
        return nullptr;
    }

#pragma endregion

#pragma region timeline

    emoteTimeVarFrame::emoteTimeVarFrame(emotefile* filePtr, uint32_t startOffset)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // time type
        filePtr->parseReal(time, _rootData["time"]);
        int64_t tmp;
        filePtr->parseNumber(tmp, _rootData["type"]);
        type = static_cast<int8_t>(tmp);
        // content
        auto it = _rootData.find("content");
        if (it != _rootData.end())
        {
            if (filePtr->parseObject(_rootData, it->second))
            {
                hasContent = true;
                // easing value
                it = _rootData.find("easing");
                if (it != _rootData.end())
                {
                    filePtr->parseReal(easing, it->second);
                }
                it = _rootData.find("value");
                if (it != _rootData.end())
                {
                    filePtr->parseReal(value, it->second);
                }
            }
        }
    }
    emoteTimeVarFrame::~emoteTimeVarFrame()
    {

    }
    emoteTimeVar::emoteTimeVar(emotefile* filePtr, uint32_t startOffset)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // label
        filePtr->parseString(label, _rootData["label"]);
        // frameList
        std::vector<uint32_t> _tmpList;
        filePtr->parseList(_tmpList, _rootData["frameList"]);
        for (size_t i = 0; i < _tmpList.size(); i++)
        {
            emoteTimeVarFrame* tmp = new emoteTimeVarFrame(filePtr, _tmpList.at(i));
            frameList.push_back(tmp);
        }
    }
    emoteTimeVar::~emoteTimeVar()
    {
        for (auto itm : frameList)
        {
            if (itm != nullptr)
                delete itm;
        }
    }
    emotetimeline::emotetimeline(emotefile* filePtr, uint32_t startOffset)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // frameinfo
        int64_t tmp = 0;
        filePtr->parseNumber(tmp, _rootData["lastTime"]);
        lastTime = static_cast<int32_t>(tmp);
        filePtr->parseNumber(tmp, _rootData["loopBegin"]);
        loopBegin = static_cast<int32_t>(tmp);
        filePtr->parseNumber(tmp, _rootData["loopEnd"]);
        loopEnd = static_cast<int32_t>(tmp);
        if (filePtr->parseNumber(tmp, _rootData["diff"]))
            diff = static_cast<int8_t>(tmp);
        
        // label
        filePtr->parseString(label, _rootData["label"]);
        // variableList
        std::vector<uint32_t> _tmpList;
        filePtr->parseList(_tmpList, _rootData["variableList"]);
        for (size_t i = 0; i < _tmpList.size(); i++)
        {
            emoteTimeVar* tmp = new emoteTimeVar(filePtr, _tmpList.at(i));
            variableList.push_back(tmp);
        }
    }
    emotetimeline::~emotetimeline()
    {
        for (auto itm : variableList)
        {
            if (itm != nullptr)
                delete itm;
        }
    }

#pragma endregion

#pragma region Physics

    class EasingForce
    {
    public:
        std::string name;
        Vector3 targetForce;
        Vector3 startForce;
        Vector3 currentForce;
        float duration;
        float elapsedTime;
        float easing;
        bool isActive;

        EasingForce() : duration(0), elapsedTime(0), easing(0), isActive(false) {}

        EasingForce(const std::string& n, const Vector3& target, float t, float e)
          : name(n),
            targetForce(target),
            startForce(Vector3::zero()),
            currentForce(Vector3::zero()),
            duration(t),
            elapsedTime(0),
            easing(e),
            isActive(true)
        {
        }

        void update(float deltaTime)
        {
            if (!isActive)
                return;

            elapsedTime += deltaTime;
            float progress = std::min(elapsedTime / duration, 1.0f);

            float t = applyEasing(progress, easing);
            currentForce = startForce + (targetForce - startForce) * t;

            if (progress >= 1.0f)
            {
                isActive = false;
            }
        }

        bool shouldRemove() const { return !isActive; }

    private:
        float applyEasing(float t, float easing)
        {
            if (easing < 0)
            {
                return 1.0f - std::cos((t * 3.14159265f) / 2.0f);
            }
            else if (easing > 0)
            {
                return std::sin((t * 3.14159265f) / 2.0f);
            }
            else
            {
                return t;
            }
        }
    };
    class OuterForceSystem
    {
    private:
        std::unordered_map<std::string, EasingForce> activeForces;

    public:
        void setOuterForce(
            const std::string& name, float ofx, float ofy, float time = 0, float easing = 0)
        {
            Vector3 targetForce(ofx, ofy, 0);

            if (time <= 0)
            {
                EasingForce force(name, targetForce, 0, 0);
                force.currentForce = targetForce;
                force.isActive = false;
                activeForces[name] = force;
            }
            else
            {
                EasingForce force(name, targetForce, time / 1000.0f, easing);
                activeForces[name] = force;
            }
        }

        void removeOuterForce(const std::string& name) { activeForces.erase(name); }

        void clearAllForces() { activeForces.clear(); }

        void update(float deltaTime)
        {
            for (auto it = activeForces.begin(); it != activeForces.end();)
            {
                it->second.update(deltaTime);
                if (it->second.shouldRemove())
                {
                    it = activeForces.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        Vector3 getTotalOuterForce()
        {
            Vector3 totalForce = Vector3::zero();
            for (auto& pair : activeForces)
            {
                totalForce = totalForce + pair.second.currentForce;
            }
            return totalForce;
        }

        bool hasForce(const std::string& name) const
        {
            return activeForces.find(name) != activeForces.end();
        }
    };
    class WindSystem
    {
    private:
        Vector3 windStart;
        Vector3 windGoal;
        Vector3 windDirection;
        float windSpeed;
        float windPowerMin;
        float windPowerMax;
        float currentWindPower;
        bool isWindActive;
        float timeAccumulator;

    public:
        WindSystem()
          : windSpeed(0),
            windPowerMin(0),
            windPowerMax(0),
            currentWindPower(0),
            isWindActive(false),
            timeAccumulator(0)
        {
        }

        void startWind(
            const Vector3& start, const Vector3& goal, float speed, float powMin, float powMax)
        {
            windStart = start;
            windGoal = goal;
            windSpeed = speed;
            windPowerMin = powMin;
            windPowerMax = powMax;
            windDirection = (goal - start).normalized();
            isWindActive = true;
            currentWindPower = powMin;
            timeAccumulator = 0;
        }

        void stopWind()
        {
            isWindActive = false;
            currentWindPower = 0.0f;
        }

        void update(float deltaTime)
        {
            if (!isWindActive)
                return;

            timeAccumulator += deltaTime;
            currentWindPower =
                windPowerMin + (windPowerMax - windPowerMin) *
                                   (0.5f + 0.5f * std::sin(timeAccumulator * windSpeed));
        }

        Vector3 getWindForceAtPosition(const Vector3& position)
        {
            if (!isWindActive)
                return Vector3::zero();

            float distanceFactor = calculateDistanceFactor(position);
            float powerVariation = 0.8f + 0.2f * noise(position.x, position.z);

            return windDirection * currentWindPower * distanceFactor * powerVariation;
        }

        bool isActive() const { return isWindActive; }

    private:
        float calculateDistanceFactor(const Vector3& position)
        {
            Vector3 closestPoint = findClosestPointOnLine(position, windStart, windGoal);
            float distance = (position - closestPoint).magnitude();
            return std::max(0.0f, 1.0f - distance / 50.0f);
        }

        Vector3 findClosestPointOnLine(const Vector3& point,
                                       const Vector3& lineStart,
                                       const Vector3& lineEnd)
        {
            Vector3 lineVec = lineEnd - lineStart;
            float lineLength = lineVec.magnitude();
            if (lineLength == 0)
                return lineStart;

            Vector3 lineDir = lineVec / lineLength;
            Vector3 pointVec = point - lineStart;
            float projection = pointVec.dot(lineDir);
            projection = std::max(0.0f, std::min(projection, lineLength));
            return lineStart + lineDir * projection;
        }

        float noise(float x, float z) { return (std::sin(x * 0.1f) + std::sin(z * 0.15f)) * 0.5f; }
    };
    class BustPhysicsSimulator
    {
    private:
        struct BustState
        {
            Vector3 restPosition; // 静止位置 (op)
            Vector3 position;     // 当前位置 (p)
            Vector3 velocity;     // 当前速度 (pv)
            float offset;         // 偏移量 (ofs)
        };

        BustState state;
        float spring;       // 弹簧系数
        float friction;     // 摩擦力
        float gravity;      // 重力
        float scale_x;      // X轴缩放
        float scale_y;      // Y轴缩放
        std::string var_lr; // 左右变量名
        std::string var_ud; // 上下变量名
        bool enabled;

    public:
        BustPhysicsSimulator()
          : spring(0),
            friction(0),
            gravity(0),
            scale_x(1),
            scale_y(1),
            enabled(false)
        {
        }

        void initialize(const std::string& baseLayer,
                        bool enable,
                        float fric,
                        float grav,
                        const Vector3& op,
                        const Vector3& p,
                        const Vector3& pv,
                        float ofs,
                        float spr,
                        float scl_x,
                        float scl_y,
                        const std::string& lr,
                        const std::string& ud)
        {
            state.restPosition = op;
            state.position = p;
            state.velocity = pv;
            state.offset = ofs;
            spring = spr;
            friction = fric;
            gravity = grav;
            scale_x = scl_x;
            scale_y = scl_y;
            var_lr = lr;
            var_ud = ud;
            enabled = enable;
        }

        void update(float deltaTime, const Vector3& externalForce)
        {
            if (!enabled)
                return;

            // 弹簧力 (胡克定律)
            Vector3 springForce = (state.restPosition - state.position) * spring;

            // 重力
            Vector3 gravityForce = Vector3(0, -gravity, 0);

            // 摩擦力
            Vector3 frictionForce = state.velocity * -friction;

            // 合力
            Vector3 totalForce = springForce + gravityForce + frictionForce + externalForce;

            // 更新物理状态
            state.velocity = state.velocity + totalForce * deltaTime;
            state.position = state.position + state.velocity * deltaTime;

            // 更新输出变量
            updateOutputVariables();
        }

        float getVariableValue(const std::string& varName) { return 0.0f; }

    private:
        void updateOutputVariables()
        {
            // 计算相对于静止位置的偏移
            Vector3 offsetFromRest = state.position - state.restPosition;

            // 应用缩放和基础偏移
            float bust_LR = offsetFromRest.x * scale_x + state.offset;
            float bust_UD = offsetFromRest.y * scale_y + state.offset;

            // 设置输出变量
            setVariableValue(var_lr, bust_LR);
            setVariableValue(var_ud, bust_UD);
        }

        void setVariableValue(const std::string& varName, float value) {}
    };
    class HairSegment
    {
    public:
        Vector3 basePosition;
        Vector3 position;
        Vector3 velocity;
        float scaleX;
        float scaleY;
        float length;

        HairSegment() : scaleX(1), scaleY(1), length(0) {}
    };
    class HairPhysicsSimulator
    {
    private:
        std::vector<HairSegment> segments;

        float b_rate;
        float gravity;
        float friction_x;
        float friction_y;
        float bend_spd;
        float bend_vol;
        float offset;
        bool v_bound;

        std::string var_lr;
        std::string var_lrm;
        std::string var_ud;

    public:
        HairPhysicsSimulator()
          : b_rate(0),
            gravity(0),
            friction_x(0),
            friction_y(0),
            bend_spd(0),
            bend_vol(0),
            offset(0),
            v_bound(false)
        {
        }

        void initialize(const std::vector<HairSegment>& segs,
                        float bRate,
                        float grav,
                        float fricX,
                        float fricY,
                        float bendSpd,
                        float bendVol,
                        float ofs,
                        bool vBound,
                        const std::string& lr,
                        const std::string& lrm,
                        const std::string& ud)
        {
            segments = segs;
            b_rate = bRate;
            gravity = grav;
            friction_x = fricX;
            friction_y = fricY;
            bend_spd = bendSpd;
            bend_vol = bendVol;
            offset = ofs;
            v_bound = vBound;
            var_lr = lr;
            var_lrm = lrm;
            var_ud = ud;
        }

        void update(float deltaTime, const Vector3& externalForce)
        {
            for (size_t i = 0; i < segments.size(); ++i)
            {
                updateSegment(segments[i], deltaTime, externalForce, i);
            }
            updateOutputVariables();
        }

        float getVariableValue(const std::string& varName) { return 0.0f; }

    private:
        void updateSegment(HairSegment& segment,
                           float deltaTime,
                           const Vector3& externalForce,
                           int segmentIndex)
        {
            Vector3 springForce = (segment.basePosition - segment.position) * b_rate;
            Vector3 gravityForce = Vector3(0, -gravity, 0);

            float windInfluence = 0.5f + 0.5f * (segmentIndex / (float)(segments.size() - 1));
            Vector3 externalForceWithWind = externalForce * (1.0f + windInfluence);

            Vector3 frictionForce;
            frictionForce.x = -segment.velocity.x * friction_x;
            frictionForce.y = -segment.velocity.y * friction_y;
            frictionForce.z = -segment.velocity.z * std::min(friction_x, friction_y);

            Vector3 bendForce = calculateBendForce(segment, segmentIndex);

            Vector3 totalForce =
                springForce + gravityForce + externalForceWithWind + frictionForce + bendForce;

            segment.velocity = segment.velocity + totalForce * deltaTime;

            if (v_bound)
            {
                segment.velocity = clampVelocity(segment.velocity);
            }

            segment.position = segment.position + segment.velocity * deltaTime;
        }

        Vector3 calculateBendForce(const HairSegment& segment, int segmentIndex)
        {
            Vector3 force = Vector3::zero();

            if (segmentIndex > 0)
            {
                const HairSegment& prevSegment = segments[segmentIndex - 1];
                Vector3 dir = (segment.position - prevSegment.position).normalized();
                force = dir * bend_vol * bend_spd;
            }

            return force;
        }

        Vector3 clampVelocity(const Vector3& velocity)
        {
            float maxSpeed = 10.0f;
            float currentSpeed = velocity.magnitude();

            if (currentSpeed > maxSpeed && currentSpeed > 0)
            {
                return velocity * (maxSpeed / currentSpeed);
            }
            return velocity;
        }

        void updateOutputVariables()
        {
            if (segments.size() < 2)
                return;

            Vector3 segment1Offset = segments[0].position - segments[0].basePosition;
            float lr1 = segment1Offset.x * segments[0].scaleX;
            float ud1 = segment1Offset.y * segments[0].scaleY;

            Vector3 segment2Offset = segments[1].position - segments[1].basePosition;
            float lr2 = segment2Offset.x * segments[1].scaleX;
            float ud2 = segment2Offset.y * segments[1].scaleY;

            float hair_LR = (lr1 + lr2) * 0.5f + offset;
            float hair_LR_M = lr1 * 0.7f + lr2 * 0.3f + offset;
            float hair_UD = (ud1 + ud2) * 0.5f + offset;

            setVariableValue(var_lr, hair_LR);
            setVariableValue(var_lrm, hair_LR_M);
            setVariableValue(var_ud, hair_UD);
        }

        void setVariableValue(const std::string& varName, float value) {}
    };
    class CompletePhysicsSystem
    {
    private:
        HairPhysicsSimulator hairPhysics;
        BustPhysicsSimulator bustPhysics;
        WindSystem windSystem;
        OuterForceSystem outerForceSystem;

    public:
        void update(float deltaTime,
                    const Vector3& headMovement,
                    const Vector3& headPosition,
                    const Vector3& bodyMovement)
        {
            // 更新外力系统
            outerForceSystem.update(deltaTime);

            // 获取风力和外力
            Vector3 windForce = windSystem.getWindForceAtPosition(headPosition);
            Vector3 outerForce = outerForceSystem.getTotalOuterForce();

            // 合并所有外力
            Vector3 totalExternalForce = calculateTotalForce(headMovement, windForce, outerForce);

            // 更新头发物理（使用头部运动）
            hairPhysics.update(deltaTime, totalExternalForce);

            // 更新胸部物理（使用身体运动）
            Vector3 bustExternalForce = calculateBustForce(bodyMovement, windForce, outerForce);
            bustPhysics.update(deltaTime, bustExternalForce);

            // 更新风系统
            windSystem.update(deltaTime);
        }

        // 风系统控制
        void startWind(
            const Vector3& start, const Vector3& goal, float speed, float powMin, float powMax)
        {
            windSystem.startWind(start, goal, speed, powMin, powMax);
        }

        void stopWind() { windSystem.stopWind(); }

        // 外力系统控制
        void setOuterForce(
            const std::string& name, float ofx, float ofy, float time = 0, float easing = 0)
        {
            outerForceSystem.setOuterForce(name, ofx, ofy, time, easing);
        }

        void removeOuterForce(const std::string& name) { outerForceSystem.removeOuterForce(name); }

        void clearAllOuterForces() { outerForceSystem.clearAllForces(); }

        // 头发系统初始化
        void initializeHairPhysics(const std::vector<HairSegment>& segs,
                                   float bRate,
                                   float grav,
                                   float fricX,
                                   float fricY,
                                   float bendSpd,
                                   float bendVol,
                                   float ofs,
                                   bool vBound,
                                   const std::string& lr,
                                   const std::string& lrm,
                                   const std::string& ud)
        {
            hairPhysics.initialize(segs, bRate, grav, fricX, fricY, bendSpd, bendVol, ofs, vBound,
                                   lr, lrm, ud);
        }

        // 胸部系统初始化
        void initializeBustPhysics(const std::string& baseLayer,
                                   bool enable,
                                   float fric,
                                   float grav,
                                   const Vector3& op,
                                   const Vector3& p,
                                   const Vector3& pv,
                                   float ofs,
                                   float spr,
                                   float scl_x,
                                   float scl_y,
                                   const std::string& lr,
                                   const std::string& ud)
        {
            bustPhysics.initialize(baseLayer, enable, fric, grav, op, p, pv, ofs, spr, scl_x, scl_y,
                                   lr, ud);
        }

        bool isWindActive() const { return windSystem.isActive(); }

    private:
        Vector3 calculateTotalForce(const Vector3& headMovement,
                                    const Vector3& windForce,
                                    const Vector3& outerForce)
        {
            Vector3 totalForce;

            // 头部运动的影响（对头发）
            totalForce.x = headMovement.x * 2.0f;
            totalForce.y = headMovement.y * 1.0f;
            totalForce.z = headMovement.z * 1.5f;

            // 叠加风力和外力
            totalForce = totalForce + windForce + outerForce;

            return totalForce;
        }

        Vector3 calculateBustForce(const Vector3& bodyMovement,
                                   const Vector3& windForce,
                                   const Vector3& outerForce)
        {
            Vector3 totalForce;

            // 身体运动的影响（对胸部）
            totalForce.x = bodyMovement.x * 1.5f;
            totalForce.y = bodyMovement.y * 1.0f;
            totalForce.z = bodyMovement.z * 1.0f;

            // 叠加风力和外力（胸部对风的反应较小）
            totalForce = totalForce + (windForce * 0.3f) + outerForce;

            return totalForce;
        }
    };
    class ExampleUsage
    {
    private:
        CompletePhysicsSystem physicsSystem;

    public:
        void initialize()
        {
            // 初始化头发系统
            std::vector<HairSegment> hairSegments(2);

            hairSegments[0].basePosition = Vector3(0, 64, 0);
            hairSegments[0].position = Vector3(0, 225.684357, 0);
            hairSegments[0].velocity = Vector3(0, -4.91552055E-06, 0);
            hairSegments[0].scaleX = 0.75f;
            hairSegments[0].scaleY = 2.0f;
            hairSegments[0].length = 64.0f;

            hairSegments[1].basePosition = Vector3(0, 112, 0);
            hairSegments[1].position = Vector3(0, 274.228119, 0);
            hairSegments[1].velocity = Vector3(0, 0.543750048, 0);
            hairSegments[1].scaleX = 0.25f;
            hairSegments[1].scaleY = 3.0f;
            hairSegments[1].length = 48.0f;

            physicsSystem.initializeHairPhysics(
                hairSegments, 0.00371093745f, 0.6f, 0.046875f, 0.09375f, 0.2f, 3.0f, -161.684357f,
                true, "hair_LR_front", "hair_LR_M_front", "hair_UD_front");

            // 初始化胸部系统
            physicsSystem.initializeBustPhysics("center_bust",               // baseLayer
                                                true,                        // enabled
                                                0.06f,                       // friction
                                                0.1f,                        // gravity
                                                Vector3(0, 0, 0),            // op
                                                Vector3(0, 3.200208, 0),     // p
                                                Vector3(0, 9.272695E-06, 0), // pv
                                                3.1996305f,                  // ofs
                                                0.03125f,                    // spring
                                                1.0f,                        // scale_x
                                                2.0f,                        // scale_y
                                                "bust_LR",                   // var_lr
                                                "bust_UD"                    // var_ud
            );
        }

        void update(float deltaTime,
                    const Vector3& headPosition,
                    const Vector3& headMovement,
                    const Vector3& bodyMovement)
        {
            physicsSystem.update(deltaTime, headMovement, headPosition, bodyMovement);
        }

        void triggerWind()
        {
            physicsSystem.startWind(Vector3(-100, 50, 0), Vector3(100, 50, 0), 2.0f, 0.5f, 3.0f);
        }

        void applyQuickShake() { physicsSystem.setOuterForce("shake", 5.0f, 2.0f, 200, 1.0f); }

        void applyGentleSway() { physicsSystem.setOuterForce("sway", -3.0f, 1.0f, 1000, -1.0f); }

        void stopAllForces()
        {
            physicsSystem.stopWind();
            physicsSystem.clearAllOuterForces();
        }
    };

    bustControl::bustControl(emotefile* filePtr, uint32_t startOffset)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // friction/gravity/spring
        filePtr->parseReal(friction, _rootData["friction"]);
        filePtr->parseReal(gravity, _rootData["gravity"]);
        filePtr->parseReal(spring, _rootData["spring"]);
        //scale_x/y
        filePtr->parseReal(scale_x, _rootData["scale_x"]);
        filePtr->parseReal(scale_y, _rootData["scale_y"]);
        //lable
        filePtr->parseString(var_lr, _rootData["var_lr"]);
        filePtr->parseString(var_ud, _rootData["var_ud"]);
        //param
        if (filePtr->parseObject(_rootData, _rootData["param"]))
        {
            filePtr->parseReal(param.ofs, _rootData["ofs"]);
            // op
            std::map<std::string, uint32_t> _tmpData;
            if (filePtr->parseObject(_tmpData, _rootData["op"]))
            {
                filePtr->parseReal(param.op.x, _tmpData["x"]);
                filePtr->parseReal(param.op.y, _tmpData["y"]);
                filePtr->parseReal(param.op.z, _tmpData["z"]);
            }
            // p
            if (filePtr->parseObject(_tmpData, _rootData["p"]))
            {
                filePtr->parseReal(param.p.x, _tmpData["x"]);
                filePtr->parseReal(param.p.y, _tmpData["y"]);
                filePtr->parseReal(param.p.z, _tmpData["z"]);
            }
            // pv
            if (filePtr->parseObject(_tmpData, _rootData["pv"]))
            {
                filePtr->parseReal(param.pv.x, _tmpData["x"]);
                filePtr->parseReal(param.pv.y, _tmpData["y"]);
                filePtr->parseReal(param.pv.z, _tmpData["z"]);
            }
        }
    }
    bustControl::~bustControl()
    {

    }

    uniControl::uniControl(emotefile* filePtr, uint32_t startOffset)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // b_rate/bend_spd/bend_vol/friction_x/friction_y/gravity
        filePtr->parseReal(b_rate, _rootData["b_rate"]);
        filePtr->parseReal(bend_spd, _rootData["bend_spd"]);
        filePtr->parseReal(bend_vol, _rootData["bend_vol"]);
        filePtr->parseReal(friction_x, _rootData["friction_x"]);
        filePtr->parseReal(friction_y, _rootData["friction_y"]);
        filePtr->parseReal(gravity, _rootData["gravity"]);
        // ud_eft/v_bound
        filePtr->parseReal(ud_eft, _rootData["ud_eft"]);
        filePtr->parseReal(v_bound, _rootData["v_bound"]);
        // lable
        filePtr->parseString(var_lr, _rootData["var_lr"]);
        filePtr->parseString(var_lrm, _rootData["var_lrm"]);
        filePtr->parseString(var_ud, _rootData["var_ud"]);
        // param
        std::vector<uint32_t> _sx, _sy, _len;
        filePtr->parseList(_sx, _rootData["scale_x"]);
        filePtr->parseList(_sy, _rootData["scale_y"]);
        filePtr->parseList(_len, _rootData["length"]);
        if (filePtr->parseObject(_rootData, _rootData["param"]))
        {
            // ofs/bendR/bendS/op
            filePtr->parseReal(ofs, _rootData["ofs"]);
            filePtr->parseReal(bendR, _rootData["bendR"]);
            filePtr->parseReal(bendS, _rootData["bendS"]);
            std::map<std::string, uint32_t> _tmpData;
            if (filePtr->parseObject(_tmpData, _rootData["op"]))
            {
                filePtr->parseReal(op.x, _tmpData["x"]);
                filePtr->parseReal(op.y, _tmpData["y"]);
                filePtr->parseReal(op.z, _tmpData["z"]);
            }
            // for get
            std::vector<uint32_t> _bp, _p, _pv;
            filePtr->parseList(_bp, _rootData["bp"]);
            filePtr->parseList(_p, _rootData["p"]);
            filePtr->parseList(_pv, _rootData["pv"]);
            // bp/p/pv
            int cts = std::min(_bp.size(), std::min(_p.size(), _pv.size()));
            for (int i = 0; i < cts; i++)
            {
                uniSegment _psD;
                filePtr->parseObject(_tmpData, _bp.at(i));
                filePtr->parseReal(_psD.bp.x, _tmpData["x"]);
                filePtr->parseReal(_psD.bp.y, _tmpData["y"]);
                filePtr->parseReal(_psD.bp.z, _tmpData["z"]);
                filePtr->parseObject(_tmpData, _p.at(i));
                filePtr->parseReal(_psD.p.x, _tmpData["x"]);
                filePtr->parseReal(_psD.p.y, _tmpData["y"]);
                filePtr->parseReal(_psD.p.z, _tmpData["z"]);
                filePtr->parseObject(_tmpData, _pv.at(i));
                filePtr->parseReal(_psD.pv.x, _tmpData["x"]);
                filePtr->parseReal(_psD.pv.y, _tmpData["y"]);
                filePtr->parseReal(_psD.pv.z, _tmpData["z"]);
                filePtr->parseReal(_psD.scale_x, _sx.at(i));
                filePtr->parseReal(_psD.scale_y, _sy.at(i));
                filePtr->parseReal(_psD.length, _len.at(i));
                segments.push_back(_psD);
            }
        }
    }
    uniControl::~uniControl()
    {

    }

    // 物理玩不明白，这都是ai写的 分别用于parts/hair/bust
    SmoothPeriodicRandom::SmoothPeriodicRandom()
      : noise_seed(std::rand() / float(RAND_MAX) * 1000.0f)
    {
    }
    float SmoothPeriodicRandom::generate(float tick, float a, float b, float period)
    {
        // 基础周期
        float base = std::sin(tick * 2.0f * 3.14159f / period);

        // 平滑随机偏移（使用噪声函数）
        float random_offset = std::sin(tick * 0.3f + noise_seed) * 0.4f;

        // 组合
        float value = base * 0.6f + random_offset * 0.4f;

        // 优雅的映射
        return a + (b - a) * (0.5f + 0.4f * std::tanh(value));
    }
    HairSwaySimulator::HairSwaySimulator()
    {
        // 设置多个频率分量模拟不同发丝的飘动
        frequencies = {0.8f, 1.2f, 1.7f, 2.3f, 0.5f};
        amplitudes = {0.5f, 0.3f, 0.15f, 0.08f, 0.2f};

        phases.resize(frequencies.size());

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(0.0f, 6.28318f);

        for (auto& phase : phases)
        {
            phase = dist(gen);
        }

        wind_seed = dist(gen);
        wind.active = false;
        wind.progress = 0.0f;
    }
    // 风开始
    void HairSwaySimulator::startWind(
        glm::vec2 start, glm::vec2 goal, float speed, float powMin, float powMax)
    {
        wind.start = start;
        wind.goal = goal;
        wind.speed = speed;
        wind.powMin = powMin;
        wind.powMax = powMax;
        wind.progress = 0.0f;
        wind.active = true;

        // 重置风种子以获得新的随机模式
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(0.0f, 6.28318f);
        wind_seed = dist(gen);
    }
    // 停止风
    void HairSwaySimulator::stopWind()
    {
        wind.active = false;
        wind.progress = 0.0f;
    }
    // 获取当前风力强度（基于位置和进度）
    float HairSwaySimulator::getCurrentWindStrength(glm::vec2 hairPosition)
    {
        if (!wind.active)
            return 0.0f;

        // 计算风路径的方向和距离
        glm::vec2 windDir = glm::normalize(wind.goal - wind.start);
        glm::vec2 toHair = hairPosition - wind.start;

        // 计算头发在风路径上的投影距离
        float projection = glm::dot(toHair, windDir);
        float windLength = glm::length(wind.goal - wind.start);

        // 计算风的影响范围（风宽度）
        float windWidth = windLength * 0.3f;
        float lateralDist = glm::length(toHair - windDir * projection);

        // 如果头发在风路径之外，影响减弱
        float lateralFactor = std::max(0.0f, 1.0f - lateralDist / windWidth);

        // 计算风进程的影响
        float windFront = wind.progress * windLength;
        float distanceToFront = std::abs(projection - windFront);

        // 风前沿的影响（高斯分布）
        float frontFactor =
            std::exp(-distanceToFront * distanceToFront / (windWidth * windWidth * 0.5f));

        // 组合所有因素
        float strength = wind.powMin + (wind.powMax - wind.powMin) * wind.progress;
        strength *= lateralFactor * frontFactor;

        return strength;
    }
    // 获取风的方向
    glm::vec2 HairSwaySimulator::getWindDirection()
    {
        if (!wind.active)
            return glm::vec2(1.0f, 0.0f); // 默认方向
        return glm::normalize(wind.goal - wind.start);
    }
    float HairSwaySimulator::generate(float tick, glm::vec2 hairPosition, float a, float b)
    {
        // 更新风进程
        if (wind.active)
        {
            wind.progress += wind.speed * 0.01f; // 调整速度系数
            if (wind.progress >= 1.0f)
            {
                wind.active = false; // 风到达目标
            }
        }

        float result = 0.0f;

        // 1. 基础飘动（多个频率合成）
        for (size_t i = 0; i < frequencies.size(); ++i)
        {
            float freq = frequencies[i] * (0.9f + 0.2f * std::sin(tick * 0.01f));
            result += amplitudes[i] * std::sin(tick * freq * 0.05f + phases[i]);
        }

        // 2. 风的影响
        float wind_strength = getCurrentWindStrength(hairPosition);
        glm::vec2 wind_dir = getWindDirection();

        if (wind_strength > 0.0f)
        {
            // 主风浪
            float wind_wave = std::sin(tick * 0.3f + wind_seed) * wind_strength;

            // 风湍流（高频分量）
            float wind_turbulence = std::sin(tick * 1.5f + wind_seed * 2.0f) * wind_strength * 0.3f;

            // 风向影响（主要影响水平分量）
            float wind_direction_factor = std::abs(wind_dir.x); // 假设x是水平方向

            result += (wind_wave + wind_turbulence) * wind_direction_factor;
        }

        // 3. 随机扰动
        // float random_jitter = std::sin(tick * 5.0f + wind_seed * 2.0f) *
        //    std::cos(tick * 3.0f + wind_seed) * 0.1f;
        // result += random_jitter;

        // 4. 缓慢的基线漂移
        float base_drift = std::sin(tick * 0.02f) * 0.2f;
        result += base_drift;

        // 映射到 [a, b] 范围
        float normalized = std::tanh(result * 1.5f);
        return a + (b - a) * (0.5f + 0.4f * normalized);
    }
    // 检查风是否活跃
    bool HairSwaySimulator::isWindActive()
    {
        return wind.active;
    }
    // 获取风进度
    float HairSwaySimulator::getWindProgress()
    {
        return wind.progress;
    }
    BreastJiggleSimulator::SpringDamper::SpringDamper(float stiff,
                                                      float damp,
                                                      float m )
      : position(0),
        velocity(0),
        target(0),
        stiffness(stiff),
        damping(damp),
        mass(m)
    {
    }
    void BreastJiggleSimulator::SpringDamper::update(float delta_time)
    {
        // 弹簧阻尼器物理更新
        float acceleration = (target - position) * stiffness - velocity * damping;
        acceleration /= mass;
        velocity += acceleration * delta_time;
        position += velocity * delta_time;
    }
    BreastJiggleSimulator::SpringDamper vertical_spring;   // 垂直运动
    BreastJiggleSimulator::SpringDamper horizontal_spring; // 水平运动
    BreastJiggleSimulator::SpringDamper bounce_spring;     // 弹跳运动
    float base_movement;                                   // 基础运动（如呼吸）
    float last_body_movement;                              // 上一帧身体运动
    BreastJiggleSimulator::BreastJiggleSimulator()
      : vertical_spring(0.15f, 0.08f, 1.1f),   // 垂直：较软，较重
        horizontal_spring(0.25f, 0.12f, 0.9f), // 水平：较硬，较轻
        bounce_spring(0.3f, 0.2f, 0.8f),       // 弹跳：最硬，恢复快
        base_movement(0),
        last_body_movement(0)
    {
    }
    float BreastJiggleSimulator::generate(
        float tick, float body_movement, float a, float b, float delta_time)
    {
        // 1. 基础生理运动（呼吸等）
        base_movement = std::sin(tick * 0.3f) * 0.1f + std::sin(tick * 0.7f) * 0.05f;

        // 2. 身体运动引起的驱动（走路、跑步等）
        float movement_delta = body_movement - last_body_movement;
        float movement_force = movement_delta * 2.0f; // 运动变化产生力

        // 3. 更新各个弹簧系统
        // 垂直运动（主要响应上下运动）
        vertical_spring.target = base_movement + movement_force * 0.8f;
        vertical_spring.update(delta_time);

        // 水平运动（响应左右运动）
        horizontal_spring.target = std::sin(tick * 0.5f) * 0.05f + movement_force * 0.3f;
        horizontal_spring.update(delta_time);

        // 弹跳运动（跳跃等冲击）
        if (std::abs(movement_delta) > 0.5f)
        {
            bounce_spring.target = movement_force * 1.5f;
        }
        else
        {
            bounce_spring.target = 0;
        }
        bounce_spring.update(delta_time);

        // 4. 合成最终运动（带相位差模拟滞后）
        float vertical = vertical_spring.position;
        float horizontal = horizontal_spring.position * 0.7f;
        float bounce = bounce_spring.position * 0.4f;

        // 添加轻微随机扰动（模拟肌肉微颤）
        float micro_tremor = std::sin(tick * 8.0f) * 0.02f * std::cos(tick * 3.0f);

        float combined = vertical + horizontal + bounce + micro_tremor;

        last_body_movement = body_movement;

        // 5. 非线性映射（模拟软组织特性）
        float softened = std::tanh(combined * 2.0f) * 0.5f; // 软化曲线
        return a + (b - a) * (0.5f + softened);
    }
    // 外部冲击（如跳跃落地）
    void BreastJiggleSimulator::addImpulse(float force)
    {
        bounce_spring.velocity += force * 0.5f;
        vertical_spring.velocity += force * 0.3f;
    }

#pragma endregion

    emoteattrcomp::emoteattrcomp(emotefile* filePtr, uint32_t startOffset)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // lable
        filePtr->parseString(lable, _rootData["label"]);
        // remove_data
        if (filePtr->parseObject(_rootData, _rootData["data"]))
        {
            std::vector<uint32_t> _tmpVec;
            if (filePtr->parseList(_tmpVec, _rootData["remove"]))
            {
                for (auto vecItm : _tmpVec)
                {
                    emoteattrcompRemoveItem _tmpitm;
                    if (filePtr->parseObject(_rootData, vecItm))
                    {
                        filePtr->parseReal(_tmpitm.value, _rootData["value"]);
                        if (filePtr->parseObject(_rootData, _rootData["id"]))
                        {
                            filePtr->parseString(_tmpitm.chara, _rootData["chara"]);
                            filePtr->parseString(_tmpitm.motion, _rootData["motion"]);
                            filePtr->parseString(_tmpitm.layer, _rootData["layer"]);
                        }
                    }
                    data_remove.push_back(_tmpitm);
                }
            }
        }
    }
    emoteattrcomp::~emoteattrcomp()
    {

    }

    emoteselect::emoteselect(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // lable
        filePtr->parseString(lable, _rootData["label"]);
        // optionList
        std::vector<uint32_t> _tmpList;
        if (filePtr->parseList(_tmpList, _rootData["optionList"]))
        {
            for (auto attcom : _tmpList)
            {
                if (filePtr->parseObject(_rootData, attcom))
                {
                    emoteselectItem _tmpItm;
                    filePtr->parseString(_tmpItm.label, _rootData["label"]);
                    filePtr->parseReal(_tmpItm.offValue, _rootData["offValue"]);
                    filePtr->parseReal(_tmpItm.onValue, _rootData["onValue"]);
                    selectItem.push_back(_tmpItm);
                }
            }
        }
    }
    emoteselect::~emoteselect()
    {

    }
    bool emoteselect::selectValue(int32_t opt)
    {
        if (opt < 0 || opt >= selectItem.size())
            return false;
        for (int i = 0; i < selectItem.size(); i++)
        {
            if (i == opt)
                _filePtr->setVariable(selectItem.at(i).label, selectItem.at(i).onValue);
            else
                _filePtr->setVariable(selectItem.at(i).label, selectItem.at(i).offValue);
        }
        return true;
    }

    emotemetadata::emotemetadata(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        if (!filePtr->parseObject(_rootData, startOffset))
        {
            filePtr->isMotion = true;
            return;
        }
        // label
        std::map<std::string, uint32_t> _base;
        filePtr->parseObject(_base, _rootData["base"]);
        filePtr->parseString(chara, _base["chara"]);
        filePtr->parseString(motion, _base["motion"]);

        // attrcomp
        std::vector<uint32_t> _tmpList;
        auto attrItm = _rootData.find("attrcomp");
        if (attrItm != _rootData.end())
        {
            if (filePtr->parseList(_tmpList, attrItm->second))
            {
                for (auto attcom : _tmpList)
                {
                    emoteattrcomp* _tmpattr = new emoteattrcomp(filePtr, attcom);
                    _attrcomp.push_back(_tmpattr);
                }
            }
        }
        // selectorControl
        _tmpList.clear();
        attrItm = _rootData.find("selectorControl");
        if (attrItm != _rootData.end())
        {
            if (filePtr->parseList(_tmpList, attrItm->second))
            {
                for (auto attcom : _tmpList)
                {
                    emoteselect* _tmpattr = new emoteselect(filePtr, attcom);
                    _selectorControl.push_back(_tmpattr);
                }
            }
        }
        // mirrorControl
        attrItm = _rootData.find("mirror");
        if (attrItm != _rootData.end())
        {
            tjs_int64 isNeed = 0;
            filePtr->parseNumber(isNeed, attrItm->second);
            if (isNeed == 1)
            {
                filePtr->isMirror = true;
                //if (filePtr->parseObject(_base, _rootData["mirrorControl"]))
                //{
                //    auto it = _base.find("variableMatchList");
                //    if (it != _base.end())
                //    {
                //        std::vector<uint32_t> tmpList;
                //        if (filePtr->parseList(tmpList, it->second))
                //        {
                //            for (auto mtc : tmpList)
                //            {
                //                std::string tmpStr;
                //                filePtr->parseString(tmpStr, mtc);
                //                _mirrorControl.push_back(tmpStr);
                //            }
                //        }
                //    }      
                //}
            }
        }

        // timelineControl
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["timelineControl"]);
        for (auto itm : _tmpList)
        {
            emotetimeline* tmp = new emotetimeline(filePtr, itm);
            _timelineControl.push_back(tmp);
        }

        // variableList Simplified
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["variableList"]);
        for (auto itm : _tmpList)
        {
            filePtr->parseObject(_base, itm);
            auto it = _base.find("label");
            if (it != _base.end())
            {
                std::string key;
                filePtr->parseString(key, it->second);
                _varList.insert(std::pair<std::string, float>(key, 0.0f));
            }
        }

        // eyeControl Simplified
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["eyeControl"]);
        for (auto itm : _tmpList)
        {
            filePtr->parseObject(_base, itm);
            eyeControl* tmp = new eyeControl();
            int64_t tmpNum = 0;
            auto it = _base.find("label");
            if (it != _base.end())
            {
                filePtr->parseString(tmp->label, it->second);
            }
            it = _base.find("beginFrame");
            if (it != _base.end())
            {
                filePtr->parseNumber(tmpNum, it->second);
                tmp->beginFrame = static_cast<float>(tmpNum);
            }
            it = _base.find("endFrame");
            if (it != _base.end())
            {
                filePtr->parseNumber(tmpNum, it->second);
                tmp->endFrame = static_cast<float>(tmpNum);
            }
            else
            {
                tmp->endFrame = tmp->beginFrame;
            }
            it = _base.find("blinkFrameCount");
            if (it != _base.end())
            {
                filePtr->parseNumber(tmpNum, it->second);
                tmp->blinkFrameCount = static_cast<float>(tmpNum);
            }
            it = _base.find("blinkIntervalMax");
            if (it != _base.end())
            {
                filePtr->parseNumber(tmpNum, it->second);
                tmp->blinkIntervalMax = static_cast<float>(tmpNum);
            }
            it = _base.find("blinkIntervalMin");
            if (it != _base.end())
            {
                filePtr->parseNumber(tmpNum, it->second);
                tmp->blinkIntervalMin = static_cast<float>(tmpNum);
            }
            tmp->uid = std::uniform_int_distribution<int32_t>(tmp->blinkIntervalMin, tmp->blinkIntervalMax);
            _eyeControl.push_back(tmp);
        }
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["eyebrowControl"]);
        for (auto itm : _tmpList)
        {
            filePtr->parseObject(_base, itm);
            eyebrowControl* tmp = new eyebrowControl();
            int64_t tmpNum = 0;
            auto it = _base.find("label");
            if (it != _base.end())
            {
                filePtr->parseString(tmp->label, it->second);
            }
            it = _base.find("beginFrame");
            if (it != _base.end())
            {
                filePtr->parseNumber(tmpNum, it->second);
                tmp->beginFrame = static_cast<float>(tmpNum);
            }
            it = _base.find("endFrame");
            if (it != _base.end())
            {
                filePtr->parseNumber(tmpNum, it->second);
                tmp->endFrame = static_cast<float>(tmpNum);
            }
            else
            {
                tmp->endFrame = tmp->beginFrame;
            }
            _eyebrowControl.push_back(tmp);
        }

        // bustControl
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["bustControl"]);
        for (auto itm : _tmpList)
        {
            bustControl* tmp = new bustControl(filePtr, itm);
            _bustControl.push_back(tmp);
        }
        // hairControl
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["hairControl"]);
        for (auto itm : _tmpList)
        {
            uniControl* tmp = new uniControl(filePtr, itm);
            _hairControl.push_back(tmp);
        }
        // partsControl
        _tmpList.clear();
        filePtr->parseList(_tmpList, _rootData["partsControl"]);
        for (auto itm : _tmpList)
        {
            uniControl* tmp = new uniControl(filePtr, itm);
            _partsControl.push_back(tmp);
        }
    }
    emotemetadata::~emotemetadata()
    {
        for (auto itm : _attrcomp)
        {
            if (itm != nullptr)
                delete itm;
        }
        for (auto itm : _selectorControl)
        {
            if (itm != nullptr)
                delete itm;
        }
        for (auto itm : _timelineControl)
        {
            if (itm != nullptr)
                delete itm;
        }
        for (auto itm : _eyeControl)
        {
            if (itm != nullptr)
                delete itm;
        }
        for (auto itm : _eyebrowControl)
        {
            if (itm != nullptr)
                delete itm;
        }
        for (auto itm : _bustControl)
        {
            if (itm != nullptr)
                delete itm;
        }
        for (auto itm : _hairControl)
        {
            if (itm != nullptr)
                delete itm;
        }
        for (auto itm : _partsControl)
        {
            if (itm != nullptr)
                delete itm;
        }
    }

    emoteicon::emoteicon(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // base
        filePtr->parseReal(originX, _rootData["originX"]);
        filePtr->parseReal(originY, _rootData["originY"]);
        filePtr->parseReal(width, _rootData["width"]);
        filePtr->parseReal(height, _rootData["height"]);
        if (filePtr->isKrkr)
        {
            // compress type
            auto it = _rootData.find("compress");
            if (it == _rootData.end())
            {
                compress = "none";
            }
            else
            {
                filePtr->parseString(compress, it->second);
            }
            // pixel id
            filePtr->parseResource(pixel, _rootData["pixel"]);
            // is pal?
            it = _rootData.find("pal");
            if (it != _rootData.end())
            {
                type = "pal";
                filePtr->parseResource(pal, it->second);
            }
        }
        else
        {
            // left/top
            filePtr->parseReal(left, _rootData["left"]);
            filePtr->parseReal(top, _rootData["top"]);
        }

        // clip
        auto it = _rootData.find("clip");
        if (it != _rootData.end())
        {
            filePtr->parseObject(_rootData, it->second);
            filePtr->parseReal(_clip.bottom, _rootData["bottom"]);
            filePtr->parseReal(_clip.left, _rootData["left"]);
            filePtr->parseReal(_clip.right, _rootData["right"]);
            filePtr->parseReal(_clip.top, _rootData["top"]);
        }
    }
    emoteicon::~emoteicon()
    {

    }

    emotesource::emotesource(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
    {
        // dic
        std::map<std::string, uint32_t> _rootData;
        filePtr->parseObject(_rootData, startOffset);
        // type
        int64_t tmp;
        filePtr->parseNumber(tmp, _rootData["type"]);
        type = static_cast<int8_t>(tmp);
        // icon
        std::map<std::string, uint32_t> _motion;
        filePtr->parseObject(_motion, _rootData["icon"]);
        for (auto _obj : _motion)
        {
            emoteicon* tmp = new emoteicon(filePtr, _obj.second);
            icon.insert(std::pair<std::string, emoteicon*>(_obj.first, tmp));
        }
        // texture
        if (!filePtr->isKrkr)
        {
            if (filePtr->parseObject(_rootData, _rootData["texture"]))
            {
                std::string intType;
                double width = 0;
                double height = 0;
                int32_t pixel = -1;
                filePtr->parseReal(width, _rootData["width"]);
                filePtr->parseReal(height, _rootData["height"]);
                filePtr->parseString(intType, _rootData["type"]);
                filePtr->parseResource(pixel, _rootData["pixel"]);
                // 子类赋值
                for (auto _obj : icon)
                {
                    _obj.second->type = intType;
                    _obj.second->pixel = pixel;
                    _obj.second->texWidth = width;
                    _obj.second->texHeight = height;
                }
            }
        }
    }
    emotesource::~emotesource()
    {
        for (auto ic : icon)
        {
            if (ic.second != nullptr)
                delete ic.second;
        }
    }

    emotefile::emotefile()
    {
        _header.version = 0;
        _header.encrypt = 0;
        _header.offsetEncrypt = 0;
        _header.offsetNames = 0;
        _header.offsetStrings = 0;
        _header.offsetStringsData = 0;
        _header.offsetChunkOffsets = 0;
        _header.offsetChunkLengths = 0;
        _header.offsetChunkData = 0;
        _header.offsetEntries = 0;
        _header.checksum = 0;
        _header.offsetExtraChunkOffsets = 0;
        _header.offsetExtraChunkLengths = 0;
        _header.offsetExtraChunkData = 0;
    }
    emotefile::~emotefile()
    {
        if (filePtr != nullptr)
            delete filePtr;
        ClearAniTree();
    }
    void emotefile::setSeed(tjs_int seed)
    {
        _seed = seed;
    }
    void emotefile::setFun(tTJSVariantClosure decryptClo)
    {
        _decryptClo = decryptClo;
    }
    bool emotefile::load(const ttstr& filePath)
    {
        if (filePtr != NULL)
            delete filePtr;
        filePtr = TVPCreateStream(filePath);
        if (!filePtr)
            return false;
        const size_t readSize = filePtr->GetSize();
        if (readSize < 9)
            return false;

        // Decompress MDF
        char sign[5];
        filePtr->Read(sign, 5);
        sign[4] = '\0';
        char lzfs[] = {0x04, 0x22, 0x4d, 0x18, 0x00};
        if (SDL_strcasecmp(sign, lzfs) == 0) // lzfs
        {
            filePtr = GetLz4Stream(filePtr);
        }
        else
        {
            sign[3] = '\0';
            if (SDL_strcasecmp(sign, "MDF") == 0)
            {
                // uncompress data
                uLongf uncompressedSize = filePtr->ReadI32LE();
                tjs_uint8* _uncompress_buffer = new tjs_uint8[uncompressedSize];
                // compress data
                tjs_uint8* _compress_buffer = new tjs_uint8[readSize - 8];
                filePtr->Read(_compress_buffer, readSize - 8);
                // uncompress buffer
                if (uncompress(_uncompress_buffer, &uncompressedSize, _compress_buffer,
                               readSize - 8) != Z_OK)
                {
                    delete[] _uncompress_buffer, _compress_buffer;
                    return false;
                }
                // compress data
                tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, uncompressedSize);
                memcpy(_stream->GetInternalBuffer(), _uncompress_buffer, uncompressedSize);
                // clear data
                delete[] _uncompress_buffer, _compress_buffer;
                delete filePtr;
                filePtr = _stream;
            }
        }

        // ReadHeader
        filePtr->SetPosition(0);
        _header.parsePSBHeader(filePtr);

        if (_seed > 0)
        {
            // decrypt
            tjs_uint32 key[4];
            key[0] = 0x075BCD15;
            key[1] = 0x159A55E5;
            key[2] = 0x1F123BB5;
            key[3] = _seed;
            EMoteCTX emoteCtx{};
            init_emote_ctx(&emoteCtx, key);

            if (_header.isEncrypted() && _header.getHeaderLength() > filePtr->GetSize())
            {

                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetEncrypt), 4);
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetNames), 4);
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetStrings), 4);
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetStringsData),
                              4);
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetChunkOffsets),
                              4);
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetChunkLengths),
                              4);
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetChunkData), 4);
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetEntries), 4);

                if (_header.version > 2)
                {
                    emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.checksum), 4);
                }

                if (_header.version > 3)
                {
                    emote_decrypt(&emoteCtx,
                                  reinterpret_cast<tjs_uint8*>(&_header.offsetExtraChunkOffsets),
                                  4);
                    emote_decrypt(&emoteCtx,
                                  reinterpret_cast<tjs_uint8*>(&_header.offsetExtraChunkLengths),
                                  4);
                    emote_decrypt(&emoteCtx,
                                  reinterpret_cast<tjs_uint8*>(&_header.offsetExtraChunkData), 4);
                }
            }

            if (_header.version == 2)
            {
                // convert to memorystream
                tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, filePtr->GetSize());
                filePtr->SetPosition(0);
                filePtr->ReadBuffer(_stream->GetInternalBuffer(), filePtr->GetSize());

                emote_decrypt(&emoteCtx,
                              &static_cast<std::uint8_t*>(
                                  _stream->GetInternalBuffer())[_header.offsetEncrypt],
                              _header.offsetChunkOffsets - _header.offsetEncrypt);

                delete filePtr;
                filePtr = _stream;
            }
        }
        if (_decryptClo.Object != NULL)
        {
            tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, filePtr->GetSize());
            filePtr->SetPosition(0);
            filePtr->ReadBuffer(_stream->GetInternalBuffer(), filePtr->GetSize());
            CBinaryAccessor* cba =
                new CBinaryAccessor((unsigned char*)_stream->GetInternalBuffer(), _stream->GetSize());
            tTJSVariant buff(cba);
            cba->Release();
            tTJSVariant bufferLen((tjs_int)_stream->GetSize());
            tTJSVariant* vars[] = {&buff, &bufferLen};
            _decryptClo.FuncCall(0, NULL, NULL, NULL, 2, vars, NULL);

            delete filePtr;
            filePtr = _stream;

            // reReadHeader
            filePtr->SetPosition(0);
            _header.parsePSBHeader(filePtr);
        }

        // Encrypted
        if (_header.isEncrypted() && _header.getHeaderLength() > filePtr->GetSize())
        {
            TVPConsoleLog("psb file is encrypted");
            return false;
        }

        // Pre Load Strings
        filePtr->SetPosition(_header.offsetStrings);
        PSB::parsePSBArray(
            &stringsOffset,
            filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);

        // Pre Load Names
        if (_header.version == 1)
        {
            // don't believe HeaderLength
            if (_header.offsetEncrypt >= filePtr->GetSize())
            {
                _header.offsetEncrypt = _header.getHeaderLength();
            }
            filePtr->SetPosition(_header.offsetEncrypt);
            PSB::parsePSBArray(
                &nameIndexes,
                filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
            // Load Names
            namesCache.reserve(nameIndexes.size());
            for (int i = 0; i < namesCache.size(); i++)
            {
                filePtr->SetPosition(_header.offsetNames + nameIndexes[i]);
                std::string str;
                tjs_uint8 val8 = 0;
                while (true)
                {
                    if (filePtr->Read(&val8, 1) == 0)
                        break;

                    if (val8 == '\0')
                    {
                        str.append(1, val8);
                        break;
                    }
                    str.append(1, val8);
                }
                namesCache.push_back(str);
            }
        }
        else
        {
            filePtr->SetPosition(_header.offsetNames);
            PSB::parsePSBArray(
                &charset, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                filePtr);
            PSB::parsePSBArray(
                &namesData,
                filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
            PSB::parsePSBArray(
                &nameIndexes,
                filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
            // Load Names
            namesCache.reserve(nameIndexes.size());
            for (int i = 0; i < nameIndexes.size(); i++)
            {
                std::string list;
                const auto index = nameIndexes[i];
                auto chr = namesData[index];
                while (chr != 0)
                {
                    const auto code = namesData[chr];
                    const auto d = charset[code];
                    const auto realChr = chr - d;
                    chr = code;
                    list.append(1, realChr);
                }
                std::reverse(list.begin(), list.end());
                namesCache.push_back(list);
            }
        }

        // Pre Load Resources (Chunks)
        filePtr->SetPosition(_header.offsetChunkOffsets);
        PSB::parsePSBArray(
            &chunkOffsets,
            filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
        filePtr->SetPosition(_header.offsetChunkLengths);
        PSB::parsePSBArray(
            &chunkLengths,
            filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);

        if (_header.version >= 4 && _header.offsetExtraChunkOffsets < filePtr->GetSize())
        {
            // Pre Load Extra Resources (Chunks)
            filePtr->SetPosition(_header.offsetExtraChunkOffsets);
            PSB::parsePSBArray(
                &extraChunkOffsets,
                filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
            filePtr->SetPosition(_header.offsetExtraChunkLengths);
            PSB::parsePSBArray(
                &extraChunkLengths,
                filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
        }

        return GenerateAniTree();
    }
    tTJSVariant emotefile::root()
    {
        return readAllObjs("root", _header.offsetEntries);
    }
    tTJSVariant emotefile::readAllObjs(const ttstr& key, tjs_uint32 _objOffset)
    {
        filePtr->SetPosition(_objOffset);
        auto typeByte = filePtr->ReadI8LE();
        switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
        {
            case PSB::PSBObjType::None:
                return NULL;
            case PSB::PSBObjType::Null:
                return tTJSVariant();
            case PSB::PSBObjType::False:
            case PSB::PSBObjType::True:
                return tTJSVariant(type == PSB::PSBObjType::True);
            case PSB::PSBObjType::NumberN0:
                return tTJSVariant(0);
            case PSB::PSBObjType::NumberN1:
            {
                tjs_int8 val8 = 0;
                filePtr->ReadBuffer(&val8, 1);
                return tTJSVariant(val8);
            }
            case PSB::PSBObjType::NumberN2:
            {
                tjs_int16 val16 = 0;
                filePtr->ReadBuffer(&val16, 2);
                return tTJSVariant(val16);
            }
            case PSB::PSBObjType::NumberN3:
            {
                tjs_int32 val32 = 0;
                filePtr->ReadBuffer(&val32, 3);
                val32 |= ((val32 & 0x800000) == 0 ? 0 : 0xFFFF0000);
                return tTJSVariant(val32);
            }
            case PSB::PSBObjType::NumberN4:
            {
                tjs_int32 val32 = 0;
                filePtr->ReadBuffer(&val32, 4);
                return tTJSVariant(val32);
            }
            case PSB::PSBObjType::NumberN5:
            {
                tjs_int64 val64 = 0;
                filePtr->ReadBuffer(&val64, 5);
                val64 |= ((val64 & 0x8000000000) == 0 ? 0 : 0xFFFFFFFF00000000);
                return tTJSVariant(val64);
            }
            case PSB::PSBObjType::NumberN6:
            {
                tjs_int64 val64 = 0;
                filePtr->ReadBuffer(&val64, 6);
                val64 |= ((val64 & 0x800000000000) == 0 ? 0 : 0xFFFFFF0000000000);
                return tTJSVariant(val64);
            }
            case PSB::PSBObjType::NumberN7:
            {
                tjs_int64 val64 = 0;
                filePtr->ReadBuffer(&val64, 7);
                val64 |= ((val64 & 0x80000000000000) == 0 ? 0 : 0xFFFF000000000000);
                return tTJSVariant(val64);
            }
            case PSB::PSBObjType::NumberN8:
            {
                tjs_int64 val64 = 0;
                filePtr->ReadBuffer(&val64, 8);
                return tTJSVariant(val64);
            }
            case PSB::PSBObjType::Float0:
                return tTJSVariant(0.0f);
            case PSB::PSBObjType::Float:
            {
                tjs_uint8 buffer[4];
                filePtr->ReadBuffer(buffer, 4);
                float tmp;
                std::memcpy(&tmp, buffer, 4);
                return tTJSVariant((tjs_real)tmp);
            }
            case PSB::PSBObjType::Double:
            {
                tjs_uint8 buffer[8];
                filePtr->ReadBuffer(buffer, 8);
                double tmp;
                std::memcpy(&tmp, buffer, 8);
                return tTJSVariant((tjs_real)tmp);
            }
            case PSB::PSBObjType::ArrayN1:
            case PSB::PSBObjType::ArrayN2:
            case PSB::PSBObjType::ArrayN3:
            case PSB::PSBObjType::ArrayN4:
            case PSB::PSBObjType::ArrayN5:
            case PSB::PSBObjType::ArrayN6:
            case PSB::PSBObjType::ArrayN7:
            case PSB::PSBObjType::ArrayN8:
            {
                std::vector<tjs_uint32> tmp;
                PSB::parsePSBArray(
                    &tmp, typeByte - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
                iTJSDispatch2* array = TJSCreateArrayObject();
                for (auto i : tmp)
                {
                    tTJSVariant tmp(static_cast<tjs_int32>(i));
                    tTJSVariant* args[] = {&tmp};
                    static tjs_uint addHint = 0;
                    array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, args, array);
                }
                tTJSVariant result(array, array);
                array->Release();
                return result;
            }
            case PSB::PSBObjType::StringN1:
            case PSB::PSBObjType::StringN2:
            case PSB::PSBObjType::StringN3:
            case PSB::PSBObjType::StringN4:
            {
                tjs_int32 idx = 0;
                filePtr->ReadBuffer(&idx, typeByte -
                                              static_cast<tjs_int8>(PSB::PSBObjType::StringN1) + 1);
                filePtr->SetPosition(_header.offsetStringsData + stringsOffset[idx]);
                std::string str;
                tjs_uint8 val8 = 0;
                while (true)
                {
                    if (filePtr->Read(&val8, 1) == 0)
                        break;

                    if (val8 == '\0')
                    {
                        str.append(1, val8);
                        return tTJSVariant(str);
                    }
                    str.append(1, val8);
                }
                return tTJSVariant("");
            }
            case PSB::PSBObjType::ResourceN1:
            case PSB::PSBObjType::ResourceN2:
            case PSB::PSBObjType::ResourceN3:
            case PSB::PSBObjType::ResourceN4:
            {
                return tTJSVariant();
            }
            case PSB::PSBObjType::ExtraChunkN1:
            case PSB::PSBObjType::ExtraChunkN2:
            case PSB::PSBObjType::ExtraChunkN3:
            case PSB::PSBObjType::ExtraChunkN4:
            {
                return tTJSVariant();
            }
            case PSB::PSBObjType::List:
            {
                std::vector<tjs_uint32> _objsOffset;
                tjs_uint32 _tmpOffset = this->readListInfo(&_objsOffset);

                iTJSDispatch2* array = TJSCreateArrayObject();
                for (auto _offset : _objsOffset)
                {
                    tTJSVariant obj = readAllObjs(ttstr(), _tmpOffset + _offset);
                    tTJSVariant* args[] = {&obj};
                    static tjs_uint addHint = 0;
                    array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, args, array);
                }
                tTJSVariant result(array, array);
                array->Release();
                return result;
            }
            case PSB::PSBObjType::Objects:
            {
                std::vector<tjs_uint32> _objsOffset, _objsNamesIdx;
                tjs_uint32 _tmpOffset = 0;
                if (_header.version == 1)
                {
                    _tmpOffset = this->readListInfo(&_objsOffset);
                    this->refreshListInfo(&_objsOffset, &_objsNamesIdx);
                }
                else
                {
                    this->readListInfo(&_objsNamesIdx);
                    _tmpOffset = this->readListInfo(&_objsOffset);
                }

                iTJSDispatch2* dsp = TJSCreateDictionaryObject();
                for (tjs_int i = 0; i < _objsNamesIdx.size(); i++)
                {
                    ttstr keyName = namesCache.at(_objsNamesIdx.at(i));
                    tTJSVariant obj = readAllObjs(keyName, _tmpOffset + _objsOffset.at(i));
                    dsp->PropSet(TJS_MEMBERENSURE, keyName.c_str(), nullptr, &obj, dsp);
                }
                tTJSVariant result(dsp, dsp);
                dsp->Release();
                return result;
            }
            default:
                TVPConsoleLog("unknown psbObjType");
                return NULL;
        }
    }
    uint32_t emotefile::readListInfo(std::vector<uint32_t>* target)
    {
        PSB::parsePSBArray(target,
                           filePtr->ReadI8LE() - static_cast<int8_t>(PSB::PSBObjType::ArrayN1) + 1,
                           filePtr);
        return filePtr->GetPosition();
    }
    void emotefile::refreshListInfo(std::vector<uint32_t>* target1, std::vector<uint32_t>* target2)
    {
        target2->reserve(target1->size());
        uint32_t basePose = filePtr->GetPosition();
        for (uint32_t i = 0; i < target1->size(); i++)
        {
            filePtr->SetPosition(basePose + target1->at(i));
            target2->at(i) = filePtr->ReadI8LE();
            target1->at(i) += 4;
        }
    }
    bool emotefile::parseObject(std::map<std::string, uint32_t>& output, uint32_t _objOffset)
    {
        filePtr->SetPosition(_objOffset);
        auto typeByte = filePtr->ReadI8LE();
        if (static_cast<PSB::PSBObjType>(typeByte) == PSB::PSBObjType::Null)
        {
            return false;
        }
        else if (static_cast<PSB::PSBObjType>(typeByte) != PSB::PSBObjType::Objects)
        {
            SDL_Log("Invalidate Object");
            return false;
        }
        output.clear();

        std::vector<uint32_t> _objsOffset, _objsNamesIdx;
        uint32_t _tmpOffset = 0;
        if (_header.version == 1)
        {
            _tmpOffset = this->readListInfo(&_objsOffset);
            this->refreshListInfo(&_objsOffset, &_objsNamesIdx);
        }
        else
        {
            this->readListInfo(&_objsNamesIdx);
            _tmpOffset = this->readListInfo(&_objsOffset);
        }

        for (int32_t i = 0; i < _objsNamesIdx.size(); i++)
        {
            output.insert(std::pair<std::string, uint32_t>(namesCache.at(_objsNamesIdx.at(i)),
                _tmpOffset + _objsOffset.at(i)));
        }
        return true;
    }
    bool emotefile::parseNumber(int64_t& output, uint32_t _objOffset)
    {
        filePtr->SetPosition(_objOffset);
        auto typeByte = filePtr->ReadI8LE();
        switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
        {
        case PSB::PSBObjType::NumberN0:
        {
            output = 0;
            return true;
        }
        case PSB::PSBObjType::NumberN1:
        {
            int8_t val8 = 0;
            filePtr->ReadBuffer(&val8, 1);
            output = static_cast<int64_t>(val8);
            return true;
        }
        case PSB::PSBObjType::NumberN2:
        {
            int16_t val16 = 0;
            filePtr->ReadBuffer(&val16, 2);
            output = static_cast<int64_t>(val16);
            return true;
        }
        case PSB::PSBObjType::NumberN3:
        {
            int32_t val32 = 0;
            filePtr->ReadBuffer(&val32, 3);
            val32 |= ((val32 & 0x800000) == 0 ? 0 : 0xFFFF0000);
            output = static_cast<int64_t>(val32);
            return true;
        }
        case PSB::PSBObjType::NumberN4:
        {
            int32_t val32 = 0;
            filePtr->ReadBuffer(&val32, 4);
            output = static_cast<int64_t>(val32);
            return true;
        }
        case PSB::PSBObjType::NumberN5:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 5);
            val64 |= ((val64 & 0x8000000000) == 0 ? 0 : 0xFFFFFFFF00000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN6:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 6);
            val64 |= ((val64 & 0x800000000000) == 0 ? 0 : 0xFFFFFF0000000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN7:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 7);
            val64 |= ((val64 & 0x80000000000000) == 0 ? 0 : 0xFFFF000000000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN8:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 8);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::Null:
        {
            return false;
        }
        default:
            SDL_Log("Invalidate Number");
            return false;
        }
    }
    bool emotefile::parseReal(double& output, uint32_t _objOffset)
    {
        filePtr->SetPosition(_objOffset);
        auto typeByte = filePtr->ReadI8LE();
        switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
        {
            case PSB::PSBObjType::NumberN0:
            {
                output = 0;
                return true;
            }
            case PSB::PSBObjType::NumberN1:
            {
                int8_t val8 = 0;
                filePtr->ReadBuffer(&val8, 1);
                output = static_cast<int64_t>(val8);
                return true;
            }
            case PSB::PSBObjType::NumberN2:
            {
                int16_t val16 = 0;
                filePtr->ReadBuffer(&val16, 2);
                output = static_cast<int64_t>(val16);
                return true;
            }
            case PSB::PSBObjType::NumberN3:
            {
                int32_t val32 = 0;
                filePtr->ReadBuffer(&val32, 3);
                val32 |= ((val32 & 0x800000) == 0 ? 0 : 0xFFFF0000);
                output = static_cast<int64_t>(val32);
                return true;
            }
            case PSB::PSBObjType::NumberN4:
            {
                int32_t val32 = 0;
                filePtr->ReadBuffer(&val32, 4);
                output = static_cast<int64_t>(val32);
                return true;
            }
            case PSB::PSBObjType::NumberN5:
            {
                int64_t val64 = 0;
                filePtr->ReadBuffer(&val64, 5);
                val64 |= ((val64 & 0x8000000000) == 0 ? 0 : 0xFFFFFFFF00000000);
                output = static_cast<int64_t>(val64);
                return true;
            }
            case PSB::PSBObjType::NumberN6:
            {
                int64_t val64 = 0;
                filePtr->ReadBuffer(&val64, 6);
                val64 |= ((val64 & 0x800000000000) == 0 ? 0 : 0xFFFFFF0000000000);
                output = static_cast<int64_t>(val64);
                return true;
            }
            case PSB::PSBObjType::NumberN7:
            {
                int64_t val64 = 0;
                filePtr->ReadBuffer(&val64, 7);
                val64 |= ((val64 & 0x80000000000000) == 0 ? 0 : 0xFFFF000000000000);
                output = static_cast<int64_t>(val64);
                return true;
            }
            case PSB::PSBObjType::NumberN8:
            {
                int64_t val64 = 0;
                filePtr->ReadBuffer(&val64, 8);
                output = static_cast<int64_t>(val64);
                return true;
            }
            case PSB::PSBObjType::Float0:
            {
                output = 0.0;
                return true;
            }
            case PSB::PSBObjType::Float:
            {
                tjs_uint8 buffer[4];
                filePtr->ReadBuffer(buffer, 4);
                float tmp;
                std::memcpy(&tmp, buffer, 4);
                output = static_cast<tjs_real>(tmp);
                return true;
            }
            case PSB::PSBObjType::Double:
            {
                tjs_uint8 buffer[8];
                filePtr->ReadBuffer(buffer, 8);
                double tmp;
                std::memcpy(&tmp, buffer, 8);
                output = static_cast<tjs_real>(tmp);
                return true;
            }
            case PSB::PSBObjType::Null:
            {
                return false;
            }
            default:
            {
                std::string retStr;
                if (parseString(retStr, _objOffset))
                {
                    try
                    {
                        output = std::stod(retStr);
                        return true;
                    }
                    catch (...)
                    {
                        SDL_Log("Invalidate Real");
                        return false;
                    }
                }
                SDL_Log("Invalidate Real");
                return false;
            }
        }
    }
    bool emotefile::parseString(std::string& output, uint32_t _objOffset)
    {
        filePtr->SetPosition(_objOffset);
        auto typeByte = filePtr->ReadI8LE();
        switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
        {
        case PSB::PSBObjType::StringN1:
        case PSB::PSBObjType::StringN2:
        case PSB::PSBObjType::StringN3:
        case PSB::PSBObjType::StringN4:
        {
            tjs_int32 idx = 0;
            filePtr->ReadBuffer(&idx,
                                typeByte - static_cast<tjs_int8>(PSB::PSBObjType::StringN1) + 1);
            filePtr->SetPosition(_header.offsetStringsData + stringsOffset[idx]);
            std::string str;
            tjs_uint8 val8 = 0;
            while (true)
            {
                if (filePtr->Read(&val8, 1) == 0)
                    break;

                if (val8 == '\0')
                {
                    str.append(1, val8);
                    break;
                }
                str.append(1, val8);
            }
            output = str;
            return true;
        }
        default:
            SDL_Log("Invalidate String");
            return false;
        }
    }
    bool emotefile::parseList(std::vector<uint32_t>& output, uint32_t _objOffset)
    {
        filePtr->SetPosition(_objOffset);
        auto typeByte = filePtr->ReadI8LE();
        if ((static_cast<PSB::PSBObjType>(typeByte) == PSB::PSBObjType::Null))
        {
            return false;
        }
        else if (static_cast<PSB::PSBObjType>(typeByte) != PSB::PSBObjType::List)
        {
            SDL_Log("Invalidate List");
            return false;
        }
        std::vector<uint32_t> _objsOffset;
        uint32_t _tmpOffset = this->readListInfo(&_objsOffset);

        for (auto _offset : _objsOffset)
        {
            output.push_back(_tmpOffset + _offset);
        }

        return true;
    };
    bool emotefile::parseResource(int32_t& id, uint32_t _objOffset)
    {
        filePtr->SetPosition(_objOffset);
        auto typeByte = filePtr->ReadI8LE();
        switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
        {
        case PSB::PSBObjType::ResourceN1:
        case PSB::PSBObjType::ResourceN2:
        case PSB::PSBObjType::ResourceN3:
        case PSB::PSBObjType::ResourceN4:
        {
            id = 0;
            filePtr->ReadBuffer(&id,
                                typeByte - static_cast<int8_t>(PSB::PSBObjType::ResourceN1) + 1);
            return true;
        }
        default:
            SDL_Log("Invalidate Resource");
            return false;
        }
    }
    uint64_t emotefile::GetSize()
    {
        return filePtr->GetSize();
    }
    void emotefile::ReadAllData(uint8_t* output, uint32_t outputlen)
    {
        filePtr->SetPosition(0);
        filePtr->ReadBuffer(output, outputlen);
    }
    bool emotefile::GenerateAniTree()
    {
        // baseObj
        std::map<std::string, uint32_t> _rootData;
        parseObject(_rootData, _header.offsetEntries);
        // isemote
        std::string typeName;
        parseString(typeName, _rootData["spec"]);
        if (strcmp(typeName.c_str(), "krkr") == 0) // emote
        {
            isKrkr = true;
        }
        else if (strcmp(typeName.c_str(), "win") == 0) // motion
        {
            isKrkr = false;
        }
        else if (strcmp(typeName.c_str(), "common") == 0)
        {
            isKrkr = false;
            colorType = 1;
        }
        else
        {
            SDL_Log("unknown emoteType!!!");
        }
        // metadata
        _metadata = new emotemetadata(this, _rootData["metadata"]);
        // objs
        std::map<std::string, uint32_t> _tmpMap;
        parseObject(_tmpMap, _rootData["object"]);
        for (auto _obj : _tmpMap)
        {
            emoteobject* tmp = new emoteobject(this, _obj.second);
            _objects.insert(std::pair<std::string, emoteobject*>(_obj.first, tmp));
        }
        // screen
        parseObject(_tmpMap, _rootData["screenSize"]);
        int64_t tmp;
        parseNumber(tmp, _tmpMap["originX"]);
        _screenSize.originX = static_cast<int32_t>(tmp);
        parseNumber(tmp, _tmpMap["originY"]);
        _screenSize.originY = static_cast<int32_t>(tmp);
        parseNumber(tmp, _tmpMap["width"]);
        _screenSize.width = static_cast<int32_t>(tmp);
        parseNumber(tmp, _tmpMap["height"]);
        _screenSize.height = static_cast<int32_t>(tmp);
        // source
        parseObject(_tmpMap, _rootData["source"]);
        for (auto _obj : _tmpMap)
        {
            emotesource* tmp = new emotesource(this, _obj.second);
            _source.insert(std::pair<std::string, emotesource*>(_obj.first, tmp));
        }
        // stereovisionProfile
        parseObject(_tmpMap, _rootData["stereovisionProfile"]);
        parseReal(_stereovisionProfile.dist_e2d, _tmpMap["dist_e2d"]);
        parseReal(_stereovisionProfile.dist_eye, _tmpMap["dist_eye"]);
        parseReal(_stereovisionProfile.eye_angle_ltd, _tmpMap["eye_angle_ltd"]);
        parseReal(_stereovisionProfile.f_level, _tmpMap["f_level"]);
        parseReal(_stereovisionProfile.fov, _tmpMap["fov"]);
        parseReal(_stereovisionProfile.len_disp, _tmpMap["len_disp"]);

        // getLastSyncTime
        for (auto _objTmp : _objects)
        {
            for (auto _tmpMtn : _objTmp.second->motion)
            {
                std::vector<emotenode*> _stack(_tmpMtn.second->nodeList.begin(),
                                               _tmpMtn.second->nodeList.end());
                while (!_stack.empty())
                {
                    emotenode* _current = _stack.back();
                    _stack.pop_back();

                    for (auto frameTmp : _current->frameList)
                    {
                        if (frameTmp != nullptr && frameTmp->hasContent)
                        {
                            if (frameTmp->time > _tmpMtn.second->syncTime)
                                _tmpMtn.second->syncTime = frameTmp->time;

                            emotemotion* _emot = findmotionByName(frameTmp->src);
                            if (_emot != nullptr)
                            {
                                for (auto it = _emot->nodeList.begin(); it != _emot->nodeList.end();
                                     ++it)
                                {
                                    _stack.push_back(*it);
                                }
                            }
                        }
                    }
                }
            }
        }
        // mark attrcomp
        for (auto attrItm : _metadata->_attrcomp)
        {
            if (attrItm == nullptr)
                continue;
            for (auto removeDataItm : attrItm->data_remove)
            {
                if (removeDataItm.value > 0)
                    continue;
                auto cha = _objects.find(removeDataItm.chara.c_str());
                if (cha != _objects.end())
                {
                    auto mtn = cha->second->motion.find(removeDataItm.motion.c_str());
                    if (mtn != cha->second->motion.end())
                    {
                        auto lay = mtn->second->getNodeByName(removeDataItm.layer);
                        if (lay != nullptr)
                        {
                            lay->removed = true;
                        }
                    }
                }
            }
        }
        // selectValueSet
        for (auto selcItm : _metadata->_selectorControl)
        {
            selcItm->selectValue(0);
        }

        return true;
    }
    bool emotefile::ClearAniTree()
    {
        if (_metadata != nullptr)
            delete _metadata;
        for (auto obj : _objects)
        {
            if (obj.second != nullptr)
                delete obj.second;
        }
        for (auto src : _source)
        {
            if (src.second != nullptr)
                delete src.second;
        }
        return true;
    }
    void emotefile::updateZMax(float zMax)
    {
        if (abs(zMax) > _zMax)
        {
            _zMax = abs(zMax);
        }
    }
    float emotefile::getZMax(bool isMain)
    {
        float _zMaxTmp = _zMax;
        // 附属
        if (isMain)
        {
            for (auto itm : _attach)
            {
                if (itm->getZMax() > _zMaxTmp)
                    _zMaxTmp = itm->getZMax();
            }
        }
        return _zMaxTmp;
    }
    void emotefile::updateSyncTime(float _syn)
    {
        if (_syn > _syncTime)
        {
            _syncTime = _syn;
        }
    }
    float emotefile::getSyncTime()
    {
        return _syncTime;
    }
    emoteicon* emotefile::findsourceByName(const std::string& name)
    {
        std::istringstream iss(name);
        std::string token;

        // 源头
        std::getline(iss, token, '/');
        if (strcmp(token.c_str(), "src") == 0)
        {
            // 一级路径
            std::getline(iss, token, '/');
            auto tmp = _source.find(token.c_str());
            if (tmp != _source.end())
            {
                emotesource* src = tmp->second;
                // 二级路径
                std::getline(iss, token, '/');
                auto tmp1 = src->icon.find(token.c_str());
                if (tmp1 != src->icon.end())
                {
                    return tmp1->second;
                }
            }
            SDL_Log("src find failed!!!");
        }
        else
        {
            return nullptr;
        }
        return nullptr;
    }
    bool emotefile::readIconTobuffer(uint8_t* buff, uint32_t buffSize, uint32_t pitch, emoteicon* ic)
    {
        if (ic->pixel < 0 || ic->pixel >= chunkLengths.size()) return false;
        // 生数据读取
        uint8_t* srcbuff = new uint8_t[chunkLengths.at(ic->pixel)];
        filePtr->SetPosition(_header.offsetChunkData + chunkOffsets.at(ic->pixel));
        filePtr->ReadBuffer(srcbuff, chunkLengths.at(ic->pixel));
        // 解码方案
        if (strcmp(ic->compress.c_str(), "RL") == 0)
        {
            if (ic->pal > -1)
            {
                uint8_t* palbuff = new uint8_t[chunkLengths.at(ic->pal)];
                filePtr->SetPosition(_header.offsetChunkData + chunkOffsets.at(ic->pal));
                filePtr->ReadBuffer(palbuff, chunkLengths.at(ic->pal));

                uint32_t currSize = 0;
                uint8_t* currPtr = srcbuff;
                uint8_t* endPtr = currPtr + chunkLengths.at(ic->pixel);
                uint8_t* currDst = buff;
                while (currPtr < endPtr && currSize < buffSize)
                {
                    int current = *currPtr;
                    currPtr++;
                    int count;
                    if ((current & 0x80) != 0) // Redundant
                    {
                        count = (current ^ 0x80) + 3;
                        uint8_t coloridx = *currPtr;
                        currPtr += 1;
                        for (int i = 0; i < count; i++)
                        {
                            if (currSize + 4 > buffSize)
                                break;
                            memcpy(currDst, palbuff + coloridx, 4);
                            currDst += 4;
                            currSize += 4;
                        }
                    }
                    else // not redundant
                    {
                        count = (current + 1);
                        if (currSize + count * 4 > buffSize)
                            count = (buffSize - currSize) / 4;
                        for (int i = 0; i < count; i++)
                        {
                            memcpy(currDst + i * 4, palbuff + (*(currPtr + i)), 4);
                        }
                        currPtr += count;
                        currDst += count * 4;
                        currSize += count * 4;
                    }
                }
            }
            else
            {
                uint32_t currSize = 0;
                uint8_t* currPtr = srcbuff;
                uint8_t* endPtr = currPtr + chunkLengths.at(ic->pixel);
                uint8_t* currDst = buff;
                while (currPtr < endPtr && currSize < buffSize)
                {
                    int current = *currPtr;
                    currPtr++;
                    int count;
                    if ((current & 0x80) != 0) // Redundant
                    {
                        count = (current ^ 0x80) + 3;
                        uint32_t color = *((uint32_t*)currPtr);
                        currPtr += 4;
                        for (int i = 0; i < count; i++)
                        {
                            if (currSize + 4 > buffSize)
                                break;
                            memcpy(currDst, &color, 4);
                            currDst += 4;
                            currSize += 4;
                        }
                    }
                    else // not redundant
                    {
                        count = (current + 1) * 4;
                        if (currSize + count > buffSize)
                            count = buffSize - currSize;
                        memcpy(currDst, currPtr, count);
                        currPtr += count;
                        currDst += count;
                        currSize += count;
                    }
                }
            }
        }
        else if (strcmp(ic->compress.c_str(), "none") == 0)
        {
            uint32_t cpySize = std::min(chunkLengths.at(ic->pixel), buffSize);
            memcpy(buff, srcbuff, cpySize);
        }
        else
        {
            if (!isKrkr) // 采用裁切贴图
            {
                if (strcmp(ic->type.c_str(), "RGBA8") == 0)
                {
                    uint8_t* start = srcbuff + (int)(ic->top * ic->texWidth + ic->left) * 4;
                    uint32_t cppitch = std::min(
                        (uint32_t)(std::min(ic->left + ic->width, ic->texWidth) - ic->left) * 4,
                        pitch);
                    uint32_t height = std::min(
                        (uint32_t)(std::min(ic->top + ic->height, ic->texHeight) - ic->top),
                        buffSize / pitch);
                    for (uint32_t i = 0; i < height; i++)
                    {
                        memcpy(buff + i * pitch, start + (int)(ic->texWidth * 4 * i), cppitch);
                    }
                }
                else
                {
                    SDL_Log("unknown icon format");
                    delete[] srcbuff;
                    return false;
                }
            }
            else
            {
                SDL_Log("unknown icon format");
                delete[] srcbuff;
                return false;
            }
        }
        delete[] srcbuff;
        return true;
    }
    emotemotion* emotefile::findmotionByName(const std::string& name)
    {
        std::istringstream iss(name);
        std::string token, token1, token2;

        // 源头
        std::getline(iss, token, '/');
        if (strcmp(token.c_str(), "motion") == 0)
        {
            std::getline(iss, token1, '/');
            std::getline(iss, token2, '/');
            // 一级路径
            auto tmp = _objects.find(token1.c_str());
            if (tmp != _objects.end())
            {
                emoteobject* src = tmp->second;
                // 二级路径
                auto tmp1 = src->motion.find(token2.c_str());
                if (tmp1 != src->motion.end())
                {
                    return tmp1->second;
                }
            }
            // 附属
            for (auto itm : _attach)
            {
                // 一级路径
                tmp = itm->_objects.find(token1.c_str());
                if (tmp != itm->_objects.end())
                {
                    emoteobject* src = tmp->second;
                    // 二级路径
                    auto tmp1 = src->motion.find(token2.c_str());
                    if (tmp1 != src->motion.end())
                    {
                        return tmp1->second;
                    }
                }
            }
            SDL_Log("motion find failed!!!");
        }
        else
        {
            return nullptr;
        }
        return nullptr;
    }
    bool emotefile::getTickByName(const std::string& name, float& ret)
    {
        if (_metadata == nullptr) return false;
        auto it = _metadata->_varList.find(name);
        if (it == _metadata->_varList.end()) return false;
        ret = it->second;
        return true;
    }
    void emotefile::updateEyeControl(float tick, bool isMain)
    {
        // 附属
        if (isMain)
        {
            for (auto itm : _attach)
            {
                itm->updateEyeControl(tick);
            }
        }

        std::default_random_engine dre;
        // 眼动控制
        for (auto itm : _metadata->_eyeControl)
        {
            // 初始化tick
            if (!itm->hasStart)
            {
                itm->hasStart = true;
                itm->lastTick = tick;
            }
            // 初始化间隔
            if (itm->currWaitInterval < 0)
            {
                itm->currWaitInterval = itm->uid(dre);
            }
            
            // 播放时 进行参数控制
            if (itm->isBlinking)
            {
                if (tick - itm->lastTick > itm->blinkFrameCount) // 是否结束
                {
                    itm->isBlinking = false;
                    itm->currWaitInterval = -1;
                    itm->lastTick = tick;
                    // 重置初始值
                    auto varPos = _metadata->_varList.find(itm->label);
                    if (varPos != _metadata->_varList.end())
                    {
                        varPos->second = itm->baseVal;
                    }
                }
                else
                {
                    // 计算数值
                    float realVal = itm->beginFrame;
                    if ((tick - itm->lastTick) * 2 < itm->blinkFrameCount ) // 闭眼
                    {
                        realVal += (tick - itm->lastTick) * 2 * (itm->endFrame - itm->beginFrame) / itm->blinkFrameCount;
                    }
                    else // 睁开
                    {
                        realVal += (itm->blinkFrameCount - (tick - itm->lastTick)) * 2 * (itm->endFrame - itm->beginFrame) / itm->blinkFrameCount;
                    }
                    realVal = SDL_max(realVal, itm->beginFrame);
                    realVal = SDL_min(realVal, itm->endFrame);
                    // 写入参数
                    auto varPos = _metadata->_varList.find(itm->label);
                    if (varPos != _metadata->_varList.end())
                    {
                        varPos->second = realVal;
                    }
                }
            }
            else // 未播放时进行等待
            {
                if (tick - itm->lastTick > itm->currWaitInterval)
                {
                    itm->isBlinking = true;
                    itm->lastTick = tick;
                    // 获取初始值
                    auto varPos = _metadata->_varList.find(itm->label);
                    if (varPos != _metadata->_varList.end())
                    {
                        itm->baseVal = varPos->second;
                    }
                }
            }
        }
        for (auto itm : _metadata->_eyeControl)
        {
            // nothing to do
        }
    }
    void emotefile::startTimeline(float tick, const std::string& name, bool isMain)
    {
        // 附属
        if (isMain)
        {
            for (auto itm : _attach)
            {
                itm->startTimeline(tick, name);
            }
        }

        for (auto itm : _metadata->_timelineControl)
        {
            if (strcmp(itm->label.c_str(), name.c_str()) == 0)
            {
                currTimeline.push_back(itm);
                currStartTick = tick;
                return;
            }
        }
    }
    void emotefile::stopTimeline(const std::string& name, bool isMain)
    {
        // 附属
        if (isMain)
        {
            for (auto itm : _attach)
            {
                itm->stopTimeline(name);
            }
        }

        for (auto itm : _metadata->_timelineControl)
        {
            if (strcmp(itm->label.c_str(), name.c_str()) == 0)
            {
                currTimeline.erase(std::remove(currTimeline.begin(), currTimeline.end(), itm),
                                   currTimeline.end());
                currStartTick = -1.0f;
                return;
            }
        }
    }
    bool emotefile::checkTimline(const std::string& name, bool& result, bool isMain)
    {
        // 附属
        if (isMain)
        {
            for (auto itm : _attach)
            {
                bool _result = false;
                if (itm->checkTimline(name, _result))
                {
                    result = _result;
                    return true;
                }
            }
        }

        emotetimeline* matchT = nullptr;
        for (auto itm : _metadata->_timelineControl)
        {
            if (strcmp(itm->label.c_str(), name.c_str()) == 0)
            {
                matchT = itm;
                break;
            }
        }
        if (matchT == nullptr)
            return false;
        for (auto itm : currTimeline)
        {
            if (itm == matchT)
            {
                result = true;
                return true;
            }
        }
        result = false;
        return true;
    }
    void emotefile::updateTimelineControl(float tick, bool isMain)
    {
        // 附属
        if (isMain)
        {
            for (auto itm : _attach)
            {
                itm->updateTimelineControl(tick);
            }
        }

        if (currTimeline.size() < 1) return;

        for (auto timelineItm : currTimeline)
        {
            // 考察时间
            if (timelineItm->loopEnd > 0.0f &&
                tick - currStartTick + timelineItm->loopBegin > timelineItm->loopEnd)
            {
                // 重置时间戳
                currStartTick = tick;
            }
            float currRelTime = tick - currStartTick + timelineItm->loopBegin;

            // 遍历每一个变量
            for (auto varItm : timelineItm->variableList)
            {
                // 跳过
                if (varItm->frameList.size() == 0)
                    continue;

                // 对于select变量进行跳过
                bool isfindInSelect = false;
                for (auto sleItm : _metadata->_selectorControl)
                {
                    for (auto _sleItm : sleItm->selectItem)
                    {
                        if (strcmp(_sleItm.label.c_str(), varItm->label.c_str()) == 0)
                        {
                            isfindInSelect = true;
                            break;
                        }
                    }
                    if (isfindInSelect)
                        break;
                }
                if (isfindInSelect)
                    continue;

                // 确定帧位置
                emoteTimeVarFrame* currFrame = nullptr;
                size_t currFrameIdx = -1;
                for (size_t i = 0; i < varItm->frameList.size(); i++)
                {
                    if (varItm->frameList.at(i)->time <= currRelTime)
                    {
                        currFrame = varItm->frameList.at(i);
                        currFrameIdx = i;
                    }
                    else
                        break;
                }
                if (currFrame == nullptr || !currFrame->hasContent)
                    continue;
                emoteTimeVarFrame* nextframe = nullptr;
                if (currFrameIdx >= 0 && currFrameIdx < varItm->frameList.size() - 1)
                    nextframe = varItm->frameList.at(currFrameIdx + 1);
                if (nextframe != nullptr && !nextframe->hasContent)
                    nextframe = nullptr;

                // 插值
                double realVal = 0.0;
                if (nextframe != nullptr &&
                    ((currFrame->type != 2) ||
                     (currFrame->type == 2 &&
                      nextframe->type == 2))) // 存在下一帧则对关键帧进行插值
                {
                    // val
                    realVal = (currFrame->value + (nextframe->value - currFrame->value) /
                                                      (nextframe->time - currFrame->time) *
                                                      (currRelTime - currFrame->time));
                }
                else
                {
                    // 计算坐标
                    realVal = currFrame->value;
                }

                // 赋予
                auto varPos = _metadata->_varList.find(varItm->label);
                if (varPos != _metadata->_varList.end())
                {
                    varPos->second = realVal;
                }
            }
        }
    }
    emoteVar* emotefile::findVarByName(const std::string& name)
    {
        for (auto obj : _objects)
        {
            emoteVar* ret = nullptr;
            ret = obj.second->findVarByName(name);
            if (ret != nullptr)
                return ret;
        }
        return nullptr;
    }
    static bool startswith(const std::string& str, const std::string& prefix)
    {
        return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
    }
    static bool endswith(const std::string& str, const std::string& suffix)
    {
        return str.size() >= suffix.size() &&
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    void emotefile::setVariable(const std::string& name, tjs_real value, bool isMain)
    {
        // 附属
        if (isMain)
        {
            for (auto itm : _attach)
            {
                itm->setVariable(name, value);
            }
        }

        // _selectorControl
        for (auto _selItm : _metadata->_selectorControl)
        {
            if (strcmp(_selItm->lable.c_str(), name.c_str()) == 0)
            {
                _selItm->selectValue(value);
                return;
            }
        }
        // _varList
        auto varPos = _metadata->_varList.find(name);
        if (varPos != _metadata->_varList.end())
        {
            varPos->second = value;
        }
    }
    void emotefile::updatePhysics(float tick)
    {
        for (auto itm : _metadata->_bustControl)
        {
            // 寻找各变量
            auto varLR = findVarByName(itm->var_lr);
            if (varLR == nullptr)
                continue;
            auto varUD = findVarByName(itm->var_ud);
            if (varUD == nullptr)
                continue;

            // 获取摆动值
            float sway_value = breast_sim.generate(tick, 0, -30.f, 30.f);
            varLR->currVal = sway_value;
            sway_value = breast_sim.generate(tick, 0, -30.f, 30.f);
            varUD->currVal = sway_value;
        }
        // for (auto itm : _metadata->_clampControl)
        //{
        //     auto varLR = _metadata->_varList.find(itm.var_lr);
        //     if (varLR == _metadata->_varList.end()) continue;
        //     auto varUD = _metadata->_varList.find(itm.var_ud);
        //     if (varUD == _metadata->_varList.end()) continue;
        //     float clamp_sway = oscillator.generate(tick, itm.min, itm.max);
        //     varLR->second = clamp_sway;
        //     varUD->second = clamp_sway;
        // }
        //  刮风
        hair_sim.startWind(glm::vec2(0.0f, 0.0f), glm::vec2(1280.0f, 0.0f), 0.15, 0.2, 0.3);
        for (auto itm : _metadata->_hairControl)
        {
            // 寻找各变量
            auto varLR = findVarByName(itm->var_lr);
            if (varLR == nullptr)
                continue;
            auto varLRM = findVarByName(itm->var_lrm);
            if (varLRM == nullptr)
                continue;
            auto varUD = findVarByName(itm->var_ud);
            if (varUD == nullptr)
                continue;

            // 获取飘动值
            float hair_sway = hair_sim.generate(tick, glm::vec2(640.0f, 0.0f), -30.f, 30.f);
            varLR->currVal = hair_sway;
            hair_sway = hair_sim.generate(tick, glm::vec2(640.0f, 0.0f), -30.f, 30.f);
            varLRM->currVal = hair_sway;
            hair_sway = hair_sim.generate(tick, glm::vec2(640.0f, 0.0f), -30.f, 30.f);
            varUD->currVal = hair_sway;
        }
        for (auto itm : _metadata->_partsControl)
        {
            // 寻找各变量
            auto varLR = findVarByName(itm->var_lr);
            if (varLR == nullptr)
                continue;
            auto varLRM = findVarByName(itm->var_lrm);
            if (varLRM == nullptr)
                continue;
            auto varUD = findVarByName(itm->var_ud);
            if (varUD == nullptr)
                continue;

            // 获取飘动值
            float hair_sway = hair_sim.generate(tick, glm::vec2(640.0f, 0.0f), -30.f, 30.f);
            varLR->currVal = hair_sway;
            hair_sway = hair_sim.generate(tick, glm::vec2(640.0f, 0.0f), -30.f, 30.f);
            varLRM->currVal = hair_sway;
            hair_sway = hair_sim.generate(tick, glm::vec2(640.0f, 0.0f), -30.f, 30.f);
            varUD->currVal = hair_sway;
        }
    }
    void emotefile::addEmoteFile(emotefile* itm)
    {
        _attach.push_back(itm);
    }
    } // namespace emoteplayer
