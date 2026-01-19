#version 430 core

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_tex;
uniform vec3 filterColor;

void main() {
    vec4 col = texture(u_tex, v_uv);

    // If filterColor.r is -1, that means don't filter at all
    if (!(col.x == filterColor.x && col.y == filterColor.y && col.z == filterColor.z) || filterColor.x == -1){
        FragColor = col;
    }
}