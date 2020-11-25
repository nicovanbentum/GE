#include "pch.h"
#include "editor.h"
#include "gui.h"
#include "input.h"
#include "platform/OS.h"

namespace Raekor {

EditorOpenGL::EditorOpenGL() : WindowApplication(RenderAPI::OPENGL), renderer(window) {
    // gui stuff
    gui::setFont(settings.font.c_str());
    gui::setTheme(settings.themeColors);

    // create quad for render passes
    for (const auto& v : quadVertices) {
        quad.positions.push_back(v.pos);
        quad.uvs.push_back(v.uv);
        quad.normals.push_back(v.normal);
        quad.tangents.push_back(v.tangent);
        quad.bitangents.push_back(v.binormal);
    }

    for (const auto& i : quadIndices) {
        quad.indices.push_back(i.p1);
        quad.indices.push_back(i.p2);
        quad.indices.push_back(i.p3);
    }

    quad.uploadVertices();
    quad.uploadIndices();

    skinningPass            = std::make_unique<RenderPass::Skinning>();
    voxelizationPass        = std::make_unique<RenderPass::Voxelization>(128);
    shadowMapPass           = std::make_unique<RenderPass::ShadowMap>(4096, 4096);
    tonemappingPass         = std::make_unique<RenderPass::Tonemapping>(viewport);
    geometryBufferPass      = std::make_unique<RenderPass::GeometryBuffer>(viewport);
    fowardLightingPass      = std::make_unique<RenderPass::ForwardLighting>(viewport);
    DeferredLightingPass    = std::make_unique<RenderPass::DeferredLighting>(viewport);
    boundingBoxDebugPass    = std::make_unique<RenderPass::BoundingBoxDebug>(viewport);
    voxelizationDebugPass   = std::make_unique<RenderPass::VoxelizationDebug>(viewport);
    bloomPass               = std::make_unique<RenderPass::Bloom>(viewport);

    // keep a pointer to the texture that's rendered to the window
    activeScreenTexture = tonemappingPass->result;

    std::cout << "Initialization done." << std::endl;

    if (std::filesystem::is_regular_file(settings.defaultScene)) {
        if (std::filesystem::path(settings.defaultScene).extension() == ".scene") {
            SDL_SetWindowTitle(window, std::string(settings.defaultScene + " - Raekor Renderer").c_str());
            scene.openFromFile(settings.defaultScene);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////

void EditorOpenGL::update(double dt) {
    InputHandler::handleEvents(this, mouseInViewport, dt);

    scene.updateTransforms();
    
    auto animationView = scene->view<ecs::MeshAnimationComponent>();
    std::for_each(std::execution::par_unseq, animationView.begin(), animationView.end(), [&](auto entity) {
        auto& animation = animationView.get<ecs::MeshAnimationComponent>(entity);
        animation.boneTransform(static_cast<float>(dt));
    });

    scene->view<ecs::MeshAnimationComponent, ecs::MeshComponent>().each([&](auto& animation, auto& mesh) {
        skinningPass->execute(mesh, animation);
    });

    viewport.getCamera().update(true);

    // clear the main window
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // generate sun shadow map 
    glViewport(0, 0, 4096, 4096);
    shadowMapPass->execute(scene);

    if (shouldVoxelize) {
        voxelizationPass->execute(scene, viewport, shadowMapPass.get());
    }

    glViewport(0, 0, viewport.size.x, viewport.size.y);

    geometryBufferPass->execute(scene, viewport);
    DeferredLightingPass->execute(scene, viewport, shadowMapPass.get(), nullptr, geometryBufferPass.get(), nullptr, voxelizationPass.get(), quad);
    tonemappingPass->execute(DeferredLightingPass->result, quad, DeferredLightingPass->bloomHighlights);

    if (active != entt::null) {
        boundingBoxDebugPass->execute(scene, viewport, tonemappingPass->result, geometryBufferPass->depthTexture, active);
    }

    if (debugVoxels) {
        voxelizationDebugPass->execute(viewport, tonemappingPass->result, voxelizationPass.get());
    }

    //bloomPass->execute(viewport, DeferredLightingPass->bloomHighlights);

    //get new frame for ImGui and ImGuizmo
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    renderer.ImGui_NewFrame(window);
    ImGuizmo::BeginFrame();

    if (ImGui::IsAnyItemActive()) {
        // perform input mapping
    }

    dockspace.begin();

    topMenuBar.draw(this, scene, activeScreenTexture, active);
    
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), true)) {
        if (active != entt::null) {
            if (scene->has<ecs::NodeComponent>(active)) {
                scene.destroyObject(active);
            }
            else {
                scene->destroy(active);
            }
            active = entt::null;
        }
    }

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C), true)) {
        if (SDL_GetModState() & KMOD_LCTRL) {
            auto newEntity = scene->create();

            scene->visit(active, [&](const auto& component) {
                auto clone_function = ecs::cloner::getSingleton()->getFunction(component);
                if (clone_function) {
                    clone_function(scene, active, newEntity);
                }
            });
        }
    }

    assetBrowser.drawWindow(scene, active);

    inspectorWindow.draw(scene, active);

    ecsWindow.draw(scene, active);

    // post processing panel
    ImGui::Begin("Post Processing");
    
    ImGui::Separator();

    if (ImGui::SliderFloat("Exposure", &tonemappingPass->settings.exposure, 0.0f, 1.0f)) {}
    if (ImGui::SliderFloat("Gamma", &tonemappingPass->settings.gamma, 1.0f, 3.2f)) {}
    ImGui::NewLine();

    if (ImGui::Checkbox("Bloom", &doBloom)) {}
    ImGui::Separator();

    if (ImGui::DragFloat3("Threshold", glm::value_ptr(DeferredLightingPass->settings.bloomThreshold), 0.001f, 0.0f, 10.0f)) {}
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

    ImGui::DragFloat("Coverage", &voxelizationPass->worldSize, 0.05f, 1.0f, FLT_MAX, "%.2f");

    ImGui::Separator();

    if (ImGui::TreeNode("Screen Texture")) {
        if (ImGui::Selectable(nameof(tonemappingPass->result), activeScreenTexture == tonemappingPass->result))
            activeScreenTexture = tonemappingPass->result;
        if (ImGui::Selectable(nameof(geometryBufferPass->albedoTexture), activeScreenTexture == geometryBufferPass->albedoTexture))
            activeScreenTexture = geometryBufferPass->albedoTexture;
        if (ImGui::Selectable(nameof(geometryBufferPass->normalTexture), activeScreenTexture == geometryBufferPass->normalTexture))
            activeScreenTexture = geometryBufferPass->normalTexture;
        if (ImGui::Selectable(nameof(geometryBufferPass->materialTexture), activeScreenTexture == geometryBufferPass->materialTexture))
            activeScreenTexture = geometryBufferPass->materialTexture;
        if (ImGui::Selectable(nameof(geometryBufferPass->positionTexture), activeScreenTexture == geometryBufferPass->positionTexture))
            activeScreenTexture = geometryBufferPass->positionTexture;
        if (ImGui::Selectable(nameof(DeferredLightingPass->bloomHighlights), activeScreenTexture == DeferredLightingPass->bloomHighlights))
            activeScreenTexture = DeferredLightingPass->bloomHighlights;
        if (ImGui::Selectable(nameof(DeferredLightingPass->result), activeScreenTexture == DeferredLightingPass->result))
            activeScreenTexture = DeferredLightingPass->result;
        ImGui::TreePop();
    }

    ImGui::NewLine();

    ImGui::Text("Shadow Mapping");
    ImGui::Separator();

    if (ImGui::DragFloat2("Planes", glm::value_ptr(shadowMapPass->settings.planes), 0.1f)) {}
    if (ImGui::DragFloat("Size", &shadowMapPass->settings.size)) {}
    if (ImGui::DragFloat("Min bias", &DeferredLightingPass->settings.minBias, 0.0001f, 0.0f, FLT_MAX, "%.4f")) {}
    if (ImGui::DragFloat("Max bias", &DeferredLightingPass->settings.maxBias, 0.0001f, 0.0f, FLT_MAX, "%.4f")) {}

    ImGui::NewLine();
    ImGui::Separator();
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
    bool resized = viewportWindow.begin(viewport, activeScreenTexture);

    // the viewport image is a drag and drop target for dropping materials onto meshes
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("drag_drop_mesh_material")) {
            auto mousePos = gui::getMousePosWindow(viewport, ImGui::GetWindowPos());
            uint32_t pixel = geometryBufferPass->readEntity(mousePos.x, mousePos.y);
            entt::entity picked = static_cast<entt::entity>(pixel);

            if (scene->valid(picked)) {
                auto mesh = scene->try_get<ecs::MeshComponent>(picked);
                if (mesh) {
                    mesh->material = *reinterpret_cast<const entt::entity*>(payload->Data);
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    auto pos = ImGui::GetWindowPos();
    auto size = ImGui::GetWindowSize();
    mouseInViewport = ImGui::IsWindowHovered();

    auto& io = ImGui::GetIO();
    if (io.MouseClicked[0] && mouseInViewport && !(active != entt::null && ImGuizmo::IsOver(gizmo.getOperation()))) {
        auto mousePos = gui::getMousePosWindow(viewport, ImGui::GetWindowPos());
        uint32_t pixel = geometryBufferPass->readEntity(mousePos.x, mousePos.y);
        entt::entity picked = static_cast<entt::entity>(pixel);

        if (scene->valid(picked)) {
            active = active == picked ? entt::null : picked;
        } else {
            active = entt::null;
        }
    }

    if (active != entt::null) {
        viewportWindow.drawGizmo(gizmo, scene, viewport, active);
    }

    viewportWindow.end();

    metricsWindow.draw(viewport, pos);

    dockspace.end();

    renderer.ImGui_Render();
    renderer.SwapBuffers(window, doVsync);

    if (resized) {
        // resizing framebuffers
        DeferredLightingPass->deleteResources();
        DeferredLightingPass->createResources(viewport);

        fowardLightingPass->deleteResources();
        fowardLightingPass->createResources(viewport);

        boundingBoxDebugPass->deleteResources();
        boundingBoxDebugPass->createResources(viewport);

        voxelizationDebugPass->deleteResources();
        voxelizationDebugPass->createResources(viewport);

        tonemappingPass->deleteResources();
        tonemappingPass->createResources(viewport);

        geometryBufferPass->deleteResources();
        geometryBufferPass->createResources(viewport);

        bloomPass->deleteResources();
        bloomPass->createResources(viewport);
    }
}

} // raekor