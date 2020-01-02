#version 450
#extension GL_ARB_separate_shader_objects : enable

// uniform buffer binding
layout (binding = 0) uniform Camera {
	mat4 m;
	mat4 v;
	mat4 p;
	vec4 light_position;
	// shader is interpreting the angle's x as y, possible alignment problem?
	vec4 light_angle;
} ubo;

// vertex attributes
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out vec3 out_normal;
layout(location = 3) out vec3 out_pos;
layout(location = 4) out vec3 out_light_pos;
layout(location = 5) out vec3 out_light_angle;

// DEBUG COLORS
vec3 colors[4] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
	// CAMERA SPACE : VERTEX POSITION
    gl_Position = ubo.p * ubo.v * ubo.m * vec4(pos, 1.0);
	out_pos = vec3(ubo.v * ubo.m * vec4(pos, 1.0));
	out_normal = mat3(transpose(inverse(ubo.v * ubo.m))) * normal;
	out_light_pos = vec3(ubo.v * ubo.light_position);
	out_light_angle = vec3(ubo.v * ubo.light_angle);
	out_uv = uv;

	// DEBUG COLOR INDEX
    int index = gl_VertexIndex;
    clamp(index, 0, 4);

    out_color = vec4(colors[index], 1.0);
}