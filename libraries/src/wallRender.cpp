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
int texW, texH;

// Textures
GLuint outputTex = 0;
GLuint linesBuffer = 0;
GLuint texture = 0;

GLuint wallRangesBuffer = 0;
GLuint texCoordsBuffer = 0;

std::vector<float> vec2s;
std::vector<Line> lines;

// Globals for VAO/VBO/EBO
static GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;

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

void createTextures(){
    SDL_Surface* imgSurface = IMG_Load("pics/wood.png");
    if (!imgSurface){
        std::cout << "Couldn't load file.\n";
        return;
    }
    SDL_Surface* converted = SDL_ConvertSurface(imgSurface, SDL_PIXELFORMAT_ABGR8888);
    if (!converted){
        std::cout << "Couldn't convert image.\n";
        return;
    }

    texW = converted->w;
    texH = converted->h;

    if (outputTex != 0) glDeleteTextures(1, &outputTex);
    if (texture != 0) glDeleteTextures(1, &texture);

    // Create the textures
    // Output
    glGenTextures(1, &outputTex);
    glBindTexture(GL_TEXTURE_2D, outputTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    // Input (textures)
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, converted->w, converted->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, converted->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (outputTex == 0 || texture == 0){
        std::cout << "Failed to load texture.\n";
    }
}

void initBuffers(std::vector<Line>& lines){
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

    if (linesBuffer == 0 || wallRangesBuffer == 0 || texCoordsBuffer == 0) {
        std::cerr << "Buffer was not created properly." << std::endl;
    }
}

int initShaders(std::vector<Line>& inLines, int window_width, int window_height){
    W = window_width;
    H = window_height;
    lines = inLines;

    initBuffers(lines);
    createTextures();
    initQuad();

    GLuint computeProgram = CompileShader(LoadFile("shaders\\raycast.glsl"), GL_COMPUTE_SHADER);
    GLuint wallProgram = CompileShader(LoadFile("shaders\\wall.glsl"), GL_COMPUTE_SHADER);
    GLuint floorProgram = CompileShader(LoadFile("shaders\\floor.glsl"), GL_COMPUTE_SHADER);
    GLuint ceilingProgram = CompileShader(LoadFile("shaders\\ceiling.glsl"), GL_COMPUTE_SHADER);

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

    vec2s.reserve(lines.size() * 4);

    for (const auto& line : lines) {
        vec2s.push_back(line.p1.x);
        vec2s.push_back(line.p1.y);
        vec2s.push_back(line.p2.x);
        vec2s.push_back(line.p2.y);
    }

    return 1;
}

void resizeShaders(int window_width, int window_height){
    GPUResizeWindow(window_width, window_height);

    W = window_width;
    H = window_height;

    initBuffers(lines);
    createTextures();
}

enum shaderType {
    COMPUTE,
    WALL,
    FLOOR,
    CEILING
};

void dispatchShader(shaderType type, Vector lookDir, Vector camera, SDL_FPoint playerPosition){
    switch (type){
        case COMPUTE: {
            glUseProgram(computeShader);


            // Create the buffers
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, linesBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, vec2s.size() * sizeof(float), vec2s.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linesBuffer);

            std::vector<float> noDataV2(W * 2);
            std::vector<float> noDataV4(W * 4);

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallRangesBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, noDataV2.size() * sizeof(float), noDataV2.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, wallRangesBuffer); 
            
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, texCoordsBuffer); 
            glBufferData(GL_SHADER_STORAGE_BUFFER, noDataV4.size() * sizeof(float), noDataV4.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, texCoordsBuffer);

            glUniform1ui(glGetUniformLocation(computeShader, "numLines"), (unsigned int)lines.size());
            glUniform2f(glGetUniformLocation(computeShader, "lookDir"), lookDir.x, lookDir.y);
            glUniform2f(glGetUniformLocation(computeShader, "camera"), camera.x, camera.y);
            glUniform2f(glGetUniformLocation(computeShader, "playerPos"), playerPosition.x, playerPosition.y);
            glUniform1ui(glGetUniformLocation(computeShader, "WINDOW_WIDTH"), W);
            glUniform1ui(glGetUniformLocation(computeShader, "WINDOW_HEIGHT"), H);
            glUniform1ui(glGetUniformLocation(computeShader, "texWidth"), texW);
            glUniform1ui(glGetUniformLocation(computeShader, "texHeight"), texH);

            int groupsX = (W + 255) / 256;
            glDispatchCompute(groupsX, 1, 1);

            break;
        }
        case WALL: {
            glUseProgram(wallShader);

            glUniform1i(glGetUniformLocation(wallShader, "testTex"), 1);
            glUniform2f(glGetUniformLocation(wallShader, "lookDir"), lookDir.x, lookDir.y);
            glUniform2f(glGetUniformLocation(wallShader, "camera"), camera.x, camera.y);
            glUniform2f(glGetUniformLocation(wallShader, "playerPos"), playerPosition.x, playerPosition.y);
            glUniform1ui(glGetUniformLocation(wallShader, "WINDOW_WIDTH"), W);
            glUniform1ui(glGetUniformLocation(wallShader, "WINDOW_HEIGHT"), H);
            glUniform1ui(glGetUniformLocation(wallShader, "texWidth"), texW);
            glUniform1ui(glGetUniformLocation(wallShader, "texHeight"), texH);

            int renderX = (W + 15) / 16;
            int renderY = (H + 15) / 16;
            glDispatchCompute(renderX, renderY, 1);

            break;
        }
        case FLOOR: {
            glUseProgram(floorShader);

            glUniform1i(glGetUniformLocation(floorShader, "testTex"), 1);
            glUniform2f(glGetUniformLocation(floorShader, "lookDir"), lookDir.x, lookDir.y);
            glUniform2f(glGetUniformLocation(floorShader, "camera"), camera.x, camera.y);
            glUniform2f(glGetUniformLocation(floorShader, "playerPos"), playerPosition.x, playerPosition.y);
            glUniform1ui(glGetUniformLocation(floorShader, "WINDOW_WIDTH"), W);
            glUniform1ui(glGetUniformLocation(floorShader, "WINDOW_HEIGHT"), H);
            glUniform1ui(glGetUniformLocation(floorShader, "texWidth"), texW);
            glUniform1ui(glGetUniformLocation(floorShader, "texHeight"), texH);

            int renderX = (W + 15) / 16;
            int renderY = (H + 15) / 16;
            glDispatchCompute(renderX, renderY, 1);

            break;
        }
        case CEILING: {
            glUseProgram(ceilingShader);

            glUniform1i(glGetUniformLocation(ceilingShader, "testTex"), 1);
            glUniform2f(glGetUniformLocation(ceilingShader, "lookDir"), lookDir.x, lookDir.y);
            glUniform2f(glGetUniformLocation(ceilingShader, "camera"), camera.x, camera.y);
            glUniform2f(glGetUniformLocation(ceilingShader, "playerPos"), playerPosition.x, playerPosition.y);
            glUniform1ui(glGetUniformLocation(ceilingShader, "WINDOW_WIDTH"), W);
            glUniform1ui(glGetUniformLocation(ceilingShader, "WINDOW_HEIGHT"), H);
            glUniform1ui(glGetUniformLocation(ceilingShader, "texWidth"), texW);
            glUniform1ui(glGetUniformLocation(ceilingShader, "texHeight"), texH);

            int renderX = (W + 15) / 16;
            int renderY = (H + 15) / 16;
            glDispatchCompute(renderX, renderY, 1);

            break;
        }
    }

}

void renderWallz(Vector lookDir, Vector camera, SDL_FPoint playerPosition){

    dispatchShader(COMPUTE, lookDir, camera, playerPosition);

    // Wait for SSBO writes to be visible to next stage
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    // Clear output texture to avoid garbage
    glClearTexImage(outputTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture);

    dispatchShader(WALL, lookDir, camera, playerPosition);
    dispatchShader(FLOOR, lookDir, camera, playerPosition);
    dispatchShader(CEILING, lookDir, camera, playerPosition);

    // Ensure writes are visible
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    // -----------------------------
    // 6. Draw output texture to screen
    // -----------------------------
    drawTexture(screenShader, outputTex);
}