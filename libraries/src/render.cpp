#include "render.hpp"
#include <SDL3_image/SDL_image.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <array>
#include <fstream>

// Shaders
GLuint computeShader, wallShader, floorShader, ceilingShader, spriteShader;
GLuint screenShader;

// Metadata
int W, H;
int outputW, outputH;

// Textures
GLuint outputTex = 0;
GLuint spriteTex = 0;

// Buffers
GLuint linesBuffer = 0;
GLuint wallRangesBuffer = 0;
GLuint texCoordsBuffer = 0;
GLuint texLines = 0;
GLuint texColumns = 0;
GLuint depthBuf = 0;

// GPU friendly arrays
std::vector<uint32_t> lineTextures; // The texture each line has
std::vector<float> vec2s;           // The world map but in a gpu friendly array
std::vector<Line> lines;            // The world map
std::vector<GLint> units;           // Stores which texture units to bind to the texture array
std::vector<float> ZBuffer;         // The depths of the walls in each column

// Textures
std::vector<Texture> textures;
std::vector<GLuint> glTextures;      // Stores the ID of each texture
std::vector<SDL_Surface*> converted; // Make sure we dont leak memory

// Used for storing CPU textures
struct rawTexture {
    std::vector<Uint32> texture;
    int width, height;
};

std::vector<Uint32> buffer;
static std::vector<rawTexture> spriteTextures;
static std::vector<rawTexture> playerTextures;
static std::vector<rawTexture> playerRunTextures;
int numAnimations = 4; int numOrientations = 8;     // For the run textures

const SDL_PixelFormatDetails* fmt;

int maxTextures;

// Globals for VAO/VBO/EBO
static GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;

void getWindowSize(SDL_Window* window){
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    // Actual framebuffer size in pixels
    W = static_cast<int>(windowWidth);
    H = static_cast<int>(windowHeight);
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

bool createTextures(){
    if (outputTex != 0) glDeleteTextures(1, &outputTex);
    if (spriteTex != 0) glDeleteTextures(1, &spriteTex);

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

    // Input (textures)
    for (int i = 0; i < converted.size(); i++){
        if (glTextures[i] != 0) glDeleteTextures(1, &glTextures[i]);

        glGenTextures(1, &glTextures[i]);
        glBindTexture(GL_TEXTURE_2D, glTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, converted[i]->w, converted[i]->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, converted[i]->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        if (glTextures[i] == 0){
            std::cout << "Failed to load input textures.\n";
            return false;
        }

        units[i] = i;
    }

    // Output of the sprite texture
    glGenTextures(1, &spriteTex);
    glBindTexture(GL_TEXTURE_2D, spriteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    return true;
}

void generateTextures(std::string& wallStr, std::string& floorStr, std::string& ceilingStr){
    // The length of a tab
    std::string tab = "    ";

    // Insert the uniforms into every string first
    std::string arrSize = std::to_string(textures.size());
    std::string uniforms = "uniform sampler2D textures[" + arrSize + "];";

    // Search for the identifiers "// UNIFORMS" and "// SWITCH STATEMENT"
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
        std::ofstream file("shaders\\wallChanged.glsl");
        file.write(wallStr.c_str(), wallStr.size());

        file.close();
    }
    {
        std::ofstream file("shaders\\floorChanged.glsl");
        file.write(floorStr.c_str(), floorStr.size());

        file.close();
    }
    {
        std::ofstream file("shaders\\ceilingChanged.glsl");
        file.write(ceilingStr.c_str(), ceilingStr.size());

        file.close();
    }
}

void initBuffers(){
    // Delete buffers (if necessary, e.g during window resizing)
    if (linesBuffer != 0){
        glDeleteBuffers(1, &linesBuffer);
    }

    if (wallRangesBuffer != 0){
        glDeleteBuffers(1, &wallRangesBuffer);
    }

    if (texCoordsBuffer != 0){
        glDeleteBuffers(1, &texCoordsBuffer);
    }

    if (texLines != 0){
        glDeleteBuffers(1, &texLines);
    }

    if (texColumns != 0){
        glDeleteBuffers(1, &texColumns);
    }

    if (depthBuf != 0){
        glDeleteBuffers(1, &depthBuf);
    }

    // Create the buffers
    glGenBuffers(1, &linesBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, linesBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linesBuffer);

    glGenBuffers(1, &wallRangesBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallRangesBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, wallRangesBuffer); 
    
    glGenBuffers(1, &texCoordsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, texCoordsBuffer); 
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, texCoordsBuffer);

    glGenBuffers(1, &texLines);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, texLines); 
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, texLines);

    glGenBuffers(1, &texColumns);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, texColumns); 
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, texColumns);

    glGenBuffers(1, &depthBuf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, depthBuf); 
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, depthBuf);

    if (linesBuffer == 0 || wallRangesBuffer == 0 || texCoordsBuffer == 0 || texLines == 0 || texColumns == 0 || depthBuf == 0) {
        std::cerr << "Buffer was not created properly." << std::endl;
    }
}

SDL_Color Uint32ToRGBA(Uint32 pixel){
    SDL_Color c;
    SDL_GetRGBA(pixel, fmt, nullptr, &c.r, &c.g, &c.b, &c.a);
    return c;
}

Uint32 RGBAToUint32(const SDL_Color& c){
    return SDL_MapRGBA(fmt, nullptr, c.r, c.g, c.b, c.a);
}


// Copies an SDL_Surface* into an std::vector<Uint32>
void copyTexture(std::vector<Uint32>& texDst, SDL_Surface* texSrc){
    SDL_Surface* rgba = SDL_ConvertSurface(texSrc, SDL_PIXELFORMAT_RGBA32);
    const int w = rgba->w;
    const int h = rgba->h;

    std::vector<Uint32> pixels(w * h);
    if (SDL_MUSTLOCK(rgba)) SDL_LockSurface(rgba);

    Uint8* srcPixels = (Uint8*)rgba->pixels;
    for (int y = 0; y < h; ++y) {
        memcpy(
            pixels.data() + (h - 1 - y) * w,
            srcPixels + y * rgba->pitch,
            w * sizeof(Uint32)
        );
    }

    if (SDL_MUSTLOCK(rgba)) SDL_UnlockSurface(rgba);

    SDL_DestroySurface(rgba);

    texDst = pixels;
}

// Called once at initialisation
int initShaders(SDL_Window* window, const std::vector<Line>& inLines, const std::vector<Texture>& wallTex, std::array<Texture, 8> playerTexs, 
                std::array<std::array<Texture, 8>, 4> playerRunTexs, std::vector<Texture> spriteTexs){
    // Resize the window
    getWindowSize(window);

    // Initialise the pixel format differently if needed
    fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);

    // Story the constant arrays into global variables
    lines = inLines;
    textures = wallTex;
    glTextures.resize(textures.size());
    units.resize(textures.size());
    ZBuffer.resize(W, 0);

    // Make sure the textures don't exceed the number of textures openGL can handle
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextures);
    if (textures.size() > maxTextures){
        std::cout << "The number of input textures exceeded the limit of allowed textures (" + std::to_string(maxTextures) + ").\n";
        return -1;
    }

    std::string wallStr = LoadFile("shaders\\wall.glsl");
    std::string floorStr = LoadFile("shaders\\floor.glsl");
    std::string ceilingStr = LoadFile("shaders\\ceiling.glsl");

    // Changes the source code of the .glsl files, so that multiple textures can be rendered
    generateTextures(wallStr, floorStr, ceilingStr);

    // Initialize the rendering output
    initQuad();

    // Initialise buffers used for transfering data between the shaders
    initBuffers();

    GLuint computeProgram = CompileShader(LoadFile("shaders\\raycast.glsl"), GL_COMPUTE_SHADER);
    GLuint wallProgram = CompileShader(wallStr, GL_COMPUTE_SHADER);
    GLuint floorProgram = CompileShader(floorStr, GL_COMPUTE_SHADER);
    GLuint ceilingProgram = CompileShader(ceilingStr, GL_COMPUTE_SHADER);
    GLuint spriteProgram = CompileShader(LoadFile("shaders\\sprites.glsl"), GL_COMPUTE_SHADER);

    if (computeProgram == 0 || wallProgram == 0 || floorProgram == 0 || ceilingProgram == 0 || spriteProgram == 0) return -1;

    computeShader = CreateComputeProgram(computeProgram);
    wallShader = CreateComputeProgram(wallProgram);
    floorShader = CreateComputeProgram(floorProgram);
    ceilingShader = CreateComputeProgram(ceilingProgram);
    spriteShader = CreateComputeProgram(spriteProgram);

    if (computeShader == 0 || wallShader == 0 || floorShader == 0 || ceilingShader == 0) return -1;

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
    vec2s.reserve(lines.size() * 4);
    lineTextures.reserve(lines.size());

    for (const auto& line : lines) {
        vec2s.push_back(line.p1.x);
        vec2s.push_back(line.p1.y);
        vec2s.push_back(line.p2.x);
        vec2s.push_back(line.p2.y);

        lineTextures.push_back(static_cast<uint32_t>(line.texture));
    }

    // Convert the surface to the openGL format
    converted.resize(textures.size());
    for (int i = 0; i < textures.size(); i++){
        converted[i] = SDL_ConvertSurface(textures[i].texture, SDL_PIXELFORMAT_RGBA32);
    }

    // Sprite rendering related
    buffer.resize(W * H);

    spriteTextures.resize(spriteTexs.size());
    // Copy sprites
    for (int i = 0; i < spriteTexs.size(); i++){
        copyTexture(spriteTextures[i].texture, spriteTexs[i].texture);
        spriteTextures[i].width = spriteTexs[i].width;
        spriteTextures[i].height = spriteTexs[i].height;
    }

    playerTextures.resize(playerTexs.size());
    // Copy player texs
    for (int i = 0; i < playerTexs.size(); i++){
        copyTexture(playerTextures[i].texture, playerTexs[i].texture);
        playerTextures[i].width = playerTexs[i].width;
        playerTextures[i].height = playerTexs[i].height;
    }

    // Copy player run texs
    playerRunTextures.resize(numAnimations * numOrientations);
    for (int i = 0; i < numAnimations; i++){
        for (int j = 0; j < numOrientations; j++){
            copyTexture(playerRunTextures[i * numOrientations + j].texture, playerRunTexs[i][j].texture);
            playerRunTextures[i * numOrientations + j].width = playerRunTexs[i][j].width;
            playerRunTextures[i * numOrientations + j].height = playerRunTexs[i][j].height;
        }
    }

    // Create the output and input textures
    createTextures();

    return 1;
}

void resizeShaders(SDL_Window* window){
    getWindowSize(window);

    GPUResizeWindow(W, H);
    ZBuffer.resize(W, 0);
    buffer.resize(W * H);

    initBuffers();
    createTextures();
}

enum shaderType {
    COMPUTE,
    WALL,
    FLOOR,
    CEILING
};

// Set the texture uniforms to the right textures
void setUniforms(GLuint currentShader){
    glUniform1iv(glGetUniformLocation(currentShader, "textures"), units.size(), units.data());
}

void activateTextures(){
    for (int i = 0; i < glTextures.size(); i++){
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, glTextures[i]);
    }
}

void dispatchShader(shaderType type, Vector lookDir, Vector camera, SDL_FPoint playerPosition){
    // Handle compute shaders differently
    if (type == COMPUTE){
        glUseProgram(computeShader);
        glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // Create the buffers

        // Lines buffer -> the lines that make up our map
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, linesBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, vec2s.size() * sizeof(float), vec2s.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linesBuffer);

        std::vector<uint32_t> noDataUint(W, 0);
        std::vector<float> noDataV2(W * 2, 0);
        std::vector<float> noDataV4(W * 4, 0);

        // The Y ranges where we should draw walls
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallRangesBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * 2 * sizeof(float), noDataV2.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, wallRangesBuffer); 
        
        // The starting texture cordinates and step values
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, texCoordsBuffer); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * 4 * sizeof(float), noDataV4.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, texCoordsBuffer);

        // The textures that the lines have
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, texLines); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, lineTextures.size() * sizeof(uint32_t), lineTextures.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, texLines);

        // The texture that the columns have
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, texColumns); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * sizeof(uint32_t), noDataUint.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, texColumns);

        // The depth buffer, used later by the sprite rendering
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, depthBuf); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * sizeof(float), ZBuffer.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, depthBuf);

        glUniform1ui(glGetUniformLocation(computeShader, "numLines"), (unsigned int)lines.size());
        glUniform2f(glGetUniformLocation(computeShader, "lookDir"), lookDir.x, lookDir.y);
        glUniform2f(glGetUniformLocation(computeShader, "camera"), camera.x, camera.y);
        glUniform2f(glGetUniformLocation(computeShader, "playerPos"), playerPosition.x, playerPosition.y);
        glUniform1ui(glGetUniformLocation(computeShader, "WINDOW_WIDTH"), W);
        glUniform1ui(glGetUniformLocation(computeShader, "WINDOW_HEIGHT"), H);
        glUniform1f(glGetUniformLocation(computeShader, "texWidth"), 64);
        glUniform1f(glGetUniformLocation(computeShader, "texHeight"), 64);

        int groupsX = (W + 255) / 256;
        glDispatchCompute(groupsX, 1, 1);

        // Wait for SSBO writes to be visible to next stage
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // Read back the value of the depth buffer into the CPU array
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, depthBuf);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, W * sizeof(float), ZBuffer.data());
    }
    else if (type == WALL){
        glUseProgram(wallShader);
        glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, wallRangesBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, texCoordsBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, texColumns);

        // Set the texture related uniforms
        setUniforms(wallShader);

        glUniform1ui(glGetUniformLocation(wallShader, "WINDOW_WIDTH"), W);
        glUniform1ui(glGetUniformLocation(wallShader, "WINDOW_HEIGHT"), H);
        glUniform1f(glGetUniformLocation(wallShader, "texWidth"), 64);
        glUniform1f(glGetUniformLocation(wallShader, "texHeight"), 64);

        int renderX = (W + 255) / 256;
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

        // Set the texture related uniforms
        setUniforms(currentShader);

        glUniform2f(glGetUniformLocation(currentShader, "lookDir"), lookDir.x, lookDir.y);
        glUniform2f(glGetUniformLocation(currentShader, "camera"), camera.x, camera.y);
        glUniform2f(glGetUniformLocation(currentShader, "playerPos"), playerPosition.x, playerPosition.y);
        glUniform1ui(glGetUniformLocation(currentShader, "WINDOW_WIDTH"), W);
        glUniform1ui(glGetUniformLocation(currentShader, "WINDOW_HEIGHT"), H);

        int renderX = (W + 15) / 16;
        int renderY = (H + 15) / 16;
        glDispatchCompute(renderX, renderY, 1);
    }
}

// Sort algorithm
// Sort the sprites based on distance
void sortSprites(const gameState& state, std::vector<int>& order, std::vector<double>& dist){
    int amount = state.numSprites + state.numPlayerSprites;
    order.resize(amount);
    dist.resize(amount);

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

void renderSprites(const gameState& state){
    Player player = state.player;
    std::vector<Player> otherPlayers = state.otherPlayers;

    // Arrays used to sort the sprites
    std::vector<int> spriteOrder;
    std::vector<double> spriteDistance;

    sortSprites(state, spriteOrder, spriteDistance);
    int fullNumSprites = state.numPlayerSprites + state.numSprites;

    // After sorting the sprites, do the projection and draw them
    for(int i = 0; i < fullNumSprites; i++){
        int spriteIndex = spriteOrder[i];
        int spriteTexWidth, spriteTexHeight;

        std::vector<Uint32> texture; // The current texture
        SDL_FPoint spritePos; // The position of the sprite
        SDL_Color invisColor; // The player sprite and other sprites use different colors (cuz I pulled them from different sources)

        if (state.sprites[spriteIndex].isPlayer){
            // Construct the player class from the struct
            Player otherPlayer = state.otherPlayers[state.sprites[spriteIndex].index];

            Vector spriteDir = otherPlayer.lookDir.normalize();
            Vector toCamera;
            toCamera.x = player.pos.x - otherPlayer.pos.x;
            toCamera.y = player.pos.y - otherPlayer.pos.y;
            toCamera.normalize();

            double angle = atan2(
                spriteDir.x * toCamera.y - spriteDir.y * toCamera.x,
                spriteDir.x * toCamera.x + spriteDir.y * toCamera.y
            );

            double deg = angle * 180.0 / PI;
            if (deg < 0) deg += 360;

            int playerTexture = int((deg + 22.5) / 45.0) % 8;

            // Assign texture
            // Running texture
            if (otherPlayer.isMoving){
                int step = otherPlayer.animationStep;

                texture = playerRunTextures[step * numOrientations + playerTexture].texture;
                spriteTexWidth  = playerRunTextures[step * numOrientations + playerTexture].width;
                spriteTexHeight = playerRunTextures[step * numOrientations + playerTexture].height;
            }
            // Standing texture
            else {
                texture = playerTextures[playerTexture].texture;
                spriteTexWidth  = playerTextures[playerTexture].width;
                spriteTexHeight = playerTextures[playerTexture].height;
            }

            spritePos = otherPlayer.pos;

            // Weird purple thingy
            invisColor = {152, 0, 136, 255};
        }
        else {
            int tex = state.sprites[spriteIndex].texture;
            texture = spriteTextures[tex].texture;
            spriteTexWidth  = spriteTextures[tex].width;
            spriteTexHeight = spriteTextures[tex].height;
            spritePos = state.sprites[spriteIndex].pos;

            // Black
            invisColor = {0, 0, 0, 255};
        }

        // Translate sprite position to relative to camera
        double spriteX = spritePos.x - player.pos.x;
        double spriteY = spritePos.y - player.pos.y;

        // Transform sprite with the inverse camera matrix
        // [ planeX   dirX ] -1                                       [ dirY      -dirX ]
        // [               ]       =  1/(planeX*dirY-dirX*planeY) *   [                 ]
        // [ planeY   dirY ]                                          [ -planeY  planeX ]

        // Required for correct matrix multiplication
        double invDet = 1.0 / (player.camera.x * player.lookDir.y - player.lookDir.x * player.camera.y);

        double transformX = invDet * (player.lookDir.y * spriteX - player.lookDir.x * spriteY);
        // This is actually the depth inside the screen, that what Z is in 3D
        double transformY = invDet * (-player.camera.y * spriteX + player.camera.x * spriteY);

        int spriteScreenX = int((W / 2) * (1 + transformX / transformY));

        // Calculate height of the sprite on screen
        int spriteHeight = abs(int(H / (transformY))); // Using 'transformY' instead of the real distance prevents fisheye
        // Calculate lowest and highest pixel to fill in current stripe
        int drawStartY = -spriteHeight / 2 + H / 2;
        if (drawStartY < 0) drawStartY = 0;
        int drawEndY = spriteHeight / 2 + H / 2;
        if (drawEndY >= H) drawEndY = H - 1;

        // Calculate width of the sprite
        int spriteWidth = abs( int (H / (transformY)));
        int drawStartX = -spriteWidth / 2 + spriteScreenX;
        if(drawStartX < 0) drawStartX = 0;
        int drawEndX = spriteWidth / 2 + spriteScreenX;
        if(drawEndX >= W) drawEndX = W - 1;

        // Loop through every vertical stripe of the sprite on screen
        for (int stripe = drawStartX; stripe < drawEndX; stripe++){
            int texX = (stripe + spriteWidth / 2 - spriteScreenX) * spriteTexWidth / spriteWidth;
            // The conditions in the if are:
            //1) it's in front of camera plane so you don't see things behind you
            //2) it's on the screen (left)
            //3) it's on the screen (right)
            //4) ZBuffer, with perpendicular distance
            if (transformY > 0 && stripe > 0 && stripe < W && transformY < ZBuffer[stripe]){
                // For every pixel of the current stripe
                for (int y = drawStartY; y < drawEndY; y++){
                    int d = y - H / 2 + spriteHeight / 2;
                    int texY = ((d * spriteTexHeight) / spriteHeight);

                    Uint32 color = texture[spriteTexWidth * texY + texX]; // Get current color from the texture

                    // Make sure the alpha matches
                    SDL_Color compColor = Uint32ToRGBA(color);
                    compColor.a = 255;
                    if (RGBAToUint32(compColor) != RGBAToUint32(invisColor)){
                        buffer[y * W + stripe] = color;
                    }
                }
            }
        }
    }

    // Upload our buffer to the GPU
    glBindTexture(GL_TEXTURE_2D, spriteTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

    // Dispatch the compute shader
    glUseProgram(spriteShader);

    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindImageTexture(1, spriteTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

    glUniform1ui(glGetUniformLocation(spriteShader, "WINDOW_WIDTH"), W);
    glUniform1ui(glGetUniformLocation(spriteShader, "WINDOW_HEIGHT"), H);

    glDispatchCompute((W+15)/16, (H+15)/16, 1);
}

// Renders walls, floor, ceiling and sprites
void renderMap(SDL_Window* window, const gameState& state){
    // Clear the screen buffer (that sprite rendering uses)
    for (int i = 0; i < buffer.size(); i++){
        buffer[i] = 0;
    }

    // Setup for compute shader dispatches
    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glClearTexImage(outputTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    activateTextures();

    // Compute everything firsst
    dispatchShader(COMPUTE, state.player.lookDir, state.player.camera, state.player.pos);

    // Render order doesn't matter, they don't draw over each other
    dispatchShader(WALL, state.player.lookDir, state.player.camera, state.player.pos);
    dispatchShader(FLOOR, state.player.lookDir, state.player.camera, state.player.pos);
    dispatchShader(CEILING, state.player.lookDir, state.player.camera, state.player.pos);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // After rendering the map, render the sprites
    renderSprites(state);

    // Ensure writes are visible
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    drawTexture(screenShader, outputTex);
}