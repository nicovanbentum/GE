#pragma once

#include "pch.h"
#include "mesh.h"
#include "texture.h"

namespace Raekor {

// This is all really ugly and super hacky, should refactor to a pseudo ECS system

class Transformable {
public:
    Transformable();
    void reset_transform();
    void recalc_transform();

    glm::mat4 transform;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;

    glm::vec3 minAABB;
    glm::vec3 maxAABB;

    std::array<glm::vec3, 8> boundingBox;
};

class SceneObject : public Mesh, public Transformable {
public:
    SceneObject() {}
    SceneObject(const std::string& fp, const std::vector<Vertex>& vb, const std::vector<Index>& ib);
    void render();

    bool hasAlpha = false;
    std::string name;
    uint32_t transformationIndex;
    std::unique_ptr<glTexture2D> albedo;
    std::unique_ptr<glTexture2D> normal;
    std::unique_ptr<Mesh> collisionRenderable;
    std::unique_ptr<btTriangleMesh> triMesh;
    std::unique_ptr<btBvhTriangleMeshShape> shape;
};

class Scene {
public:
    Scene() {
        importer = std::make_shared<Assimp::Importer>();
    }

    void add(std::string file);

    void erase(std::string name) {
        auto it = std::find_if(objects.begin(), objects.end(), 
            [&](SceneObject& obj) { return obj.name == name; });
        objects.erase(it);
    }

    std::vector<SceneObject>::iterator at(std::string name) {
            return std::find_if(objects.begin(), objects.end(),
                [&](SceneObject& obj) { return obj.name == name; });
    }

    void renderOpaque() {
        for (auto& object : objects) {
            if (!object.hasAlpha) {
                object.render();
            }
        }
    }

    void renderTransparent() {
        for (auto& object : objects) {
            if (object.hasAlpha) {
                object.render();
            }
        }
    }

    void render() {
        renderOpaque();
        renderTransparent();
    }

    std::vector<SceneObject> objects;
private:
    std::shared_ptr<Assimp::Importer> importer;
};

} // Namespace Raekor