/*
 Copyright (C) 2016 Igalia S.L.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License as published by the Free Software Foundation; either
 version 2 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.

 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.
 */

#pragma once

#if USE(GSTREAMER_GL)

#include "CoordinatedPlatformLayerBufferVideo.h"
#include "ImageOrientation.h"
#include "TextureMapperGLHeaders.h"
#include "TextureMapperShaderProgram.h"
#include "TransformationMatrix.h"
#include <wtf/RefPtr.h>

namespace WebCore {

struct ImageOrientation;

class VideoTextureCopierGStreamer {
    WTF_MAKE_FAST_ALLOCATED;
public:
    enum class ColorConversion {
        ConvertBGRAToRGBA,
        ConvertARGBToRGBA,
        NoConvert,
    };

    VideoTextureCopierGStreamer(ColorConversion);
    ~VideoTextureCopierGStreamer();

    bool copyVideoTextureToPlatformTexture(CoordinatedPlatformLayerBufferVideo& inputTexture, IntSize& frameSize, GLuint outputTexture, GLenum outputTarget, GLint level, GLenum internalFormat, GLenum format, GLenum type, bool flipY, ImageOrientation sourceOrientation, bool premultiplyAlpha);
    void updateColorConversionMatrix(ColorConversion);
    void updateTextureSpaceMatrix();
    void updateTransformationMatrix();
    GLuint resultTexture() { return m_resultTexture; }
    void setPendingDmabufTarget(int fd, uint32_t format, uint32_t stride);

private:
    RefPtr<TextureMapperShaderProgram> m_shaderProgram;
    TextureMapperShaderProgram::Options m_shaderOptions;
    GLuint m_framebuffer { 0 };
    GLuint m_vbo { 0 };
#if !USE(OPENGL_ES)
    GLuint m_vao { 0 };
#endif
    bool m_flipY { false };
    ImageOrientation m_orientation;
    IntSize m_size;
    TransformationMatrix m_modelViewMatrix;
    TransformationMatrix m_projectionMatrix;
    TransformationMatrix m_textureSpaceMatrix;
    TransformationMatrix m_colorConversionMatrix;
    GLuint m_resultTexture { 0 };
    struct {
        int fd { -1 };
        uint32_t format;
        uint32_t stride;
    } m_pendingDmabuf;
};

} // namespace WebCore

#endif // USE(GSTREAMER_GL)
