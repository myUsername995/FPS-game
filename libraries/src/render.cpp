#include "render.hpp"
#include <vector>
#include <iostream>
#include <algorithm>
#include <array>
#include <fstream>
#include <SDL3_image/SDL_image.h>
#include "time.hpp"

std::string computeFolder = "shaders\\compute";
std::string vertexFolder = "shaders\\vertex";
std::string fragmentFolder = "shaders\\fragment";

// Sprite struct
struct spriteData {
    // General data
    float invisColor[3];
    int padding4;
    float pos[2];
    uint32_t width;
    uint32_t height;
    uint32_t texture = 0;

    uint32_t isPlayer = 0;
};
static_assert(sizeof(spriteData) == 40);

struct columnData {
    float texCoord[4];
    int wallRange[2];
    int texture;
    int padding;            // 28 -> 32, 4 bytes of padding
};
static_assert(sizeof(columnData) == 32);

struct lineData {
    float p1[2];
    float p2[2];
    int texture;
    int padding;            // 20 -> 24, 4 bytes of padding
};
static_assert(sizeof(lineData) == 24);

struct atlasUV {
    float x;
    float y;

    float width;
    float height;
};
static_assert(sizeof(atlasUV) == 16);

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
static_assert(sizeof(spriteResult) == 24);

// The minimum things someones needs for compatibility
GLint glMinTextureSize = 4096;

// Shaders
static GLuint computeShader, wallShader, floorShader, ceilingShader, spriteShader, spriteCompShader;
static GLuint screenShader, atlasShader;

// Metadata
static int W, H;
static int outputW, outputH;

// Textures
static GLuint outputTex = 0;

// Buffers
static GLuint glMapDepthBuf = 0;

// Buffers for map rendering
static GLuint glMapLinesData = 0;
static GLuint glMapColumnsData = 0;

// Buffers for sprite rendering
static GLuint glSpritesData = 0;
static GLuint glSpriteSortedIndexes = 0;
static GLuint glSpriteBoundingBoxes = 0;
static GLuint glSpriteResults = 0;

// Map data
static std::vector<lineData> mapLinesData;
static std::vector<GLint> mapUnits;           // Stores which texture units to bind to the wall textures array
static int numLines;

// Sprite data
static std::vector<spriteData> spritesData;
static std::vector<GLint> spriteUnits;
static int numSprites;

// Atlas textures
static GLuint glWallAtlas = 0;
static GLuint glSpriteAtlas = 0;
static GLuint glScreenAtlas = 0;

// Atlas buffers (for the GPU)
static GLuint glWallUVs = 0;
static GLuint glSpriteUVs = 0;
static GLuint glScreenUVs = 0;

// Atlas data (for the CPU)
static std::vector<atlasUV> wallUVs;
static std::vector<atlasUV> spriteUVs;
static std::vector<atlasUV> screenUVs;

// Globals for VAO/VBO/EBO
static GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;

enum shaderType {
    COMPUTE,
    WALL,
    FLOOR,
    CEILING,
    SPRITE
};

bool checkCompatibility(){
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

    if (maxTextureSize < glMinTextureSize){
        std::cout << "GL_MAX_TEXTURE_SIZE: " << maxTextureSize << " is less than the minimum required size: " 
                  << glMinTextureSize << std::endl;

        return false;
    }

    return true;
}

// Set the texture uniforms to the right textures for the map
void setMapUniforms(GLuint currentShader){
    glUniform1i(glGetUniformLocation(currentShader, "atlas"), 0);
}

// Set the texture uniforms to the right textures for the sprites
void setSpriteUniforms(){
    glUniform1i(glGetUniformLocation(spriteShader, "atlas"), 0);
}

void activateWallTextures(){
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glWallAtlas);
}

void activateSpriteTextures(){
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glSpriteAtlas);
}

void getWindowSize(SDL_Window* window){
    SDL_GetWindowSize(window, &W, &H);
}

void initQuad() {
    if (quadVAO != 0) return; // already initialized

    // Vertex data for a full-screen quad (NDC)
    static const float quadVertices[] = {
        // positions   // uvs
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
        1.0f, -1.0f, 1.0f, 0.0f, // bottom-right
        1.0f,  1.0f, 1.0f, 1.0f, // top-right
        -1.0f,  1.0f, 0.0f, 1.0f  // top-left
    };

    static const unsigned int quadIndices[] = {
        0, 1, 2,
        2, 3, 0
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glGenBuffers(1, &quadEBO);

    glBindVertexArray(quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // uv attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void drawTexture(GLuint tex, float r = -1, float g = -1, float b = -1){
    glUseProgram(screenShader);

    glUniform3f(glGetUniformLocation(screenShader, "filterColor"), r, g, b);

    // bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(screenShader, "u_tex"), 0); // texture unit 0

    // draw quad
    glBindVertexArray(quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void drawAtlasTexture(GLuint atlas, float atlasRect[4], float r = -1, float g = -1, float b = -1){
    glUseProgram(atlasShader);

    // bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);

    glUniform1i(glGetUniformLocation(atlasShader, "atlas"), 0); // texture unit 0
    glUniform1f(glGetUniformLocation(atlasShader, "atlasW"), glMinTextureSize);
    glUniform1f(glGetUniformLocation(atlasShader, "atlasH"), glMinTextureSize);
    glUniform1f(glGetUniformLocation(atlasShader, "texWidth"), atlasRect[2]);
    glUniform1f(glGetUniformLocation(atlasShader, "texHeight"), atlasRect[3]);
    glUniform2f(glGetUniformLocation(atlasShader, "atlasCoord"), atlasRect[0], atlasRect[1]);
    glUniform3f(glGetUniformLocation(atlasShader, "filterColor"), r, g, b);

    // draw quad
    glBindVertexArray(quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// Initialise once, then resize if necessary
bool createOutputTextures(){
    if (outputTex != 0) glDeleteTextures(1, &outputTex);

    // Create the textures
    // Output
    glGenTextures(1, &outputTex);
    glBindTexture(GL_TEXTURE_2D, outputTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    if (outputTex == 0){
        std::cout << "Failed to load output texture.\n";
        return false;
    }

    return true;
}

struct AtlasItem {
    SDL_Surface* surface;
    int index;
};

// Use the shelf-packing algorithm to generate an atlas
SDL_Surface* generateAtlas(const std::vector<SDL_Surface*>& textures, std::vector<atlasUV>& atlasUVs){
    float texWidth = glMinTextureSize;
    float texHeight = glMinTextureSize;
    SDL_Surface* atlas = SDL_CreateSurface(texWidth, texHeight, SDL_PIXELFORMAT_RGBA32);

    std::vector<AtlasItem> items(textures.size());
    for (int i = 0; i < textures.size(); i++){
        items[i].surface = textures[i];
        items[i].index = i;
    }

    atlasUVs.resize(textures.size());
    std::sort(items.begin(), items.end(), [](AtlasItem i1, AtlasItem i2){return i1.surface->h > i2.surface->h;});

    int x = 0;
    int y = 0;
    int rowHeight = 0;
    int padding = 2;
    for (auto& item : items) {
        SDL_Surface* surf = item.surface;

        int w = surf->w + padding * 2;
        int h = surf->h + padding * 2;

        if (x + w > texWidth){
            x = 0;
            y += rowHeight;
            rowHeight = 0;
        }

        if (y + h > texHeight){
            std::cout << "Atlas overflow!\n";
            break;
        }

        SDL_Rect dst {
            x + padding,
            y + padding,
            surf->w,
            surf->h
        };

        SDL_BlitSurface(surf, nullptr, atlas, &dst);

        atlasUVs[item.index] = {
            float(dst.x) / texWidth,
            float(dst.y) / texHeight,
            float(dst.w),
            float(dst.h)
        };

        x += w;
        rowHeight = std::fmax(rowHeight, h);
    }

    return atlas;
}

// Only do this once at initalisation
bool createInputTextures(const gameState& state, int numWallTexs, int numSpriteTexs, int numScreenTexs){
    // Convert the surfaces to the right format that I input to openGL
    std::vector<SDL_Surface*> wallTextures(numWallTexs);
    std::vector<SDL_Surface*> spriteTextures(numSpriteTexs);
    std::vector<SDL_Surface*> screenTextures(numScreenTexs);

    for (int i = 0; i < numWallTexs; i++) wallTextures[i] = SDL_ConvertSurface(state.wallTextures[i].texture, SDL_PIXELFORMAT_RGBA32);
    for (int i = 0; i < numSpriteTexs; i++) spriteTextures[i] = SDL_ConvertSurface(state.spriteTextures[i].texture, SDL_PIXELFORMAT_RGBA32);
    for (int i = 0; i < numScreenTexs; i++) screenTextures[i] = SDL_ConvertSurface(state.screenTextures[i].texture, SDL_PIXELFORMAT_RGBA32);

    SDL_Surface* wallAtlas = generateAtlas(wallTextures, wallUVs);
    SDL_Surface* spriteAtlas = generateAtlas(spriteTextures, spriteUVs);
    SDL_Surface* screenAtlas = generateAtlas(screenTextures, screenUVs);

    IMG_SavePNG(wallAtlas, "constTextures\\debugWallAtlas.png");
    IMG_SavePNG(spriteAtlas, "constTextures\\debugSpriteAtlas.png");
    IMG_SavePNG(screenAtlas, "constTextures\\debugScreenAtlas.png");

    // Free the textures
    for (int i = 0; i < numWallTexs; i++) SDL_DestroySurface(wallTextures[i]);
    for (int i = 0; i < numSpriteTexs; i++) SDL_DestroySurface(spriteTextures[i]);
    for (int i = 0; i < numScreenTexs; i++) SDL_DestroySurface(screenTextures[i]);

    if (glWallAtlas != 0) glDeleteTextures(1, &glWallAtlas);
    if (glSpriteAtlas != 0) glDeleteTextures(1, &glSpriteAtlas);
    if (glScreenAtlas != 0) glDeleteTextures(1, &glScreenAtlas);

    // Wall atlas
    glGenTextures(1, &glWallAtlas);
    glBindTexture(GL_TEXTURE_2D, glWallAtlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wallAtlas->w, wallAtlas->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, wallAtlas->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Sprite atlas
    glGenTextures(1, &glSpriteAtlas);
    glBindTexture(GL_TEXTURE_2D, glSpriteAtlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, spriteAtlas->w, spriteAtlas->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, spriteAtlas->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Screen atlas
    glGenTextures(1, &glScreenAtlas);
    glBindTexture(GL_TEXTURE_2D, glScreenAtlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenAtlas->w, screenAtlas->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, screenAtlas->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (glWallAtlas == 0){
        std::cout << "Failed to load input textures.\n";
        return false;
    }
    if (glSpriteAtlas == 0){
        std::cout << "Failed to load input textures.\n";
        return false;
    }
    if (glScreenAtlas == 0){
        std::cout << "Failed to load input textures.\n";
        return false;
    }

    return true;
}

/*
The players texture are put like this into a linear array ->
0-31: run texture
32-39: standing texture
39-44: death texture
44-47: shoot texture
48: hit texture
49-x: other textures
*/
int calculatePlayerTexIndex(Player& otherPlayer, const gameState& state){
    Vector spriteDir = otherPlayer.lookDir.normalize();
    Vector toCamera;
    toCamera.x = state.player.pos.x - otherPlayer.pos.x;
    toCamera.y = state.player.pos.y - otherPlayer.pos.y;
    toCamera.normalize();

    double angle = atan2(
        spriteDir.x * toCamera.y - spriteDir.y * toCamera.x,
        spriteDir.x * toCamera.x + spriteDir.y * toCamera.y
    );

    double deg = angle * 180.0 / PI;
    if (deg < 0) deg += 360;

    int playerTexture = int((deg + 22.5) / 45.0) % 8;

    // Assign texture
    // Hit texture
    if (otherPlayer.hit) return 48;

    // Fired
    if (otherPlayer.fired) return 45 + SDL_clamp(otherPlayer.gunFrame, 0, 2);

    // Dead
    if (otherPlayer.health <= 0) return 40 + otherPlayer.deadFrame;

    // Running texture
    if (otherPlayer.isMoving) return otherPlayer.animationStep * 8 + playerTexture;

    // Standing texture
    return 32 + playerTexture;
}

void fillSpriteArrays(const gameState& state){
    numSprites = state.fullNumSprites;
    spritesData.resize(numSprites);
    int mapWidth = state.map.size();
    int mapHeight = state.map[0].size();

    std::vector<Player> otherPlayers = state.otherPlayers;
    for (int i = 0; i < numSprites; i++){
        // General attributes
        spritesData[i].pos[0] = state.sprites[i].pos.x;
        spritesData[i].pos[1] = state.sprites[i].pos.y;

        // Player specific attributes
        if (state.sprites[i].isPlayer){
            Player otherP = otherPlayers[state.sprites[i].index];

            // Purple (background of sprite textures)
            spritesData[i].invisColor[0] = 152.0f / 255.0f;
            spritesData[i].invisColor[1] = 0.0f / 255.0f;
            spritesData[i].invisColor[2] = 136.0f / 255.0f;

            // Calculate the texture index CPU side (can be determined before rendering)
            Player otherPlayer = state.otherPlayers[state.sprites[i].index];
            spritesData[i].texture = calculatePlayerTexIndex(otherPlayer, state);

            spritesData[i].width = 64;
            spritesData[i].height = 64;
            spritesData[i].isPlayer = 1;
        }
        // Sprite specific data
        else {
            Texture curTex = state.spriteTextures[40 + state.sprites[i].texture];

            // Black
            spritesData[i].invisColor[0] = 0.0f / 255.0f;
            spritesData[i].invisColor[1] = 0.0f / 255.0f;
            spritesData[i].invisColor[2] = 0.0f / 255.0f;

            spritesData[i].texture = 49 + state.sprites[i].texture;

            spritesData[i].width = curTex.width;
            spritesData[i].height = curTex.height;
            spritesData[i].isPlayer = 0;
        }
    }
}

void initBuffers(){
    std::vector<GLuint*> buffers = {&glMapLinesData, &glMapColumnsData, &glMapDepthBuf, &glSpritesData, &glSpriteSortedIndexes, 
                                    &glSpriteBoundingBoxes, &glSpriteResults, &glWallUVs, &glSpriteUVs, &glScreenUVs};

    for (int i = 0; i < buffers.size(); i++){
        if (*(buffers[i]) != 0) glDeleteBuffers(1, buffers[i]);

        glGenBuffers(1, buffers[i]);
    }
}

// Called once at initialisation
int initShaders(SDL_Window* window, const gameState& state){
    if (!checkCompatibility()) return -1;

    // Resize the window
    getWindowSize(window);
    int numWallTexs = state.wallTextures.size();
    int numSpriteTexs = state.spriteTextures.size();
    int numScreenTexs = state.screenTextures.size();
    numLines = state.lineMap.size();
    numSprites = state.fullNumSprites;

    std::string wallStr = LoadFile(computeFolder + "\\wall.glsl");
    std::string floorStr = LoadFile(computeFolder + "\\floor.glsl");
    std::string ceilingStr = LoadFile(computeFolder + "\\ceiling.glsl");
    std::string spriteStr = LoadFile(computeFolder + "\\sprites.glsl");

    // Initialize the rendering output
    initQuad();

    // Initialise buffers used for transfering data between the shaders
    initBuffers();

    GLuint computeProgram = CompileShader(LoadFile(computeFolder + "\\raycast.glsl"), GL_COMPUTE_SHADER);
    GLuint wallProgram = CompileShader(wallStr, GL_COMPUTE_SHADER);
    GLuint floorProgram = CompileShader(floorStr, GL_COMPUTE_SHADER);
    GLuint ceilingProgram = CompileShader(ceilingStr, GL_COMPUTE_SHADER);
    GLuint spriteProgram = CompileShader(spriteStr, GL_COMPUTE_SHADER);
    GLuint spriteComputeProgram = CompileShader(LoadFile(computeFolder + "\\spriteCompute.glsl"), GL_COMPUTE_SHADER);

    if (computeProgram == 0 || wallProgram == 0 || floorProgram == 0 || ceilingProgram == 0 || spriteProgram == 0 || spriteComputeProgram == 0){
        return -1;
    }

    computeShader = CreateComputeProgram(computeProgram);
    wallShader = CreateComputeProgram(wallProgram);
    floorShader = CreateComputeProgram(floorProgram);
    ceilingShader = CreateComputeProgram(ceilingProgram);
    spriteShader = CreateComputeProgram(spriteProgram);
    spriteCompShader = CreateComputeProgram(spriteComputeProgram);

    if (computeShader == 0 || wallShader == 0 || floorShader == 0 || ceilingShader == 0 || spriteShader == 0 || spriteCompShader == 0){
        return -1;
    }

    // Render textures on the screen
    screenShader = CreateProgram(CompileShader(LoadFile(vertexFolder + "\\screen.vert"), GL_VERTEX_SHADER), 
                                 CompileShader(LoadFile(fragmentFolder + "\\screen.frag"), GL_FRAGMENT_SHADER));

    atlasShader = CreateProgram(CompileShader(LoadFile(vertexFolder + "\\atlas.vert"), GL_VERTEX_SHADER), 
                                CompileShader(LoadFile(fragmentFolder + "\\atlas.frag"), GL_FRAGMENT_SHADER));

    if (screenShader == 0) return -1;
    if (atlasShader == 0) return -1;

    // Create the GPU arrays
    // Map arrays
    mapLinesData.resize(numLines);
    for (int i = 0; i < numLines; i++) {
        mapLinesData[i].p1[0] = state.lineMap[i].p1.x;
        mapLinesData[i].p1[1] = state.lineMap[i].p1.y;
        mapLinesData[i].p2[0] = state.lineMap[i].p2.x;
        mapLinesData[i].p2[1] = state.lineMap[i].p2.y;

        mapLinesData[i].texture = state.lineMap[i].texture;
    }

    // Sprite arrays
    fillSpriteArrays(state);

    // Create the output and input textures
    createOutputTextures();
    createInputTextures(state, numWallTexs, numSpriteTexs, numScreenTexs);

    return 1;
}

void resizeShaders(SDL_Window* window){
    getWindowSize(window);

    GPUResizeWindow(W, H);

    initBuffers();              // The columns buffer depends on window width
    createOutputTextures();     // Only output textures need to be resized
}

// Sort algorithm
// Sort the sprites based on distance
void sortSprites(const gameState& state, std::vector<int>& order){
    int amount = numSprites;
    order.resize(amount);
    std::vector<double> dist(amount);

    // SPRITE CASTING
    // Sort sprites from far to close
    for(int i = 0; i < amount; i++){
        order[i] = i;
        dist[i] = ((state.player.pos.x - state.sprites[i].pos.x) * (state.player.pos.x - state.sprites[i].pos.x) + 
                            (state.player.pos.y - state.sprites[i].pos.y) * (state.player.pos.y - state.sprites[i].pos.y)); //sqrt not taken, unneeded
    }

    std::vector<std::pair<double, int>> sprites(amount);
    for(int i = 0; i < amount; i++) {
        sprites[i].first = dist[i];
        sprites[i].second = order[i];
    }
    std::sort(sprites.begin(), sprites.end());
    // restore in reverse order to go from farthest to nearest
    for(int i = 0; i < amount; i++) {
        dist[i] = sprites[amount - i - 1].first;
        order[i] = sprites[amount - i - 1].second;
    }
}

void dispatchShader(shaderType type, const gameState& state){
    Vector lookDir = state.player.lookDir;
    Vector camera = state.player.camera;
    SDL_FPoint playerPosition = state.player.pos;

    // Handle compute shaders differently
    if (type == COMPUTE){
        glUseProgram(computeShader);

        // Create the buffers
        // Lines buffer (all the line data)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glMapLinesData);
        glBufferData(GL_SHADER_STORAGE_BUFFER, mapLinesData.size() * sizeof(lineData), mapLinesData.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, glMapLinesData);

        // Columns buffer (clear the arrays so they can be filled by the compute shader)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glMapColumnsData);
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * sizeof(columnData), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, glMapColumnsData);

        // Depth buffer (fill with no data, because the compute shader will fill it)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glMapDepthBuf);
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, glMapDepthBuf);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glWallUVs);
        glBufferData(GL_SHADER_STORAGE_BUFFER, wallUVs.size() * sizeof(atlasUV), wallUVs.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, glWallUVs);

        glUniform1ui(glGetUniformLocation(computeShader, "numLines"), (unsigned int)(numLines));
        glUniform2f(glGetUniformLocation(computeShader, "lookDir"), lookDir.x, lookDir.y);
        glUniform2f(glGetUniformLocation(computeShader, "camera"), camera.x, camera.y);
        glUniform2f(glGetUniformLocation(computeShader, "playerPos"), playerPosition.x, playerPosition.y);
        glUniform1i(glGetUniformLocation(computeShader, "WINDOW_WIDTH"), W);
        glUniform1i(glGetUniformLocation(computeShader, "WINDOW_HEIGHT"), H);

        int groupsX = (W + 255) / 256;
        glDispatchCompute(groupsX, 1, 1);

        // Wait for SSBO writes to be visible to next stage
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    else if (type == WALL){
        glUseProgram(wallShader);
        glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glMapColumnsData);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, glMapColumnsData);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glWallUVs);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, glWallUVs);

        // Set the texture related uniforms
        setMapUniforms(wallShader);

        glUniform1ui(glGetUniformLocation(wallShader, "atlasW"), glMinTextureSize);
        glUniform1ui(glGetUniformLocation(wallShader, "atlasH"), glMinTextureSize);
        glUniform1i(glGetUniformLocation(wallShader, "WINDOW_WIDTH"), W);
        glUniform1i(glGetUniformLocation(wallShader, "WINDOW_HEIGHT"), H);
        glUniform1f(glGetUniformLocation(wallShader, "texWidth"), 64);
        glUniform1f(glGetUniformLocation(wallShader, "texHeight"), 64);

        int renderX = (W + 255) / 256;
        glDispatchCompute(renderX, 1, 1);
    }
    else if (type == SPRITE){
        // Run the compute shader first
        glUseProgram(spriteCompShader);
        glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // Sprites data (1 spriteData struct)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpritesData); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, numSprites * sizeof(spriteData), spritesData.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, glSpritesData);

        // Bounding boxes (vec4)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpriteBoundingBoxes); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, 4 * numSprites * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, glSpriteBoundingBoxes);

        // Sprite results (1 spriteResult struct)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpriteResults); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, numSprites * sizeof(spriteResult), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, glSpriteResults);

        glUniform1ui(glGetUniformLocation(spriteCompShader, "numSprites"), numSprites);
        glUniform2f(glGetUniformLocation(spriteCompShader, "lookDir"), lookDir.x, lookDir.y);
        glUniform2f(glGetUniformLocation(spriteCompShader, "camera"), camera.x, camera.y);
        glUniform2f(glGetUniformLocation(spriteCompShader, "playerPos"), playerPosition.x, playerPosition.y);
        glUniform1i(glGetUniformLocation(spriteCompShader, "WINDOW_WIDTH"), W);
        glUniform1i(glGetUniformLocation(spriteCompShader, "WINDOW_HEIGHT"), H);

        int spritesX = (numSprites + 15) / 16;
        glDispatchCompute(spritesX, 1, 1);

        // Wait for SSBO writes to be visible to next stage
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // Run the render shader after
        // Sort the sprites
        std::vector<int> spriteOrder;
        sortSprites(state, spriteOrder);

        glUseProgram(spriteShader);
        glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // Sprites data (1 spriteData struct)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpritesData); 
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, glSpritesData);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glMapDepthBuf); 
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, glMapDepthBuf);

        // Sorted indexes (1 int)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpriteSortedIndexes);
        glBufferData(GL_SHADER_STORAGE_BUFFER, numSprites * sizeof(int), spriteOrder.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, glSpriteSortedIndexes);

        // Bounding boxes (vec4)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpriteBoundingBoxes); 
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, glSpriteBoundingBoxes);

        // Sprite results (1 spriteResult struct)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpriteResults); 
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, glSpriteResults);

        // Atlas UVs (1 atlasUV struct)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glSpriteUVs);
        glBufferData(GL_SHADER_STORAGE_BUFFER, spriteUVs.size() * sizeof(atlasUV), spriteUVs.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, glSpriteUVs);

        setSpriteUniforms();

        glUniform1ui(glGetUniformLocation(spriteShader, "atlasW"), glMinTextureSize);        
        glUniform1ui(glGetUniformLocation(spriteShader, "atlasH"), glMinTextureSize);
        glUniform1ui(glGetUniformLocation(spriteShader, "numSprites"), numSprites);
        glUniform2f(glGetUniformLocation(spriteShader, "lookDir"), lookDir.x, lookDir.y);
        glUniform2f(glGetUniformLocation(spriteShader, "camera"), camera.x, camera.y);
        glUniform2f(glGetUniformLocation(spriteShader, "playerPos"), playerPosition.x, playerPosition.y);
        glUniform1i(glGetUniformLocation(spriteShader, "WINDOW_WIDTH"), W);
        glUniform1i(glGetUniformLocation(spriteShader, "WINDOW_HEIGHT"), H);

        int renderX = (W + 15) / 16;
        glDispatchCompute(renderX, 1, 1);
    }
    else {
        // The other shaders are similar, so we can handle them simpler
        GLuint currentShader;
        switch (type){
            case FLOOR: currentShader = floorShader; break;
            case CEILING: currentShader = ceilingShader; break;
        }

        glUseProgram(currentShader);
        glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glMapColumnsData);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, glMapColumnsData);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, glWallUVs);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, glWallUVs);

        // Set the texture related uniforms
        setMapUniforms(currentShader);

        glUniform1ui(glGetUniformLocation(currentShader, "atlasW"), glMinTextureSize);
        glUniform1ui(glGetUniformLocation(currentShader, "atlasH"), glMinTextureSize);
        glUniform2f(glGetUniformLocation(currentShader, "lookDir"), lookDir.x, lookDir.y);
        glUniform2f(glGetUniformLocation(currentShader, "camera"), camera.x, camera.y);
        glUniform2f(glGetUniformLocation(currentShader, "playerPos"), playerPosition.x, playerPosition.y);
        glUniform1i(glGetUniformLocation(currentShader, "WINDOW_WIDTH"), W);
        glUniform1i(glGetUniformLocation(currentShader, "WINDOW_HEIGHT"), H);

        int renderX = (W + 15) / 16;
        int renderY = (H + 15) / 16;
        glDispatchCompute(renderX, renderY, 1);
    }
}

// Renders walls, floor, ceiling and sprites
void renderMap(SDL_Window* window, const gameState& state){
    // Setup for compute shader dispatches
    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glClearTexImage(outputTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    activateWallTextures();  

    // Compute everything firsst
    dispatchShader(COMPUTE, state);

    // Render order doesn't matter, they don't draw over each other
    dispatchShader(WALL, state);
    dispatchShader(FLOOR, state);
    dispatchShader(CEILING, state);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    fillSpriteArrays(state);
    activateSpriteTextures();
    // After rendering the map, render the sprites
    dispatchShader(SPRITE, state);

    // Ensure writes are visible
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    drawTexture(outputTex);

    // if the gunFrame is -1, that means no gun is equipped
    if (state.player.gunType != -1){
        float atlasRect[4] = {
            screenUVs[state.player.gunFrame].x, 
            screenUVs[state.player.gunFrame].y,
            screenUVs[state.player.gunFrame].width,
            screenUVs[state.player.gunFrame].height
        };

        drawAtlasTexture(glScreenAtlas, atlasRect, 152.0f/255.0f, 0.0f/255.0f, 136.0f/255.0f);
    }
}