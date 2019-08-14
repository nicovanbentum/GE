#pragma once

#include "pch.h"
#include "renderer.h"

namespace Raekor {
// TODO: This should probably be moved to the DXRenderer class as static members
struct COM_PTRS {
    com_ptr<IDXGISwapChain> swap_chain;
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11RenderTargetView> back_buffer;
    com_ptr<ID3D11RasterizerState> rasterize_state;
    com_ptr<ID3D11DepthStencilView> depth_stencil_view;
    com_ptr<ID3D11RenderTargetView> render_target_view;

    void Release() {
        swap_chain->Release();
        device->Release();
        context->Release();
        back_buffer->Release();
        rasterize_state->Release();
        depth_stencil_view->Release();
        render_target_view->Release();
    }
};

extern COM_PTRS D3D;

class DXRenderer : public Renderer {
public:
    DXRenderer(SDL_Window* window);
    ~DXRenderer();
    virtual void ImGui_Render()                                     override;
    virtual void ImGui_NewFrame(SDL_Window* window)                 override;
    virtual void Clear(glm::vec4 color)                             override;
    virtual void DrawIndexed(unsigned int size, bool depth_test)    override;
    virtual void SwapBuffers() const                                override;

private:
    com_ptr<ID3D11Texture2D> depth_stencil_buffer;
    com_ptr<ID3D11DepthStencilState> depth_stencil_state;
};

} // namespace Raekor