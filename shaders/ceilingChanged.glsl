#version 430 core 

layout(local_size_x = 16, local_size_y = 16) in;

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
uniform sampler2D textures [9];
uniform vec2 lookDir;        // Look direction of our player (normalized)
uniform vec2 camera;         // Camera plane vector
uniform vec2 playerPos;      // Position of the player
uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);

    if (x >= WINDOW_WIDTH || y >= WINDOW_HEIGHT){
        return;
    }

    columnData curColumn = columns[x];

    vec2 wall = curColumn.wallRange;
    bool noCollision = wall.x == 0 && wall.y == 0;

    // Check if the current pixel is a ceiling, either: 
    // No collision: must be greater than WINDOW_HEIGHT / 2 (horizon)
    // Collision: must be greater than the walls height
    if ((noCollision && y < WINDOW_HEIGHT / 2) || (!noCollision && y < wall.y)){
        return;
    }

    float startTexX = curColumn.texCoord.x;
    float stepY = curColumn.texCoord.w;
    float startTexY = curColumn.texCoord.z;

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

    uint textureIdx = 6;
    vec2 texCoord = fract(floorPos);
    vec4 color = texture(textures[textureIdx], texCoord);

    imageStore(outImage, ivec2(x, y), color);
}