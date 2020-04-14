#version 440 core

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_normal;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec3 positions;
out vec3 normals;
out vec2 uvs;

void main() {
	normals = normalize(mat3(transpose(inverse(model))) * v_normal);
	positions = (model * vec4(v_pos, 1.0)).xyz;
	gl_Position = projection * view * vec4(positions, 1.0);

	uvs = v_uv;
}