#version 430 core 

layout(local_size_x = 256, local_size_y = 1) in;

struct columnData {
    vec4 texCoord;
    ivec2 wallRange;
    int texture;
};

layout(std430, binding = 0) buffer columnBuf {
    columnData columns[];
};

layout(binding = 0, rgba8) writeonly uniform image2D outImage;  // Output image

// UNIFORMS
uniform sampler2D textures[9];
uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;
uniform float texWidth;
uniform float texHeight;
uniform int xRange;
uniform int yRange;

void main() {
    int x = int(gl_GlobalInvocationID.x);

    if (x >= WINDOW_WIDTH || x >= xRange){
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
    float maxY = clamp(yRange, wall.x, wall.y);

    for (int y = int(wall.x); y < int(maxY); y++){
        texX = clamp(texX, 0, texWidth - 1);
        texY = clamp(texY, 0, texHeight - 1);
        float normalizedX = texX / texWidth;
        float normalizedY = texY / texHeight;

        vec4 color = texture(textures[int(textureIdx)], vec2(normalizedX, normalizedY));

        imageStore(outImage, ivec2(int(x), int(y)), color);

        texY += stepY;
    }
}