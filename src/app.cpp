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
};

struct VertexUBO {
    glm::mat4 view, projection;
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
    Renderer::setAPI(RenderAPI::OPENGL);
    Renderer::Init(directxwindow);

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
    std::unique_ptr<GLShader> hdrShader;
    std::unique_ptr<GLShader> CubemapDebugShader;
    std::unique_ptr<GLShader> SSAOshader;
    std::unique_ptr<GLShader> blurShader;
    std::unique_ptr<GLShader> bloomShader;
    std::unique_ptr<GLShader> GBufferShader;

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

    std::vector<Shader::Stage> blur_shaders;
    blur_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\quad.vert");
    blur_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\gaussian.frag");

    std::vector<Shader::Stage> bloom_shaders;
    bloom_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\quad.vert");
    bloom_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\bloom.frag");

    std::vector<Shader::Stage> hdr_shaders;
    std::vector<std::string> tonemappingShaders = {
       "shaders\\OpenGL\\HDR.frag",
       "shaders\\OpenGL\\HDRreinhard.frag",
       "shaders\\OpenGL\\HDRuncharted.frag"
    };
    hdr_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\HDR.vert");
    hdr_shaders.emplace_back(Shader::Type::FRAG, tonemappingShaders.begin()->c_str());

    std::vector<Shader::Stage> cubedebug_shaders;
    cubedebug_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\simple.vert");
    cubedebug_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\simple.frag");

    std::vector<Shader::Stage> SSAO_shaders;
    SSAO_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\SSAO.vert");
    SSAO_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\SSAO.frag");

    std::vector<Shader::Stage> gbuffer_shaders;
    gbuffer_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\gbuffer.vert");
    gbuffer_shaders.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\gbuffer.frag");

    skyShader.reset(new GLShader(skybox_shaders.data(), skybox_shaders.size()));
    depthShader.reset(new GLShader(depth_shaders.data(), depth_shaders.size()));
    quadShader.reset(new GLShader(quad_shaders.data(), quad_shaders.size()));
    depthCubeShader.reset(new GLShader(depthCube_shaders.data(), depthCube_shaders.size()));
    hdrShader.reset(new GLShader(hdr_shaders.data(), hdr_shaders.size()));
    CubemapDebugShader.reset(new GLShader(cubedebug_shaders.data(), cubedebug_shaders.size()));
    SSAOshader.reset(new GLShader(SSAO_shaders.data(), SSAO_shaders.size()));
    blurShader.reset(new GLShader(blur_shaders.data(), blur_shaders.size()));
    bloomShader.reset(new GLShader(bloom_shaders.data(), bloom_shaders.size()));
    GBufferShader.reset(new GLShader(gbuffer_shaders.data(), gbuffer_shaders.size()));

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

    uint32_t renderWidth = static_cast<uint32_t>(displays[display].w * .8f); 
    uint32_t renderHeight = static_cast<uint32_t>(displays[display].h * .8f);

    renderWidth = 2003;
    renderHeight = 1370;

    constexpr unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;

    glTexture2D albedoTexture, normalTexture, positionTexture;
    
    albedoTexture.bind();
    albedoTexture.init(renderWidth, renderHeight, Format::HDR);
    albedoTexture.setFilter(Sampling::Filter::None);
    albedoTexture.unbind();

    normalTexture.bind();
    normalTexture.init(renderWidth, renderHeight, Format::HDR);
    normalTexture.setFilter(Sampling::Filter::None);
    normalTexture.unbind();

    positionTexture.bind();
    positionTexture.init(renderWidth, renderHeight, Format::HDR);
    positionTexture.setFilter(Sampling::Filter::None);
    positionTexture.unbind();

    glRenderbuffer GDepthBuffer;
    GDepthBuffer.init(renderWidth, renderHeight, GL_DEPTH32F_STENCIL8);

    glFramebuffer GBuffer;
    GBuffer.bind();
    GBuffer.attach(positionTexture,     GL_COLOR_ATTACHMENT0);
    GBuffer.attach(normalTexture,       GL_COLOR_ATTACHMENT1);
    GBuffer.attach(albedoTexture,       GL_COLOR_ATTACHMENT2);
    GBuffer.attach(GDepthBuffer,        GL_DEPTH_STENCIL_ATTACHMENT);
    GBuffer.unbind();


    glTexture2D preprocessTexture;
    preprocessTexture.bind();
    preprocessTexture.init(renderWidth, renderHeight, Format::HDR);
    preprocessTexture.setFilter(Sampling::Filter::Bilinear);
    preprocessTexture.unbind();

    glFramebuffer preprocessFramebuffer;
    preprocessFramebuffer.bind();
    preprocessFramebuffer.attach(preprocessTexture, GL_COLOR_ATTACHMENT0);
    preprocessFramebuffer.unbind();

    glTexture2D hdrTexture;
    hdrTexture.bind();
    hdrTexture.init(renderWidth, renderHeight, Format::HDR);
    hdrTexture.setFilter(Sampling::Filter::Bilinear);
    hdrTexture.unbind();

    glTexture2D bloomTexture;
    bloomTexture.bind();
    bloomTexture.init(renderWidth, renderHeight, Format::HDR);
    bloomTexture.setFilter(Sampling::Filter::Bilinear);
    bloomTexture.unbind();

    glRenderbuffer hdrRenderbuffer;
    hdrRenderbuffer.init(renderWidth, renderHeight, GL_DEPTH32F_STENCIL8);

    glFramebuffer hdrFramebuffer;
    hdrFramebuffer.bind();
    hdrFramebuffer.attach(hdrTexture, GL_COLOR_ATTACHMENT0);
    hdrFramebuffer.attach(bloomTexture, GL_COLOR_ATTACHMENT1);
    hdrFramebuffer.attach(hdrRenderbuffer, GL_DEPTH_STENCIL_ATTACHMENT);
    hdrFramebuffer.unbind();

    glTexture2D finalTexture;
    finalTexture.bind();
    finalTexture.init(renderWidth, renderHeight, Format::SDR);
    finalTexture.setFilter(Sampling::Filter::Bilinear);
    finalTexture.unbind();

    glRenderbuffer finalRenderbuffer;
    finalRenderbuffer.init(renderWidth, renderHeight, GL_DEPTH32F_STENCIL8);

    glFramebuffer finalFramebuffer;
    finalFramebuffer.bind();
    finalFramebuffer.attach(finalTexture, GL_COLOR_ATTACHMENT0);
    finalFramebuffer.attach(finalRenderbuffer, GL_DEPTH_STENCIL_ATTACHMENT);
    finalFramebuffer.unbind();

    glTexture2D quadTexture;
    quadTexture.bind();
    quadTexture.init(SHADOW_WIDTH, SHADOW_HEIGHT, Format::SDR);
    quadTexture.setFilter(Sampling::Filter::Bilinear);
    quadTexture.unbind();

    glRenderbuffer quadRenderbuffer;
    quadRenderbuffer.init(SHADOW_WIDTH, SHADOW_HEIGHT, GL_DEPTH32F_STENCIL8);
    
    glFramebuffer quadFramebuffer;
    quadFramebuffer.bind();
    quadFramebuffer.attach(quadTexture, GL_COLOR_ATTACHMENT0);
    quadFramebuffer.attach(quadRenderbuffer, GL_DEPTH_STENCIL_ATTACHMENT);
    quadFramebuffer.unbind();

    glTexture2D pingpongTextures[2];
    glFramebuffer pingpongFramebuffers[2];

    for (unsigned int i = 0; i < 2; i++) {
        pingpongTextures[i].bind();
        pingpongTextures[i].init(renderWidth, renderHeight, Format::HDR);   
        pingpongTextures[i].setFilter(Sampling::Filter::Bilinear);
        pingpongTextures[i].setWrap(Sampling::Wrap::ClampEdge);
        pingpongTextures[i].unbind();

        pingpongFramebuffers[i].bind();
        pingpongFramebuffers[i].attach(pingpongTextures[i], GL_COLOR_ATTACHMENT0);
        pingpongFramebuffers[i].unbind();
    }
       
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
    for (unsigned int i = 0; i < themeColors.size(); i++) {
        auto& savedColor = themeColors[i];
        colors[i] = ImVec4(savedColor[0], savedColor[1], savedColor[2], savedColor[3]);
    }

    static unsigned int selected_mesh = 0;
    bool show_settings_window = false;

    Timer dt_timer;
    double dt = 0;

    float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };

    glTexture2D shadowTexture;
    shadowTexture.bind();
    shadowTexture.init(SHADOW_WIDTH, SHADOW_HEIGHT, Format::Depth);
    shadowTexture.setFilter(Sampling::Filter::None);
    shadowTexture.setWrap(Sampling::Wrap::ClampBorder);

    glFramebuffer shadowFramebuffer;
    shadowFramebuffer.bind();
    shadowFramebuffer.attach(shadowTexture, GL_DEPTH_ATTACHMENT);
    shadowFramebuffer.unbind();


    glTextureCube depthCubeTexture;
    depthCubeTexture.bind();
    for (unsigned int i = 0; i < 6; ++i) {
        depthCubeTexture.init(SHADOW_WIDTH, SHADOW_HEIGHT, i, Format::Depth, nullptr);
    }
    depthCubeTexture.setFilter(Sampling::Filter::None);
    depthCubeTexture.setWrap(Sampling::Wrap::ClampEdge);

    glFramebuffer depthCubeFramebuffer;

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

    static bool wireframeOnly = false;
    static bool frustrumCulling = false;

    glm::vec3 bloomThreshold { 2.0f, 2.0f, 2.0f };
    static bool doBloom = false;

    //////////////////////////////////////////////////////////////
    //// main application loop //////////////////////////////////
    ////////////////////////////////////////////////////////////
    while (running) {
        dt_timer.start();
        handle_sdl_gui_events({ directxwindow }, debugShadows ? sunCamera : camera, dt); // in shadow debug we're in control of the shadow map camera
        sunCamera.update(true);
        camera.update(true);

        // clear the main window
        Renderer::Clear({ 0.22f, 0.32f, 0.42f, 1.0f });

        // setup the shadow map 
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        shadowFramebuffer.bind();
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);

        // render the entire scene to the directional light shadow map
        depthShader->bind();
        shadowUbo.cameraMatrix = sunCamera.getProjection() * sunCamera.getView();
        shadowVertUbo->update(&shadowUbo, sizeof(shadowUbo));
        shadowVertUbo->bind(0);

        // render the scene 
        for (auto& object : scene.objects) {
            if (!object.hasAlpha) {
                depthShader->getUniform("model") = object.transform;
                object.render();
            }
        }

        for (auto& object : scene.objects) {
            if (object.hasAlpha) {
                depthShader->getUniform("model") = object.transform;
                object.render();
            }
        }

        // setup the 3D shadow map 
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        depthCubeFramebuffer.bind();
        glCullFace(GL_BACK);

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
            depthCubeFramebuffer.attach(depthCubeTexture, GL_DEPTH_ATTACHMENT, i);
            glClear(GL_DEPTH_BUFFER_BIT);
            depthCubeShader->getUniform("projView") = shadowTransforms[i];
            depthCubeShader->getUniform("lightPos") = glm::make_vec3(lightPos);

            // render the scene 
            for (auto& object : scene.objects) {
                if (!object.hasAlpha) {
                    depthCubeShader->getUniform("model") = object.transform;
                    object.render();
                }
            }

            for (auto& object : scene.objects) {
                if (object.hasAlpha) {
                    depthCubeShader->getUniform("model") = object.transform;
                    object.render();
                }
            }
        }

        // bind the generated shadow map and render it to a quad for debugging in-editor
        shadowTexture.bind();
        quadFramebuffer.bind();
        Renderer::Clear({ 1,0,0,1 });
        quadShader->bind();
        Quad->render();
        quadShader->unbind();
        shadowTexture.unbind();
        quadFramebuffer.unbind();

        // start G-Buffer pass
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glViewport(0, 0, renderWidth, renderHeight);
        GBuffer.bind();

        //// render a cubemap with depth testing disabled to generate a skybox
        //// update the camera without translation
        //skyShader->bind();
        //skyShader->getUniform("view") = glm::mat4(glm::mat3(camera.getView()));
        //skyShader->getUniform("proj") = camera.getProjection();

        //sky_image->bind(0);
        //skycube->bind();
        //Renderer::DrawIndexed(skycube->get_index_buffer()->get_count(), false);

        GBufferShader->bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        GBufferShader->getUniform("projection") = camera.getProjection();
        GBufferShader->getUniform("view") = camera.getView();

        // render the scene 
        for (auto& object : scene.objects) {
            if (!object.hasAlpha) {
                GBufferShader->getUniform("model") = object.transform;
                object.render();
            }
        }

        for (auto& object : scene.objects) {
            if (object.hasAlpha) {
                GBufferShader->getUniform("model") = object.transform;
                object.render();
            }
        }

        GBuffer.unbind();

        // bind the main framebuffer
        hdrFramebuffer.bind();
        glViewport(0, 0, renderWidth, renderHeight);
        Renderer::Clear({ 0.0f, 0.0f, 0.0f, 1.0f });

        // set uniforms
        mainShader->bind();
        mainShader->getUniform("sunColor") = uniforms.sunColor;
        mainShader->getUniform("minBias") = uniforms.minBias;
        mainShader->getUniform("maxBias") = uniforms.maxBias;
        mainShader->getUniform("farPlane") = farPlane;
        mainShader->getUniform("bloomThreshold") = bloomThreshold;
        
        // bind textures to shader binding slots
        shadowTexture.bindToSlot(0);
        depthCubeTexture.bindToSlot(1);
        positionTexture.bindToSlot(2);
        albedoTexture.bindToSlot(3);
        normalTexture.bindToSlot(4);

        ubo.view = camera.getView();
        ubo.projection = camera.getProjection();
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(lightmatrix), lightPos, lightRot, lightScale);
        ubo.pointLightPos = glm::vec4(glm::make_vec3(lightPos), 1.0f);
        ubo.DirLightPos = glm::vec4(sunCamera.getPosition(), 1.0);
        ubo.DirViewPos = glm::vec4(camera.getPosition(), 1.0);
        ubo.lightSpaceMatrix = sunCamera.getProjection() * sunCamera.getView();

        dxrb->update(&ubo, sizeof(VertexUBO));
        dxrb->bind(0);

        Quad->render();

        if (wireframeOnly) {
            // render collision shapes
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            for (auto& object : scene.objects) {
                //if (object.name == "Mesh.366-1") {
                mainShader->getUniform("model") = object.transform;
                object.collisionRenderable->render();
                //}
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // unbind the main frame buffer
        hdrFramebuffer.unbind();

        // perform gaussian blur on the bloom texture using "ping pong" framebuffers that
        // take each others color attachments as input and perform a directional gaussian blur each
        // iteration
        bool horizontal = true, firstIteration = true;
        blurShader->bind();
        for (unsigned int i = 0; i < 10; i++) {
            pingpongFramebuffers[horizontal].bind();
            blurShader->getUniform("horizontal") = horizontal;
            if (firstIteration) {
                bloomTexture.bindToSlot(0);
                firstIteration = false;
            } else {
                pingpongTextures[!horizontal].bindToSlot(0);
            }
            Quad->render();
            horizontal = !horizontal;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // blend the bloom and scene texture together
        preprocessFramebuffer.bind();
        bloomShader->bind();
        hdrTexture.bindToSlot(0);
        pingpongTextures[!horizontal].bindToSlot(1);
        Quad->render();
        preprocessFramebuffer.unbind();

        
        static bool hdr = true;
        if (hdr) {
            finalFramebuffer.bind();
            glViewport(0, 0, renderWidth, renderHeight);
            hdrShader->bind();
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            hdrUbo->update(&hdr_ubo, sizeof(HDR_UBO));
            hdrUbo->bind(0);
            if (doBloom) {
                preprocessTexture.bindToSlot(0);
            } else {
                hdrTexture.bindToSlot(0);
            }
            Quad->render();
            finalFramebuffer.unbind();
        }

        //get new frame for ImGui and ImGuizmo
        Renderer::ImGuiNewFrame(directxwindow);
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

        // model panel
        ImGui::Begin("Entities");
        if (ImGui::IsWindowFocused()) {
            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), false)) {}
        }
        auto tree_node_flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (ImGui::TreeNodeEx("Objects", tree_node_flags)) {
            ImGui::Columns(1, NULL, false);
            // draw a selectable for every object in the scene
            unsigned int uniqueID = 0;
            for (auto& object : scene.objects) {
                    bool selected = activeObject->name == object.name;
                    std::string selectableName = object.name + "##" + std::to_string(uniqueID);
                    if (ImGui::Selectable(object.name.c_str(), selected)) {
                        activeObject = scene.at(object.name);
                    }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                uniqueID = uniqueID + 1;
            }
            ImGui::TreePop();
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

        if (ImGui::RadioButton("Wireframe", wireframeOnly)) {
            wireframeOnly = !wireframeOnly;
        }

        if (ImGui::RadioButton("Frustrum Culling", frustrumCulling)) {
            frustrumCulling = !frustrumCulling;
        }
        
        if (ImGui::RadioButton("Animate Light", move_light)) {
            move_light = !move_light;
        }

        if (ImGui::RadioButton("Debug shadows", debugShadows)) {
            debugShadows = !debugShadows;
        }

        if (ImGui::Button("Reload shaders")) {
            mainShader.reset(new GLShader(modelStages.data(), modelStages.size()));
        }

        ImGui::NewLine(); 

        ImGui::Text("Directional Light");
        ImGui::Separator();
        if (ImGui::DragFloat2("Angle", glm::value_ptr(sunCamera.getAngle()), 0.01f)) {}
        
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
        ImGui::Text("Post-processing");
        ImGui::Separator();
        if (ImGui::Checkbox("HDR", &hdr)) {}
        ImGui::SameLine();
        if (ImGui::Checkbox("Bloom", &doBloom)) {}

        static bool doNormalMapping = true;
        if (ImGui::Checkbox("Normal mapping", &doNormalMapping)) {
            if (!doNormalMapping) {
                gbuffer_shaders[0].defines = { "NO_NORMAL_MAP" };
                gbuffer_shaders[1].defines = { "NO_NORMAL_MAP" };
            }
            else {
                gbuffer_shaders[0].defines.clear();
                gbuffer_shaders[1].defines.clear();
            }
            GBufferShader.reset(new GLShader(gbuffer_shaders.data(), gbuffer_shaders.size()));
        }

        if (ImGui::DragFloat3("Bloom Threshold", glm::value_ptr(bloomThreshold), 0.001f, 0.0f, 10.0f)) {}

        static const char* current = tonemappingShaders.begin()->c_str();
        if (ImGui::BeginCombo("Tonemapping", current)) {
            for (auto it = tonemappingShaders.begin(); it != tonemappingShaders.end(); it++) {
                bool selected = (it->c_str() == current);
                if (ImGui::Selectable(it->c_str(), selected)) {
                    current = it->c_str();

                    hdr_shaders.clear();
                    hdr_shaders.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\HDR.vert");
                    hdr_shaders.emplace_back(Shader::Type::FRAG, current);
                    hdrShader.reset(new GLShader(hdr_shaders.data(), hdr_shaders.size()));
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::SliderFloat("Exposure", &hdr_ubo.exposure, 0.0f, 1.0f)) {}
        if (ImGui::SliderFloat("Gamma", &hdr_ubo.gamma, 1.0f, 3.2f)) {}

        ImGui::End();

        ImGui::ShowMetricsWindow();

        ImGui::Begin("Camera Properties");
        if (ImGui::DragFloat("FOV", &fov, 1.0f, 35.0f, 120.0f)) {
            camera.getProjection() = glm::perspectiveRH(glm::radians(fov), 16.0f / 9.0f, 0.1f, 100.0f);
        }
        if (ImGui::DragFloat("Camera Move Speed", camera.get_move_speed(), 0.001f, 0.001f, FLT_MAX, "%.3f")) {}
        if (ImGui::DragFloat("Camera Look Speed", camera.get_look_speed(), 0.0001f, 0.0001f, FLT_MAX, "%.4f")) {}
        ImGui::End();



        // if the scene containt at least one model, AND the active model is pointing at a valid model,
        // AND the active model has a mesh to modify, the properties window draws
        static ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE;
        if (!scene.objects.empty() && activeObject != scene.objects.end()) {
            ImGui::Begin("Mesh Properties");

            std::array<const char*, 3> previews = {
                "TRANSLATE", "ROTATE", "SCALE"
            };
            static ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE;
            if (ImGui::BeginCombo("Gizmo mode", previews[operation])) {
                for (int i = 0; i < previews.size(); i++) {
                    bool selected = (i == operation);
                    if (ImGui::Selectable(previews[i], selected)) {
                        operation = (ImGuizmo::OPERATION)i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // resets the model's transformation
            if (ImGui::Button("Reset")) {
                activeObject->reset_transform();
            }
            ImGui::End();

            // draw the imguizmo at the center of the light
            if (!move_light)
                ImGuizmo::Manipulate(glm::value_ptr(camera.getView()), glm::value_ptr(camera.getProjection()), operation, ImGuizmo::MODE::WORLD, glm::value_ptr(lightmatrix));
            else
                ImGuizmo::Manipulate(glm::value_ptr(camera.getView()), glm::value_ptr(camera.getProjection()), operation, ImGuizmo::MODE::WORLD, glm::value_ptr(activeObject->transform));

        }

        // renderer viewport
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Renderer", NULL, ImGuiWindowFlags_AlwaysAutoResize);
        static bool resizing = false;
        auto size = ImGui::GetWindowSize();
        if (renderWidth != size.x || renderHeight != size.y - 25) {
            resizing = true;
            renderWidth = static_cast<uint32_t>(size.x), renderHeight = static_cast<uint32_t>(size.y - 25.0f);
        }
        auto pos = ImGui::GetWindowPos();

        bloomTexture.bind();
        bloomTexture.init(renderWidth, renderHeight, Format::HDR);
        bloomTexture.unbind();


        camera.getProjection() = glm::perspectiveRH(glm::radians(fov), (float)renderWidth / (float)renderHeight, 0.1f, 100.0f);
        ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
        // function that calls an ImGui image with the framebuffer's color stencil data
        if (debugShadows) {
            ImGui::Image(quadTexture.ImGuiID(), ImVec2(SHADOW_WIDTH, SHADOW_HEIGHT), { 0,1 }, { 1,0 });
        }
        else {
            if (hdr) 
                ImGui::Image(finalTexture.ImGuiID(), ImVec2((float)renderWidth, (float)renderHeight), { 0,1 }, { 1,0 });
            else 
                ImGui::Image(hdrTexture.ImGuiID(), ImVec2((float)renderWidth, (float)renderHeight), { 0,1 }, { 1,0 });
        }

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::End();
        Renderer::ImGuiRender();
        Renderer::SwapBuffers(use_vsync);

        if (resizing) {
            // resizing
            hdrTexture.bind();
            hdrTexture.init(renderWidth, renderHeight, Format::HDR);

            hdrRenderbuffer.init(renderWidth, renderHeight, GL_DEPTH32F_STENCIL8);

            finalTexture.bind();
            finalTexture.init(renderWidth, renderHeight, Format::SDR);

            finalRenderbuffer.init(renderWidth, renderHeight, GL_DEPTH32F_STENCIL8);

            bloomTexture.bind();
            bloomTexture.init(renderWidth, renderHeight, Format::HDR);

            for (unsigned int i = 0; i < 2; i++) {
                pingpongTextures[i].bind();
                pingpongTextures[i].init(renderWidth, renderHeight, Format::HDR);
                pingpongTextures[i].unbind();
            }

            resizing = false;
        }

        dt_timer.stop();
        dt = dt_timer.elapsed_ms();

    } // while true loop

    display = SDL_GetWindowDisplayIndex(directxwindow);
    //clean up SDL
    SDL_DestroyWindow(directxwindow);
    SDL_Quit();
} // main

} // namespace Raekor  