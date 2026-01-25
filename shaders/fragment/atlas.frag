#version 430

in vec2 texCoord;
out vec4 FragColor;

uniform sampler2D atlas;
uniform float atlasW;
uniform float atlasH;
uniform float texWidth;
uniform float texHeight;
uniform vec2 atlasCoord;
uniform vec3 filterColor;

void main(){
    // Convert local space texCoord to atlas space
    vec2 atlasUV = texCoord;
    atlasUV.x = (atlasUV.x * texWidth) / atlasW;
    atlasUV.y = (atlasUV.y * texHeight) / atlasH;

    vec4 col = texture(atlas, atlasCoord + atlasUV);

    float epsilon = 0.1; // tolerance for the color comparison

    bool isFiltered = filterColor.x != -1.0 &&
                      abs(col.r - filterColor.r) < epsilon &&
                      abs(col.g - filterColor.g) < epsilon &&
                      abs(col.b - filterColor.b) < epsilon;

    if (!isFiltered){
        FragColor = col;
    }
    // Transparent color (just so we write to the whole screen cuz idk)
    else {
        FragColor = vec4(0.0);
    }
};