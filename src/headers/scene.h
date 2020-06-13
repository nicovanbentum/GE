#pragma once

#include "mesh.h"
#include "camera.h"
#include "texture.h"

#include "ecs.h"
#include "components.h"
#include "async.h"

namespace Raekor {

class Scene {
public:
    ECS::Entity createObject(const std::string& name);
    ECS::Entity createPointLight(const std::string& name);
    ECS::Entity createDirectionalLight(const std::string& name);

    inline ECS::TransformComponent* getTransform(ECS::Entity entity) {
        return transforms.getComponent(entity);
    }

    ECS::MeshComponent& addMesh();

    void remove(ECS::Entity entity);


public:
    ECS::ComponentManager<ECS::NameComponent> names;
    ECS::ComponentManager<ECS::TransformComponent> transforms;
    ECS::ComponentManager<ECS::NodeComponent> nodes;
    ECS::ComponentManager<ECS::MeshComponent> meshes;
    ECS::ComponentManager<ECS::MeshRendererComponent> meshRenderers;
    ECS::ComponentManager<ECS::MaterialComponent> materials;
    ECS::ComponentManager<ECS::DirectionalLightComponent> directionalLights;
    ECS::ComponentManager<ECS::PointLightComponent> pointLights;

public:
    std::vector<ECS::Entity> entities;
};

class AssimpImporter {
public:
    void loadFromDisk(Scene& scene, const std::string& file, AsyncDispatcher& dispatcher);

private:
    void processAiNode(Scene& scene, const aiScene* aiscene, aiNode* node);

    // TODO: every mesh in the file is created as an Entity that has 1 name, 1 mesh and 1 material component
    // we might want to incorporate meshrenderers and seperate entities for materials
    void loadMesh(Scene& scene, aiMesh* assimpMesh, aiMaterial* assimpMaterial, aiMatrix4x4 localTransform);

    void loadTexturesAsync(const aiScene* scene, const std::string& directory, AsyncDispatcher& dispatcher);

private:
    std::unordered_map<std::string, Stb::Image> images;
};

} // Namespace Raekor