#pragma once

#include "ecs.h"

namespace Raekor {
namespace ECS {

struct TransformComponent {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };

    glm::mat4 matrix = glm::mat4(1.0f);
    glm::mat4 worldTransform = glm::mat4(1.0f);

    glm::vec3 localPosition = { 0.0f, 0.0f, 0.0f };

    void recalculateMatrix();
};

struct DirectionalLightComponent {
    struct ShaderBuffer {
        glm::vec4 direction = { 0.0f, 0.0f, 0.0f, 0.0f };
        glm::vec4 colour = { 1.0f, 1.0f, 1.0f, 1.0f };
    } buffer;


};

struct PointLightComponent {
    struct ShaderBuffer {
        glm::vec4 position = { 0.0f, 0.0f, 0.0f, 0.0f };
        glm::vec4 colour = { 1.0f, 1.0f, 1.0f, 1.0f };
    } buffer;


};

struct NodeComponent {
    ECS::Entity parent = NULL;
    bool hasChildren = false;
};

struct BoneInfo {
    glm::mat4 boneOffset;
    glm::mat4 finalTransformation;
};

struct MeshComponent {
    std::vector<Vertex> vertices;
    std::vector<Triangle> indices;

    std::vector<glm::vec4> boneWeights;
    std::vector<glm::ivec4> boneIndices;

    int boneCount = 0;
    std::vector<BoneInfo> boneInfos;
    glm::mat4 inverseGlobalTransform;
    std::vector<glm::mat4> boneTransforms;
    std::unordered_map<std::string, uint32_t> bonemapping;

    const aiScene* scene;
    aiAnimation animation;

    const aiNodeAnim* findNodeAnim(const aiAnimation* animation, const char* nodeName) {
        for (unsigned int i = 0; i < animation->mNumChannels; i++) {
            
            auto nodeAnim = animation->mChannels[i];

            if (strcmp(nodeAnim->mNodeName.C_Str(), nodeName) == 0) {
                return nodeAnim;
            }
        }

        return nullptr;
    }

    unsigned int FindPosition(float AnimationTime, const aiNodeAnim* pNodeAnim) {
        for (unsigned int i = 0; i < pNodeAnim->mNumPositionKeys - 1; i++) {
            if (AnimationTime < (float)pNodeAnim->mPositionKeys[i + 1].mTime) {
                return i;
            }
        }
        return 0;
    }

    unsigned int FindRotation(float AnimationTime, const aiNodeAnim* pNodeAnim) {
        for (unsigned int i = 0; i < pNodeAnim->mNumRotationKeys - 1; i++) {
            if (AnimationTime < (float)pNodeAnim->mRotationKeys[i + 1].mTime) {
                return i;
            }
        }
        return 0;
    }

    unsigned int FindScaling(float AnimationTime, const aiNodeAnim* pNodeAnim) {
        for (unsigned int i = 0; i < pNodeAnim->mNumScalingKeys - 1; i++) {
            if (AnimationTime < (float)pNodeAnim->mScalingKeys[i + 1].mTime) {
                return i;
            }
        }
        return 0;
    }

    glm::vec3 InterpolateTranslation(float animationTime, const aiNodeAnim* nodeAnim) {
        if (nodeAnim->mNumPositionKeys == 1) {
            // No interpolation necessary for single value
            auto v = nodeAnim->mPositionKeys[0].mValue;
            return { v.x, v.y, v.z };
        }

        uint32_t PositionIndex = FindPosition(animationTime, nodeAnim);
        uint32_t NextPositionIndex = (PositionIndex + 1);
        
        float DeltaTime = (float)(nodeAnim->mPositionKeys[NextPositionIndex].mTime - nodeAnim->mPositionKeys[PositionIndex].mTime);
        float Factor = (animationTime - (float)nodeAnim->mPositionKeys[PositionIndex].mTime) / DeltaTime;
        if (Factor < 0.0f) Factor = 0.0f;
        
        const aiVector3D& Start = nodeAnim->mPositionKeys[PositionIndex].mValue;
        const aiVector3D& End = nodeAnim->mPositionKeys[NextPositionIndex].mValue;
        aiVector3D Delta = End - Start;
        auto aiVec = Start + Factor * Delta;
        return { aiVec.x, aiVec.y, aiVec.z };
    }

    glm::quat InterpolateRotation(float animationTime, const aiNodeAnim* nodeAnim) {
        if (nodeAnim->mNumRotationKeys == 1) {
            // No interpolation necessary for single value
            auto v = nodeAnim->mRotationKeys[0].mValue;
            return glm::quat(v.w, v.x, v.y, v.z);
        }

        uint32_t RotationIndex = FindRotation(animationTime, nodeAnim);
        uint32_t NextRotationIndex = (RotationIndex + 1);
        
        float DeltaTime = (float)(nodeAnim->mRotationKeys[NextRotationIndex].mTime - nodeAnim->mRotationKeys[RotationIndex].mTime);
        float Factor = (animationTime - (float)nodeAnim->mRotationKeys[RotationIndex].mTime) / DeltaTime;
        if (Factor < 0.0f) Factor = 0.0f;
        
        const aiQuaternion& StartRotationQ = nodeAnim->mRotationKeys[RotationIndex].mValue;
        const aiQuaternion& EndRotationQ = nodeAnim->mRotationKeys[NextRotationIndex].mValue;
        auto q = aiQuaternion();
        aiQuaternion::Interpolate(q, StartRotationQ, EndRotationQ, Factor);
        q = q.Normalize();
        return glm::quat(q.w, q.x, q.y, q.z);
    }

    glm::vec3 InterpolateScale(float animationTime, const aiNodeAnim* nodeAnim) {
        if (nodeAnim->mNumScalingKeys == 1) {
            // No interpolation necessary for single value
            auto v = nodeAnim->mScalingKeys[0].mValue;
            return { v.x, v.y, v.z };
        }

        uint32_t index = FindScaling(animationTime, nodeAnim);
        uint32_t nextIndex = (index + 1);
        
        float deltaTime = (float)(nodeAnim->mScalingKeys[nextIndex].mTime - nodeAnim->mScalingKeys[index].mTime);
        float factor = (animationTime - (float)nodeAnim->mScalingKeys[index].mTime) / deltaTime;
        if (factor < 0.0f) factor = 0.0f;
        
        const auto& start = nodeAnim->mScalingKeys[index].mValue;
        const auto& end = nodeAnim->mScalingKeys[nextIndex].mValue;
        auto delta = end - start;
        auto aiVec = start + factor * delta;
        return { aiVec.x, aiVec.y, aiVec.z };
    }

    static glm::mat4 aiMat4toGLM(const aiMatrix4x4& from) {
        glm::mat4 to;
        //the a,b,c,d in assimp is the row ; the 1,2,3,4 is the column
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    };

    void ReadNodeHierarchy(float animationTime, const aiNode* pNode, const glm::mat4& parentTransform) {
        auto animation = scene->mAnimations[0];
        
        glm::mat4 nodeTransform = aiMat4toGLM(pNode->mTransformation);
        
        auto nodeAnim = findNodeAnim(animation, pNode->mName.C_Str());

        if (nodeAnim) {
            glm::vec3 translation = InterpolateTranslation(animationTime, nodeAnim);
            glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(translation.x, translation.y, translation.z));

            glm::quat rotation = InterpolateRotation(animationTime, nodeAnim);
            glm::mat4 rotationMatrix = glm::toMat4(rotation);

            glm::vec3 scale = InterpolateScale(animationTime, nodeAnim);
            glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(scale.x, scale.y, scale.z));

            nodeTransform = translationMatrix * rotationMatrix * scaleMatrix;
        }

        auto globalTransformation = parentTransform * nodeTransform;

            if (bonemapping.find(pNode->mName.C_Str()) != bonemapping.end()) {
                unsigned int boneIndex = bonemapping[pNode->mName.C_Str()];
                boneInfos[boneIndex].finalTransformation = globalTransformation * boneInfos[boneIndex].boneOffset;
            }


            for (unsigned int i = 0; i < pNode->mNumChildren; i++) {
                ReadNodeHierarchy(animationTime, pNode->mChildren[i], globalTransformation);
            }
    }

    void boneTransform(float TimeInSeconds) {
        float ticks = static_cast<float>(scene->mAnimations[0]->mTicksPerSecond);
        float duration = static_cast<float>(scene->mAnimations[0]->mDuration);
        float TicksPerSecond = ticks != 0.0 ? ticks : 25.0f;
        float TimeInTicks = TimeInSeconds * TicksPerSecond;
        float AnimationTime = fmod(TimeInTicks, duration);

        auto identity = glm::mat4(1.0f);
        ReadNodeHierarchy(AnimationTime, scene->mRootNode, identity);

        boneTransforms.resize(boneCount);
        for (int i = 0; i < boneCount; i++) {
            boneTransforms[i] = boneInfos[i].finalTransformation;
        }
    }

    glVertexBuffer vertexBuffer;
    glVertexBuffer skinnedVertexBuffer;
    glIndexBuffer indexBuffer;

    unsigned int boneIndexBuffer;
    unsigned int boneWeightBuffer;
    unsigned int boneTransformsBuffer;

    std::array<glm::vec3, 2> aabb;

    void generateAABB();
    
    void uploadVertices();
    void uploadIndices();
};

struct MeshRendererComponent {

};

struct MaterialComponent {
    std::unique_ptr<glTexture2D> albedo;
    std::unique_ptr<glTexture2D> normals;
    std::unique_ptr<glTexture2D> metalrough;
};

struct NameComponent {
    std::string name;
};

} // ECS
} // raekor