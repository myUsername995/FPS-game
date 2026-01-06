#version 430 core

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_tex;   // tex to render

void main() {
    FragColor = texture(u_tex, v_uv);
}