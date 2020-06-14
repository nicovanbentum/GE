#pragma once

#include "ecs.h"
#include "scene.h"
#include "mesh.h"
#include "texture.h"
#include "framebuffer.h"

namespace Raekor {
namespace RenderPass {

class ShadowMap {
private:
    struct {
        glm::mat4 cameraMatrix;
    } uniforms;

 public:
     struct {
         glm::vec<2, float> planes = { 1.0f, 200.0f };
         float size = 100.0f;
     } settings;

    ShadowMap(uint32_t width, uint32_t height);
    void execute(Scene& scene);

private:
    glShader shader;
    glFramebuffer framebuffer;
    glUniformBuffer uniformBuffer;

public:
    Camera sunCamera;
    glTexture2D result;
};

//////////////////////////////////////////////////////////////////////////////////

class OmniShadowMap {
public:
    struct {
        uint32_t width, height;
        float nearPlane = 0.1f, farPlane = 25.0f;
    } settings;

    OmniShadowMap(uint32_t width, uint32_t height);
    void execute(Scene& scene, const glm::vec3& lightPosition);

private:
    glShader shader;
    glFramebuffer depthCubeFramebuffer;

public:
    glTextureCube result;
};

//////////////////////////////////////////////////////////////////////////////////

class GeometryBuffer {
public:
    uint32_t culled = 0;

    GeometryBuffer(Viewport& viewport);
    void execute(Scene& scene, Viewport& viewport);
    void resize(Viewport& viewport);

    ECS::Entity pick(uint32_t x, uint32_t y) {
        int id;
        GBuffer.bind();
        glReadPixels(x, y, 1, 1, GL_STENCIL_INDEX, GL_INT, &id);
        GBuffer.unbind();
        return id;
    }

private:
    glShader shader;
    glFramebuffer GBuffer;
    glRenderbuffer GDepthBuffer;

    ShaderHotloader hotloader;
  
public:
    glTexture2D albedoTexture, normalTexture, positionTexture, materialTexture;
};

//////////////////////////////////////////////////////////////////////////////////

class ScreenSpaceAmbientOcclusion {
public:
    struct {
        float samples = 64.0f;
        float bias = 0.025f, power = 2.5f;
    } settings;

    ScreenSpaceAmbientOcclusion(Viewport& viewport);
    void execute(Viewport& viewport, GeometryBuffer* geometryPass, Mesh* quad);
    void resize(Viewport& viewport);

private:
    glTexture2D noise;
    glShader shader;
    glShader blurShader;
    glFramebuffer framebuffer;
    glFramebuffer blurFramebuffer;

public:
    glTexture2D result;
    glTexture2D preblurResult;

private:
    glm::vec2 noiseScale;
    std::vector<glm::vec3> ssaoKernel;
};

//////////////////////////////////////////////////////////////////////////////////

class Bloom {
public:
    Bloom(Viewport& viewport);
    void execute(glTexture2D& scene, glTexture2D& highlights, Mesh* quad);
    void resize(Viewport& viewport);

private:
    glShader blurShader;
    glShader bloomShader;
    glTexture2D blurTextures[2];
    glFramebuffer blurBuffers[2];
    glFramebuffer resultFramebuffer;
    
public:
    glTexture2D result;
};

//////////////////////////////////////////////////////////////////////////////////

class Tonemapping {
public:
    struct {
        float exposure = 1.0f;
        float gamma = 2.2f;
    } settings;

    Tonemapping(Viewport& viewport);
    void resize(Viewport& viewport);
    void execute(glTexture2D& scene, Mesh* quad);

private:
    glShader shader;
    glFramebuffer framebuffer;
    glUniformBuffer uniformBuffer;

public:
    glTexture2D result;
};

//////////////////////////////////////////////////////////////////////////////////

class Voxelization {
public:
    Voxelization(int size);
    void execute(Scene& scene, Viewport& viewport, ShadowMap* shadowmap);

private:
    int size;
    glm::mat4 px, py, pz;
    glShader shader;
    ShaderHotloader hotloader;

public:
    float worldSize = 150.0f;
    glTexture3D result;
};

//////////////////////////////////////////////////////////////////////////////////

class VoxelizationDebug {
public:
    VoxelizationDebug(Viewport& viewport);
    void execute(Viewport& viewport, glTexture2D& input, Voxelization* voxels);
    void resize(Viewport& viewport);

private:
    glFramebuffer frameBuffer;
    glRenderbuffer renderBuffer;
    glShader shader;
};

class BoundingBoxDebug {
public:
    BoundingBoxDebug(Viewport& viewport);
    void execute(Scene& scene, Viewport& viewport, glTexture2D& texture, ECS::Entity active);
    void resize(Viewport& viewport);

private:
    glShader shader;
    glFramebuffer frameBuffer;
    glRenderbuffer renderBuffer;
    glVertexBuffer vertexBuffer;
    glIndexBuffer indexBuffer;

public:
    glTexture2D result;
};

class ForwardLightingPass {
private:
    struct {
        glm::mat4 view, projection;
        glm::mat4 lightSpaceMatrix;
        glm::vec4 cameraPosition;
        ECS::DirectionalLightComponent::ShaderBuffer dirLights[1];
        ECS::PointLightComponent::ShaderBuffer pointLights[10];
        unsigned int renderFlags = 0b00000001;
    } uniforms;

public:
    ForwardLightingPass(Viewport& viewport);
    void execute(Viewport& viewport, Scene& scene, Voxelization* voxels, ShadowMap* shadowmap);
    void resize(Viewport& viewport);

    ECS::Entity pick(uint32_t x, uint32_t y) {
        int id;
        framebuffer.bind();
        glReadPixels(x, y, 1, 1, GL_STENCIL_INDEX, GL_INT, &id);
        framebuffer.unbind();
        return id;
    }

private:
    glShader shader;
    glFramebuffer framebuffer;
    glRenderbuffer renderbuffer;
    glUniformBuffer uniformBuffer;

    ShaderHotloader hotloader;

public:
    int culled = 0;
    glTexture2D result;
};

//////////////////////////////////////////////////////////////////////////////////

class SkyPass {
public:
    struct {
        float time = 0.0f;
        float cirrus = 0.65f;
    } settings;

    SkyPass(Viewport& viewport) {
        std::vector<glShader::Stage> stages;
        stages.emplace_back(Shader::Type::VERTEX, "shaders\\OpenGL\\sky.vert");
        stages.emplace_back(Shader::Type::FRAG, "shaders\\OpenGL\\sky.frag");
        shader.reload(stages.data(), stages.size());
        hotloader.watch(&shader, stages.data(), stages.size());
    
        result.bind();
        result.init(viewport.size.x, viewport.size.y, { GL_RGBA32F, GL_RGBA, GL_FLOAT });
        result.setFilter(Sampling::Filter::None);
        result.setWrap(Sampling::Wrap::ClampEdge);
        result.unbind();

        framebuffer.attach(result, GL_COLOR_ATTACHMENT0);
    }

    void execute( Viewport& viewport, Mesh* quad) {
        hotloader.checkForUpdates();

        framebuffer.bind();
        glClear(GL_COLOR_BUFFER_BIT);

        shader.bind();
        shader.getUniform("projection") = viewport.getCamera().getProjection();
        shader.getUniform("view") = viewport.getCamera().getView();
        shader.getUniform("time") = settings.time;
        shader.getUniform("cirrus") = settings.cirrus;

        quad->render();

        framebuffer.unbind();
    }

    void resize(Viewport& viewport) {
        result.bind();
        result.init(viewport.size.x, viewport.size.y, { GL_RGBA32F, GL_RGBA, GL_FLOAT });
        result.setFilter(Sampling::Filter::None);
        result.setWrap(Sampling::Wrap::ClampEdge);
        result.unbind();
    }

private:
    glShader shader;
    ShaderHotloader hotloader;
    glFramebuffer framebuffer;

public:
    glTexture2D result;


};

class DeferredLighting {
private:
    struct {
        glm::mat4 view, projection;
        glm::mat4 lightSpaceMatrix;
        glm::vec4 cameraPosition;
        ECS::DirectionalLightComponent::ShaderBuffer dirLights[1];
        ECS::PointLightComponent::ShaderBuffer pointLights[10];
        unsigned int renderFlags = 0b00000001;
    } uniforms;

public:
    struct {
        float farPlane = 25.0f;
        float minBias = 0.000f, maxBias = 0.0f;
        glm::vec4 sunColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3 bloomThreshold{ 2.0f, 2.0f, 2.0f };
    } settings;


    DeferredLighting(Viewport& viewport);
    void execute(Scene& sscene, Viewport& viewport, ShadowMap* shadowMap, OmniShadowMap* omniShadowMap,
        GeometryBuffer* GBuffer, ScreenSpaceAmbientOcclusion* ambientOcclusion, Voxelization* voxels, Mesh* quad, SkyPass* skyPass);
    void resize(Viewport& viewport);

private:
    glShader shader;
    glFramebuffer framebuffer;
    glUniformBuffer uniformBuffer;

    ShaderHotloader hotloader;

public:
    glTexture2D result;
    glTexture2D bloomHighlights;
};

} // renderpass
} // raekor
