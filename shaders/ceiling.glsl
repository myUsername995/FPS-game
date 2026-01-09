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

    // Check if the current pixel is a floor, if not then return
    if (y < wall.y){
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

    float p = y - (WINDOW_HEIGHT / 2);
    float rowDistance = (WINDOW_HEIGHT / 2) / p;

    vec2 floorStep = vec2(rowDistance * (rayDirX1 - rayDirX0) / WINDOW_WIDTH,
                            rowDistance * (rayDirY1 - rayDirY0) / WINDOW_WIDTH);

    vec2 floorPos = vec2(playerPos.x + rowDistance * rayDirX0,
                            playerPos.y + rowDistance * rayDirY0);

    floorPos += floorStep * float(x);

    vec4 color = texture(testTex, fract(floorPos)); // fract() replaces floorX-floor(floorX)
    imageStore(outImage, ivec2(x, y), color);
}