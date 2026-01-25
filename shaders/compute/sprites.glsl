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

struct atlasUV {
    float x;
    float y;

    float width;
    float height;
};

layout(std430, binding = 0) buffer spriteBuf {
    spriteData sprites[];
};

// A buffer that shows the depth of each column
layout(std430, binding = 1) buffer ZBufferBuf {
    float ZBuffer[];
};

layout(std430, binding = 2) buffer sortedIndexesBuffer {
    int sortedIndexes[];
};

layout(std430, binding = 3) buffer boundingBoxesBuf {
    vec4 boundingBoxes[];
};

layout(std430, binding = 4) buffer spriteResultsbuf {
    spriteResult spriteResults[];
};

layout(std430, binding = 5) buffer atlasBuf {
    atlasUV atlasUVs[];
};

layout (rgba8, binding = 0) writeonly uniform image2D outImage;

uniform sampler2D atlas;
uniform uint atlasW;
uniform uint atlasH;
uniform vec2 playerPos;
uniform vec2 lookDir;
uniform vec2 camera;
uniform uint numSprites;

uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;

void main() {
    int x = int(gl_GlobalInvocationID.x);

    if (x >= WINDOW_WIDTH){
        return;
    }

    for (int i = 0; i < numSprites; i++){
        spriteData sprite = sprites[sortedIndexes[i]];
        uint textureIdx = sprite.texture;
        vec3 invisColor = vec3(sprite.invisColor[0], sprite.invisColor[1], sprite.invisColor[2]);

        vec4 box = boundingBoxes[sortedIndexes[i]];
        spriteResult result = spriteResults[sortedIndexes[i]];

        float spriteScreenX = result.spriteScreenX;
        float transformY = result.transformY;
        float spriteWidth = result.spriteWidth;
        float spriteHeight = result.spriteHeight;

        float spriteTexWidth = result.spriteTexWidth;
        float spriteTexHeight = result.spriteTexHeight;

        float drawStartX = box.x;
        float drawStartY = box.y;
        float drawEndX = box.z;
        float drawEndY = box.w;

        // Check if the sprites current pixel is visible + 
        if (x >= drawStartX && x < drawEndX && transformY > 0 && transformY < ZBuffer[x]){
            // Loop through every vertical stripe of the sprite on screen
            for (float y = drawStartY; y < drawEndY; y++){
                int texX = int((x + spriteWidth / 2 - spriteScreenX) * spriteTexWidth / spriteWidth);
                int texY = int((((y - WINDOW_HEIGHT / 2 + spriteHeight / 2) * spriteTexHeight) / spriteHeight));

                atlasUV atlasRect = atlasUVs[textureIdx];
                vec2 atlasCoord = vec2(atlasRect.x, atlasRect.y);

                // Load the color from the image based on the previously set attributes
                vec2 texCoord;
                texCoord.x = float(texX) / float(atlasW);
                texCoord.y = float(texY) / float(atlasH);

                vec4 color = texture(atlas, atlasCoord + texCoord);
                
                // Don't draw if its an invisible color
                if (!(color.x == invisColor.x && color.y == invisColor.y && color.z == invisColor.z)){
                    imageStore(outImage, ivec2(int(x), int(y)), color);
                }
            }
        }
    }
}