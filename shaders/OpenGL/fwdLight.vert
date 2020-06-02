#version 440 core

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;

#define MAX_POINT_LIGHTS 10
#define MAX_DIR_LIGHTS 1

struct DirectionalLight {
    vec3 direction;
	vec4 color;
};

struct PointLight {
	vec4 position;
	vec4 color;
};

layout (std140) uniform stuff {
	mat4 view, projection;
	mat4 lightSpaceMatrix;
	vec4 cameraPosition;
    DirectionalLight dirLights[MAX_DIR_LIGHTS];
    PointLight pointLights[MAX_POINT_LIGHTS];
} ubo;

uniform mat4 model;

out vec2 uv;
out vec3 position;
out vec3 normal;
out mat3 TBN;
out vec3 cameraDirection;
out vec4 depthPosition;

void main() {
	position = (model * vec4(v_pos ,1)).xyz;
    gl_Position =  ubo.projection * ubo.view * vec4(position , 1.0);

    vec3 T = normalize(vec3(model * vec4(v_tangent,		0.0)));
	vec3 B = normalize(vec3(model * vec4(v_bitangent,	0.0)));
	normal = normalize(vec3(model * vec4(v_normal,		0.0)));
	TBN = mat3(T, B, normal.xyz);

    depthPosition = ubo.lightSpaceMatrix * model * vec4(v_pos, 1);
	depthPosition.xyz = depthPosition.xyz * 0.5 + 0.5;

	cameraDirection = ubo.cameraPosition.xyz - position;

	uv = v_uv;
}