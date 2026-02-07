/*
 * Copyright (C) 2015, 2024 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CoordinatedPlatformLayerBufferYUV.h"

#if USE(COORDINATED_GRAPHICS)
#include "GLContext.h"
#include "PlatformDisplay.h"
#include "TextureMapper.h"
#include "TextureMapperShaderProgram.h"

#if USE(LIBEPOXY)
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#include <wtf/RuntimeApplicationChecks.h>

namespace WebCore {

std::unique_ptr<CoordinatedPlatformLayerBufferYUV> CoordinatedPlatformLayerBufferYUV::create(unsigned planeCount, std::array<unsigned, 4>&& planes, std::array<unsigned, 4>&& yuvPlane, std::array<unsigned, 4>&& yuvPlaneOffset, YuvToRgbColorSpace yuvToRgbColorSpace, TransferFunction transferFunction, const IntSize& size, OptionSet<TextureMapperFlags> flags, std::unique_ptr<GLFence>&& fence)
{
    return makeUnique<CoordinatedPlatformLayerBufferYUV>(planeCount, WTF::move(planes), WTF::move(yuvPlane), WTF::move(yuvPlaneOffset), yuvToRgbColorSpace, transferFunction, size, flags, WTF::move(fence));
}

std::unique_ptr<CoordinatedPlatformLayerBufferYUV> CoordinatedPlatformLayerBufferYUV::create(unsigned planeCount, Vector<RefPtr<BitmapTexture>, 4>&& textures, std::array<unsigned, 4>&& yuvPlane, std::array<unsigned, 4>&& yuvPlaneOffset, YuvToRgbColorSpace yuvToRgbColorSpace, TransferFunction transferFunction, const IntSize& size, OptionSet<TextureMapperFlags> flags, std::unique_ptr<GLFence>&& fence)
{
    return makeUnique<CoordinatedPlatformLayerBufferYUV>(planeCount, WTF::move(textures), WTF::move(yuvPlane), WTF::move(yuvPlaneOffset), yuvToRgbColorSpace, transferFunction, size, flags, WTF::move(fence));
}

CoordinatedPlatformLayerBufferYUV::CoordinatedPlatformLayerBufferYUV(unsigned planeCount, std::array<unsigned, 4>&& planes, std::array<unsigned, 4>&& yuvPlane, std::array<unsigned, 4>&& yuvPlaneOffset, YuvToRgbColorSpace yuvToRgbColorSpace, TransferFunction transferFunction, const IntSize& size, OptionSet<TextureMapperFlags> flags, std::unique_ptr<GLFence>&& fence)
    : CoordinatedPlatformLayerBuffer(Type::YUV, size, flags, WTF::move(fence))
    , m_planeCount(planeCount)
    , m_planes(WTF::move(planes))
    , m_yuvPlane(WTF::move(yuvPlane))
    , m_yuvPlaneOffset(WTF::move(yuvPlaneOffset))
    , m_yuvToRgbColorSpace(yuvToRgbColorSpace)
    , m_transferFunction(transferFunction)
{
}

CoordinatedPlatformLayerBufferYUV::CoordinatedPlatformLayerBufferYUV(unsigned planeCount, Vector<RefPtr<BitmapTexture>, 4>&& textures, std::array<unsigned, 4>&& yuvPlane, std::array<unsigned, 4>&& yuvPlaneOffset, YuvToRgbColorSpace yuvToRgbColorSpace, TransferFunction transferFunction, const IntSize& size, OptionSet<TextureMapperFlags> flags, std::unique_ptr<GLFence>&& fence)
    : CoordinatedPlatformLayerBuffer(Type::YUV, size, flags, WTF::move(fence))
    , m_planeCount(planeCount)
    , m_textures(WTF::move(textures))
    , m_yuvPlane(WTF::move(yuvPlane))
    , m_yuvPlaneOffset(WTF::move(yuvPlaneOffset))
    , m_yuvToRgbColorSpace(yuvToRgbColorSpace)
    , m_transferFunction(transferFunction)
{
    for (unsigned i = 0; i < m_textures.size(); ++i)
        m_planes[i] = m_textures[i] ? m_textures[i]->id() : 0;
}

CoordinatedPlatformLayerBufferYUV::~CoordinatedPlatformLayerBufferYUV() = default;

std::array<GLfloat, 16> CoordinatedPlatformLayerBufferYUV::getYuvToRgbMatrix()
{
    // clang-format off
    static constexpr std::array<GLfloat, 16> s_bt601ConversionMatrix {
        1.164383561643836,  0.0,                1.596026785714286, -0.874202217873451,
        1.164383561643836, -0.391762290094914, -0.812967647237771,  0.531667823499146,
        1.164383561643836,  2.017232142857143,  0.0,               -1.085630789302022,
        0.0,                0.0,                0.0,                1.0,
    };
    static constexpr std::array<GLfloat, 16> s_bt709ConversionMatrix {
        1.164383561643836,  0.0,                1.792741071428571, -0.972945075016308,
        1.164383561643836, -0.213248614273730, -0.532909328559444,  0.301482665475862,
        1.164383561643836,  2.112401785714286,  0.0,               -1.133402217873451,
        0.0,                0.0,                0.0,                1.0,
    };
    static constexpr std::array<GLfloat, 16> s_bt2020ConversionMatrix {
        1.164383561643836,  0.0,                1.678674107142857, -0.915687932159165,
        1.164383561643836, -0.187326104219343, -0.650424318505057,  0.347458498519301,
        1.164383561643836,  2.141772321428571,  0.0,               -1.148145075016308,
        0.0,                0.0,                0.0,                1.0,
    };
    static constexpr std::array<GLfloat, 16> s_smpte240MConversionMatrix {
        1.164383561643836,  0.0,                1.793651785714286, -0.973402217873451,
        1.164383561643836, -0.256532845251675, -0.542724809537390,  0.328136638536074,
        1.164383561643836,  2.07984375,         0.0,               -1.117059360730593,
        0.0,                0.0,                0.0,                1.0,
    };
    // clang-format on
    switch (m_yuvToRgbColorSpace) {
    case YuvToRgbColorSpace::Bt601:
        return s_bt601ConversionMatrix;
    case YuvToRgbColorSpace::Bt709:
        return s_bt709ConversionMatrix;
    case YuvToRgbColorSpace::Bt2020:
        return s_bt2020ConversionMatrix;
    case YuvToRgbColorSpace::Smpte240M:
        return s_smpte240MConversionMatrix;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

void CoordinatedPlatformLayerBufferYUV::paintToTextureMapper(TextureMapper& textureMapper, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity)
{
    waitForContentsIfNeeded();
    const auto& yuvToRgbMatrix = getYuvToRgbMatrix();

    TextureMapper::TransferFunction textureMapperTransferFunction;
    switch (m_transferFunction) {
    case TransferFunction::Bt709:
        textureMapperTransferFunction = TextureMapper::TransferFunction::Bt709;
        break;
    case TransferFunction::Pq:
        textureMapperTransferFunction = TextureMapper::TransferFunction::Pq;
        break;
    }

    switch (m_planeCount) {
    case 1:
        ASSERT(m_yuvPlane[0] == m_yuvPlane[1] && m_yuvPlane[1] == m_yuvPlane[2]);
        ASSERT(m_yuvPlaneOffset[0] == 2 && m_yuvPlaneOffset[1] == 1 && !m_yuvPlaneOffset[2]);
        textureMapper.drawTexturePackedYUV(m_planes[m_yuvPlane[0]], yuvToRgbMatrix, m_flags, targetRect, modelViewMatrix, opacity, textureMapperTransferFunction);
        break;
    case 2:
        ASSERT(!m_yuvPlaneOffset[0]);
        textureMapper.drawTextureSemiPlanarYUV(std::array<GLuint, 2> { m_planes[m_yuvPlane[0]], m_planes[m_yuvPlane[1]] }, !!m_yuvPlaneOffset[1],
            yuvToRgbMatrix, m_flags, targetRect, modelViewMatrix, opacity, textureMapperTransferFunction);
        break;
    case 3:
        ASSERT(!m_yuvPlaneOffset[0] && !m_yuvPlaneOffset[1] && !m_yuvPlaneOffset[2]);
        textureMapper.drawTexturePlanarYUV(std::array<GLuint, 3> { m_planes[m_yuvPlane[0]], m_planes[m_yuvPlane[1]], m_planes[m_yuvPlane[2]] },
            yuvToRgbMatrix, m_flags, targetRect, modelViewMatrix, opacity, std::nullopt, textureMapperTransferFunction);
        break;
    case 4:
        ASSERT(!m_yuvPlaneOffset[0] && !m_yuvPlaneOffset[1] && !m_yuvPlaneOffset[2]);
        textureMapper.drawTexturePlanarYUV(std::array<GLuint, 3> { m_planes[m_yuvPlane[0]], m_planes[m_yuvPlane[1]], m_planes[m_yuvPlane[2]] },
            yuvToRgbMatrix, m_flags, targetRect, modelViewMatrix, opacity, m_planes[m_yuvPlane[3]], textureMapperTransferFunction);
        break;
    }
}

static inline TransformationMatrix createProjectionMatrix(const IntSize& size, bool flipY, double zNear, double zFar)
{
    const double nearValue = std::min(zNear + 1, 9999999.0);
    const double farValue = std::max(zFar - 1, -99999.0);
    return TransformationMatrix(2.0 / size.width(), 0, 0, 0,
        0, (flipY ? 2.0 : -2.0) / size.height(), 0, 0,
        0, 0, 2.0 / (farValue - nearValue), 0,
        -1, flipY ? -1 : 1, -(farValue + nearValue) / (farValue - nearValue), 1);
}

#define LOG_GL_ERR()                                                                           \
    do {                                                                                       \
        auto err = glGetError();                                                               \
        bool ok = (err == GL_NO_ERROR);                                                        \
        WTFLogAlways("%s line %d OK: %d error: 0x%X", __PRETTY_FUNCTION__, __LINE__, ok, err); \
    } while(0)

bool CoordinatedPlatformLayerBufferYUV::copyToTexture(PlatformGLObject outputTexture, GCGLenum outputTarget, GCGLint level, GCGLenum internalFormat, GCGLenum format, GCGLenum type)
{
    // RELEASE_ASSERT(WTF::isInGPUProcess());
    // return false;
    waitForContentsIfNeeded();
    const auto& yuvToRgbMatrix = getYuvToRgbMatrix();

    // Save previous context and activate the sharing one.
    GLContext* previousContext = GLContext::current();

    LOG_GL_ERR();
    if (!PlatformDisplay::sharedDisplay().sharingGLContext()->makeContextCurrent()) {
        // Restore previous context.
        if (previousContext)
            previousContext->makeContextCurrent();
        return false;
    }
    LOG_GL_ERR();

    m_textureSpaceMatrix.makeIdentity();
    m_textureSpaceMatrix.flipY();
    m_textureSpaceMatrix.translate(0, -1);
    m_colorConversionMatrix.makeIdentity();
    m_colorConversionMatrix.setMatrix(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);

    TextureMapperShaderProgram::Options options;
    switch (m_planeCount) {
    case 1:
        ASSERT(m_yuvPlane[0] == m_yuvPlane[1] && m_yuvPlane[1] == m_yuvPlane[2]);
        ASSERT(m_yuvPlaneOffset[0] == 2 && m_yuvPlaneOffset[1] == 1 && !m_yuvPlaneOffset[2]);
        options = TextureMapperShaderProgram::TexturePackedYUV;
        break;
    case 2:
        ASSERT(!m_yuvPlaneOffset[0]);
        options = m_yuvPlaneOffset[1] ? TextureMapperShaderProgram::TextureNV21 : TextureMapperShaderProgram::TextureNV12;
        break;
    case 3:
        ASSERT(!m_yuvPlaneOffset[0] && !m_yuvPlaneOffset[1] && !m_yuvPlaneOffset[2]);
        options = TextureMapperShaderProgram::TextureYUV;
        break;
    case 4:
        ASSERT(!m_yuvPlaneOffset[0] && !m_yuvPlaneOffset[1] && !m_yuvPlaneOffset[2]);
        options = TextureMapperShaderProgram::TextureYUVA;
        break;
    }

    // if (m_flags.contains(WebCore::TextureMapperFlags::ShouldPremultiply))
    //     options.add(TextureMapperShaderProgram::Option::Premultiply);
    LOG_GL_ERR();

    RefPtr shaderProgram = TextureMapperShaderProgram::create(options);

    // Save previous bound framebuffer, texture and viewport.
    GLint boundFramebuffer = 0;
    GLint boundTexture = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    LOG_GL_ERR();

    // Set the viewport.
    glViewport(0, 0, m_size.width(), m_size.height());
    LOG_GL_ERR();

    static const GLfloat vertices[] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 8, vertices, GL_STATIC_DRAW);
    LOG_GL_ERR();

    glGenFramebuffers(1, &m_framebuffer);
    LOG_GL_ERR();

    // Set proper parameters to the output texture and allocate uninitialized memory for it.
    glBindTexture(outputTarget, outputTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(outputTarget, level, internalFormat, m_size.width(), m_size.height(), 0, format, type, nullptr);
    LOG_GL_ERR();

    glBindTexture(outputTarget, 0);
    LOG_GL_ERR();

    // Bind framebuffer to paint and attach the destination texture to it.
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, level);
    glBindTexture(outputTarget, 0);
    LOG_GL_ERR();

    // Set program parameters.
    glUseProgram(shaderProgram->programID());

    switch (m_planeCount) {
    case 1:
        glUniform1i(shaderProgram->samplerLocation(), m_yuvPlane[0]);
        break;
    case 2:
        glUniform1i(shaderProgram->samplerYLocation(), m_yuvPlane[0]);
        glUniform1i(shaderProgram->samplerULocation(), m_yuvPlane[1]);
        break;
    case 3:
        glUniform1i(shaderProgram->samplerYLocation(), m_yuvPlane[0]);
        glUniform1i(shaderProgram->samplerULocation(), m_yuvPlane[1]);
        glUniform1i(shaderProgram->samplerVLocation(), m_yuvPlane[2]);
        break;
    case 4:
        glUniform1i(shaderProgram->samplerYLocation(), m_yuvPlane[0]);
        glUniform1i(shaderProgram->samplerULocation(), m_yuvPlane[1]);
        glUniform1i(shaderProgram->samplerVLocation(), m_yuvPlane[2]);
        glUniform1i(shaderProgram->samplerALocation(), m_yuvPlane[3]);
        break;
    }

    glUniformMatrix4fv(shaderProgram->yuvToRgbLocation(), 1, GL_FALSE, static_cast<const GLfloat*>(&yuvToRgbMatrix[0]));
    LOG_GL_ERR();

    for (int i = m_planeCount - 1; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_planes[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    LOG_GL_ERR();

    FloatRect targetRect = FloatRect(FloatPoint(), m_size);
    TransformationMatrix identityMatrix;
    m_modelViewMatrix = TransformationMatrix(identityMatrix).multiply(TransformationMatrix::rectToRect(FloatRect(0, 0, 1, 1), targetRect));

    bool flipY = m_flags.contains(WebCore::TextureMapperFlags::ShouldFlipTexture);
    // flipY = true;

    m_projectionMatrix = createProjectionMatrix(m_size, flipY, 0, 0);
    shaderProgram->setMatrix(shaderProgram->modelViewMatrixLocation(), m_modelViewMatrix);
    shaderProgram->setMatrix(shaderProgram->projectionMatrixLocation(), m_projectionMatrix);
    shaderProgram->setMatrix(shaderProgram->textureSpaceMatrixLocation(), m_textureSpaceMatrix);
    shaderProgram->setMatrix(shaderProgram->textureColorSpaceMatrixLocation(), m_colorConversionMatrix);
    // glUniform1f(shaderProgram->opacityLocation(), 1);

    glEnableVertexAttribArray(shaderProgram->vertexLocation());
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(shaderProgram->vertexLocation(), 2, GL_FLOAT, false, 0, 0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    // glFlush();
    LOG_GL_ERR();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(shaderProgram->vertexLocation());
    glUseProgram(0);
    // glFlush();
    LOG_GL_ERR();

    // Restore previous bindings and viewport.
    if (boundFramebuffer)
        glBindFramebuffer(GL_FRAMEBUFFER, boundFramebuffer);
    if (boundTexture)
        glBindTexture(outputTarget, boundTexture);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    // glFlush();
    LOG_GL_ERR();

    // auto err = glGetError();
    // bool ok = (err == GL_NO_ERROR);

    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteBuffers(1, &m_vbo);
    LOG_GL_ERR();

    // Restore previous context.
    if (previousContext)
        previousContext->makeContextCurrent();

    LOG_GL_ERR();
    //WTFLogAlways("%s line %d OK: %d error: 0x%X", __PRETTY_FUNCTION__, __LINE__, ok, err);
    return true;
}

} // namespace WebCore

#endif // USE(COORDINATED_GRAPHICS)
