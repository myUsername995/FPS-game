#version 430 core 

layout(local_size_x = 256, local_size_y = 1) in;

layout(std430, binding = 1) buffer wallRangesBuffer {
    vec2 wallRanges[];
};

// Stores: startTexX, stepX (0), startTexY, stepY
layout(std430, binding = 2) buffer texCoordsBuffer {
    vec4 texCoords[];
};

// The output texture for each column
layout(std430, binding = 4) readonly buffer textureColumnsBuffer {
    uint textureColumns[];
};

layout(binding = 0, rgba8) writeonly uniform image2D outImage;  // Output image

// UNIFORMS
uniform sampler2D textures[9];
uniform uint WINDOW_WIDTH;
uniform uint WINDOW_HEIGHT;
uniform float texWidth;
uniform float texHeight;

void main() {
    uint x = gl_GlobalInvocationID.x;

    if (x >= WINDOW_WIDTH){
        return;
    }

    vec2 wall = wallRanges[x];
    bool noCollision = wall.x == 0 && wall.y == 0;

    if (noCollision){
        return;
    }

    float texX = texCoords[x].x;
    float stepX = texCoords[x].y;     // Unused
    float texY = texCoords[x].z;
    float stepY = texCoords[x].w;

    // The texture of the current column
    uint textureIdx = 3;

    for (int y = int(wall.x); y < int(wall.y); y++){
        vec2 texCoord;
        float normalizedX = texX / texWidth;
        float normalizedY = texY / texHeight;
        texCoord.x = clamp(normalizedX, 0.0, 1.0);
        texCoord.y = clamp(normalizedY, 0.0, 1.0);

        vec4 color = texture(textures[int(textureIdx-2)], texCoord);

        imageStore(outImage, ivec2(int(x), int(y)), color);

        texY += stepY;
    }
}