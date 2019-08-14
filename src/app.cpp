#include "pch.h"
#include "app.h"
#include "util.h"
#include "model.h"
#include "entry.h"
#include "camera.h"
#include "shader.h"
#include "framebuffer.h"
#include "PlatformContext.h"
#include "renderer.h"
#include "DXRenderer.h"
#include "DXShader.h"
#include "buffer.h"
#include "DXBuffer.h"
#include "DXFrameBuffer.h"
#include "DXTexture.h"
#include "DXResourceBuffer.h"

namespace Raekor {

void Application::serialize_settings(const std::string& filepath, bool write) {
    if (write) {
        std::ofstream os(filepath);
        cereal::JSONOutputArchive archive(os);
        serialize(archive);
    } else {
        std::ifstream is(filepath);
        cereal::JSONInputArchive archive(is);
        serialize(archive);
    }
}

void Application::run() {
    auto context = Raekor::PlatformContext();

    // retrieve the application settings from the config file
    serialize_settings("config.json");

    m_assert(SDL_Init(SDL_INIT_VIDEO) == 0, "failed to init sdl");

    Uint32 wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL |
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED;

    std::vector<SDL_Rect> displays;
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        displays.push_back(SDL_Rect());
        SDL_GetDisplayBounds(i, &displays.back());
    }

    // if our display setting is higher than the nr of displays we pick the default display
    auto index = display > displays.size() - 1 ? 0 : display;
    auto directxwindow = SDL_CreateWindow(name.c_str(),
        displays[index].x,
        displays[index].y,
        displays[index].w,
        displays[index].h,
        wflags);

    // create the renderer object that does sets up the API and does all the runtime magic
    std::unique_ptr<Renderer> dxr;
    dxr.reset(Renderer::construct(directxwindow));

    // create a Camera we can use to move around our scene
    Camera camera(glm::vec3(0, 0, 5), 45.0f);

    std::unique_ptr<Model> model;
    std::unique_ptr<Shader> dx_shader;
    std::unique_ptr<FrameBuffer> dxfb;
    std::unique_ptr<ResourceBuffer<cb_vs>> dxrb;
    std::unique_ptr<Texture> sky_image;
    std::unique_ptr<Mesh> skycube;
    std::unique_ptr<Shader> sky_shader;

    model.reset(new Model("resources/models/testcube.obj", "resources/textures/test.png"));
    dx_shader.reset(Shader::construct("shaders/simple_vertex", "shaders/simple_fp"));
    dxfb.reset(FrameBuffer::construct({ 1280, 720 }));
    dxrb.reset(ResourceBuffer<cb_vs>::construct());
    sky_image.reset(Texture::construct(skyboxes["lake"]));
    skycube.reset(new Mesh("resources/models/testcube.obj"));
    sky_shader.reset(Shader::construct("shaders/skybox_vertex", "shaders/skybox_fp"));

    // persistent imgui variable values
    std::string mesh_file = "";
    std::string texture_file = "";
    auto active_skybox = skyboxes.begin();
    auto fsize = dxfb->get_size();

    SDL_SetWindowInputFocus(directxwindow);

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImFont* pFont = io.Fonts->AddFontFromFileTTF(font.c_str(), 18.0f);
    if (!io.Fonts->Fonts.empty()) {
        io.FontDefault = io.Fonts->Fonts.back();
    }

    bool running = true;
    bool transpose = Renderer::get_activeAPI() == RenderAPI::DIRECTX11 ? true : false;

    //main application loop
    while(running) {
        //handle sdl and imgui events
        handle_sdl_gui_events({ directxwindow }, camera);

        dxr->Clear({ 0.22f, 0.32f, 0.42f, 1.0f });
        dxfb->bind();
        dxr->Clear({ 0.0f, 0.32f, 0.42f, 1.0f });

        // bind the shader
        sky_shader->bind();
        // update the camera without translation/model matrix
        camera.update();
        // upload the camera's mvp matrix
        dxrb->get_data().MVP = camera.get_mvp(transpose);
        // bind the resource buffer containing the mvp
        dxrb->bind(0);
        // bind the skybox cubemap
        sky_image->bind();
        // bind the skycube mesh
        skycube->bind();
        // draw the indexed vertices
        dxr->DrawIndexed(skycube->get_index_buffer()->get_count(), false);
        
        // bind the model's shader
        dx_shader->bind();
        // add rotation to the cube so somethings animated
        auto cube_rotation = model->get_mesh()->rotation_ptr();
        *cube_rotation += 0.01f;
        // recalculate the transformation if anything has changed
        model->get_mesh()->recalc_transform();
        // update the camera with the model's transform
        camera.update(model->get_mesh()->get_transform());
        // update the MVP resource
        dxrb->get_data().MVP = camera.get_mvp(transpose);
        // bind the resource buffer to slot 0
        dxrb->bind(0);
        // bind the model (mesh + texture)
        model->bind();
        // draw the indexed vertices
        dxr->DrawIndexed(model->get_mesh()->get_index_buffer()->get_count());

        // unbind the framebuffer which switches to application's backbuffer
        dxfb->unbind();

        //get new frame for render API, sdl and imgui
        dxr->ImGui_NewFrame(directxwindow);

        // draw the top level bar that contains stuff like "File" and "Edit"
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Close", "CTRL+S")) {
                    //clean up imgui
                    ImGui_ImplOpenGL3_Shutdown();
                    ImGui_ImplSDL2_Shutdown();
                    ImGui::DestroyContext();

                    //clean up SDL
                    SDL_DestroyWindow(directxwindow);
                    SDL_Quit();
                    return;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // renderer viewport
        ImGui::SetNextWindowContentSize(ImVec2(fsize.x, fsize.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Renderer", NULL, ImGuiWindowFlags_AlwaysAutoResize);

        auto image_data = dxfb->ImGui_data();
        if (Renderer::get_activeAPI() == RenderAPI::DIRECTX11) {
            ImGui::Image((ID3D11ShaderResourceView*)image_data, ImVec2(fsize.x, fsize.y));
        }
        else if (Renderer::get_activeAPI() == RenderAPI::OPENGL) {
            ImGui::Image(image_data, ImVec2(fsize.x, fsize.y), { 0,1 }, { 1,0 });
        }
        ImGui::End();
        ImGui::PopStyleVar();

        dxfb->unbind();

        // scene panel
        ImGui::SetNextWindowSize(ImVec2(760, 260), ImGuiCond_Once);
        ImGui::Begin("Scene");
        if (ImGui::BeginCombo("Skybox", active_skybox->first.c_str())) {
            for (auto it = skyboxes.begin(); it != skyboxes.end(); it++) {
                bool selected = (it == active_skybox);
                if (ImGui::Selectable(it->first.c_str(), selected)) {
                    active_skybox = it;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::RadioButton("OpenGL", Renderer::get_activeAPI() == RenderAPI::OPENGL)) {
            // check if the active api is not already openGL
            if (Renderer::get_activeAPI() != RenderAPI::OPENGL) {
                Renderer::set_activeAPI(RenderAPI::OPENGL);
                running = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("DirectX 11", Renderer::get_activeAPI() == RenderAPI::DIRECTX11)) {
            // check if the active api is not already directx
            if (Renderer::get_activeAPI() != RenderAPI::DIRECTX11) {
                Renderer::set_activeAPI(RenderAPI::DIRECTX11);
                running = false;
            }
        }

        // toggle button for openGl vsync
        static bool is_vsync = true;
        if (ImGui::RadioButton("USE VSYNC", is_vsync)) {
            is_vsync = !is_vsync;
            SDL_GL_SetSwapInterval(is_vsync);
        }
        ImGui::End();

            //start drawing a new imgui window. TODO: make this into a reusable component
            ImGui::SetNextWindowSize(ImVec2(760, 260), ImGuiCond_Once);
            ImGui::Begin("Object Properties");

            if (ImGui::DragFloat3("Scale", model->get_mesh()->scale_ptr(), 0.01f, 0.0f, 10.0f)) {
            }
            if (ImGui::DragFloat3("Position", model->get_mesh()->pos_ptr(), 0.01f, -100.0f, 100.0f)) {
            }
            if (ImGui::DragFloat3("Rotation", model->get_mesh()->rotation_ptr(), 0.01f, (float)(-M_PI), (float)(M_PI))) {
            }

            // resets the model's transformation
            if (ImGui::Button("Reset")) {
                model->get_mesh()->reset_transform();
            }
            ImGui::End();
            dxr->ImGui_Render();
            dxr->SwapBuffers();
        }
    }

} // namespace Raekor