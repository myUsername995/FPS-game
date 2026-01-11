#include "wallRender.hpp"
#include <SDL3_image/SDL_image.h>
#include <vector>
#include <iostream>

// Shaders
GLuint computeShader, wallShader, floorShader, ceilingShader;
GLuint screenShader;

// Metadata
int W, H;
int outputW, outputH;

// Textures
GLuint outputTex = 0;
GLuint linesBuffer = 0;
GLuint texture = 0;

// Buffers
GLuint wallRangesBuffer = 0;
GLuint texCoordsBuffer = 0;
GLuint texLines = 0;
GLuint texColumns = 0;

// GPU friendly arrays
std::vector<uint32_t> lineTextures;
std::vector<float> vec2s;
std::vector<Line> lines;

// Textures
std::vector<Texture> textures;
std::vector<GLuint> glTextures;

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
    for (int i = 0; i < textures.size(); i++){
        SDL_Surface* surf = SDL_ConvertSurface(textures[i].texture, SDL_PIXELFORMAT_RGBA32);
        
        glGenTextures(1, &glTextures[i]);
        glBindTexture(GL_TEXTURE_2D, glTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, textures[i].width, textures[i].height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 
                     surf->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        if (glTextures[i] == 0){
            std::cout << "Failed to load input textures.\n";
            return false;
        }
    }

    return true;
}

#include <fstream>

void generateTextures(std::string& wallStr, std::string& floorStr, std::string& ceilingStr){
    // The length of a tab
    std::string tab = "    ";

    // Insert the uniforms into every string first
    std::string uniforms;

    // Generate the text
    for (int i = 0; i < textures.size(); i++){
        uniforms += "uniform sampler2D tex" + std::to_string(i+1) + ";\n";
    }

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

    // Insert the switch statement into the string too
    std::string switchStatement;
    switchStatement += tab + "switch (textureIdx){\n";

    // Generate the text
    for (int i = 0; i < textures.size(); i++){
        switchStatement += tab + tab + "case " + std::to_string(i+1) + ": color = texture(tex" + std::to_string(i+1) + ", texCoord); break;\n";
    }

    // Default case
    switchStatement += tab + tab + "default: break;\n";
    switchStatement += tab + "}\n";

    std::string switchID = "// SWITCH STATEMENT\n";

    // Find the positions
    int wallSwitch = wallStr.find(switchID) + switchID.length();
    int floorSwitch = floorStr.find(switchID) + switchID.length();
    int ceilingSwitch = ceilingStr.find(switchID) + switchID.length();
    
    // Insert
    wallStr.insert(wallSwitch, switchStatement);
    floorStr.insert(floorSwitch, switchStatement);
    ceilingStr.insert(ceilingSwitch, switchStatement);
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

    if (linesBuffer == 0 || wallRangesBuffer == 0 || texCoordsBuffer == 0 || texLines == 0 || texColumns == 0) {
        std::cerr << "Buffer was not created properly." << std::endl;
    }
}

int initShaders(SDL_Window* window, const std::vector<Line>& inLines, const std::vector<Texture>& wallTex){
    getWindowSize(window);

    lines = inLines;
    textures = wallTex;
    glTextures.resize(textures.size());

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

    // Initialise buffers used for transfering data between the compute shaders that render the scene and the ones that calculate
    // stuff
    initBuffers();
    createTextures();

    GLuint computeProgram = CompileShader(LoadFile("shaders\\raycast.glsl"), GL_COMPUTE_SHADER);
    GLuint wallProgram = CompileShader(wallStr, GL_COMPUTE_SHADER);
    GLuint floorProgram = CompileShader(floorStr, GL_COMPUTE_SHADER);
    GLuint ceilingProgram = CompileShader(ceilingStr, GL_COMPUTE_SHADER);

    if (computeProgram == 0 || wallProgram == 0 || floorProgram == 0 || ceilingProgram == 0) return -1;

    computeShader = CreateComputeProgram(computeProgram);
    wallShader = CreateComputeProgram(wallProgram);
    floorShader = CreateComputeProgram(floorProgram);
    ceilingShader = CreateComputeProgram(ceilingProgram);

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

    return 1;
}

void resizeShaders(SDL_Window* window){
    getWindowSize(window);

    GPUResizeWindow(W, H);

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
    std::string lookUp;
    for (int i = 0; i < glTextures.size(); i++){
        lookUp = "tex" + std::to_string(i+1);
        glUniform1i(glGetUniformLocation(currentShader, lookUp.c_str()), i);
    }
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
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, linesBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, vec2s.size() * sizeof(float), vec2s.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linesBuffer);

        std::vector<uint32_t> noDataUint(W, 0);
        std::vector<float> noDataV2(W * 2, 0);
        std::vector<float> noDataV4(W * 4, 0);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallRangesBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * 2 * sizeof(float), noDataV2.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, wallRangesBuffer); 
        
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, texCoordsBuffer); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * 4 * sizeof(float), noDataV4.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, texCoordsBuffer);

        // Put some actual data into this array
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, texLines); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, lineTextures.size() * sizeof(uint32_t), lineTextures.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, texLines);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, texColumns); 
        glBufferData(GL_SHADER_STORAGE_BUFFER, W * sizeof(uint32_t), noDataUint.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, texColumns);

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

        // Return to not run any other shaders
        return;
    }

    // The other shaders are similar, so we can handle them simpler
    GLuint currentShader;
    switch (type){
        case WALL: currentShader = wallShader; break;
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
    glUniform1f(glGetUniformLocation(currentShader, "texWidth"), 64);
    glUniform1f(glGetUniformLocation(currentShader, "texHeight"), 64);

    int renderX = (W + 15) / 16;
    int renderY = (H + 15) / 16;
    glDispatchCompute(renderX, renderY, 1);
}

int step = 0;
double threshold = std::stod(LoadFile("a"));

void renderWallz(SDL_Window* window, Vector lookDir, Vector camera, SDL_FPoint playerPosition){
    getWindowSize(window);

    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    // Clear output texture to avoid garbage
    glClearTexImage(outputTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    dispatchShader(COMPUTE, lookDir, camera, playerPosition);

    // Wait for SSBO writes to be visible to next stage
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    activateTextures();

    step++;
    // Render order doesn't matter, they don't draw over each other
    if (step >= threshold) dispatchShader(WALL, lookDir, camera, playerPosition);
    if (step >= threshold * 2) dispatchShader(CEILING, lookDir, camera, playerPosition);
    if (step >= threshold * 3) dispatchShader(FLOOR, lookDir, camera, playerPosition);
    if (step >= threshold * 4) step = 0;

    // Ensure writes are visible
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Draw the output texture
    drawTexture(screenShader, outputTex);
}