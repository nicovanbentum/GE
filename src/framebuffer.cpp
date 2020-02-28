#include "pch.h"
#include "util.h"
#include "framebuffer.h"
#include "renderer.h"

#ifdef _WIN32
#include "DXFrameBuffer.h"
#endif

namespace Raekor {

FrameBuffer* FrameBuffer::construct(FrameBuffer::ConstructInfo* info) {
    auto active_api = Renderer::get_activeAPI();
    switch (active_api) {
        case RenderAPI::OPENGL: {
            return nullptr;
        } break;
#ifdef _WIN32
        case RenderAPI::DIRECTX11: {
            return new DXFrameBuffer(info);
        } break;
#endif
    }
    return nullptr;
}

glRenderbuffer::glRenderbuffer() {
    glGenRenderbuffers(1, &mID);
}

glRenderbuffer::~glRenderbuffer() {
    glDeleteRenderbuffers(1, &mID);
}

void glRenderbuffer::init(uint32_t width, uint32_t height, GLenum format) {
    glBindRenderbuffer(GL_RENDERBUFFER, mID);
    glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

glFramebuffer::glFramebuffer() {
    glGenFramebuffers(1, &mID);
}

glFramebuffer::~glFramebuffer() {
    glDeleteFramebuffers(1, &mID);
}

void glFramebuffer::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, mID);
}

void glFramebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void glFramebuffer::attach(glTexture& texture, GLenum type) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, type, texture.mTarget, texture.mID, 0);

    // if its a color attachment and we haven't seen it before, store it
    for (int slot = GL_COLOR_ATTACHMENT0; slot < GL_COLOR_ATTACHMENT0 + GL_MAX_COLOR_ATTACHMENTS; slot++) {
        if (type == slot) {
            if (auto it = std::find(colorAttachments.begin(), colorAttachments.end(), type); it == colorAttachments.end()) {
                colorAttachments.push_back(type);
                break;
            }
        }
    }

    // tell it to use all stored color attachments so far for rendering
    if (!colorAttachments.empty()) {
        glNamedFramebufferDrawBuffers(mID, static_cast<GLsizei>(colorAttachments.size()), colorAttachments.data());
        // else we assume its a depth only attachment for now
    }
    else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
}

void glFramebuffer::attach(glRenderbuffer& buffer, GLenum attachment) {
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, buffer.mID);
}

} // Raekor
