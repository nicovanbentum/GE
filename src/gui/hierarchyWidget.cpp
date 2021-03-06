#include "pch.h"
#include "editor.h"
#include "hierarchyWidget.h"

namespace Raekor {

HierarchyWidget::HierarchyWidget(Editor* editor) : IWidget(editor, "Scene") {}



void HierarchyWidget::draw() {
    ImGui::Begin(title.c_str());

    auto nodeView = editor->scene.view<ecs::NodeComponent>();
    for (auto entity : nodeView) {
        auto& node = nodeView.get<ecs::NodeComponent>(entity);

        if (node.parent == entt::null) {
            if (node.firstChild != entt::null) {
                if (drawFamilyNode(editor->scene, entity, editor->active)) {
                    drawFamily(editor->scene, entity, editor->active);
                    ImGui::TreePop();
                }
            } else {
                drawChildlessNode(editor->scene, entity, editor->active);
            }
        }
    }

    ImGui::End();
}



bool HierarchyWidget::drawFamilyNode(entt::registry& scene, entt::entity entity, entt::entity& active) {
    auto& node = scene.get<ecs::NodeComponent>(entity);

    auto selected = active == entity ? ImGuiTreeNodeFlags_Selected : 0;
    auto treeNodeFlags = selected | ImGuiTreeNodeFlags_OpenOnArrow;
    auto name = scene.get<ecs::NameComponent>(entity);
    bool opened = ImGui::TreeNodeEx(name.name.c_str(), treeNodeFlags);
    if (ImGui::IsItemClicked()) {
        active = active == entity ? entt::null : entity;
    }
    return opened;
}



void HierarchyWidget::drawChildlessNode(entt::registry& scene, entt::entity entity, entt::entity& active) {
    auto& node = scene.get<ecs::NodeComponent>(entity);
    auto name = scene.get<ecs::NameComponent>(entity);
    if (ImGui::Selectable(std::string(name.name + "##" + std::to_string(static_cast<uint32_t>(entity))).c_str(), entity == active)) {
        active = active == entity ? entt::null : entity;
    }
}



void HierarchyWidget::drawFamily(entt::registry& scene, entt::entity entity, entt::entity& active) {
    auto& node = scene.get<ecs::NodeComponent>(entity);

    if (node.firstChild != entt::null) {
        for (auto it = node.firstChild; it != entt::null; it = scene.get<ecs::NodeComponent>(it).nextSibling) {
            auto& itNode = scene.get<ecs::NodeComponent>(it);

            if (itNode.firstChild != entt::null) {
                if (drawFamilyNode(scene, it, active)) {
                    drawFamily(scene, it, active);
                    ImGui::TreePop();
                }
            } else {
                drawChildlessNode(scene, it, active);
            }
        }
    }
}

} // raekor