#version 330 core
layout (location = 0) in vec3 pos;

out vec3 texture_pos;

layout (std140) uniform Camera {
	mat4 model;
	mat4 view;
	mat4 proj;
};

void main()
{
	mat4 centered_view = mat4(mat3(view));
    vec3 worldPos = vec3(model * vec4(pos, 1.0));
    gl_Position = proj * centered_view * vec4(worldPos, 1.0);
    texture_pos = pos;
}  