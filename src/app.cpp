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
#include "buffer.h"
#include "timer.h"

namespace Raekor {

struct Uniforms {
    glm::vec4 sunColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    float minBias = 0.005f, maxBias = 0.05f;
    float farPlane = 25.0f;
};

struct shadow3DUBO {
    glm::mat4 model;
    std::array<glm::mat4, 6> faceMatrices;
    glm::vec4 lightPos;
    float farPlane;
    float x, y, z;
};

struct HDR_UBO {
    float exposure = 1.0f;
    float gamma = 1.8f;
};

void Application::serialize_settings(const std::string& filepath, bool write) {
    if (write) {
        std::ofstream os(filepath);
        cereal::JSONOutputArchive archive(os);
        serialize(archive);
    }
    else {
        std::ifstream is(filepath);
        cereal::JSONInputArchive archive(is);
        serialize(archive);
    }
}

void Application::run() {
    auto context = Raekor::PlatformContext();

    // retrieve the application settings from the config file
    serialize_settings("config.json");

    int sdl_err = SDL_Init(SDL_INIT_VIDEO);
    m_assert(sdl_err == 0, "failed to init SDL for video");

    Uint32 wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL |
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED;

    std::vector<SDL_Rect> displays;
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        displays.push_back(SDL_Rect());
        SDL_GetDisplayBounds(i, &displays.back());
    }

    // if our display setting is higher than the nr of displays we pick the default display
    display = display > displays.size() - 1 ? 0 : display;
    auto directxwindow = SDL_CreateWindow(name.c_str(),
        displays[display].x,
        displays[display].y,
        displays[display].w,
        displays[display].h,
        wflags);

    // initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // create the renderer object that does sets up the API and does all the runtime magic
    Renderer::set_activeAPI(RenderAPI::OPENGL);
    Render::Init(directxwindow);

    // create a Camera we can use to move around our scene
    Camera camera(glm::vec3(0, 1.0, 0), glm::perspectiveRH_ZO(glm::radians(65.0f), 16.0f / 9.0f, 0.1f, 10000.0f));

    shadow3DUBO ubo3d = {};
    HDR_UBO hdr_ubo;
    Uniforms uniforms;

    std::unique_ptr<Model> model;

    std::unique_ptr<GLShader> dx_shader;
    std::unique_ptr<GLShader> sky_shader;
    std::unique_ptr<GLShader> depth_shader;
    std::unique_ptr<GLShader> depthCube_shader;
    std::unique_ptr<GLShader> quad_shader;
    std::unique_ptr<GLShader> sphere_shader;
    std::unique_ptr<GLShader> hdr_shader;

    std::unique_ptr<GLFrameBuffer> hdrBuffer;
    std::unique_ptr<GLFrameBuffer> finalBuffer;
    std::unique_ptr<GLFrameBuffer> quadFB;

    std::unique_ptr<GLResourceBuffer> Shadow3DUbo;

    std::unique_ptr<GLTextureCube> sky_image;

    std::unique_ptr<Mesh> skycube;

    Stb::Image testImage = Stb::Image(RGBA);
    testImage.load("resources/textures/test.png", true);
    std::unique_ptr<Texture> testTexture;
    testTexture.reset(Texture::construct(testImage));


    Ffilter ft_mesh;
    ft_mesh.name = "Supported Mesh Files";
    ft_mesh.extensions = "*.obj;*.fbx;*.gltf";

    Ffilter ft_texture;
    ft_texture.name = "Supported Image Files";
    ft_texture.extensions = "*.png;*.jpg;*.jpeg;*.tga";

    Shader::Stage vertex(Shader::Type::VERTEX, "shaders\\OpenGL\\main.vert");
    Shader::Stage frag(Shader::Type::FRAG, "shaders\\OpenGL\\main.frag");
    std::array<Shader::Stage, 2> modelStages = { vertex, frag };
    dx_shader.reset(new GLShader(modelStages.data(), modelStages.size()));

    std::vector<Shader::Stage> skybox_shaders;
    skybox_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\skybox.vert");
    skybox_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\skybox.frag");

    std::vector<Shader::Stage> depth_shaders;
    depth_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\depth.vert");
    depth_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\depth.frag");

    std::vector<Shader::Stage> depthCube_shaders;
    depthCube_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\depthCube.vert");
    depthCube_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\depthCube.frag");
    depthCube_shaders.emplace_back(Shader::Type::GEO, "shaders\\OpenGL\\depthCube.geo");

    std::vector<Shader::Stage> quad_shaders;
    quad_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\quad.vert");
    quad_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\quad.frag");

    std::vector<Shader::Stage> sphere_shaders;
    sphere_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\lightSphere.vert");
    sphere_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\lightSphere.frag");

    std::vector<Shader::Stage> hdr_shaders;
    hdr_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\HDR.vert");
    hdr_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\HDR.frag");


    sky_shader.reset(new GLShader(skybox_shaders.data(), skybox_shaders.size()));
    depth_shader.reset(new GLShader(depth_shaders.data(), depth_shaders.size()));
    quad_shader.reset(new GLShader(quad_shaders.data(), quad_shaders.size()));
    depthCube_shader.reset(new GLShader(depthCube_shaders.data(), depthCube_shaders.size()));
    sphere_shader.reset(new GLShader(sphere_shaders.data(), sphere_shaders.size()));
    hdr_shader.reset(new GLShader(hdr_shaders.data(), hdr_shaders.size()));


    skycube.reset(new Mesh(Shape::Cube));
    skycube->get_vertex_buffer()->set_layout({
        {"POSITION",    ShaderType::FLOAT3},
        {"UV",          ShaderType::FLOAT2},
        {"NORMAL",      ShaderType::FLOAT3},
        {"TANGENT",     ShaderType::FLOAT3},
        {"BINORMAL",    ShaderType::FLOAT3}
    });

    sky_image.reset(new GLTextureCube(skyboxes["lake"]));

    std::unique_ptr<Mesh> Quad;
    Quad.reset(new Mesh(Shape::Quad));
    Quad->get_vertex_buffer()->set_layout({
        {"POSITION",    ShaderType::FLOAT3},
        {"UV",          ShaderType::FLOAT2},
        {"NORMAL",      ShaderType::FLOAT3},
        {"TANGENT",     ShaderType::FLOAT3},
        {"BINORMAL",    ShaderType::FLOAT3}
    });

    Shadow3DUbo.reset(new GLResourceBuffer(sizeof(shadow3DUBO)));

    FrameBuffer::ConstructInfo finalInfo = {};
    finalInfo.size = {
        displays[display].w * 0.8f,
        displays[display].w * 0.8f
    };
    finalInfo.depthOnly = false;

    FrameBuffer::ConstructInfo renderFBinfo = {};
    renderFBinfo.size = {
        displays[display].w * 0.8f,
        displays[display].w * 0.8f
    };
    renderFBinfo.depthOnly = false;
    renderFBinfo.HDR = true;

    finalBuffer.reset(new GLFrameBuffer(&finalInfo));
    hdrBuffer.reset(new GLFrameBuffer(&renderFBinfo));

    constexpr unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;

    FrameBuffer::ConstructInfo quadFBinfo = {};
    quadFBinfo.size.x = SHADOW_WIDTH;
    quadFBinfo.size.y = SHADOW_HEIGHT;
    quadFBinfo.depthOnly = false;
    quadFBinfo.writeOnly = false;

    quadFB.reset(new GLFrameBuffer(&quadFBinfo));

    using Scene = std::vector<Model>;
    Scene models;
    Scene::iterator active_m = models.end();

    // persistent imgui variable values
    auto active_skybox = skyboxes.find("lake");

    SDL_SetWindowInputFocus(directxwindow);

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDockingWithShift = true;
    ImFont* pFont = io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);
    if (!io.Fonts->Fonts.empty()) {
        io.FontDefault = io.Fonts->Fonts.back();
    }

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
    colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.22f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 0.67f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.44f, 0.44f, 0.44f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.46f, 0.47f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.70f, 0.70f, 0.70f, 0.31f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.70f, 0.70f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.48f, 0.50f, 0.52f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.72f, 0.72f, 0.72f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.73f, 0.60f, 0.15f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.87f, 0.87f, 0.87f, 0.35f);
    colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_TabActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);

    static unsigned int selected_mesh = 0;
    bool show_settings_window = false;

    Timer dt_timer;
    double dt = 0;

    // configure depth map FBO
    // -----------------------
    unsigned int depthMapFBO;
    glGenFramebuffers(1, &depthMapFBO);
    // create depth texture
    unsigned int depthTexture;
    glGenTextures(1, &depthTexture);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    // attach depth texture as FBO's depth buffer
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);


    // configure 3D depth map FBO
    // -----------------------
    unsigned int depthCubeFBO;
    glGenFramebuffers(1, &depthCubeFBO);
    // generate depth cube map
    unsigned int depthCubeTexture;
    glGenTextures(1, &depthCubeTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubeTexture);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT,
            SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, depthCubeFBO);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthCubeTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // load the model files listed in the project section of config.json
    // basically acts like a budget project file
    for (const std::string& path : project) {
        models.push_back(Model(path));
        active_m = models.end() - 1;
        auto scale = active_m->scale_ptr();
        active_m->recalc_transform();
    }

    // setup  light matrices for a movable point light
    glm::mat4 lightmatrix = glm::mat4(1.0f);
    lightmatrix = glm::translate(lightmatrix, { 0.0f, 0.8f, 3.0f });
    float lightPos[3], lightRot[3], lightScale[3];
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(lightmatrix), lightPos, lightRot, lightScale);

    // setup the camera that acts as the sun's view (directional light)
    glm::vec2 planes = { 1.0, 20.0f };
    float orthoSize = 16.0f;
    Camera sunCamera(glm::vec3(0, 15.0, 0), glm::orthoRH_ZO(-orthoSize, orthoSize, -orthoSize, orthoSize, planes.x, planes.y));
    sunCamera.getView() = glm::lookAtRH (
        glm::vec3(-2.0f, 12.0f, 2.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    sunCamera.getAngle().y = -1.325f;

    float nearPlane = 0.1f;
    float farPlane = 25.0f;
    glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), float(SHADOW_WIDTH / SHADOW_HEIGHT), nearPlane, farPlane);

    // toggle bool for changing the shadow map
    bool debugShadows = false;

    Timer timer;

    //////////////////////////////////////////////////////////////
    //// main application loop //////////////////////////////////
    ////////////////////////////////////////////////////////////
    while (running) {
        dt_timer.start();
        handle_sdl_gui_events({ directxwindow }, debugShadows ? sunCamera : camera, dt); // in shadow debug we're in control of the shadow map camera
        sunCamera.update();
        camera.update();
        // clear the main window
        Render::Clear({ 0.22f, 0.32f, 0.42f, 1.0f });

        // setup the shadow map 
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);

        // render the entire scene to the directional light shadow map
        depth_shader->bind();
        auto& depthShader = *depth_shader;
        for (auto& m : models) {
            m.recalc_transform();
            (*depth_shader)["lightSpaceMatrix"] = sunCamera.getProjection() * sunCamera.getView();
            (*depth_shader)["model"] = m.get_transform();
            m.render();
        }
        glCullFace(GL_BACK);



        // setup the 3D shadow map 
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthCubeFBO);
        glClear(GL_DEPTH_BUFFER_BIT);

        // generate the view matrices for calculating lightspace
        glm::vec3 lightPosition = glm::make_vec3(lightPos);
        ubo3d.faceMatrices[0] = shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0));
        ubo3d.faceMatrices[1] = shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0));
        ubo3d.faceMatrices[2] = shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0));
        ubo3d.faceMatrices[3] = shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0));
        ubo3d.faceMatrices[4] = shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0));
        ubo3d.faceMatrices[5] = shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0));


        // retrieve the depthcube shader
        auto& depthcubeShader = *depthCube_shader;
        depthcubeShader.bind();
        
        // set depthcube shader uniforms
        depthcubeShader["faceMatrices"] = ubo3d.faceMatrices;
        depthcubeShader["farPlane"] = farPlane;
        depthcubeShader["lightPos"] = glm::vec4(lightPosition.x, lightPosition.y, lightPosition.z, 1.0f);;
        
        for (auto& m : models) {
            m.recalc_transform();
            depthcubeShader["model"] = m.get_transform();
            m.render();
        }

        // bind the generated shadow map and render it to a quad for debugging in-editor
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        quadFB->bind();
        Render::Clear({ 1,0,0,1 });
        quad_shader->bind();
        Quad->render();
        quad_shader->unbind();
        glBindTexture(GL_TEXTURE_2D, 0);
        quadFB->unbind();

        // bind the main framebuffer
        hdrBuffer->bind();
        Render::Clear({ 0.0f, 0.32f, 0.42f, 1.0f });

        // render a cubemap with depth testing disabled to generate a skybox
        // update the camera without translation
        auto& skyShader = *sky_shader;
        skyShader.bind();
        skyShader["model"] = glm::mat4(1.0f);
        skyShader["view"] = camera.getView();
        skyShader["proj"] = camera.getProjection();

        sky_image->bind(0);
        skycube->bind();
        Render::DrawIndexed(skycube->get_index_buffer()->get_count(), false);

        // set uniforms
        auto& shader = *dx_shader;
        shader.bind();
        shader["sunColor"] = uniforms.sunColor;
        shader["minBias"] = uniforms.minBias;
        shader["maxBias"] = uniforms.maxBias;
        shader["farPlane"] = uniforms.farPlane;
        // bind depth map to 1
        glBindTextureUnit(1, depthTexture);
        // bind omni depth map to 2
        glBindTextureUnit(2, depthCubeTexture);
        for (auto& m : models) {
            // if the mesh doesn't have any textures we bind a default texture
            if (!m.hasTexture()) {
                testTexture->bind(0);
            }
            // update all the model UBO attributes
            m.recalc_transform();
            shader["model"] = m.get_transform();
            shader["view"] = camera.getView();
            shader["projection"] = camera.getProjection();
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(lightmatrix), lightPos, lightRot, lightScale);
            shader["pointLightPos"] = glm::vec4(glm::make_vec3(lightPos), 1.0f);
            shader["DirLightPos"] = glm::vec4(sunCamera.getPosition(), 1.0);
            shader["cameraPosition"] = glm::vec4(camera.getPosition(), 1.0);
            shader["lightSpaceMatrix"] = sunCamera.getProjection() * sunCamera.getView();

            // render the mesh
            m.render();
        }

        // unbind the framebuffer to switch to the application's backbuffer for ImGui
        hdrBuffer->unbind();

        auto& hdrShader = *hdr_shader;
        static bool hdr = true;
        if (hdr) {
            finalBuffer->bind();
            hdrShader.bind();
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            hdrShader["exposure"] = hdr_ubo.exposure;
            hdrShader["gamma"] = hdr_ubo.gamma;
            hdrBuffer->bindTexture(0);
            Quad->render();
            finalBuffer->unbind();
        }

        //get new frame for ImGui and ImGuizmo
        Render::ImGui_NewFrame(directxwindow);
        ImGuizmo::BeginFrame();
        ImGuizmo::Enable(true);

        static bool opt_fullscreen_persistant = true;
        bool opt_fullscreen = opt_fullscreen_persistant;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen) {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, 
        // so we ask Begin() to not render a background.
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        static bool p_open = true;
        ImGui::Begin("DockSpace", &p_open, window_flags);
        ImGui::PopStyleVar();
        if (opt_fullscreen) ImGui::PopStyleVar(2);

        // DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        // move the light by a fixed amount and let it bounce between -125 and 125 units/pixels on the x axis
        static double move_amount = 0.003;
        static double bounds = 7.5f;
        static bool move_light = true;
        double light_delta = move_amount * dt;
        if ((lightPos[0] >= bounds && move_amount > 0) || (lightPos[0] <= -bounds && move_amount < 0)) {
            move_amount *= -1;
        }
        if (move_light) {
            lightmatrix = glm::translate(lightmatrix, { light_delta, 0.0, 0.0 });
        }

        // draw the top user bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save project", "CTRL + S")) {
                    serialize_settings("config.json", true);
                }
                if (ImGui::MenuItem("Settings", "")) {
                    show_settings_window = true;
                }
                if (ImGui::MenuItem("Exit", "Escape")) {
                    running = false;
                }

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Delete", "DEL")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About", "")) {}
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // draw the imguizmo at the center of the light
        ImGuizmo::Manipulate(glm::value_ptr(camera.getView()), glm::value_ptr(camera.getProjection()), ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::MODE::WORLD, glm::value_ptr(lightmatrix));

        // model panel
        ImGui::Begin("ECS");
        if (ImGui::IsWindowFocused()) {
            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), false)) {}
        }
        auto tree_node_flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (ImGui::TreeNodeEx("Models", tree_node_flags)) {
            ImGui::Columns(1, NULL, false);
            for (Scene::iterator it = models.begin(); it != models.end(); it++) {
                bool selected = it == active_m;
                // draw a tree node for every model in the scene
                ImGuiTreeNodeFlags treeflags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (it == active_m) {
                    treeflags |= ImGuiTreeNodeFlags_Selected;
                }
                auto open = ImGui::TreeNodeEx(it->get_path().c_str(), treeflags);
                if (ImGui::IsItemClicked()) active_m = it;
                if (open) {
                    // draw a selectable for every mesh in the scene
                    for (unsigned int i = 0; i < it->mesh_count(); i++) {
                        ImGui::PushID(i);
                        if (it->get_mesh(i).has_value()) {
                            bool selected = (i == selected_mesh) && (it == active_m);
                            if (ImGui::Selectable(it->get_mesh(i).value()->get_name().c_str(), selected)) {
                                active_m = it;
                                selected_mesh = i;
                            }
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }

        static char input_text[120];
        if (ImGui::InputText("", input_text, sizeof(input_text), ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string model_name = std::string(input_text);
            if (!model_name.empty() && active_m != models.end()) {
                active_m->set_path(model_name);
                memset(input_text, 0, sizeof(input_text));
            }
            else {
                memset(input_text, 0, sizeof(input_text));
            }
        }
        if (ImGui::Button("Load Model")) {
            std::string path = context.open_file_dialog({ ft_mesh });
            if (!path.empty()) {
                models.push_back(Model(path));
                active_m = models.end() - 1;
                auto scale = active_m->scale_ptr();
                active_m->recalc_transform();
                project.push_back(path);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Model")) {
            if (active_m != models.end()) {
                models.erase(active_m);
                active_m = models.end();
                int index = (int)std::distance(models.begin(), active_m);
                project.erase(project.begin() + index);
            }
        }
        ImGui::End();

        // scene panel
        ImGui::Begin("Scene");
        if (ImGui::BeginCombo("Skybox", active_skybox->first.c_str())) {
            for (auto it = skyboxes.begin(); it != skyboxes.end(); it++) {
                bool selected = (it == active_skybox);
                if (ImGui::Selectable(it->first.c_str(), selected)) {
                    active_skybox = it;
                    sky_image.reset(new GLTextureCube(active_skybox->second));
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        // toggle button for openGl vsync
        static bool use_vsync = false;
        if (ImGui::RadioButton("USE VSYNC", use_vsync)) {
            use_vsync = !use_vsync;
        }
        
        if (ImGui::RadioButton("Animate Light", move_light)) {
            move_light = !move_light;
        }

        if (ImGui::RadioButton("Debug shadows", debugShadows)) {
            debugShadows = !debugShadows;
        }

        static bool doNormalMapping = true;
        if (ImGui::RadioButton("Normal mapping", doNormalMapping)) {
            if (doNormalMapping) {
                modelStages[0].defines = { "NO_NORMAL_MAP" };
                modelStages[1].defines = { "NO_NORMAL_MAP" };
            } else {
                modelStages[0].defines.clear();
                modelStages[1].defines.clear();
            }
            dx_shader.reset(new GLShader(modelStages.data(), modelStages.size()));
            doNormalMapping = !doNormalMapping;
        }

        if (ImGui::Button("Reload shaders")) {
                dx_shader.reset(new GLShader(modelStages.data(), modelStages.size()));
        }

        ImGui::NewLine(); 

        ImGui::Text("Directional Light");
        ImGui::Separator();
        if (ImGui::DragFloat2("Angle", glm::value_ptr(sunCamera.getAngle()), 0.001f)) {}
        
        if (ImGui::DragFloat2("Planes", glm::value_ptr(planes), 0.1f)) {
            sunCamera.getProjection() = glm::orthoRH_ZO(-orthoSize, orthoSize, -orthoSize, orthoSize, planes.x, planes.y);
        }
        if (ImGui::DragFloat("Size", &orthoSize)) {
            sunCamera.getProjection() = glm::orthoRH_ZO(-orthoSize, orthoSize, -orthoSize, orthoSize, planes.x, planes.y);
        }
        if (ImGui::DragFloat("Min bias", &uniforms.minBias, 0.0001f, 0.0f, FLT_MAX, "%.4f")) {}
        if (ImGui::DragFloat("Max bias", &uniforms.maxBias, 0.0001f, 0.0f, FLT_MAX, "%.4f")) {}
        if (ImGui::ColorEdit3("Color", glm::value_ptr(uniforms.sunColor))) {}
        
        ImGui::NewLine();
        ImGui::Text("Point Light");
        ImGui::Separator();
        if (ImGui::DragFloat("far plane", &farPlane)) {
            shadowProj = glm::perspective(glm::radians(90.0f), float(SHADOW_WIDTH / SHADOW_HEIGHT), nearPlane, farPlane);
        }
        
        ImGui::NewLine();
        ImGui::Text("HDR");
        ImGui::Separator();
        if (ImGui::RadioButton("Enabled", hdr)) {
            hdr = !hdr;
        }
        if (ImGui::SliderFloat("Exposure", &hdr_ubo.exposure, 0.0f, 1.0f)) {}
        if (ImGui::SliderFloat("Gamma", &hdr_ubo.gamma, 1.0f, 3.2f)) {}

        ImGui::End();

        ImGui::ShowMetricsWindow();

        ImGui::Begin("Camera Properties");
        static float fov = 45.0f;
        if (ImGui::DragFloat("FOV", &fov, 1.0f, 35.0f, 120.0f)) {
            camera.getProjection() = glm::perspectiveRH_ZO(glm::radians(fov), 16.0f / 9.0f, 0.1f, 10000.0f);
        }
        if (ImGui::DragFloat("Camera Move Speed", camera.get_move_speed(), 0.01f, 0.1f, FLT_MAX, "%.2f")) {}
        if (ImGui::DragFloat("Camera Look Speed", camera.get_look_speed(), 0.0001f, 0.0001f, FLT_MAX, "%.4f")) {}
        ImGui::End();



        // if the scene containt at least one model, AND the active model is pointing at a valid model,
        // AND the active model has a mesh to modify, the properties window draws
        if (!models.empty() && active_m != models.end()) {
            ImGui::Begin("Mesh Properties");

            if (ImGui::DragFloat3("Scale", active_m->scale_ptr(), 0.01f, 0.0f, 10.0f)) {}
            if (ImGui::DragFloat3("Position", active_m->pos_ptr(), 0.01f, -100.0f, 100.0f)) {}
            if (ImGui::DragFloat3("Rotation", active_m->rotation_ptr(), 0.01f, (float)(-M_PI), (float)(M_PI))) {}

            // resets the model's transformation
            if (ImGui::Button("Reset")) {
                active_m->reset_transform();
            }
            ImGui::End();

        }
        // renderer viewport
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Renderer", NULL, ImGuiWindowFlags_AlwaysAutoResize);
        static bool resizing = true;
        auto size = ImGui::GetWindowSize();
        hdrBuffer->resize({ size.x, size.y - 25 });
        finalBuffer->resize({ size.x, size.y - 25 });
        camera.getProjection() = glm::perspectiveRH_ZO(glm::radians(fov), size.x / size.y, 0.1f, 10000.0f);
        ImGuizmo::SetRect(0, 0, size.x, size.y);
        // function that calls an ImGui image with the framebuffer's color stencil data
        if (debugShadows) {
            quadFB->ImGui_Image();
        }
        else {
            if (hdr) 
                finalBuffer->ImGui_Image();
            else 
                hdrBuffer->ImGui_Image();
        }

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::End();
        Render::ImGui_Render();

        Render::SwapBuffers(use_vsync);
        dt_timer.stop();
        dt = dt_timer.elapsed_ms();

    } // while true loop

    display = SDL_GetWindowDisplayIndex(directxwindow);
    //clean up SDL
    SDL_DestroyWindow(directxwindow);
    SDL_Quit();
} // main

} // namespace Raekor  