#version 450
#extension GL_KHR_vulkan_glsl: enable
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform Camera {
    mat4 mvp;
} ubo;

layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 out_uv;


vec3 colors[4] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    gl_Position = ubo.mvp * vec4(pos, 1.0);
    int index = gl_VertexIndex;
    clamp(index, 0, 4);
    color = vec4(colors[index], 1.0);
	out_uv = uv;
}