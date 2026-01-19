#version 430 core

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_tex;
uniform vec3 filterColor;

void main() {
    vec4 col = texture(u_tex, v_uv);

    float epsilon = 0.1; // tolerance for the color comparison

    bool isFiltered = filterColor.x != -1.0 &&
                      abs(col.r - filterColor.r) < epsilon &&
                      abs(col.g - filterColor.g) < epsilon &&
                      abs(col.b - filterColor.b) < epsilon;

    if (!isFiltered){
        FragColor = col;
    }
    else {
        FragColor = vec4(0);
    }
}