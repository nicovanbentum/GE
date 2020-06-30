    #version 440 core

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_binormal;

uniform mat4 model;

// texture coordinate for sampling gbuffers
out vec2 uv;

void main()
{
    uv = v_uv;
    gl_Position = vec4(v_pos.x, v_pos.y, 0.0, 1.0);
}