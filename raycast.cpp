/*
INTRODUCTION: A raycaster that renders on the GPU, allows multiplayer and other stuff.

Dependencies:
-SDL3, SDL3_image, SDL3_ttf     -> libSDL3.dll.a, etc..., DLLs required at runtime
-ENet                           -> libenet.a
-C++ standard library
-GPU.hpp (my own silly library) -> libmyLibrary.a
-glad                           -> libglad.a
*/


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

// Textures of the sprites and walls
std::vector<Texture> wallTextures;
std::vector<Texture> spriteTextures;
std::array<Texture, 8> playerTextures;
std::array<std::array<Texture, 8>, 4> playerRunTextures;
std::vector<Texture> screenTextures;
Texture skyTexture;

enum textureType {
    TEXTURE_WALL,
    TEXTURE_SPRITE,
    TEXTURE_SKY
};

std::pair<float, float> getWidthAndHeight(TTF_Font* font, const std::string& str){
    SDL_Surface* surface = TTF_RenderText_Solid(font, str.c_str(), str.length(), {0, 0, 0, 0});

    return {(float)surface->w, (float)surface->h};
}

// OpenGL wants me to flip the surface cuz it renders upside down and stuff
SDL_Surface* flipSurfaceVertical(SDL_Surface* src) {
    if (!src) return nullptr;

    // Create a new surface with the same format and size
    SDL_Surface* flipped = SDL_CreateSurface(src->w, src->h, src->format);
    if (!flipped) {
        std::cerr << "Failed to create surface: " << SDL_GetError() << "\n";
        return nullptr;
    }

    SDL_LockSurface(src);
    SDL_LockSurface(flipped);

    int pitch = src->pitch; // bytes per row
    uint8_t* srcPixels = (uint8_t*)src->pixels;
    uint8_t* dstPixels = (uint8_t*)flipped->pixels;

    // Copy rows from bottom to top
    for (int y = 0; y < src->h; ++y) {
        memcpy(
            dstPixels + y * pitch,                // destination row
            srcPixels + (src->h - 1 - y) * pitch, // source row from bottom
            pitch
        );
    }

    SDL_UnlockSurface(src);
    SDL_UnlockSurface(flipped);

    return flipped;
}

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

                playerTextures[i].texture = flipSurfaceVertical(playerTextures[i].texture);
            }
            else {
                playerRunTextures[j-1][i].texture = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA8888);

                playerRunTextures[j-1][i].width = 64;
                playerRunTextures[j-1][i].height = 64;

                // Where to copy it from, from the image of images
                SDL_Rect srcRect = {x, y, 64, 64};
                SDL_BlitSurface(surface, &srcRect, playerRunTextures[j-1][i].texture, NULL);

                playerRunTextures[j-1][i].texture = flipSurfaceVertical(playerRunTextures[j-1][i].texture);
            }
        }
    }
}

void parseScreenTextures(const std::string& path){
    screenTextures.resize(15);

    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        SDL_Log("IMG_Load failed: %s", SDL_GetError());
        return;
    }

    // Every picture is 64 by 64 pixels, we have 3 rows and 5 columns. There is also a 1 pixel gap between each texture.
    for (int i = 0; i < 3; i++){
        for (int j = 0; j < 5; j++){
            screenTextures[i*5+j].texture = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA8888);
            screenTextures[i*5+j].width = 64;
            screenTextures[i*5+j].height = 64;

            SDL_Rect srcRect;
            srcRect.x = 1 + j * 65;
            srcRect.y = 1 + i * 65;
            srcRect.w = 64;
            srcRect.h = 64;

            SDL_BlitSurface(surface, &srcRect, screenTextures[i*5+j].texture, NULL);

            screenTextures[i*5+j].texture = flipSurfaceVertical(screenTextures[i*5+j].texture);
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

        spriteTextures[id].texture = flipSurfaceVertical(spriteTextures[id].texture);
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
    int height = state.map.size();
    int width = state.map[0].size();

    // 4 lines -> 2 offsets for the 2 points, stored in x,y order
    int lineOffsets[4][4] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}};
    // For each cube create 4 lines
    for (int x = 0; x < width; x++){
        for (int y = 0; y < height; y++){
            if (state.map[x][y] == 0) continue;

            for (int i = 0; i < 4; i++){
                Line newLine;
                newLine.p1.x = (x + lineOffsets[i][0]);
                newLine.p1.y = (y + lineOffsets[i][1]);
                newLine.p2.x = (x + lineOffsets[i][2]);
                newLine.p2.y = (y + lineOffsets[i][3]);
                newLine.texture = state.map[x][y]-1;

                state.lineMap.push_back(newLine);
            }
        }
    }
}

// Calculate at what height some players head is at
bool calculatePlayerHead(const gameState& state, int index, TTF_Font*& font, SDL_FRect& headBox){
    if (index >= state.numPlayers) return false;

    Player otherPlayer = state.otherPlayers[index];

    // Translate sprite position to relative to camera
    double spriteX = otherPlayer.pos.x - state.player.pos.x;
    double spriteY = otherPlayer.pos.y - state.player.pos.y;

    double planeX = state.player.camera.x;
    double planeY = state.player.camera.y;
    double dirX = state.player.lookDir.x;
    double dirY = state.player.lookDir.y;

    double invDet = 1.0 / (planeX * dirY - dirX * planeY);

    double transformX = invDet * (dirY * spriteX - dirX * spriteY);
    // This is actually the depth inside the screen, that what Z is in 3D
    double transformY = invDet * (-planeY * spriteX + planeX * spriteY);

    int spriteScreenX = int((WINDOW_WIDTH / 2) * (1 + transformX / transformY));

    //calculate height of the sprite on screen
    int spriteHeight = abs(int(WINDOW_HEIGHT / (transformY))); //using 'transformY' instead of the real distance prevents fisheye
    //calculate lowest and highest pixel to fill in current stripe
    int drawStartY = -spriteHeight / 2 + WINDOW_HEIGHT / 2;
    if(drawStartY < 0) drawStartY = 0;
    int drawEndY = spriteHeight / 2 + WINDOW_HEIGHT / 2;
    if(drawEndY >= WINDOW_HEIGHT) drawEndY = WINDOW_HEIGHT - 1;

    //calculate width of the sprite
    int spriteWidth = abs( int (WINDOW_HEIGHT / (transformY)));
    int drawStartX = -spriteWidth / 2 + spriteScreenX;
    if(drawStartX < 0) drawStartX = 0;
    int drawEndX = spriteWidth / 2 + spriteScreenX;
    if(drawEndX >= WINDOW_WIDTH) drawEndX = WINDOW_WIDTH - 1;

    // If the player is not on the screen, don't render their name
    if (transformY < 0){
        return false;
    }

    // Change the font size
    float arbitraryConstant = 100;
    float scaleFactor = 1.0f / transformY;
    int fontSize = std::clamp(int(arbitraryConstant * scaleFactor), 8, 100);

    font = TTF_OpenFont("TIMES.TTF", fontSize);

    std::pair<float, float> dims = getWidthAndHeight(font, otherPlayer.username);
    float normalizedStartX = ((spriteWidth - dims.first) / 2) / spriteWidth;
    float normalizedStartY = 0.05;

    float screenX = normalizedStartX * spriteWidth + spriteScreenX - spriteWidth / 2;
    float screenY = normalizedStartY * spriteHeight + WINDOW_HEIGHT / 2 - spriteHeight / 2;

    headBox.x = screenX;
    headBox.y = screenY;
    headBox.w = dims.first;
    headBox.h = dims.second;

    return true;
}

void renderTab(TTF_Font* font, const gameState& state){
    // Get the height of the first username (all other usernames should be the same height)
    std::pair<float, float> userHeights = getWidthAndHeight(font, state.player.username);

    float verticalGap = 10; // The gap between entries
    float rectWidth = 400; // width of the tab
    float gap1 = 150; // gap between username and ping
    float gap2 = 100; // gap between the ping and kills

    float height = userHeights.second; // Height of each username
    SDL_Color tabColor = {64, 64, 64, 127};
    int numEntries = state.numPlayers + 1;

    // Render the background rectangle first
    SDL_FRect backgroundRect;
    backgroundRect.x = WINDOW_WIDTH / 2 - rectWidth / 2;
    backgroundRect.y = 0;
    backgroundRect.w = rectWidth;
    backgroundRect.h = (height + verticalGap) * (numEntries + 1);
    GPURenderRect(backgroundRect, {64, 64, 64, 127}, true);

    // Render the header of the tab
    SDL_FRect headerRect;
    headerRect.x = WINDOW_WIDTH / 2 - rectWidth / 2; 
    headerRect.y = 0; 
    headerRect.w = rectWidth; 
    headerRect.h = (height + verticalGap);

    SDL_FPoint rectStart = {WINDOW_WIDTH / 2 - rectWidth / 2, 0};
    SDL_FPoint textStart = {headerRect.x + 10, headerRect.y};

    auto renderTabEntry = [&textStart, gap1, gap2, font, verticalGap, height, rectWidth](std::string name, std::string ping, std::string kills){
        float curY = textStart.y;
        float centeredY = curY + verticalGap / 2;
        SDL_FPoint userText = {textStart.x, centeredY};
        SDL_FPoint pingText = {textStart.x + gap1, centeredY};
        SDL_FPoint killText = {pingText.x + gap2, centeredY};
        GPURenderRect({userText.x-10, curY, gap1, height+verticalGap}, {255, 255, 255, 255}, false);
        GPURenderText(font, name, textStart, {255, 255, 255, 255});

        GPURenderRect({pingText.x-10, curY, gap2, height+verticalGap}, {255, 255, 255, 255}, false);
        GPURenderText(font, ping, pingText, {255, 255, 255, 255});

        GPURenderRect({killText.x-10, curY, rectWidth - gap2 - gap1, height+verticalGap}, {255, 255, 255, 255}, false);
        GPURenderText(font, kills, killText, {255, 255, 255, 255});
    };

    // Header descriptions
    renderTabEntry("Username", "Ping", "Kills");

    // Initialise the players array
    std::vector<Player> allPlayers(numEntries);
    allPlayers[0] = state.player;
    for (int i = 0; i < state.numPlayers; i++) allPlayers[i+1] = state.otherPlayers[i];

    for (int i = 0; i < numEntries; i++){
        rectStart.y += height + verticalGap;
        textStart.y += height + verticalGap;

        renderTabEntry(allPlayers[i].username, std::to_string(allPlayers[i].ping), std::to_string(0));
    }
}

void renderPlayerNames(std::string fontName, const gameState& state){
    for (int i = 0; i < state.numPlayers; i++){
        TTF_Font* font;
        SDL_FRect headBox;
        // Render each player's name above their head
        bool isOnScreen = calculatePlayerHead(state, i, font, headBox);
        if (!isOnScreen) continue;

        // Draw a rectangle
        GPURenderRect(headBox, {64, 64, 64, 127}, true);

        // Create a new sized font
        GPURenderText(font, state.otherPlayers[i].username, {headBox.x, headBox.y}, {255, 255, 255, 255});
        TTF_CloseFont(font);
    }
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
    
    // Connecting to the server
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

    // These textures never change, but parsing them everytime we initialise is easier, and probably not that much slower than just 
    // storing the raw binary data in a file
    parsePlayerTextures("pics/player.png");
    parseScreenTextures("pics/guns.png");

    // Convert the 3 different sprite texture arrays into one flattened one (0 - 32 -> player run textures, 32 - 40 -> player textures, 
    // 40 - x -> sprite textures)
    int numRunTexs = playerRunTextures.size() * playerRunTextures[0].size();
    int fullNumSprites = playerTextures.size() + numRunTexs + spriteTextures.size();
    std::vector<Texture> allSpriteTextures(fullNumSprites);

    // Run texs
    for (int i = 0; i < 4; i++){
        for (int j = 0; j < 8; j++){
            int flatIndex = i * 8 + j;
            allSpriteTextures[flatIndex] = playerRunTextures[i][j];
        }
    }
    // Standing texs
    for (int i = 0; i < 8; i++){
        allSpriteTextures[32 + i] = playerTextures[i];
    }
    // Normal sprite textures
    for (int i = 0; i < spriteTextures.size(); i++){
        allSpriteTextures[40 + i] = spriteTextures[i];
    }

    state.wallTextures = wallTextures;
    state.spriteTextures = allSpriteTextures;
    state.screenTextures = screenTextures;
    if (initShaders(window, state) == -1) return 0;

    TTF_Font* font = TTF_OpenFont("Roboto_Condensed-Black.ttf", 20);
    TTF_Font* font2 = TTF_OpenFont("TIMES.TTF", 15);

    double FPSCap = 1000;
    double FPS = 0;
    double dt = 0;

    double playerSpeed = 0.005;
    double rotationSpeed = 0.01;
    double animationSpeed = 0.05; // The step size used to get from one frame to the next (1 -> normal)
    double animationAccumulate = 0; // Where we accumulate the step sizes that might be fractional

    SDL_Event event;

    bool rightMouseButtonDown = false;
    SDL_FPoint start_pan = {0, 0};

    Clk clock; Clk pingTime;
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

        pingTime.begin();
        connection.sendData(state);

        // Receive data right before rendering so that we get the most up to date data
        if (connection.receiveData(state)){
            pingTime.end();
            state.player.ping = pingTime.getTime();
        }

        renderMap(window, state);

        // Render player's names above their heads
        renderPlayerNames("TIMES.TTF", state);

        // Tab pressed
        if (keyboardState[SDL_SCANCODE_TAB]){
            // Render player's names on the tab
            renderTab(font2, state);
        }

        SDL_FRect rect = GPURenderText(font, "FPS: " + std::to_string(FPS), {10, 10}, {255, 255, 255, 255});

        rect.y += rect.h + 10;
        rect = GPURenderText(font, "Speed: " + std::to_string(playerSpeed), {rect.x, rect.y}, {255, 255, 255, 255});

        rect.y += rect.h + 10;
        rect = GPURenderText(font, "Ping: " + std::to_string(state.player.ping), {rect.x, rect.y}, {255, 255, 255, 255});

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