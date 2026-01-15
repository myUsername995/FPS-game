#version 430

layout (local_size_x = 16, local_size_y = 16) in;

struct spriteData {
    // General data
    vec2 pos;
    uint width;
    uint height;

    // playerData
    vec2 lookDir;
    uint isMoving;
    uint animationStep;
    
    // Sprite specific data
    uint texture;

    uint isPlayer;
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

layout (rgba8, binding = 0) writeonly uniform image2D outImage;

// 0 - 32 -> playerRunTextures
// 32 - 40 -> playerTextures
// spriteTextures -> different array

// UNIFORMS
uniform sampler2D spriteTextures[3];
uniform sampler2DArray playerTextures;

uniform vec2 playerPos;
uniform vec2 lookDir;
uniform vec2 camera;
uniform uint numSprites;

uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);

    if (x >= WINDOW_WIDTH || y >= WINDOW_HEIGHT){
        return;
    }

    for (int i = 0; i < numSprites; i++){
        // These are the attributes that differ between a player and a sprite texture
        vec3 invisColor; // RGB (alpha doesn't matter)
        uint textureIdx;

        // General sprite attributes
        spriteData sprite = sprites[sortedIndexes[i]];

        vec2 spritePos = sprite.pos;
        float spriteTexWidth = sprite.width;
        float spriteTexHeight = sprite.height;

        // Load sprite textures width and height
        if (sprite.isPlayer == 0){
            textureIdx = sprite.texture;
            invisColor = vec3(0,0,0);         // Black
        }
        // Calculate the right textureIdx to show the right player
        else {
            invisColor = vec3(152.0/255.0, 0.0, 136.0/255.0);   // Purple (background of player sprites)

            vec2 spriteDir = normalize(sprite.lookDir);
            vec2 toCamera;
            toCamera.x = playerPos.x - spritePos.x;
            toCamera.y = playerPos.y - spritePos.y;
            toCamera = normalize(toCamera);

            float angle = atan(
                spriteDir.x * toCamera.y - spriteDir.y * toCamera.x, // 2D cross
                dot(spriteDir, toCamera)                              // dot product
            );

            float degree = angle * 180.0 / 3.14159;
            if (degree < 0) degree += 360;

            uint playerTexture = uint(mod(uint((degree + 22.5) / 45.0), 8));

            // Moving player texture
            if (sprite.isMoving){
                textureIdx = sprite.animationStep * 8 + playerTexture;
            }
            // Standing player texture
            else {
                textureIdx = 32 + playerTexture;
            }
        }

        float spriteX = spritePos.x - playerPos.x;
        float spriteY = spritePos.y - playerPos.y;

        float invDet = 1.0 / (camera.x * lookDir.y - lookDir.x * camera.y);

        float transformX = invDet * (lookDir.y * spriteX - lookDir.x * spriteY);
        float transformY = invDet * (-camera.y * spriteX + camera.x * spriteY);

        int spriteScreenX = int((WINDOW_WIDTH / 2) * (1 + transformX / transformY));

        // Calculate height of the sprite on screen
        int spriteHeight = int(abs((WINDOW_HEIGHT / transformY))); // Using 'transformY' instead of the real distance prevents fisheye
        int drawStartY = WINDOW_HEIGHT / 2 - spriteHeight / 2;
        if (drawStartY < 0) drawStartY = 0;
        int drawEndY = WINDOW_HEIGHT / 2 + spriteHeight  / 2;
        if (drawEndY >= WINDOW_HEIGHT) drawEndY = WINDOW_HEIGHT;

        // Calculate width of the sprite
        int spriteWidth = int(abs((WINDOW_HEIGHT / transformY)));
        int drawStartX = spriteScreenX - spriteWidth / 2;
        if (drawStartX < 0) drawStartX = 0;
        int drawEndX = spriteScreenX + spriteWidth / 2;
        if (drawEndX >= WINDOW_WIDTH) drawEndX = WINDOW_WIDTH - 1;

        // Check if the sprites current pixel is visible
        if (x >= drawStartX && x < drawEndX && y >= drawStartY && y < drawEndY){
            // Loop through every vertical stripe of the sprite on screen
            int texX = int((x + spriteWidth / 2 - spriteScreenX) * spriteTexWidth / spriteWidth);

            // Is the sprite infront of the camera and is it infront of all the walls?
            if (transformY > 0 && transformY < ZBuffer[x]){
                int texY = int((((y - WINDOW_HEIGHT / 2 + spriteHeight / 2) * spriteTexHeight) / spriteHeight));

                // Load the color from the image based on the previously set attributes
                vec2 texCoord;
                texCoord.x = 1.0 - float(texX) / float(spriteTexWidth);
                texCoord.y = 1.0 - float(texY) / float(spriteTexHeight);

                vec4 color;
                if (sprite.isPlayer == 0){
                    color = texture(spriteTextures[textureIdx], texCoord);
                }
                else {
                    color = texture(playerTextures, vec3(texCoord.xy, textureIdx));
                }
                
                // Don't draw if its an invisible color
                if (!(color.x == invisColor.x && color.y == invisColor.y && color.z == invisColor.z)){
                    imageStore(outImage, ivec2(int(x), int(y)), color);
                }
            }
        }
    }
}