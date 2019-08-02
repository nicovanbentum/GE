#pragma once

#include "buffer.h"
#include "util.h"

namespace Raekor {

class DXVertexBuffer : public VertexBuffer {
public:
    DXVertexBuffer(const std::vector<glm::vec3>& vertices);
    virtual void bind() const override;
    virtual void unbind() const override;

private:
    com_ptr<ID3D11Buffer> vertex_buffer;
};


class DXIndexBuffer : public IndexBuffer {
public:
    DXIndexBuffer(const std::vector<unsigned int>& indices);
    virtual void bind() const override;
    virtual void unbind() const override;

private:
    com_ptr<ID3D11Buffer> index_buffer;
};

} // namespace Raekor