#include "wallRender.hpp"
#include <string>
#include "GPU.hpp"

struct Lines {
    SDL_FPoint p1;
    SDL_FPoint p2;
    int texture;
};

GLuint computeShader;
GLuint drawShader;
void initShaders(){
    std::string computeStr = LoadFile("C:/Files/Cpp_files/silly/MultiplayerFPS/shaders/walls.glsl");
    std::string vertexStr = LoadFile("C:/Files/Cpp_files/silly/MultiplayerFPS/shaders/render.vert");
    std::string fragmentStr = LoadFile("C:/Files/Cpp_files/silly/MultiplayerFPS/shaders/render.frag");

    computeShader = CreateComputeProgram(CompileShader(computeStr, GL_COMPUTE_SHADER));
    drawShader = CreateProgram(CompileShader(vertexStr, GL_VERTEX_SHADER), CompileShader(fragmentStr, GL_FRAGMENT_SHADER));
}

void renderWalls(){
    // Bind the walls in the world
}