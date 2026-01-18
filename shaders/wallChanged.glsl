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
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;
uniform sampler2D tex3;
uniform sampler2D tex4;
uniform sampler2D tex5;
uniform sampler2D tex6;
uniform sampler2D tex7;
uniform sampler2D tex8;

uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;
uniform float texWidth;
uniform float texHeight;

void main() {
    int x = int(gl_GlobalInvocationID.x);

    if (x >= WINDOW_WIDTH){
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

    for (int y = int(wall.x); y < int(wall.y); y++){
        texX = clamp(texX, 0, texWidth - 1);
        texY = clamp(texY, 0, texHeight - 1);
        float normalizedX = texX / texWidth;
        float normalizedY = texY / texHeight;

        vec2 texCoord = vec2(normalizedX, normalizedY);
        vec4 color = vec4(0);
        // SWITCH STATEMENT
        switch(textureIdx){
            case -1: color = vec4(0); break;
            case 0: color = texture(tex0, texCoord); break;
            case 1: color = texture(tex1, texCoord); break;
            case 2: color = texture(tex2, texCoord); break;
            case 3: color = texture(tex3, texCoord); break;
            case 4: color = texture(tex4, texCoord); break;
            case 5: color = texture(tex5, texCoord); break;
            case 6: color = texture(tex6, texCoord); break;
            case 7: color = texture(tex7, texCoord); break;
            case 8: color = texture(tex8, texCoord); break;
        }

        imageStore(outImage, ivec2(int(x), int(y)), color);

        texY += stepY;
    }
}