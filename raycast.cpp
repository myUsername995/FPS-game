#include <SDL3/SDL.h>               // Rendering to windows
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <enet/enet.h>              // Communicating with clients
#include <iostream>
#include <array>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <thread>
#include <vector>
#include "time.hpp"
#include "networking.hpp"           // Abstracts away the communicating with the server part
#include "render.hpp"               // Abstracts how the rendering is done on the GPU
#include "gameState.hpp"            // Our gameState struct, stored in an .hpp to make sure we don't include it multiple times

int WINDOW_HEIGHT = 800;
int WINDOW_WIDTH = 800;

float PI = 3.14159;
std::string serverIP = "192.168.0.99";

// Textures of the sprites and walls
std::vector<Texture> wallTextures;
std::vector<Texture> spriteTextures;
std::array<Texture, 8> playerTextures;
std::array<std::array<Texture, 8>, 4> playerRunTextures;
Texture skyTexture;

enum textureType {
    TEXTURE_WALL,
    TEXTURE_SPRITE,
    TEXTURE_PLAYER,
    TEXTURE_SKY
};

// Helper function to parse the player.png picture into the playerTextures array
void parsePlayerTextures(const std::string& path){
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        SDL_Log("IMG_Load failed: %s", SDL_GetError());
        return;
    }

    // Every picture is 64 by 64 pixels, we want the first row of 8 pictures. Additionally there is a 1 pixel gap between each picture
    // Go through 5 rows -> 1st row: standing player, 1st-5th rows: running player (animation)
    for (int j = 0; j < 5; j++){
        int y = j * 65;
        for (int i = 0; i < 8; i++){
            // Account for the one pixel gap
            int x = i * 65;

            if (j == 0){
                playerTextures[i].texture = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA8888);

                playerTextures[i].width = 64;
                playerTextures[i].height = 64;

                // Where to copy it from, from the image of images
                SDL_Rect srcRect = {x, y, 64, 64};
                SDL_BlitSurface(surface, &srcRect, playerTextures[i].texture, NULL);
            }
            else {
                playerRunTextures[j-1][i].texture = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA8888);

                playerRunTextures[j-1][i].width = 64;
                playerRunTextures[j-1][i].height = 64;

                // Where to copy it from, from the image of images
                SDL_Rect srcRect = {x, y, 64, 64};
                SDL_BlitSurface(surface, &srcRect, playerRunTextures[j-1][i].texture, NULL);
            }
        }
    }
}

// Loads an image into different arrays
bool loadImage(textureType type, const std::string& path, int id = 0) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface){
        std::cerr << "Couldn't load file: " << path << std::endl;
        return false;
    }

    Texture tex;
    tex.width = surface->w;
    tex.height = surface->h;
    tex.texture = surface;

    if (type == TEXTURE_WALL){
        if (id >= wallTextures.size()){
            wallTextures.resize(id+1);
        }
        wallTextures[id] = tex;
    }
    else if (type == TEXTURE_SPRITE){
        if (id >= spriteTextures.size()){
            spriteTextures.resize(id+1);
        }
        spriteTextures[id] = tex;
    }
    else if (type == TEXTURE_PLAYER){
        parsePlayerTextures(path);
    }
    else if (type == TEXTURE_SKY){
        skyTexture = tex;
    }
    else {
        std::cout << "Didn't input type.\n";
        return false;
    }

    return true;
}

// Create the lineMap from the legacy cubes map (FOR TESTING)
void cubeToLines(gameState& state){
    int width = state.map.size();
    int height = state.map[0].size();

    // 4 lines -> 2 offsets for the 2 points, stored in x,y order
    int lineOffsets[4][4] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}};
    // For each cube create 4 lines
    for (int x = 0; x < width; x++){
        for (int y = 0; y < height; y++){
            if (state.map[x][y] == 0) continue;

            for (int i = 0; i < 4; i++){
                Line newLine;
                newLine.p1.x = x + lineOffsets[i][0];
                newLine.p1.y = y + lineOffsets[i][1];
                newLine.p2.x = x + lineOffsets[i][2];
                newLine.p2.y = y + lineOffsets[i][3];
                newLine.texture = state.map[x][y]+1;

                state.lineMap.push_back(newLine);
            }
        }
    }
}

void renderText(SDL_Renderer* renderer, TTF_Font* font, SDL_FRect& pos, const std::string& str, const SDL_Color& color){
    SDL_Surface* surface = TTF_RenderText_Blended(font, str.c_str(), str.size(), color);

    pos.w = surface->w;
    pos.h = surface->h;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);

    SDL_RenderTexture(renderer, tex, NULL, &pos);

    SDL_DestroySurface(surface);
    SDL_DestroyTexture(tex);
}

int main(int argc, char* argv[]){

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet.\n";
        return EXIT_FAILURE;
    }
    atexit(enet_deinitialize);

    SDL_Window* window = SDL_CreateWindow("Multiplayer FPS game", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (initializeGPU(window, WINDOW_WIDTH, WINDOW_HEIGHT) == -1) return 0;

    double FOV = 90.0;
    gameState state;
    state.player = Player({5, 5}, Vector(1, 1), FOV * (PI / 180.0));

    Client connection;
    if (!connection.connectToServer(state, serverIP)) return 0;

    // Convert our map representation to be made up of lines instead of cubes
    cubeToLines(state);

    // On connection, send our player info immediately to the other clients
    connection.sendData(state);

    // Wall textures
    loadImage(TEXTURE_WALL, "pics/eagle.png", 0);
    loadImage(TEXTURE_WALL, "pics/redbrick.png", 1);
    loadImage(TEXTURE_WALL, "pics/purplestone.png", 2);
    loadImage(TEXTURE_WALL, "pics/greystone.png", 3);
    loadImage(TEXTURE_WALL, "pics/bluestone.png", 4);
    loadImage(TEXTURE_WALL, "pics/mossy.png", 5);
    loadImage(TEXTURE_WALL, "pics/wood.png", 6);
    loadImage(TEXTURE_WALL, "pics/colorstone.png", 7);
    loadImage(TEXTURE_WALL, "pics/sky.jpg", 8);
    
    // Sprite textures
    loadImage(TEXTURE_SPRITE, "pics/barrel.png", 0);
    loadImage(TEXTURE_SPRITE, "pics/pillar.png", 1);
    loadImage(TEXTURE_SPRITE, "pics/greenlight.png", 2);

    // Sky
    loadImage(TEXTURE_SKY, "pics/doomSky.png");

    // Player
    loadImage(TEXTURE_PLAYER, "pics/player.png");

    if (initShaders(window, state.lineMap, wallTextures, playerTextures, playerRunTextures, spriteTextures) == -1) return 0;

    TTF_Font* font = TTF_OpenFont("Roboto_Condensed-Black.ttf", 20);

    double FPSCap = 1000;
    double FPS = 0;
    double dt = 0;

    double playerSpeed = 0.005;
    double rotationSpeed = 0.01;
    double animationSpeed = playerSpeed * 50; // The step size used to get from one frame to the next (1 -> normal)
    double animationAccumulate = 0; // Where we accumulate the step sizes that might be fractional

    SDL_Event event;

    bool rightMouseButtonDown = false;
    SDL_FPoint start_pan = {0, 0};

    Clk clock;
    bool run = true;

    while (run){
        clock.begin();
        GPUClearScreen();

        while (SDL_PollEvent(&event)){
            switch (event.type){
                case SDL_EVENT_QUIT: {
                    run = false;
                    break;
                }
                case SDL_EVENT_WINDOW_RESIZED: {
                    WINDOW_WIDTH = event.window.data1;
                    WINDOW_HEIGHT = event.window.data2;

                    resizeShaders(window);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (event.button.button == SDL_BUTTON_RIGHT){
                        rightMouseButtonDown = true;
                        start_pan = {event.button.x, event.button.y};
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    if (event.button.button == SDL_BUTTON_RIGHT){
                        rightMouseButtonDown = false;
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    playerSpeed += event.wheel.y * playerSpeed / 20;

                    // Make the animation faster as the player gets faster
                    animationSpeed = playerSpeed * 50;
                    break;
                }
            }
        }

        SDL_PumpEvents();

        const bool* keyboardState = SDL_GetKeyboardState(NULL);

        double speed = playerSpeed * dt;
        SDL_FPoint pos = state.player.pos;
        Vector dir = state.player.lookDir;
        Vector velocity(0, 0);
        int numKeysPressed = 0;

        // Forward
        if (keyboardState[SDL_SCANCODE_W]){
            numKeysPressed++;
            // Move forward if no wall
            if (state.map[int(pos.x + dir.x * speed)][int(pos.y)] == false) velocity.x += state.player.lookDir.x * speed;
            if (state.map[int(pos.x)][int(pos.y + dir.y * speed)] == false) velocity.y += state.player.lookDir.y * speed;
        }

        // Backward
        if (keyboardState[SDL_SCANCODE_S]){
            numKeysPressed++;
            if (state.map[int(pos.x - dir.x * speed)][int(pos.y)] == false) velocity.x -= state.player.lookDir.x * speed;
            if (state.map[int(pos.x)][int(pos.y - dir.y * speed)] == false) velocity.y -= state.player.lookDir.y * speed;
        }

        // Strafe right
        if (keyboardState[SDL_SCANCODE_A]){
            numKeysPressed++;
            if (state.map[int(pos.x - dir.y * speed)][int(pos.y)] == false) velocity.x -= state.player.lookDir.y * speed;
            if (state.map[int(pos.x)][int(pos.y + dir.x * speed)] == false) velocity.y += state.player.lookDir.x * speed;
        }
    
        if (keyboardState[SDL_SCANCODE_D]){
            numKeysPressed++;
            // Strafe left
            if (state.map[int(pos.x + dir.y * speed)][int(pos.y)] == false) velocity.x += state.player.lookDir.y * speed;
            if (state.map[int(pos.x)][int(pos.y - dir.x * speed)] == false) velocity.y -= state.player.lookDir.x * speed;
        }

        double adjustment = 1.0;
        if (numKeysPressed >= 2){
            adjustment = 1.0 / sqrt(2);
        }

        state.player.pos.x += velocity.x * adjustment;
        state.player.pos.y += velocity.y * adjustment;

        // Get the mouse state
        float x, y;
        SDL_GetMouseState(&x, &y);

        bool cameraChanged = rightMouseButtonDown;
        if (rightMouseButtonDown){
            double changeX = start_pan.x - x;
            if (changeX == 0) cameraChanged = false;
            start_pan = {x, y};

            // Both camera direction and camera plane must be rotated
            double rotSpeed = rotationSpeed * changeX;

            state.player.lookDir.rotate(rotSpeed);
            state.player.camera.rotate(rotSpeed);
        }

        // Only send packets if something changed
        // if (cameraChanged || state.player.isMoving){
        //     // Send the new state of the player to the server
        //     connection.sendData(state);
        // }

        // Determine if the player is moving or not
        if (numKeysPressed > 0){
            animationAccumulate = fmod(animationAccumulate + animationSpeed, 4);

            state.player.isMoving = true;
            state.player.animationStep = (int)animationAccumulate;
        }
        else {
            state.player.isMoving = false;
            state.player.animationStep = 0;
            animationAccumulate = 0;
        }

        float width = state.map.size();
        float height = state.map[0].size();

        Clk ping;
        ping.begin();
        connection.sendData(state);

        // Receive data right before rendering so that we get the most up to date data
        connection.receiveData(state);
        ping.end();

        renderMap(window, state);

        SDL_FRect rect = GPURenderText(font, "FPS: " + std::to_string(FPS), {10, 10}, {255, 255, 255, 255});

        rect.y += rect.h + 10;
        rect = GPURenderText(font, "Speed: " + std::to_string(playerSpeed), {rect.x, rect.y}, {255, 255, 255, 255});

        rect.y += rect.h + 10;
        rect = GPURenderText(font, "Username: " + state.player.username, {rect.x, rect.y}, {255, 255, 255, 255});

        rect.y += rect.h + 10;
        rect = GPURenderText(font, "Ping (ms): " + std::to_string(ping.getAvgTime()), {rect.x, rect.y}, {255, 255, 255, 255});

        SDL_GL_SwapWindow(window);

        clock.end();

        clock.capFPS(FPSCap);

        FPS = clock.calculateFPS();
        dt = clock.getTime();
    }

    // Disconnect from the server
    connection.disconnectFromServer();

    return 0;
}