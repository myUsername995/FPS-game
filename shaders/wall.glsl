#version 440 compatibility

layout(local_size_x = 256, local_size_y = 1) in;

struct columnData {
    vec4 texCoord;
    ivec2 wallRange;
    int texture;
};

layout(std430, binding = 0) buffer columnBuf {
    columnData columns[];
};

layout(binding = 0, rgba8) uniform image2D outImage;  // Output image

// UNIFORMS

uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;
uniform float texWidth;
uniform float texHeight;

void main() {
    int x = int(gl_GlobalInvocationID.x);

    if (x >= WINDOW_WIDTH){
        return;
    }

    columnData curColumn = columns[x];

    vec2 wall = curColumn.wallRange;
    bool noCollision = wall.x == 0 && wall.y == 0;

    if (noCollision){
        return;
    }

    float texX = curColumn.texCoord.x;
    float texY = curColumn.texCoord.z;
    float stepY = curColumn.texCoord.w;

    // The texture of the current column
    int textureIdx = curColumn.texture;

    for (int y = int(wall.x); y < int(wall.y); y++){
        vec2 texCoord;
        float normalizedX = texX / texWidth;
        float normalizedY = texY / texHeight;
        texCoord.x = clamp(normalizedX, 0.0, 1.0);
        texCoord.y = clamp(normalizedY, 0.0, 1.0);

        vec4 color = texture(textures[int(textureIdx)], texCoord);

        imageStore(outImage, ivec2(int(x), int(y)), color);

        texY += stepY;
    }
}