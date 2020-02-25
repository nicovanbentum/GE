#include "pch.h"
#include "scene.h"
#include "mesh.h"
#include "timer.h"

namespace Raekor {

Transformable::Transformable() :
    transform(1.0f),
    position(0.0f),
    rotation(0.0f),
    scale(1.0f)
{
    btCollisionShape* boxCollisionShape = new btBoxShape(btVector3(1.0f, 1.0f, 1.0f));
    glm::quat orientation = glm::toQuat(transform);
    btDefaultMotionState* motionstate = new btDefaultMotionState(btTransform(
        btQuaternion(orientation.x, orientation.y, orientation.z, orientation.w),
        btVector3(position.x, position.y, position.z)
    ));


    btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(
        0,                  // mass, in kg. 0 -> Static object, will never move.
        motionstate,
        boxCollisionShape,  // collision shape of body
        btVector3(0, 0, 0)    // local inertia
    );

    rigidBody = new btRigidBody(rigidBodyCI);
}

void Transformable::reset_transform() {
    transform = glm::mat4(1.0f);
    scale = glm::vec3(1.0f);
    position = glm::vec3(0.0f);
    rotation = glm::vec3(0.0f);
}

void Transformable::recalc_transform() {
    transform = glm::mat4(1.0f);
    transform = glm::translate(transform, position);
    auto rotation_quat = static_cast<glm::quat>(rotation);
    transform = transform * glm::toMat4(rotation_quat);
    transform = glm::scale(transform, scale);
}

SceneObject::SceneObject(const std::string& fp, const std::vector<Vertex>& vbuffer, const std::vector<Index>& ibuffer)
    : Mesh(fp, vbuffer, ibuffer) {
    vb->set_layout({
        {"POSITION",    ShaderType::FLOAT3},
        {"UV",          ShaderType::FLOAT2},
        {"NORMAL",      ShaderType::FLOAT3},
        {"TANGENT",     ShaderType::FLOAT3},
        {"BINORMAL",    ShaderType::FLOAT3}
        });
}

void SceneObject::render() {
    if (albedo != nullptr) albedo->bind(0);
    if (normal != nullptr) normal->bind(3);
    bind();
    int drawCount = ib->get_count();
    Render::DrawIndexed(drawCount);
}

void Scene::add(std::string file) {
    constexpr unsigned int flags =
        aiProcess_CalcTangentSpace |
        aiProcess_Triangulate |
        aiProcess_SortByPType |
        //aiProcess_PreTransformVertices |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenUVCoords |
        aiProcess_OptimizeMeshes |
        aiProcess_Debone |
        aiProcess_ValidateDataStructure;

    auto scene = importer->ReadFile(file, flags);
    m_assert(scene && scene->HasMeshes(), "failed to load mesh");

    // pre-load all the textures asynchronously
    std::vector<Stb::Image> albedos;
    std::vector<Stb::Image> normals;

    for (uint64_t index = 0; index < scene->mNumMeshes; index++) {
        m_assert(scene && scene->HasMeshes(), "failed to load mesh");
        auto ai_mesh = scene->mMeshes[index];

        aiString albedoFile, normalmapFile;
        auto material = scene->mMaterials[ai_mesh->mMaterialIndex];
        material->GetTexture(aiTextureType_DIFFUSE, 0, &albedoFile);
        material->GetTexture(aiTextureType_NORMALS, 0, &normalmapFile);

        std::string texture_path = get_file(file, PATH_OPTIONS::DIR) + std::string(albedoFile.C_Str());
        std::string normal_path = get_file(file, PATH_OPTIONS::DIR) + std::string(normalmapFile.C_Str());

        if (!texture_path.empty()) {
            Stb::Image image;
            image.format = RGBA;
            image.isSRGB = true;
            image.filepath = texture_path;
            albedos.push_back(image);
        }

        if (!normal_path.empty()) {
            Stb::Image image;
            image.format = RGBA;
            image.isSRGB = false;
            image.filepath = normal_path;
            normals.push_back(image);
        }
    }

    // asyncronously load textures from disk
    std::vector<std::future<void>> futures;
    for (auto& img : albedos) {
        //img.load(img.filepath, true);
        futures.push_back(std::async(std::launch::async, &Stb::Image::load, &img, img.filepath, true));
    }

    for (auto& img : normals) {
        //img.load(img.filepath, true);
        futures.push_back(std::async(std::launch::async, &Stb::Image::load, &img, img.filepath, true));
    }

    for (auto& future : futures) {
        future.wait();
    }    
    
    auto processMesh = [&](aiMesh * mesh, aiMatrix4x4 localTransform, const aiScene * scene) {
        std::vector<Vertex> vertices;
        std::vector<Index> indices;

        // create a glm mat4 transformation out of the mesh's assimp local transform
        aiVector3D position, rotation, scale;
        localTransform.Decompose(scale, rotation, position);
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, { position.x, position.y, position.y });
        auto rotation_quat = static_cast<glm::quat>(glm::vec3(0.0f, 0.0f, 0.0f));
        transform = transform * glm::toMat4(rotation_quat);
        transform = glm::scale(transform, { scale.x, scale.y, scale.z });
        transforms.push_back(transform);

        // extract vertices
        vertices.reserve(mesh->mNumVertices);
        for (size_t i = 0; i < vertices.capacity(); i++) {
            Vertex v = {};
            v.pos = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

            if (mesh->HasTextureCoords(0)) {
                v.uv = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            }
            if (mesh->HasNormals()) {
                v.normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
            }

            if (mesh->HasTangentsAndBitangents()) {
                v.tangent = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
                v.binormal = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
            }
            vertices.push_back(std::move(v));
        }
        // extract indices
        indices.reserve(mesh->mNumFaces);
        for (size_t i = 0; i < indices.capacity(); i++) {
            m_assert((ai_mesh->mFaces[i].mNumIndices == 3), "faces require 3 indices");
            indices.emplace_back(mesh->mFaces[i].mIndices[0], mesh->mFaces[i].mIndices[1], mesh->mFaces[i].mIndices[2]);
        }

        std::string name = mesh->mName.C_Str();
        objects.push_back(SceneObject(name, vertices, indices));
        objects.back().name = name;
        auto& object = objects.back();
        object.transformationIndex = transforms.size() - 1;

        aiString albedoFile, normalmapFile;
        auto material = scene->mMaterials[mesh->mMaterialIndex];
        material->GetTextureCount(aiTextureType_DIFFUSE);
        material->GetTexture(aiTextureType_DIFFUSE, 0, &albedoFile);
        material->GetTexture(aiTextureType_NORMALS, 0, &normalmapFile);

        std::string texture_path = get_file(file, PATH_OPTIONS::DIR) + std::string(albedoFile.C_Str());
        std::string normal_path = get_file(file, PATH_OPTIONS::DIR) + std::string(normalmapFile.C_Str());

        auto albedoIter = std::find_if(albedos.begin(), albedos.end(), [&](const Stb::Image& img) {
            return img.filepath == texture_path;
            });

        auto normalIter = std::find_if(normals.begin(), normals.end(), [&](const Stb::Image& img) {
            return img.filepath == normal_path;
            });

        if (albedoIter != albedos.end()) {
            auto index = albedoIter - albedos.begin();
            objects.back().albedo.reset(new GLTexture(albedos[index]));
            if (albedoIter->channels == 4) {
                object.albedo->hasAlpha = true;
            }
            else {
                object.albedo->hasAlpha = false;
            }
        }
        if (normalIter != normals.end()) {
            auto index = normalIter - normals.begin();
            object.normal.reset(new GLTexture(normals[index]));
        }
    };

    std::function<void(aiNode*, const aiScene*)> processNode = [&](aiNode* node, const aiScene* scene) {
        for (uint32_t i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            processMesh(mesh, node->mTransformation, scene);
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    };

    // process the assimp node tree, creating a scene object for every mesh with its textures and transformation
    processNode(scene->mRootNode, scene);

}

} // Namespace Raekor