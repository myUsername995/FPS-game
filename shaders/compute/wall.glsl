#version 430

layout(local_size_x = 256, local_size_y = 1) in;

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

layout(binding = 0, rgba8) uniform image2D outImage;  // Output image

uniform sampler2D atlas;
uniform uint atlasW;
uniform uint atlasH;
uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;

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
        vec2 texCoord;
        float normalizedX = texX / float(atlasW);
        float normalizedY = texY / float(atlasH);
        texCoord.x = clamp(normalizedX, 0.0, 1.0);
        texCoord.y = clamp(normalizedY, 0.0, 1.0);

        atlasUV atlasRect = atlasUVs[textureIdx];
        vec2 atlasCoord = vec2(atlasRect.x, atlasRect.y);

        vec4 color = texture(atlas, atlasCoord + texCoord);

        imageStore(outImage, ivec2(int(x), int(y)), color);

        texY += stepY;
    }
}