#version 430

layout (local_size_x = 16, local_size_y = 1) in;

struct spriteData {
    float invisColor[3]; // vec3 is padded to vec4 in 430, so just use float[3]
    vec2 pos;
    uint width;
    uint height;
    uint texture;

    uint isPlayer;
};

// The results from the calculations done by spriteCompute
struct spriteResult {
    // World attributes
    float spriteScreenX;
    float transformY;
    float spriteWidth;
    float spriteHeight;

    // Texture attributes
    float spriteTexWidth;
    float spriteTexHeight;
};

layout(std430, binding = 0) buffer spriteBuf {
    spriteData sprites[];
};

// Stores bounding boxes per sprite (startX, startY, endX, endY)
layout(std430, binding = 1) buffer boundingBoxesBuf {
    vec4 boundingBoxes[];
};

layout(std430, binding = 2) buffer spriteResultsbuf {
    spriteResult spriteResults[];
};

layout (rgba8, binding = 0) writeonly uniform image2D outImage;

uniform uint numSprites;
uniform vec2 playerPos;
uniform vec2 lookDir;
uniform vec2 camera;

uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;

void main() {
    int index = int(gl_GlobalInvocationID.x);
    if (index >= numSprites) return;

    spriteData sprite = sprites[index];

    vec2 spritePos = sprite.pos;

    float spriteX = spritePos.x - playerPos.x;
    float spriteY = spritePos.y - playerPos.y;

    float invDet = 1.0 / (camera.x * lookDir.y - lookDir.x * camera.y);

    float transformX = invDet * (lookDir.y * spriteX - lookDir.x * spriteY);
    float transformY = invDet * (-camera.y * spriteX + camera.x * spriteY);

    int spriteScreenX = int((WINDOW_WIDTH / 2) * (1 + transformX / transformY));

    // Calculate height of the sprite on screen
    int spriteHeight = int(abs((WINDOW_HEIGHT / transformY))); // Using 'transformY' instead of the real distance prevents fisheye
    float drawStartY = WINDOW_HEIGHT / 2 - spriteHeight / 2;
    if (drawStartY < 0) drawStartY = 0;
    float drawEndY = WINDOW_HEIGHT / 2 + spriteHeight  / 2;
    if (drawEndY >= WINDOW_HEIGHT) drawEndY = WINDOW_HEIGHT;

    // Calculate width of the sprite
    int spriteWidth = int(abs((WINDOW_HEIGHT / transformY)));
    float drawStartX = spriteScreenX - spriteWidth / 2;
    if (drawStartX < 0) drawStartX = 0;
    float drawEndX = spriteScreenX + spriteWidth / 2;
    if (drawEndX >= WINDOW_WIDTH) drawEndX = WINDOW_WIDTH - 1;

    boundingBoxes[index] = vec4((drawStartX), (drawStartY), (drawEndX), (drawEndY));

    spriteResults[index].spriteScreenX = spriteScreenX;
    spriteResults[index].transformY = transformY;
    spriteResults[index].spriteWidth = spriteWidth;
    spriteResults[index].spriteHeight = spriteHeight;

    spriteResults[index].spriteTexWidth = sprite.width;
    spriteResults[index].spriteTexHeight = sprite.height;
}