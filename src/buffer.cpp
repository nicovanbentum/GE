#include "pch.h"
#include "buffer.h"
#include "renderer.h"
#include "util.h"

#ifdef _WIN32
    #include "DXBuffer.h"
    #include "DXResourceBuffer.h"
#endif

namespace Raekor {
    ResourceBuffer* ResourceBuffer::construct(size_t size) {
        auto active_api = Renderer::get_activeAPI();
        switch (active_api) {
        case RenderAPI::OPENGL: {
            return new GLResourceBuffer(size);
        } break;
#ifdef _WIN32
        case RenderAPI::DIRECTX11: {
            return new DXResourceBuffer(size);
        } break;
#endif
        }
        return nullptr;
    }

    GLResourceBuffer::GLResourceBuffer(size_t size) {
        glGenBuffers(1, &id);
        glBindBuffer(GL_UNIFORM_BUFFER, id);
        glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void GLResourceBuffer::update(void* data, const size_t size) const {
        glBindBuffer(GL_UNIFORM_BUFFER, id);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, size, data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void GLResourceBuffer::bind(uint8_t slot) const {
        glBindBufferBase(GL_UNIFORM_BUFFER, slot, id);
    }

uint32_t size_of(ShaderType type) {
    switch (type) {
    case ShaderType::FLOAT1: return sizeof(float);
    case ShaderType::FLOAT2: return sizeof(float) * 2;
    case ShaderType::FLOAT3: return sizeof(float) * 3;
    case ShaderType::FLOAT4: return sizeof(float) * 4;
    default: return 0;
    }
}

GLShaderType::GLShaderType(ShaderType shader_type) {
    switch (shader_type) {
        case ShaderType::FLOAT1: {
            type = GL_FLOAT;
            count = 1;
        } break;
        case ShaderType::FLOAT2: {
            type = GL_FLOAT;
            count = 2;
        } break;
        case ShaderType::FLOAT3: {
            type = GL_FLOAT;
            count = 3;
        } break;
        case ShaderType::FLOAT4: {
            type = GL_FLOAT;
            count = 4;
        } break;
    }
}

InputLayout::InputLayout(const std::initializer_list<Element> element_list) : layout(element_list), stride(0) {
    uint32_t offset = 0;
    for (auto& element : layout) {
        element.offset = offset;
        offset += element.size;
        stride += element.size;
    }
}

VertexBuffer* VertexBuffer::construct(const std::vector<Vertex>& vertices) {
    auto active = Renderer::get_activeAPI();
    switch(active) {
        case RenderAPI::OPENGL: {
            return new GLVertexBuffer(vertices);
        } break;
#ifdef _WIN32
        case RenderAPI::DIRECTX11: {
            return new DXVertexBuffer(vertices);
        } break;
#endif
    }
    return nullptr;
}

IndexBuffer* IndexBuffer::construct(const std::vector<Index>& indices) {
    auto active = Renderer::get_activeAPI();
    switch (active) {
        case RenderAPI::OPENGL: {
            return new GLIndexBuffer(indices);
        } break;
#ifdef _WIN32
        case RenderAPI::DIRECTX11: {
            return new DXIndexBuffer(indices);
        } break;
#endif
    }
    return nullptr;
}

GLVertexBuffer::GLVertexBuffer(const std::vector<Vertex>& vertices) {
    id = gen_gl_buffer(vertices, GL_ARRAY_BUFFER);
}

void GLVertexBuffer::bind() const {
    glBindBuffer(GL_ARRAY_BUFFER, id);
    GLuint index = 0;
    for (auto& element : inputLayout) {
        auto GLType = static_cast<GLShaderType>(element.type);
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(
            index, // hlsl layout index
            GLType.count, // number of types, e.g 3 floats
            GLType.type, // type, e.g float
            GL_FALSE, // normalized?
            (GLsizei)inputLayout.get_stride(), // stride of the entire layout
            (const void*)((intptr_t)element.offset) // starting offset, casted up
        );
        index++;
    }
}

void GLVertexBuffer::set_layout(const InputLayout& layout) const {
    inputLayout = layout;
}

GLIndexBuffer::GLIndexBuffer(const std::vector<Index>& indices) {
    count = (unsigned int)(indices.size() * 3);
    id = gen_gl_buffer(indices, GL_ELEMENT_ARRAY_BUFFER);
}


void GLIndexBuffer::bind() const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
}

} // namespace Raekor