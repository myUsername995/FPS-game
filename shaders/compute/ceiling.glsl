#version 430 

layout(local_size_x = 16, local_size_y = 16) in;

struct columnData {
    vec4 texCoord;
    ivec2 wallRange;
    int texture;
};

struct atlasUV {
    float x;
    float y;

    float width;
    float height;
};

layout(std430, binding = 0) buffer columnBuf {
    columnData columns[];
};

layout(std430, binding = 1) buffer atlasBuf {
    atlasUV atlasUVs[];
};

layout(binding = 0, rgba8) writeonly uniform image2D outImage;  // Output image

uniform sampler2D atlas;
uniform uint atlasW;
uniform uint atlasH;
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

    vec2 ceilStep = vec2(rowDistance * (rayDirX1 - rayDirX0) / WINDOW_WIDTH,
                            rowDistance * (rayDirY1 - rayDirY0) / WINDOW_WIDTH);

    vec2 ceilPos = vec2(playerPos.x + rowDistance * rayDirX0,
                            playerPos.y + rowDistance * rayDirY0);

    ceilPos += ceilStep * float(x);

    uint textureIdx = 6;
    atlasUV atlasRect = atlasUVs[textureIdx];
    vec2 atlasCoord = vec2(atlasRect.x, atlasRect.y);

    vec2 texCoord = fract(ceilPos); // Coordinates in local texture space
    // Convert to atlas space
    texCoord.x = (texCoord.x * atlasRect.width) / float(atlasW);
    texCoord.y = (texCoord.y * atlasRect.height) / float(atlasH);

    vec4 color = texture(atlas, atlasCoord + texCoord);

    imageStore(outImage, ivec2(x, y), color);
}