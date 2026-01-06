#version 430 core

layout(location = 0) in vec2 a_pos;  // vertex position in NDC (-1 to 1)
layout(location = 1) in vec2 a_uv;   // tex coordinates (0 to 1)

out vec2 v_uv;  // pass to fragment shader

void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}