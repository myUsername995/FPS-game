#version 430 core 

layout(local_size_x = 16, local_size_y = 16) in;

layout(std430, binding = 1) buffer wallRangesBuffer {
    vec2 wallRanges[];
};

// Stores: startTexX, stepX (0), startTexY, stepY
layout(std430, binding = 2) buffer texCoordsBuffer {
    vec4 texCoords[];
};

layout(binding = 0, rgba8) writeonly uniform image2D outImage;  // Output image

uniform sampler2D testTex;
uniform vec2 lookDir;        // Look direction of our player (normalized)
uniform vec2 camera;         // Camera plane vector
uniform vec2 playerPos;      // Position of the player
uniform uint WINDOW_WIDTH;
uniform uint WINDOW_HEIGHT;
uniform uint texWidth;
uniform uint texHeight;

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;

    if (x >= WINDOW_WIDTH || y >= WINDOW_HEIGHT){
        imageStore(outImage, ivec2(int(x), int(y)), vec4(1, 1, 1, 1));
        return;
    }

    vec2 wall = wallRanges[x];
    if (wall.x == 0 && wall.y == 0){
        imageStore(outImage, ivec2(int(x), int(y)), vec4(1, 1, 1, 1));
        return;
    }

    // Check if the current point is a wall, if not then don't render anything
    if (y > wall.y || y < wall.x){
        return;
    }

    float startTexX = texCoords[x].x;
    float stepX = texCoords[x].y;     // Unused
    float stepY = texCoords[x].w;
    float startTexY = texCoords[x].z;

    // Floor texture mapping
    float rayDirX0 = lookDir.x - camera.x;
    float rayDirY0 = lookDir.y - camera.y;
    float rayDirX1 = lookDir.x + camera.x;
    float rayDirY1 = lookDir.y + camera.y;

    int texX = int(startTexX);
    startTexY += stepY * (wall.y - y);
    int texY = clamp(int(startTexY), 0, int(texHeight) - 1);

    vec4 color = texture(testTex, vec2(float(texX) / float(texWidth), float(texY) / float(texHeight)));
    imageStore(outImage, ivec2(int(x), int(y)), color);
}