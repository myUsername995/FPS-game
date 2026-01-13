#version 430

layout (local_size_x = 16, local_size_y = 16) in;

layout (rgba8, binding = 0) writeonly uniform image2D outTex;
layout (rgba8, binding = 1) readonly uniform image2D spriteTex;

uniform uint WINDOW_WIDTH;
uniform uint WINDOW_HEIGHT;

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);

    if (p.x >= WINDOW_WIDTH || p.y >= WINDOW_HEIGHT){
        return;
    }

    vec4 sprite = imageLoad(spriteTex, p);

    if (sprite.a > 0.0) {
        imageStore(outTex, p, sprite);
    }
}