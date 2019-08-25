#pragma once

#include "pch.h"

namespace Raekor {

    class FrameBuffer {

    public:
        virtual ~FrameBuffer() {}
        static FrameBuffer* construct(const glm::vec2& new_size);
        virtual void bind() const = 0;
        virtual void unbind() const = 0;
        virtual void ImGui_Image() const = 0;
        virtual void resize(const glm::vec2& size) = 0;
        inline glm::vec2 get_size() const { return size; }

    protected:
        glm::vec2 size;
        unsigned int fbo_id;
        unsigned int rbo_id;
        unsigned int render_texture_id;
    };

    class GLFrameBuffer : public FrameBuffer {

    public:
        GLFrameBuffer(const glm::vec2& size);
        ~GLFrameBuffer();
        virtual void bind() const override;
        virtual void unbind() const override;
        virtual void ImGui_Image() const override;
        virtual void resize(const glm::vec2& size) override;
    };

} // Raekor
