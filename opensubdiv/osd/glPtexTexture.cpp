//
//     Copyright (C) Pixar. All rights reserved.
//
//     This license governs use of the accompanying software. If you
//     use the software, you accept this license. If you do not accept
//     the license, do not use the software.
//
//     1. Definitions
//     The terms "reproduce," "reproduction," "derivative works," and
//     "distribution" have the same meaning here as under U.S.
//     copyright law.  A "contribution" is the original software, or
//     any additions or changes to the software.
//     A "contributor" is any person or entity that distributes its
//     contribution under this license.
//     "Licensed patents" are a contributor's patent claims that read
//     directly on its contribution.
//
//     2. Grant of Rights
//     (A) Copyright Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free copyright license to reproduce its contribution,
//     prepare derivative works of its contribution, and distribute
//     its contribution or any derivative works that you create.
//     (B) Patent Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free license under its licensed patents to make, have
//     made, use, sell, offer for sale, import, and/or otherwise
//     dispose of its contribution in the software or derivative works
//     of the contribution in the software.
//
//     3. Conditions and Limitations
//     (A) No Trademark License- This license does not grant you
//     rights to use any contributor's name, logo, or trademarks.
//     (B) If you bring a patent claim against any contributor over
//     patents that you claim are infringed by the software, your
//     patent license from such contributor to the software ends
//     automatically.
//     (C) If you distribute any portion of the software, you must
//     retain all copyright, patent, trademark, and attribution
//     notices that are present in the software.
//     (D) If you distribute any portion of the software in source
//     code form, you may do so only under this license by including a
//     complete copy of this license with your distribution. If you
//     distribute any portion of the software in compiled or object
//     code form, you may only do so under a license that complies
//     with this license.
//     (E) The software is licensed "as-is." You bear the risk of
//     using it. The contributors give no express warranties,
//     guarantees or conditions. You may have additional consumer
//     rights under your local laws which this license cannot change.
//     To the extent permitted under your local laws, the contributors
//     exclude the implied warranties of merchantability, fitness for
//     a particular purpose and non-infringement.
//

#if defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <GL/glew.h>
#endif

#include "../osd/glPtexTexture.h"
#include "../osd/ptexTextureLoader.h"
#include <Ptexture.h>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

OsdGLPtexTexture::OsdGLPtexTexture()
    : _width(0), _height(0), _depth(0), _pages(0), _layout(0), _texels(0) {
}

OsdGLPtexTexture::~OsdGLPtexTexture() {

    // delete pages lookup ---------------------------------
    if (glIsTexture(_pages))
       glDeleteTextures(1, &_pages);

    // delete layout lookup --------------------------------
    if (glIsTexture(_layout))
       glDeleteTextures(1, &_layout);

    // delete textures lookup ------------------------------
    if (glIsTexture(_texels))
       glDeleteTextures(1, &_texels);
}

static GLuint
genTextureBuffer(GLenum format, GLsizeiptr size, GLvoid const * data) {

    GLuint buffer, result;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_TEXTURE_BUFFER, buffer);
    glBufferData(GL_TEXTURE_BUFFER, size, data, GL_STATIC_DRAW);

    glGenTextures(1, & result);
    glBindTexture(GL_TEXTURE_BUFFER, result);
    glTexBuffer(GL_TEXTURE_BUFFER, format, buffer);

    glDeleteBuffers(1, &buffer);

    return result;
}

OsdGLPtexTexture *
OsdGLPtexTexture::Create(PtexTexture * reader,
                      unsigned long int targetMemory,
                      int gutterWidth,
                      int pageMargin) {

    OsdGLPtexTexture * result = NULL;

    // Read the ptexture data and pack the texels
    OsdPtexTextureLoader ldr(reader, gutterWidth, pageMargin);

    unsigned long int nativeSize = ldr.GetNativeUncompressedSize(),
           targetSize = targetMemory;

    if (targetSize != 0 && targetSize != nativeSize)
        ldr.OptimizeResolution(targetSize);

    GLint maxnumpages = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxnumpages);

    ldr.OptimizePacking(maxnumpages);

    if (!ldr.GenerateBuffers())
        return result;

    // Setup GPU memory
    unsigned long int nfaces = ldr.GetNumBlocks();

    GLuint pages = genTextureBuffer(GL_R32I,
                                    nfaces * sizeof(GLint),
                                    ldr.GetIndexBuffer());

    GLuint layout = genTextureBuffer(GL_RGBA32F,
                                     nfaces * 4 * sizeof(GLfloat),
                                     ldr.GetLayoutBuffer());

    GLenum format, type;
    switch (reader->dataType()) {
        case Ptex::dt_uint16 : type = GL_UNSIGNED_SHORT; break;
        case Ptex::dt_float  : type = GL_FLOAT; break;
        case Ptex::dt_half   : type = GL_HALF_FLOAT; break;
        default              : type = GL_UNSIGNED_BYTE; break;
    }

    switch (reader->numChannels()) {
        case 1 : format = GL_RED; break;
        case 2 : format = GL_RG; break;
        case 3 : format = GL_RGB; break;
        case 4 : format = GL_RGBA; break;
        default: format = GL_RED; break;
    }

    // actual texels texture array
    GLuint texels;
    glGenTextures(1, &texels);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texels);

    // XXXX for the time being, filtering is off - once cross-patch filtering
    // is in place, we will use glGenSamplers to dynamically access these settings.
    if (gutterWidth > 0) {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0,
                 (type == GL_FLOAT) ? GL_RGBA32F : GL_RGBA,
                 ldr.GetPageSize(),
                 ldr.GetPageSize(),
                 ldr.GetNumPages(),
                 0, format, type,
                 ldr.GetTexelBuffer());

    ldr.ClearBuffers();

    // Return the Osd Ptexture object
    result = new OsdGLPtexTexture;

    result->_width = ldr.GetPageSize();
    result->_height = ldr.GetPageSize();
    result->_depth = ldr.GetNumPages();

    result->_format = format;

    result->_pages = pages;
    result->_layout = layout;
    result->_texels = texels;

    return result;
}

}  // end namespace OPENSUBDIV_VERSION
}  // end namespace OpenSubdiv
