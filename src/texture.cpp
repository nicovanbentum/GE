#include "pch.h"
#include "util.h"
#include "texture.h"
#include "renderer.h"

#ifdef _WIN32
#include "DXTexture.h"
#endif

namespace Raekor {

Texture* Texture::construct(const std::string& path) {
    if (path.empty()) return nullptr;
    auto active_api = Renderer::getActiveAPI();
    switch (active_api) {
        case RenderAPI::OPENGL: {
            return nullptr;
        } break;
#ifdef _WIN32
        case RenderAPI::DIRECTX11: {
            return new DXTexture(path);
        } break;
#endif
    }
    return nullptr;
}

Texture* Texture::construct(const std::array <std::string, 6>& face_files) {
    auto active_api = Renderer::getActiveAPI();
    switch (active_api) {
    case RenderAPI::OPENGL: {
        return new GLTextureCube(face_files);
    } break;
#ifdef _WIN32
    case RenderAPI::DIRECTX11: {
        return new DXTextureCube(face_files);
    } break;
#endif
    }
    return nullptr;
}

Texture* Texture::construct(const Stb::Image& image) {
    auto active_api = Renderer::getActiveAPI();
    switch (active_api) {
    case RenderAPI::OPENGL: {
        return nullptr;
    } break;
#ifdef _WIN32
    case RenderAPI::DIRECTX11: {
        return new DXTexture(image);
    } break;
#endif
    }
    return nullptr;
}


GLTextureCube::GLTextureCube(const std::array<std::string, 6>& face_files) {
    stbi_set_flip_vertically_on_load(false);

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);

    // for every face file we generate an OpenGL texture image
    int width, height, n_channels;
    for(unsigned int i = 0; i < face_files.size(); i++) {
        auto image = stbi_load(face_files[i].c_str(), &width, &height, &n_channels, 0);
        m_assert(image, "failed to load image");
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
        stbi_image_free(image);
    }

    // set the text parameters
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

GLTextureCube::~GLTextureCube() {
    glDeleteTextures(1, &id);
}

void GLTextureCube::bind(uint32_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
}

} // Namespace Raekor