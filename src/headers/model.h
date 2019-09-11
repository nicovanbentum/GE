#pragma once

#include "pch.h"
#include "mesh.h"
#include "texture.h"

namespace Raekor {

class Model {

public:
    Model(const std::string& m_file = "");

    void load_from_disk();
    inline void set_path(const std::string& new_path) { path = new_path; }

    void reset_transform();
    void recalc_transform();
    
    inline glm::mat4& get_transform() { return transform; }

    inline float* scale_ptr() { return &scale.x; }
    inline float* pos_ptr() { return &position.x; }
    inline float* rotation_ptr() { return &rotation.x; }
    inline unsigned int mesh_count() { return static_cast<unsigned int>(meshes.size()); }

    void render() const;

    // explicit to avoid implicit bool conversions
    explicit operator bool() const;

    std::optional<const Mesh*> operator[](unsigned int index) {
        if (index < meshes.size()) {
            return &meshes[index];
        }
        return {};
    }

protected:
    glm::mat4 transform;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;

    std::string path;
    std::vector<Mesh> meshes;
    std::vector<std::shared_ptr<Texture>> textures;
};

} // Namespace Raekor