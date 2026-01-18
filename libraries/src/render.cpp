#include "render.hpp"
#include <vector>
#include <iostream>
#include <algorithm>
#include <array>
#include <fstream>
#include "time.hpp"

std::string shaderFolder = "shaders";

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

// Shaders
static GLuint computeShader, wallShader, floorShader, ceilingShader, spriteShader, spriteCompShader;
static GLuint screenShader;

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

// Textures
static std::vector<GLuint> glWallTextures;         // Stores the ID of each wall texture
static std::vector<GLuint> glSpriteTextures;   // Stores the ID of each sprite texture (players too)
static GLuint playerTextureID;

// Globals for VAO/VBO/EBO
static GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;

enum shaderType {
    COMPUTE,
    WALL,
    FLOOR,
    CEILING,
    SPRITE
};

// Set the texture uniforms to the right textures for the map
void setMapUniforms(GLuint currentShader){
    glUniform1iv(glGetUniformLocation(currentShader, "textures"), mapUnits.size(), mapUnits.data());
}

// Set the texture uniforms to the right textures for the sprites
void setSpriteUniforms(GLuint currentShader){
    glUniform1i(glGetUniformLocation(spriteShader, "playerTextures"), 0);
    glUniform1iv(glGetUniformLocation(currentShader, "spriteTextures"), spriteUnits.size(), spriteUnits.data());
}

void activateWallTextures(){
    for (int i = 0; i < glWallTextures.size(); i++){
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, glWallTextures[i]);
    }
}

void activateSpriteTextures(){
    // Player textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, playerTextureID);

    // Sprite textures
    for (int i = 0; i < glSpriteTextures.size(); i++){
        glActiveTexture(GL_TEXTURE1 + i);
        glBindTexture(GL_TEXTURE_2D, glSpriteTextures[i]);
    }
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

void drawTexture(GLuint screenShader, GLuint tex, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
    glUseProgram(screenShader);

    // set uniform color
    glUniform4f(glGetUniformLocation(screenShader, "u_color"), r, g, b, a);

    // bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(screenShader, "u_tex"), 0); // texture unit 0

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

// Only do this once at initalisation
bool createInputTextures(const std::vector<SDL_Surface*>& wallTextures, const std::vector<SDL_Surface*>& spriteTextures){
    // Wall textures
    for (int i = 0; i < wallTextures.size(); i++){
        if (glWallTextures[i] != 0) glDeleteTextures(1, &glWallTextures[i]);

        glGenTextures(1, &glWallTextures[i]);
        glBindTexture(GL_TEXTURE_2D, glWallTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wallTextures[i]->w, wallTextures[i]->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 
                     wallTextures[i]->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        if (glWallTextures[i] == 0){
            std::cout << "Failed to load input textures.\n";
            return false;
        }

        mapUnits[i] = i;
    }

    // Sprite textures
    // Player textures
    if (playerTextureID != 0) glDeleteTextures(1, &playerTextureID);

    glGenTextures(1, &playerTextureID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, playerTextureID);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 64, 64, 40, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (int i = 0; i < 40; i++){
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, 64, 64, 1, GL_RGBA, GL_UNSIGNED_BYTE, spriteTextures[i]->pixels);
    }

    // Normal sprites
    for (int i = 0; i < spriteTextures.size() - 40; i++){
        if (glSpriteTextures[i] != 0) glDeleteTextures(1, &glSpriteTextures[i]);

        glGenTextures(1, &glSpriteTextures[i]);
        glBindTexture(GL_TEXTURE_2D, glSpriteTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, spriteTextures[40 + i]->w, spriteTextures[40 + i]->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 
                     spriteTextures[40 + i]->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        if (glSpriteTextures[i] == 0){
            std::cout << "Failed to load input textures.\n";
            return false;
        }

        // Bind to the 1st, 2nd, 3nd etc texture units, because the 0th unit is bound to the player texture array
        spriteUnits[i] = i+1;
    }

    return true;
}

// GENERATE TEXTURES FOR WALL, FLOOR AND CEILING SHADER
void generateMapTextures(std::string& wallStr, std::string& floorStr, std::string& ceilingStr, int numTextures){
    // The length of a tab
    std::string tab = "    ";

    // Insert the uniforms into every string first
    std::string arrSize = std::to_string(numTextures);
    std::string uniforms = "uniform sampler2D textures[" + arrSize + "];";

    // Search for the identifier "// UNIFORMS""
    std::string uniformID = "// UNIFORMS\n";

    // Find positions
    int wallUniform = wallStr.find(uniformID) + uniformID.length();
    int floorUniform = floorStr.find(uniformID) + uniformID.length();
    int ceilingUniform = ceilingStr.find(uniformID) + uniformID.length();

    // Insert into the right place
    wallStr.insert(wallUniform, uniforms);
    floorStr.insert(floorUniform, uniforms);
    ceilingStr.insert(ceilingUniform, uniforms);

    {
        std::ofstream file(shaderFolder + "\\wallChanged.glsl");
        file.write(wallStr.c_str(), wallStr.size());

        file.close();
    }
    {
        std::ofstream file(shaderFolder + "\\floorChanged.glsl");
        file.write(floorStr.c_str(), floorStr.size());

        file.close();
    }
    {
        std::ofstream file(shaderFolder + "\\ceilingChanged.glsl");
        file.write(ceilingStr.c_str(), ceilingStr.size());

        file.close();
    }
}

// GENERATE TEXTURES FOR SPRITE SHADER
void generateSpriteTextures(std::string& spriteStr, int numTextures){
        // The length of a tab
    std::string tab = "    ";

    // Insert the uniforms into every string first
    std::string arrSize = std::to_string(numTextures);
    std::string uniforms = "uniform sampler2D spriteTextures[" + arrSize + "];";

    // Search for the identifier "// UNIFORMS"
    std::string uniformID = "// UNIFORMS\n";

    // Find positions
    int spriteUniform = spriteStr.find(uniformID) + uniformID.length();

    // Insert into the right place
    spriteStr.insert(spriteUniform, uniforms);

    std::ofstream file(shaderFolder + "\\spritesChanged.glsl");
    file.write(spriteStr.c_str(), spriteStr.size());

    file.close();
}

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

    int playerTexture = (int(((360 - deg) + 22.5) / 45.0) % 8);

    // Assign texture
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

            spritesData[i].texture = state.sprites[i].texture;

            spritesData[i].width = curTex.width;
            spritesData[i].height = curTex.height;
            spritesData[i].isPlayer = 0;
        }
    }
}

void initBuffers(){
    std::vector<GLuint*> buffers = {&glMapLinesData, &glMapColumnsData, &glMapDepthBuf, &glSpritesData, &glSpriteSortedIndexes, 
                                    &glSpriteBoundingBoxes, &glSpriteResults};

    for (int i = 0; i < buffers.size(); i++){
        if (*(buffers[i]) != 0) glDeleteBuffers(1, buffers[i]);

        glGenBuffers(1, buffers[i]);
    }
}

// Called once at initialisation
int initShaders(SDL_Window* window, const gameState& state){
    // Resize the window
    getWindowSize(window);
    int numWallTexs = state.wallTextures.size();
    int numSpriteTexs = state.spriteTextures.size();
    numLines = state.lineMap.size();
    numSprites = state.fullNumSprites;

    // Story the constant arrays into global variables
    glWallTextures.resize(numWallTexs);
    mapUnits.resize(numWallTexs);

    glSpriteTextures.resize(numSpriteTexs - 40); // Don't include players
    spriteUnits.resize(numSpriteTexs - 40);

    std::string wallStr = LoadFile(shaderFolder + "\\wall.glsl");
    std::string floorStr = LoadFile(shaderFolder + "\\floor.glsl");
    std::string ceilingStr = LoadFile(shaderFolder + "\\ceiling.glsl");
    std::string spriteStr = LoadFile(shaderFolder + "\\sprites.glsl");

    // Changes the source code of the .glsl files, so that multiple textures can be rendered
    generateMapTextures(wallStr, floorStr, ceilingStr, numWallTexs);
    generateSpriteTextures(spriteStr, numSpriteTexs - 40); // Don't include player textures

    // Initialize the rendering output
    initQuad();

    // Initialise buffers used for transfering data between the shaders
    initBuffers();

    GLuint computeProgram = CompileShader(LoadFile(shaderFolder + "\\raycast.glsl"), GL_COMPUTE_SHADER);
    GLuint wallProgram = CompileShader(wallStr, GL_COMPUTE_SHADER);
    GLuint floorProgram = CompileShader(floorStr, GL_COMPUTE_SHADER);
    GLuint ceilingProgram = CompileShader(ceilingStr, GL_COMPUTE_SHADER);
    GLuint spriteProgram = CompileShader(spriteStr, GL_COMPUTE_SHADER);
    GLuint spriteComputeProgram = CompileShader(LoadFile(shaderFolder + "\\spriteCompute.glsl"), GL_COMPUTE_SHADER);

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
    const char* screen_frag = 
    "#version 430 core\n"
    "\n"
    "in vec2 v_uv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_tex;\n"
    "\n"
    "void main() {\n"
    "    FragColor = texture(u_tex, v_uv);\n"
    "}\n";

    const char* screen_vert = 
    "#version 430 core\n"
    "\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "\n"
    "out vec2 v_uv;\n"
    "\n"
    "void main() {\n"
        "v_uv = a_uv;\n"
        "gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

    screenShader = CreateProgram(CompileShader(screen_vert, GL_VERTEX_SHADER), CompileShader(screen_frag, GL_FRAGMENT_SHADER));

    if (screenShader == 0) return -1;

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

    // Convert the surfaces to the right format that I input to openGL
    std::vector<SDL_Surface*> wallsConverted(numWallTexs);
    for (int i = 0; i < numWallTexs; i++){
        wallsConverted[i] = SDL_ConvertSurface(state.wallTextures[i].texture, SDL_PIXELFORMAT_RGBA32);
    }

    std::vector<SDL_Surface*> spritesConverted(numSpriteTexs);
    for (int i = 0; i < numSpriteTexs; i++){
        spritesConverted[i] = SDL_ConvertSurface(state.spriteTextures[i].texture, SDL_PIXELFORMAT_RGBA32);
    }

    // Create the output and input textures
    createOutputTextures();
    createInputTextures(wallsConverted, spritesConverted);

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

        glUniform1ui(glGetUniformLocation(computeShader, "numLines"), (unsigned int)(numLines));
        glUniform2f(glGetUniformLocation(computeShader, "lookDir"), lookDir.x, lookDir.y);
        glUniform2f(glGetUniformLocation(computeShader, "camera"), camera.x, camera.y);
        glUniform2f(glGetUniformLocation(computeShader, "playerPos"), playerPosition.x, playerPosition.y);
        glUniform1i(glGetUniformLocation(computeShader, "WINDOW_WIDTH"), W);
        glUniform1i(glGetUniformLocation(computeShader, "WINDOW_HEIGHT"), H);
        glUniform1f(glGetUniformLocation(computeShader, "texWidth"), 64);
        glUniform1f(glGetUniformLocation(computeShader, "texHeight"), 64);

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

        // Set the texture related uniforms
        setMapUniforms(wallShader);

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

        setSpriteUniforms(spriteShader);

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

        // Set the texture related uniforms
        setMapUniforms(currentShader);

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

    drawTexture(screenShader, outputTex);
}