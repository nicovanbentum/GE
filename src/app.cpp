#include "pch.h"
#include "app.h"
#include "util.h"
#include "scene.h"
#include "entry.h"
#include "camera.h"
#include "shader.h"
#include "framebuffer.h"
#include "PlatformContext.h"
#include "renderer.h"
#include "buffer.h"
#include "timer.h"

namespace Raekor {

struct shadowUBO {
    glm::mat4 cameraMatrix;
    glm::mat4 model;
};

struct VertexUBO {
    glm::mat4 model, view, projection;
    glm::mat4 lightSpaceMatrix;
    glm::vec4 DirViewPos;
    glm::vec4 DirLightPos;
    glm::vec4 pointLightPos;
};

struct Uniforms {
    glm::vec4 sunColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    float minBias = 0.005f, maxBias = 0.05f;
    float farPlane = 25.0f;
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

    // load the model files listed in the project section of config.json
// basically acts like a budget project file
    Scene scene;
    std::vector<SceneObject>::iterator activeObject = scene.objects.end();
    Timer timer;
    timer.start();
    for (const std::string& path : project) {
        scene.add(path);
    }
    timer.stop();
    std::cout << "Setup time = " << timer.elapsed_ms() << '\n';

    if (!scene.objects.empty()) {
        activeObject = scene.objects.begin();
    }

    // create a Camera we can use to move around our scene
    static float fov = 45.0f;
    Camera camera(glm::vec3(0, 1.0, 0), glm::perspectiveRH(glm::radians(fov), 16.0f / 9.0f, 1.0f, 100.0f));

    VertexUBO ubo = {};
    shadowUBO shadowUbo;
    HDR_UBO hdr_ubo;
    Uniforms uniforms;

    std::unique_ptr<GLShader> mainShader;
    std::unique_ptr<GLShader> skyShader;
    std::unique_ptr<GLShader> depthShader;
    std::unique_ptr<GLShader> depthCubeShader;
    std::unique_ptr<GLShader> quadShader;
    std::unique_ptr<GLShader> sphereShader;
    std::unique_ptr<GLShader> hdrShader;
    std::unique_ptr<GLShader> CubemapDebugShader;
    std::unique_ptr<GLShader> SSAOshader;

    std::unique_ptr<GLFrameBuffer> hdrBuffer;
    std::unique_ptr<GLFrameBuffer> finalBuffer;
    std::unique_ptr<GLFrameBuffer> quadFB;
    std::unique_ptr<GLFrameBuffer> ssaoFB;

    std::unique_ptr<GLResourceBuffer> dxrb;
    std::unique_ptr<GLResourceBuffer> shadowVertUbo;
    std::unique_ptr<GLResourceBuffer> extraUbo;
    std::unique_ptr<GLResourceBuffer> Shadow3DUbo;
    std::unique_ptr<GLResourceBuffer> mat4Ubo;
    std::unique_ptr<GLResourceBuffer> hdrUbo;

    std::unique_ptr<GLTextureCube> sky_image;

    std::unique_ptr<Mesh> skycube;

    Stb::Image testImage = Stb::Image(RGBA);
    testImage.load("resources/textures/test.png", true);
    std::unique_ptr<Texture> testTexture;
    testTexture.reset(Texture::construct(testImage));


    Ffilter ft_mesh;
    ft_mesh.name = "Supported Mesh Files";
    ft_mesh.extensions = "*.obj;*.fbx;*.gltf;*.glb";

    Ffilter ft_texture;
    ft_texture.name = "Supported Image Files";
    ft_texture.extensions = "*.png;*.jpg;*.jpeg;*.tga";

    Shader::Stage vertex(Shader::Type::VERTEX, "shaders\\OpenGL\\main.vert");
    Shader::Stage frag(Shader::Type::FRAG, "shaders\\OpenGL\\main.frag");
    std::array<Shader::Stage, 2> modelStages = { vertex, frag };
    mainShader.reset(new GLShader(modelStages.data(), modelStages.size()));

    std::vector<Shader::Stage> skybox_shaders;
    skybox_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\skybox.vert");
    skybox_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\skybox.frag");

    std::vector<Shader::Stage> depth_shaders;
    depth_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\depth.vert");
    depth_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\depth.frag");

    std::vector<Shader::Stage> depthCube_shaders;
    depthCube_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\depthCube.vert");
    depthCube_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\depthCube.frag");

    std::vector<Shader::Stage> quad_shaders;
    quad_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\quad.vert");
    quad_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\quad.frag");

    std::vector<Shader::Stage> sphere_shaders;
    sphere_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\lightSphere.vert");
    sphere_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\lightSphere.frag");

    std::vector<Shader::Stage> hdr_shaders;
    hdr_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\HDR.vert");
    hdr_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\HDR.frag");

    std::vector<Shader::Stage> cubedebug_shaders;
    cubedebug_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\simple.vert");
    cubedebug_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\simple.frag");

    std::vector<Shader::Stage> SSAO_shaders;
    SSAO_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\SSAO.vert");
    SSAO_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\SSAO.frag");

    skyShader.reset(new GLShader(skybox_shaders.data(), skybox_shaders.size()));
    depthShader.reset(new GLShader(depth_shaders.data(), depth_shaders.size()));
    quadShader.reset(new GLShader(quad_shaders.data(), quad_shaders.size()));
    depthCubeShader.reset(new GLShader(depthCube_shaders.data(), depthCube_shaders.size()));
    sphereShader.reset(new GLShader(sphere_shaders.data(), sphere_shaders.size()));
    hdrShader.reset(new GLShader(hdr_shaders.data(), hdr_shaders.size()));
    CubemapDebugShader.reset(new GLShader(cubedebug_shaders.data(), cubedebug_shaders.size()));
    SSAOshader.reset(new GLShader(SSAO_shaders.data(), SSAO_shaders.size()));

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

    dxrb.reset(new GLResourceBuffer(sizeof(VertexUBO)));
    shadowVertUbo.reset(new GLResourceBuffer(sizeof(shadowUBO)));
    mat4Ubo.reset(new GLResourceBuffer(sizeof(glm::mat4)));
    hdrUbo.reset(new GLResourceBuffer(sizeof(HDR_UBO)));

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

    FrameBuffer::ConstructInfo ssaoFBinfo = {};
    ssaoFBinfo.size = {
        displays[display].w * 0.8f,
        displays[display].h * 0.8f
    };
    ssaoFBinfo.depthOnly = false;
    ssaoFBinfo.writeOnly = false;

    ssaoFB.reset(new GLFrameBuffer(&ssaoFBinfo));

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

    float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };

    // configure depth prepass FBO
// -----------------------
    unsigned int depthPrepassFBO;
    glGenFramebuffers(1, &depthPrepassFBO);
    // create depth prepass texture
    unsigned int depthPrepassTexture;
    glGenTextures(1, &depthPrepassTexture);
    glBindTexture(GL_TEXTURE_2D, depthPrepassTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, displays[display].w, displays[display].h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    // attach depth prepass texture as FBO's depth buffer
    glBindFramebuffer(GL_FRAMEBUFFER, depthPrepassFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthPrepassTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

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
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT,
            SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, depthCubeFBO);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);


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

    std::uniform_real_distribution<float> randomFloats(0.0, 1.0); // random floats between 0.0 - 1.0
    std::default_random_engine generator;
    std::vector<glm::vec3> ssaoKernel;
    for (unsigned int i = 0; i < 64; ++i)
    {
        glm::vec3 sample(
            randomFloats(generator),
            randomFloats(generator),
            randomFloats(generator)
        );
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = static_cast<float>(i / 64.0);

        auto lerp = [](float a, float b, float f) {
            return a + f * (b - a);
        };

        scale = lerp(0.1f, 1.0f, scale * scale);
        sample *= scale;
        ssaoKernel.push_back(sample);
        ssaoKernel.push_back(sample);
    }

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

        // depth pre-pass
        glViewport(0, 0, displays[display].w, displays[display].h);
        glBindFramebuffer(GL_FRAMEBUFFER, depthPrepassFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);

        // render the scene for the depth pre-pass
        depthShader->bind();
        shadowUbo.cameraMatrix = camera.getProjection() * camera.getView();
        shadowUbo.model = glm::mat4(1.0f);
        shadowVertUbo->update(&shadowUbo, sizeof(shadowUbo));
        shadowVertUbo->bind(0);
        
        scene.render();

        // bind the generated depth pre-pass and perform SSAO 
        glBindTextureUnit(1, depthPrepassTexture);
        ssaoFB->bind();
        Render::Clear({ 0, 0, 0, 1 });
        SSAOshader->bind();

        // set SSAO uniforms
        SSAOshader->getUniform("aspectRatio") = static_cast<float>(displays[display].w / displays[display].h);
        SSAOshader->getUniform("tanHalfFOV") = tanf(glm::radians(fov / 2.0f));
        SSAOshader->getUniform("gSampleRad") = 1.5f;
        SSAOshader->getUniform("gProj") = camera.getProjection();
        glUniform3fv(SSAOshader->getUniform("gKernel").id, 64, (GLfloat*)&ssaoKernel[0]);

        Quad->render();
        SSAOshader->unbind();
        glBindTexture(GL_TEXTURE_2D, 0);
        ssaoFB->unbind();

        // setup the shadow map 
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);

        // render the entire scene to the directional light shadow map
        depthShader->bind();
        shadowUbo.cameraMatrix = sunCamera.getProjection() * sunCamera.getView();
        shadowUbo.model = glm::mat4(1.0f);
        shadowVertUbo->update(&shadowUbo, sizeof(shadowUbo));
        shadowVertUbo->bind(0);

        scene.render();

        // setup the 3D shadow map 
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthCubeFBO);
        glCullFace(GL_FRONT);

        // generate the view matrices for calculating lightspace
        std::vector<glm::mat4> shadowTransforms;
        glm::vec3 lightPosition = glm::make_vec3(lightPos);
        shadowTransforms.push_back(shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(  1.0, 0.0, 0.0),    glm::vec3(0.0, -1.0, 0.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3( -1.0, 0.0, 0.0),    glm::vec3(0.0, -1.0, 0.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(  0.0, 1.0, 0.0),    glm::vec3(0.0, 0.0, 1.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(  0.0, -1.0, 0.0),   glm::vec3(0.0, 0.0, -1.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(  0.0, 0.0, 1.0),    glm::vec3(0.0, -1.0, 0.0)));
        shadowTransforms.push_back(shadowProj * glm::lookAtRH(lightPosition, lightPosition + glm::vec3(  0.0, 0.0, -1.0),   glm::vec3(0.0, -1.0, 0.0)));

        // render every model using the depth cubemap shader
        depthCubeShader->bind();
        depthCubeShader->getUniform("farPlane") = farPlane;
        for (uint32_t i = 0; i < 6; i++) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, depthCubeTexture, 0);
            glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

            depthCubeShader->getUniform("model") = glm::mat4(1.0f);
            depthCubeShader->getUniform("projView") = shadowTransforms[i];
            depthCubeShader->getUniform("lightPos") = glm::make_vec3(lightPos);

            scene.render();
        }
        glCullFace(GL_BACK);

        // bind the generated shadow map and render it to a quad for debugging in-editor
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        quadFB->bind();
        Render::Clear({ 1,0,0,1 });
        quadShader->bind();
        Quad->render();
        quadShader->unbind();
        glBindTexture(GL_TEXTURE_2D, 0);
        quadFB->unbind();

        // bind the main framebuffer
        hdrBuffer->bind();
        Render::Clear({ 0.0f, 0.32f, 0.42f, 1.0f });

        // render a cubemap with depth testing disabled to generate a skybox
        // update the camera without translation
        skyShader->bind();
        skyShader->getUniform("view") = glm::mat4(glm::mat3(camera.getView()));
        skyShader->getUniform("proj") = camera.getProjection();

        sky_image->bind(0);
        skycube->bind();
        Render::DrawIndexed(skycube->get_index_buffer()->get_count(), false);

        // set uniforms
        mainShader->bind();
        mainShader->getUniform("sunColor") = uniforms.sunColor;
        mainShader->getUniform("minBias") = uniforms.minBias;
        mainShader->getUniform("maxBias") = uniforms.maxBias;
        mainShader->getUniform("farPlane") = farPlane;
        // bind depth map to 1
        glBindTextureUnit(1, depthTexture);
        // bind omni depth map to 2
        glBindTextureUnit(2, depthCubeTexture);

        ubo.model = glm::mat4(1.0f);
        ubo.view = camera.getView();
        ubo.projection = camera.getProjection();
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(lightmatrix), lightPos, lightRot, lightScale);
        ubo.pointLightPos = glm::vec4(glm::make_vec3(lightPos), 1.0f);
        ubo.DirLightPos = glm::vec4(sunCamera.getPosition(), 1.0);
        ubo.DirViewPos = glm::vec4(camera.getPosition(), 1.0);
        ubo.lightSpaceMatrix = sunCamera.getProjection() * sunCamera.getView();

        dxrb->update(&ubo, sizeof(VertexUBO));
        dxrb->bind(0);

        scene.render();

        // unbind the framebuffer to switch to the application's backbuffer for ImGui
        hdrBuffer->unbind();

        static bool hdr = true;
        if (hdr) {
            finalBuffer->bind();
            hdrShader->bind();
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            hdrUbo->update(&hdr_ubo, sizeof(HDR_UBO));
            hdrUbo->bind(0);
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
                if (ImGui::MenuItem("Load model")) {
                    std::string path = context.open_file_dialog({ ft_mesh });
                    if (!path.empty()) {
                        project.push_back(path);
                        scene.add(path);
                    }
                }
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
                if (ImGui::MenuItem("Delete", "DEL")) {
                    // on press we remove the scene object
                    if (activeObject != scene.objects.end()) {
                        scene.erase(activeObject->name);
                        activeObject = scene.objects.begin();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), true)) {
                // on press we remove the scene object
                if (activeObject != scene.objects.end()) {
                    scene.erase(activeObject->name);
                    activeObject = scene.objects.begin();
                }
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
        ImGui::Begin("Entities");
        if (ImGui::IsWindowFocused()) {
            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), false)) {}
        }
        auto tree_node_flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (ImGui::TreeNodeEx("Objects", tree_node_flags)) {
            ImGui::Columns(1, NULL, false);
            // draw a selectable for every object in the scene
            for (auto& object : scene.objects) {
                    bool selected = activeObject->name == object.name;
                    if (ImGui::Selectable(object.name.c_str(), selected)) {
                        activeObject = scene.at(object.name);
                    }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::TreePop();
        }

        //static char input_text[120];
        //if (ImGui::InputText("", input_text, sizeof(input_text), ImGuiInputTextFlags_EnterReturnsTrue)) {
        //    std::string model_name = std::string(input_text);
        //    activeObject->name = model_name;
        //    memset(input_text, 0, sizeof(input_text));
        //}

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
            mainShader.reset(new GLShader(modelStages.data(), modelStages.size()));
            doNormalMapping = !doNormalMapping;
        }

        if (ImGui::Button("Reload shaders")) {
            mainShader.reset(new GLShader(modelStages.data(), modelStages.size()));
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
        if (ImGui::DragFloat("FOV", &fov, 1.0f, 35.0f, 120.0f)) {
            camera.getProjection() = glm::perspectiveRH(glm::radians(fov), 16.0f / 9.0f, 1.0f, 100.0f);
        }
        if (ImGui::DragFloat("Camera Move Speed", camera.get_move_speed(), 0.001f, 0.001f, FLT_MAX, "%.3f")) {}
        if (ImGui::DragFloat("Camera Look Speed", camera.get_look_speed(), 0.0001f, 0.0001f, FLT_MAX, "%.4f")) {}
        ImGui::End();



        // if the scene containt at least one model, AND the active model is pointing at a valid model,
        // AND the active model has a mesh to modify, the properties window draws
        if (!scene.objects.empty() && activeObject != scene.objects.end()) {
            ImGui::Begin("Mesh Properties");

            if (ImGui::DragFloat3("Scale", glm::value_ptr(activeObject->scale), 0.01f, 0.0f, 10.0f)) {}
            if (ImGui::DragFloat3("Position", glm::value_ptr(activeObject->position), 0.01f, -100.0f, 100.0f)) {}
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(activeObject->rotation), 0.01f, (float)(-M_PI), (float)(M_PI))) {}

            // resets the model's transformation
            if (ImGui::Button("Reset")) {
                activeObject->reset_transform();
            }
            ImGui::End();

        }
        // renderer viewport
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Renderer", NULL, ImGuiWindowFlags_AlwaysAutoResize);
        static bool resizing = true;
        auto size = ImGui::GetWindowSize();
        auto pos = ImGui::GetWindowPos();
        hdrBuffer->resize({ size.x, size.y - 25 });
        finalBuffer->resize({ size.x, size.y - 25 });
        camera.getProjection() = glm::perspectiveRH(glm::radians(fov), size.x / size.y, 1.0f, 100.0f);
        ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
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