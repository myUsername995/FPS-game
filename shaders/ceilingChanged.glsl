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

// UNIFORMS
uniform sampler2D tex1;
uniform sampler2D tex2;
uniform sampler2D tex3;
uniform sampler2D tex4;
uniform sampler2D tex5;
uniform sampler2D tex6;
uniform sampler2D tex7;
uniform sampler2D tex8;
uniform sampler2D tex9;

uniform vec2 lookDir;        // Look direction of our player (normalized)
uniform vec2 camera;         // Camera plane vector
uniform vec2 playerPos;      // Position of the player
uniform uint WINDOW_WIDTH;
uniform uint WINDOW_HEIGHT;

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;

    if (x >= WINDOW_WIDTH || y >= WINDOW_HEIGHT){
        imageStore(outImage, ivec2(int(x), int(y)), vec4(1, 1, 1, 1));
        return;
    }

    vec2 wall = wallRanges[x];
    bool noCollision = wall.x == 0 && wall.y == 0;

    // Check if the current pixel is a ceiling, either: 
    // No collision: must be greater than WINDOW_HEIGHT / 2 (horizon)
    // Collision: must be greater than the walls height
    if ((noCollision && y < WINDOW_HEIGHT / 2) || (!noCollision && y < wall.y)){
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

    uint textureIdx = 5;
    vec2 texCoord = fract(floorPos);
    vec4 color;

    // SWITCH STATEMENT
    switch (textureIdx){
        case 1: color = texture(tex1, texCoord); break;
        case 2: color = texture(tex2, texCoord); break;
        case 3: color = texture(tex3, texCoord); break;
        case 4: color = texture(tex4, texCoord); break;
        case 5: color = texture(tex5, texCoord); break;
        case 6: color = texture(tex6, texCoord); break;
        case 7: color = texture(tex7, texCoord); break;
        case 8: color = texture(tex8, texCoord); break;
        case 9: color = texture(tex9, texCoord); break;
        default: break;
    }

    imageStore(outImage, ivec2(x, y), color);
}