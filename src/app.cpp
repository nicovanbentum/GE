#include "pch.h"
#include "app.h"
#include "ecs.h"
#include "util.h"
#include "scene.h"
#include "mesh.h"
#include "entry.h"
#include "serial.h"
#include "camera.h"
#include "shader.h"
#include "script.h"
#include "framebuffer.h"
#include "platform/OS.h"
#include "renderer.h"
#include "buffer.h"
#include "timer.h"
#include "renderpass.h"
#include "gui.h"

namespace Raekor {

void Application::serializeSettings(const std::string& filepath, bool write) {
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
    // retrieve the application settings from the config file
    serializeSettings("config.json");

    int sdlError = SDL_Init(SDL_INIT_VIDEO);
    m_assert(sdlError == 0, "failed to init SDL for video");



    Uint32 wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL |
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED;

    // init scripting language
    auto chai = create_chaiscript();

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

    SDL_SetWindowInputFocus(directxwindow);

    Viewport viewport = Viewport(displays[display]);

    // initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // create the renderer object that does sets up the API and does all the runtime magic
    Renderer::setAPI(RenderAPI::OPENGL);
    Renderer::Init(directxwindow);

    std::unique_ptr<Mesh> cube;
    cube.reset(new Mesh(Shape::Cube));
    cube->getVertexBuffer()->setLayout({
        {"POSITION",    ShaderType::FLOAT3},
        {"UV",          ShaderType::FLOAT2},
        {"NORMAL",      ShaderType::FLOAT3},
        {"TANGENT",     ShaderType::FLOAT3},
        {"BINORMAL",    ShaderType::FLOAT3},
    });

    std::unique_ptr<Mesh> unitCube;
    unitCube.reset(new Mesh());
    unitCube->setVertexBuffer(unitCubeVertices);
    unitCube->setIndexBuffer(cubeIndices);
    unitCube->getVertexBuffer()->setLayout({
        {"POSITION",    ShaderType::FLOAT3},
        {"UV",          ShaderType::FLOAT2},
        {"NORMAL",      ShaderType::FLOAT3},
        {"TANGENT",     ShaderType::FLOAT3},
        {"BINORMAL",    ShaderType::FLOAT3},
    });

    std::unique_ptr<Mesh> Quad;
    Quad.reset(new Mesh(Shape::Quad));
    Quad->getVertexBuffer()->setLayout({
        {"POSITION",    ShaderType::FLOAT3},
        {"UV",          ShaderType::FLOAT2},
        {"NORMAL",      ShaderType::FLOAT3},
        {"TANGENT",     ShaderType::FLOAT3},
        {"BINORMAL",    ShaderType::FLOAT3},
    });

    viewport.size.x = 2003, viewport.size.y = 1370;
    constexpr unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;

    // get GUI i/o and set a bunch of settings
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDockingWithShift = true;

    // set UI font that's saved in config 
    ImFont* pFont = io.Fonts->AddFontFromFileTTF(font.c_str(), 15.0f);
    if (!io.Fonts->Fonts.empty()) {
        io.FontDefault = io.Fonts->Fonts.back();
    }

    // load the UI's theme from config
    ImVec4* colors = ImGui::GetStyle().Colors;
    for (unsigned int i = 0; i < themeColors.size(); i++) {
        auto& savedColor = themeColors[i];
        colors[i] = ImVec4(savedColor[0], savedColor[1], savedColor[2], savedColor[3]);
    }

    colors[ImGuiCol_DockingEmptyBg] = ImVec4(1, 0, 0, 1);

    ImGui::GetStyle().WindowRounding = 0.0f;
    ImGui::GetStyle().ChildRounding = 0.0f;
    ImGui::GetStyle().FrameRounding = 0.0f;
    ImGui::GetStyle().GrabRounding = 0.0f;
    ImGui::GetStyle().PopupRounding = 0.0f;
    ImGui::GetStyle().ScrollbarRounding = 0.0f;
    ImGui::GetStyle().WindowBorderSize = 0.0f;
    ImGui::GetStyle().ChildBorderSize = 0.0f;
    ImGui::GetStyle().FrameBorderSize = 0.0f;

    // timer for keeping frametime
    Timer deltaTimer;
    double deltaTime = 0;

    // setup the camera that acts as the sun's view (directional light)
    std::cout << "Creating render passes..." << std::endl;

    // all render passes
    auto lightingPass           = std::make_unique<RenderPass::DeferredLighting>(viewport);
    auto shadowMapPass          = std::make_unique<RenderPass::ShadowMap>(SHADOW_WIDTH, SHADOW_HEIGHT);
    auto tonemappingPass        = std::make_unique<RenderPass::Tonemapping>(viewport);
    auto geometryBufferPass     = std::make_unique<RenderPass::GeometryBuffer>(viewport);
    auto aabbDebugPass          = std::make_unique<RenderPass::BoundingBoxDebug>(viewport);
    auto ConeTracePass          = std::make_unique<RenderPass::ForwardLightingPass>(viewport);
    auto voxelizePass           = std::make_unique<RenderPass::Voxelization>(128);
    auto voxelDebugPass         = std::make_unique<RenderPass::VoxelizationDebug>(viewport);
    auto skyPass                = std::make_unique<RenderPass::SkyPass>(viewport);
    auto skinningPass            = std::make_unique<RenderPass::Skinning>();


    // boolean settings needed for a couple things
    bool doSSAO = false, doBloom = false, debugVoxels = false, doDeferred = true;
    bool mouseInViewport = false, gizmoEnabled = false, showSettingsWindow = false;

    // keep a pointer to the texture that's rendered to the window
    glTexture2D* activeScreenTexture = &tonemappingPass->result;

    int coreCount = std::thread::hardware_concurrency();
    int threadCount = std::max(1, coreCount - 1);
    auto dispatcher = AsyncDispatcher(threadCount);

    AssimpImporter importer;
    entt::registry scene;
    entt::registry assets;
    static entt::entity active = entt::null;

    std::cout << "Initialization done." << std::endl;

    SDL_ShowWindow(directxwindow);
    SDL_MaximizeWindow(directxwindow);

    gui::Guizmo gizmo;
    gui::EntityWindow ecsWindow;
    gui::ConsoleWindow consoleWindow;
    gui::InspectorWindow inspectorWindow;
    gui::AssetBrowser assetBrowser;

    bool shouldVoxelize = true;

    while (running) {
        deltaTimer.start();

        updateTransforms(scene);

        scene.view<ecs::MeshAnimationComponent, ecs::MeshComponent>().each([&](auto& animation, auto& mesh) {
            animation.boneTransform(static_cast<float>(deltaTime));
            skinningPass->execute(mesh, animation);
        });

        handleEvents(directxwindow, viewport.getCamera(), mouseInViewport, deltaTime);
        viewport.getCamera().update(true);

        // clear the main window
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // generate sun shadow map 
            glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
            shadowMapPass->execute(scene);

            if (shouldVoxelize) {
                voxelizePass->execute(scene, viewport, shadowMapPass.get());
            }
        
            glViewport(0, 0, viewport.size.x, viewport.size.y);

            if (doDeferred) {
                if (!scene.view<ecs::MeshComponent>().empty()) {
                    geometryBufferPass->execute(scene, viewport);
                    lightingPass->execute(scene, viewport, shadowMapPass.get(), nullptr, geometryBufferPass.get(), nullptr, voxelizePass.get(), Quad.get());
                    tonemappingPass->execute(lightingPass->result, Quad.get());
                }
            } else {
                if (!scene.view<ecs::MeshComponent>().empty()) {
                    ConeTracePass->execute(viewport, scene, voxelizePass.get(), shadowMapPass.get());
                    tonemappingPass->execute(ConeTracePass->result, Quad.get());
                }
            }

            if (active != entt::null) {
                aabbDebugPass->execute(scene, viewport, tonemappingPass->result, geometryBufferPass->GDepthBuffer,  active);
            }

            if (debugVoxels) {
                voxelDebugPass->execute(viewport, tonemappingPass->result, voxelizePass.get());
            }

        //get new frame for ImGui and ImGuizmo
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        Renderer::ImGuiNewFrame(directxwindow);
        ImGuizmo::BeginFrame();

        if (ImGui::IsAnyItemActive()) {
            // perform input mapping
        }

        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags dockWindowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        ImGuiViewport* imGuiViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(imGuiViewport->Pos);
        ImGui::SetNextWindowSize(imGuiViewport->Size);
        ImGui::SetNextWindowViewport(imGuiViewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        dockWindowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, 
        // so we ask Begin() to not render a background.
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) dockWindowFlags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        static bool p_open = true;
        ImGui::Begin("DockSpace", &p_open, dockWindowFlags);
        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);

        // DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        // move the light by a fixed amount and let it bounce between -125 and 125 units/pixels on the x axis
        static double lightMoveSpeed = 0.003;
        static double bounds = 7.5f;
        static bool moveLight = false;
        double lightMoveAmount = lightMoveSpeed * deltaTime;

        // draw the top user bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open scene..")) {
                    std::string path = OS::openFileDialog("Scene Files (*.scene)\0*.scene\0");
                    if (!path.empty()) {
                        std::ifstream storage(path, std::ios::binary);
                        cereal::BinaryInputArchive input(storage);
                        scene.clear();
                        entt::snapshot_loader{ scene }.entities(input).component<
                            ecs::NameComponent, 
                            ecs::NodeComponent, 
                            ecs::TransformComponent,
                            ecs::MeshComponent,
                            ecs::MaterialComponent,
                            ecs::PointLightComponent,
                            ecs::DirectionalLightComponent>(input);
                        loadAssetsFromDisk(scene, dispatcher);
                        auto view = scene.view<ecs::MeshComponent>();
                        for (auto entity : view) {
                            auto& mesh = view.get<ecs::MeshComponent>(entity);
                            mesh.generateAABB();
                            mesh.uploadVertices();
                            mesh.uploadIndices();
                        }
                    }
                }

                if (ImGui::MenuItem("Save scene..", "CTRL + S")) {
                    std::string savePath = OS::saveFileDialog("Scene File (*.scene)\0", "scene");

                    if (!savePath.empty()) {
                        std::ofstream storage(savePath, std::ios::binary);
                        cereal::BinaryOutputArchive output(storage);
                        entt::snapshot{ scene }.entities(output).component<
                            ecs::NameComponent, 
                            ecs::NodeComponent, 
                            ecs::TransformComponent, 
                            ecs::MeshComponent, 
                            ecs::MaterialComponent,
                            ecs::PointLightComponent,
                            ecs::DirectionalLightComponent>(output);
                    }
                }

                if (ImGui::MenuItem("Load model..")) {
                    std::string path = OS::openFileDialog("Supported Files(*.gltf, *.fbx, *.obj)\0*.gltf;*.fbx;*.obj\0");
                    if (!path.empty()) {
                        importer.loadFromDisk(scene, path, assets, dispatcher);
                    }
                }

                if (ImGui::MenuItem("Save Screenshot..")) {
                    std::string savePath = OS::saveFileDialog("Uncompressed PNG (*.png)\0", "png");

                    if (!savePath.empty()) {
                        const auto bufferSize = 4 * viewport.size.x * viewport.size.y;
                        unsigned char* pixels = new unsigned char[bufferSize];
                        glGetTextureImage(tonemappingPass->result, 0, GL_RGBA, GL_UNSIGNED_BYTE, bufferSize* sizeof(unsigned char), pixels);
                        stbi_flip_vertically_on_write(true);
                        stbi_write_png(savePath.c_str(), viewport.size.x, viewport.size.y, 4, pixels, viewport.size.x * 4);
                    }

                }

                if (ImGui::MenuItem("Exit", "Escape")) {
                    running = false;
                }

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Delete", "DEL")) {
                    // on press we remove the scene object
                    if (active != entt::null) {
                        destroyNode(scene, active);
                        active = entt::null;
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Add")) {
                if (ImGui::MenuItem("Empty", "CTRL+E")) {
                    auto entity = createEmpty(scene, "Empty");

                    if (active != entt::null) {
                        auto& node = scene.get<ecs::NodeComponent>(entity);
                        node.parent = active;
                        node.hasChildren = false;
                        scene.get<ecs::NodeComponent>(node.parent).hasChildren = true;
                    }
                }
                ImGui::Separator();

                if (ImGui::BeginMenu("Light")) {

                    if (ImGui::MenuItem("Directional Light")) {
                        auto entity = createEmpty(scene, "Directional Light");
                        scene.emplace<ecs::DirectionalLightComponent>(entity);
                    }

                    if (ImGui::MenuItem("Point Light")) {
                        auto entity = createEmpty(scene, "Point Light");
                        scene.emplace<ecs::PointLightComponent>(entity);
                    }

                    ImGui::EndMenu();
                }


                ImGui::EndMenu();
            }


            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), true)) {
                if (scene.valid(active)) {
                    destroyNode(scene, active);
                    active = entt::null;
                }
            }

            static bool takeScreenshot = false;
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About", "")) {}
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        assetBrowser.drawWindow(assets, active);

        // chai console panel
        consoleWindow.Draw(chai.get());

        //Inspector panel
        inspectorWindow.draw(scene, active);

        // scene / ecs panel
        ecsWindow.draw(scene, active);

        // post processing panel
        ImGui::Begin("Post Processing");
        static bool doTonemapping = true;
        if (ImGui::Checkbox("Tonemap", &doTonemapping)) {
            if (doTonemapping) {
                activeScreenTexture = &tonemappingPass->result;
            } else {
                activeScreenTexture = &lightingPass->result;
            }
        }
        ImGui::Separator();

        if (ImGui::SliderFloat("Exposure", &tonemappingPass->settings.exposure, 0.0f, 1.0f)) {}
        if (ImGui::SliderFloat("Gamma", &tonemappingPass->settings.gamma, 1.0f, 3.2f)) {}
        ImGui::NewLine();

        if (ImGui::Checkbox("Bloom", &doBloom)) {}
        ImGui::Separator();

        if (ImGui::DragFloat3("Threshold", glm::value_ptr(lightingPass->settings.bloomThreshold), 0.001f, 0.0f, 10.0f)) {}
        ImGui::NewLine();

        ImGui::End();

        // scene panel
        ImGui::Begin("Random");
        ImGui::SetItemDefaultFocus();

        // toggle button for openGl vsync
        static bool doVsync = true;
        if (ImGui::RadioButton("Vsync", doVsync)) {
            doVsync = !doVsync;
        }

        ImGui::NewLine(); ImGui::Separator(); 
        ImGui::Text("Voxel Cone Tracing");

        if (ImGui::RadioButton("Debug", debugVoxels)) {
            debugVoxels = !debugVoxels;
        }

        if (ImGui::RadioButton("Voxelize", shouldVoxelize)) {
            shouldVoxelize = !shouldVoxelize;
        }

        if (ImGui::RadioButton("Deferred", doDeferred)) {
            doDeferred = !doDeferred;
        }

        ImGui::DragFloat("World size", &voxelizePass->worldSize, 0.05f, 1.0f, FLT_MAX, "%.2f");

        ImGui::Separator();

        if (ImGui::TreeNode("Screen Texture")) {
            if (ImGui::Selectable(nameof(tonemappingPass->result), activeScreenTexture->ImGuiID() == tonemappingPass->result.ImGuiID()))
                activeScreenTexture = &tonemappingPass->result;
            if (ImGui::Selectable(nameof(geometryBufferPass->albedoTexture), activeScreenTexture->ImGuiID() == geometryBufferPass->albedoTexture.ImGuiID()))
                activeScreenTexture = &geometryBufferPass->albedoTexture;
            if (ImGui::Selectable(nameof(geometryBufferPass->normalTexture), activeScreenTexture->ImGuiID() == geometryBufferPass->normalTexture.ImGuiID()))
                activeScreenTexture = &geometryBufferPass->normalTexture;
            if (ImGui::Selectable(nameof(geometryBufferPass->positionTexture), activeScreenTexture->ImGuiID() == geometryBufferPass->positionTexture.ImGuiID()))
                activeScreenTexture = &geometryBufferPass->positionTexture;
            if (ImGui::Selectable(nameof(geometryBufferPass->materialTexture), activeScreenTexture->ImGuiID() == geometryBufferPass->materialTexture.ImGuiID()))
                activeScreenTexture = &geometryBufferPass->materialTexture;
            if (ImGui::Selectable(nameof(lightingPass->result), activeScreenTexture->ImGuiID() == lightingPass->result.ImGuiID()))
                activeScreenTexture = &lightingPass->result;
            if (ImGui::Selectable(nameof(aabbDebugPass->result), activeScreenTexture->ImGuiID() == aabbDebugPass->result.ImGuiID()))
                activeScreenTexture = &aabbDebugPass->result;
            if (ImGui::Selectable(nameof(ConeTracePass->result), activeScreenTexture->ImGuiID() == ConeTracePass->result.ImGuiID()))
                activeScreenTexture = &ConeTracePass->result;
            if (ImGui::Selectable(nameof(skyPass->result), activeScreenTexture->ImGuiID() == skyPass->result.ImGuiID()))
                activeScreenTexture = &skyPass->result;

            ImGui::TreePop();
        }

        ImGui::NewLine();

        ImGui::Text("Shadow Mapping");
        ImGui::Separator();
        
        if (ImGui::DragFloat2("Planes", glm::value_ptr(shadowMapPass->settings.planes), 0.1f)) {}
        if (ImGui::DragFloat("Size", &shadowMapPass->settings.size)) {}
        if (ImGui::DragFloat("Min bias", &lightingPass->settings.minBias, 0.0001f, 0.0f, FLT_MAX, "%.4f")) {}
        if (ImGui::DragFloat("Max bias", &lightingPass->settings.maxBias, 0.0001f, 0.0f, FLT_MAX, "%.4f")) {}
        
        ImGui::NewLine();
        ImGui::Separator();
        ImGui::NewLine();
        ImGui::Text("Sky Settings");
        ImGui::DragFloat("time", &skyPass->settings.time, 0.01f, 0.0f, 1000.0f);
        ImGui::DragFloat("cumulus", &skyPass->settings.cumulus, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("cirrus", &skyPass->settings.cirrus, 0.01f, 0.0f, 1.0f);
        ImGui::NewLine();

        ImGui::End();

        ImGui::Begin("Camera Properties");
        if (ImGui::DragFloat("FoV", &viewport.getFov(), 1.0f, 35.0f, 120.0f)) {
            viewport.setFov(viewport.getFov());
        }
        if (ImGui::DragFloat("Move Speed", &viewport.getCamera().moveSpeed, 0.001f, 0.001f, FLT_MAX, "%.4f")) {}
        if (ImGui::DragFloat("Move Constant", &viewport.getCamera().moveConstant, 0.001f, 0.001f, FLT_MAX, "%.4f")) {}
        if (ImGui::DragFloat("Look Speed", &viewport.getCamera().lookSpeed, 0.1f, 0.0001f, FLT_MAX, "%.4f")) {}
        if (ImGui::DragFloat("Look Constant", &viewport.getCamera().lookConstant, 0.001f, 0.001f, FLT_MAX, "%.4f")) {}
        if (ImGui::DragFloat("Zoom Speed", &viewport.getCamera().zoomSpeed, 0.001f, 0.0001f, FLT_MAX, "%.4f")) {}
        if (ImGui::DragFloat("Zoom Constant", &viewport.getCamera().zoomConstant, 0.001f, 0.001f, FLT_MAX, "%.4f")) {}

        ImGui::End();

        gizmo.drawWindow();
        
        // renderer viewport
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Renderer", NULL, ImGuiWindowFlags_AlwaysAutoResize);

        // figure out if we need to resize the viewport
        static bool resizing = false;
        auto size = ImGui::GetContentRegionAvail();
        if (viewport.size.x != size.x || viewport.size.y != size.y) {
            resizing = true;
            viewport.size.x = static_cast<uint32_t>(size.x), viewport.size.y = static_cast<uint32_t>(size.y);
        }
        auto pos = ImGui::GetWindowPos();

        // determine if the mouse is hovering the viewport 
        if (ImGui::IsWindowHovered()) {
            mouseInViewport = true;
        } else {
            mouseInViewport = false;
        }

        if (io.MouseClicked[0] && mouseInViewport && !ImGuizmo::IsOver(gizmo.getOperation())) {
            
            std::puts("picking");
            // get mouse position in window
            glm::ivec2 mousePosition;
            SDL_GetMouseState(&mousePosition.x, &mousePosition.y);

            // get mouse position relative to viewport
            glm::ivec2 rendererMousePosition = { (mousePosition.x - pos.x), (mousePosition.y - pos.y) };

            // flip mouse coords for opengl
            rendererMousePosition.y = viewport.size.y - rendererMousePosition.y;

            entt::entity picked = entt::null;
            if (doDeferred) {
                picked = geometryBufferPass->pick(rendererMousePosition.x, rendererMousePosition.y);
            } else {
                picked = ConeTracePass->pick(rendererMousePosition.x, rendererMousePosition.y);
            }

            if (scene.valid(picked)) {
                active = active == picked ? entt::null : picked;
            } else {
                active = entt::null;
            }
        }

        // render the active screen texture to the view port as an imgui image
        ImGui::Image(activeScreenTexture->ImGuiID(), ImVec2((float)viewport.size.x, (float)viewport.size.y), { 0,1 }, { 1,0 });

        // draw the imguizmo at the center of the active entity
        if (active != entt::null) {
            gizmo.drawGuizmo(scene, viewport, active);
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // application/render metrics
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::SetNextWindowPos(ImVec2(pos.x + size.x - size.x / 7.5f - 5.0f, pos.y + 5.0f));
        ImGui::SetNextWindowSize(ImVec2(size.x / 7.5f, size.y / 9.0f));
        auto metricWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration;
        ImGui::Begin("GPU Metrics", (bool*)0, metricWindowFlags);
        ImGui::Text("Vendor: %s", glGetString(GL_VENDOR));
        ImGui::Text("Product: %s", glGetString(GL_RENDERER));
        ImGui::Text("Resolution: %i x %i", viewport.size.x, viewport.size.y);
        ImGui::Text("Frame %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        int culledCount = doDeferred ? geometryBufferPass->culled : ConeTracePass->culled;
        ImGui::Text("Culling: %i of %i meshes", culledCount, scene.view<ecs::MeshComponent>().size());
        ImGui::Text("Graphics API: OpenGL %s", glGetString(GL_VERSION));
        ImGui::End();


        ImGui::End();
        Renderer::ImGuiRender();
        Renderer::SwapBuffers(doVsync);

        if (resizing) {
            // adjust the camera and gizmo
            viewport.getCamera().getProjection() = glm::perspectiveRH(glm::radians(viewport.getFov()), (float)viewport.size.x / (float)viewport.size.y, 0.1f, 10000.0f);
            ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);

            // resizing framebuffers
            tonemappingPass->resize(viewport);
            geometryBufferPass->resize(viewport);
            lightingPass->resize(viewport);
            aabbDebugPass->resize(viewport);
            voxelDebugPass->resize(viewport);
            ConeTracePass->resize(viewport);

            
            resizing = false;
        }

        deltaTimer.stop();
        deltaTime = deltaTimer.elapsedMs();

    } // while true loop

    display = SDL_GetWindowDisplayIndex(directxwindow);
    //clean up SDL
    SDL_DestroyWindow(directxwindow);
    SDL_Quit();

} // application::run

} // namespace Raekor  