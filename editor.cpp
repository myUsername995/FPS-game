/*
HOW TO USE:
A and D: Switch between the different textures
Q and E: switch between the different types (NONE -> delete a wall, WALL -> place a wall, SPRITE -> place a sprite)

S -> save a file
L -> load a file
You can use backspace to remove the last letter, enter to finish typing the file's name, or press tab if you changed your mind and 
don't wanna load / save a file.

Select NONE type and click to delete a wall.
Shift and click to delete a sprite.
Shift and drag left mouse button to place walls continously.

Mouse wheel to zoom, right click to move around.

*/


#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <iostream>
#include <vector>
#include "time.hpp"

int WINDOW_WIDTH = 800;
int WINDOW_HEIGHT = 800;

constexpr int MAX_WINDOW_HEIGHT = 1017;
constexpr int MAX_WINDOW_WIDTH = 1920;

struct Texture {
    SDL_Texture* texture;
    int width, height;
};

enum Tile_type : int {
    NONE,
    WALL,
    SPRITE, 
    NUM_TYPES
};

struct Tile {
    // For now, we just have a wall
    Tile_type type;

    // Must be specified for every tile
    int ceilingTex;
    int floorTex;

    int wallTex;
};

struct Sprite {
    SDL_FPoint pos;
    int texture;
};

std::vector<Texture> texture;
std::vector<Texture> spriteTextures;

Uint32 buffer[MAX_WINDOW_HEIGHT][MAX_WINDOW_WIDTH];

SDL_FPoint world_to_screen(const SDL_FPoint& p, const double& zoom, const SDL_FPoint& top_left){
    return {float((p.x - top_left.x) / zoom), float((p.y - top_left.y) / zoom)};
}

SDL_FPoint screen_to_world(const SDL_FPoint& p, const double& zoom, const SDL_FPoint& top_left){
    return {float(p.x * zoom + top_left.x), float(p.y * zoom + top_left.y)};
}

void renderText(SDL_Renderer* renderer, TTF_Font* font, SDL_FRect& pos, const std::string& str, const SDL_Color& color){
    SDL_Surface* surface = TTF_RenderText_Blended(font, str.c_str(), str.size(), color);

    pos.w = surface->w;
    pos.h = surface->h;

    SDL_SetRenderDrawColor(renderer, 0, 127, 255, 255);
    SDL_RenderFillRect(renderer, &pos);

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);

    SDL_RenderTexture(renderer, tex, NULL, &pos);

    SDL_DestroySurface(surface);
    SDL_DestroyTexture(tex);
}

std::pair<float, float> getWidthAndHeight(TTF_Font* font, const std::string& str){
    SDL_Surface* surface = TTF_RenderText_Blended(font, str.c_str(), str.size(), {255, 255, 255, 255});

    return {surface->w, surface->h};
}

// Loads an image into the textures array
bool loadImage(SDL_Renderer* renderer, int index, const std::string& path, bool isSprite = false) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface){
        std::cerr << "Couldn't load file: " << path << std::endl;
        return false;
    }

    if (isSprite){
        spriteTextures[index].width = surface->w;
        spriteTextures[index].height = surface->h;
    }
    else {
        texture[index].width = surface->w;
        texture[index].height = surface->h;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!tex) {
        SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
    }

    if (isSprite){
        spriteTextures[index].texture = tex;
    }
    else {
        texture[index].texture = tex;
    }

    return true;
}

int nearestSprite(std::vector<Sprite> sprites, SDL_FPoint mouse){
    float nearestDist = ((float)(1e+300));
    int nearestInt = -1;
    for (int i = 0; i < sprites.size(); i++){
        double dist = (sprites[i].pos.x - mouse.x) * (sprites[i].pos.x - mouse.x) + 
                      (sprites[i].pos.y - mouse.y) * (sprites[i].pos.y - mouse.y);

        if (dist < nearestDist){
            nearestDist = dist;
            nearestInt = i;
        }
    }

    return nearestInt;
}

#include <fstream>

void writeMapData(std::string fileName, const std::vector<std::vector<Tile>>& map, std::vector<Sprite> sprites){
    std::ofstream out("maps/" + fileName, std::ios::binary);

    // The raycastserver.cpp uses this struct to store sprites, and uses int** arrays to store maps, but we can use a flat int* array
    // because he can read that and convert it to an int**

    struct sprite {
        float x, y;
        int texture;
    };

    uint32_t width  = map.size();
    uint32_t height = map[0].size();
    uint32_t numSprites = sprites.size();

    out.write((char*)&width, sizeof(width));
    out.write((char*)&height, sizeof(height));
    out.write((char*)&numSprites, sizeof(numSprites));

    for (uint32_t i = 0; i < width; i++){
        for (uint32_t j = 0; j < height; j++){
            if (map[i][j].type == WALL){
                out.write((char*)&map[i][j].wallTex, sizeof(int));
            }
            else if (map[i][j].type == NONE){
                int val = 0;
                out.write((char*)&val, sizeof(int));
            }
        }
    }

    for (auto& s : sprites) {
        out.write((char*)&s.pos.x, sizeof(float));
        out.write((char*)&s.pos.y, sizeof(float));
        out.write((char*)&s.texture, sizeof(int32_t));
    }

    out.close();
}

void readMapData(std::string fileName, std::vector<std::vector<Tile>>& map, std::vector<Sprite>& sprites){
    std::ifstream in("maps/" + fileName, std::ios::binary);

    if (!in){
        std::cout << "File doesn't exist.\n";
        return;
    }

    // Read metadata
    uint32_t width, height, numSprites;
    in.read((char*)&width, sizeof(width));
    in.read((char*)&height, sizeof(height));
    in.read((char*)&numSprites, sizeof(numSprites));

    // Read map data
    std::vector<int> flattenedMap;
    flattenedMap.resize(width * height);

    in.read((char*)flattenedMap.data(), width * height * sizeof(int));

    for (int i = 0; i < width * height; i++){
        int x = i % width;
        int y = i / width;

        if (flattenedMap[i] == 0){
            map[y][x].type = NONE;
            map[y][x].wallTex = 0;
        }
        else {
            map[y][x].type = WALL;
            map[y][x].wallTex = flattenedMap[i];
        }
    }

    struct sprite {
        float x, y;
        int texture;
    };

    std::vector<sprite> tempSprites;
    tempSprites.resize(numSprites);
    sprites.resize(numSprites);
    // Read sprite data
    for (uint32_t i = 0; i < numSprites; i++) {
        in.read((char*)&tempSprites[i].x, sizeof(float));
        in.read((char*)&tempSprites[i].y, sizeof(float));
        in.read((char*)&tempSprites[i].texture, sizeof(int32_t));
    }

    for (int i = 0; i < numSprites; i++){
        sprites[i].pos.x = tempSprites[i].x;
        sprites[i].pos.y = tempSprites[i].y;
        sprites[i].texture = tempSprites[i].texture;
    }
}

int main(int argc, char* argv[]){

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    SDL_Renderer* renderer;
    SDL_Window* window;

    SDL_CreateWindowAndRenderer("Level editor", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer);

    int numTextures = 9;
    int numSpriteTextures = 3;

    texture.resize(numTextures);
    spriteTextures.resize(numSpriteTextures);

    // Wall textures
    loadImage(renderer, 0, "pics/eagle.png");
    loadImage(renderer, 1, "pics/redbrick.png");
    loadImage(renderer, 2, "pics/purplestone.png");
    loadImage(renderer, 3, "pics/greystone.png");
    loadImage(renderer, 4, "pics/bluestone.png");
    loadImage(renderer, 5, "pics/mossy.png");
    loadImage(renderer, 6, "pics/wood.png");
    loadImage(renderer, 7, "pics/colorstone.png");
    loadImage(renderer, 8, "pics/sky.jpg");
    
    // Sprite textures
    loadImage(renderer, 0, "pics/barrel.png", true);
    loadImage(renderer, 1, "pics/pillar.png", true);
    loadImage(renderer, 2, "pics/greenlight.png", true);

    SDL_Event event;
    bool run = true;

    TTF_Font* font = TTF_OpenFont("Roboto_Condensed-Black.ttf", 25);

    int curSpriteMaterial = 0;
    int curMaterial = 0;
    int curType = 0;

    // The widths and heights of the grids in world space
    float gridWidth = 100;
    float gridHeight = 100;

    // The amount of grids in the world space
    int gridX = 24;
    int gridY = 24;

    float worldWidth = gridWidth * gridX;
    float worldHeight = gridHeight * gridY;

    // The user has to remain inside this box
    SDL_FRect boundingBox = {-800, -800, worldWidth + 1600, worldHeight + 1600};

    std::string filePath;

    bool shiftDown = false;
    bool leftMouseDown = false;
    bool rightMouseDown = false;
    bool loadFile = false;
    bool saveFile = false;

    SDL_FPoint top_left = {0, 0};
    SDL_FPoint start_pan = {0, 0};

    double zoom = 1;

    int floorTexture = 3;
    int ceilingTexture = 8;

    std::vector<Sprite> sprites;

    std::vector<std::vector<Tile>> map;
    map.resize(gridX);
    for (int i = 0; i < gridX; i++){
        map[i].resize(gridY);

        // Assign default ceiling and floor textures
        for (int j = 0; j < gridY; j++){
            map[i][j].floorTex = floorTexture;
            map[i][j].ceilingTex = ceilingTexture;
        }
    }

    // Cover the edges with the sky texture
    // Top
    for (int i = 0; i < gridX; i++){
        map[i][0].type = WALL;
        map[i][0].wallTex = 8;
    }
    // Left
    for (int i = 0; i < gridY; i++){
        map[0][i].type = WALL;
        map[0][i].wallTex = 8;
    }
    // Right
    for (int i = 0; i < gridY; i++){
        map[gridX-1][i].type = WALL;
        map[gridX-1][i].wallTex = 8;
    }
    // Bottom
    for (int i = 0; i < gridX; i++){
        map[i][gridY-1].type = WALL;
        map[i][gridY-1].wallTex = 8;
    }

    Clk clock;
    while (run){
        clock.begin();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        while (SDL_PollEvent(&event)){
            switch (event.type){
                case SDL_EVENT_QUIT: {
                    run = false;
                    break;
                }
                case SDL_EVENT_WINDOW_RESIZED: {
                    WINDOW_WIDTH = event.window.data1;
                    WINDOW_HEIGHT = event.window.data2;

                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (event.button.button == SDL_BUTTON_RIGHT){
                        start_pan = {event.button.x, event.button.y};
                        rightMouseDown = true;
                    }
                    if (event.button.button == SDL_BUTTON_LEFT){
                        leftMouseDown = true;
                        SDL_FPoint mouse = {event.button.x, event.button.y};

                        // Get the position where the user clicked
                        SDL_FPoint world = screen_to_world(mouse, zoom, top_left);

                        int posX = world.x / gridWidth;
                        int posY = world.y / gridHeight;

                        if (posX < 0 || posX >= gridX || posY < 0 || posY >= gridY){
                            break;
                        }

                        Tile_type type = static_cast<Tile_type>(curType);
                        // Set a wall texture at the current position
                        if (type == WALL){
                            map[posX][posY].type = type;
                            map[posX][posY].wallTex = curMaterial+1;
                        }
                        // Set a sprite texture at the world position of the mouse (doesn't have to be aligned to the tiles)
                        else if (type == SPRITE){
                            // Delete
                            if (shiftDown){
                                int spriteToDelete = nearestSprite(sprites, world);

                                if (spriteToDelete != -1){
                                    sprites.erase(sprites.begin() + spriteToDelete);
                                }
                            }
                            // Create
                            else {
                                Sprite newSprite;
                                newSprite.pos = world;
                                newSprite.texture = curSpriteMaterial;
                                sprites.push_back(newSprite);
                            }
                        }
                        // Reset the wall texture
                        else if (type == NONE){
                            map[posX][posY].type = type;
                            map[posX][posY].wallTex = 0;
                        }
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    if (event.button.button == SDL_BUTTON_RIGHT){
                        rightMouseDown = false;
                    }
                    if (event.button.button == SDL_BUTTON_LEFT){
                        leftMouseDown = false;
                    }
                    break;
                }
                case SDL_EVENT_KEY_DOWN: {
                    switch (event.key.key){
                        case SDLK_D: {
                            if (curType == WALL){
                                curMaterial++;
                                curMaterial %= numTextures;
                            }
                            else {
                                curSpriteMaterial++;
                                curSpriteMaterial %= numSpriteTextures;
                            }
                            break;
                        }
                        case SDLK_A: {
                            if (curType == WALL){
                                curMaterial--;

                                if (curMaterial < 0) curMaterial = numTextures - 1;
                            }
                            else {
                                curSpriteMaterial--;

                                if (curSpriteMaterial < 0) curSpriteMaterial = numSpriteTextures - 1;
                            }
                            break;
                        }
                        case SDLK_E: {
                            curType++;
                            curType %= NUM_TYPES;
                            break;
                        }
                        case SDLK_Q: {
                            curType--;

                            if (curType < 0) curType = NUM_TYPES-1;
                            break;
                        }
                        case SDLK_LSHIFT: {
                            shiftDown = true;
                            break;
                        }
                        case SDLK_S: {
                            // (1) Prevent saving and loading at the same time
                            // (2) We don't wanna start the text again as we are typing it
                            if (loadFile || saveFile) break;

                            filePath.clear();

                            SDL_StartTextInput(window);
                            saveFile = true;
                            break;
                        }
                        case SDLK_L: {
                            // (1) Prevent saving and loading at the same time
                            // (2) We don't wanna start the text again as we are typing it
                            if (saveFile || loadFile) break;

                            filePath.clear();

                            SDL_StartTextInput(window);
                            loadFile = true;
                            break;
                        }
                        case SDLK_BACKSPACE: {
                            if (filePath.size() > 0) filePath.pop_back();
                            break;
                        }
                        case SDLK_RETURN: {
                            SDL_StopTextInput(window);

                            if (loadFile){
                                readMapData(filePath, map, sprites);
                                for (int i = 0; i < sprites.size(); i++){
                                    sprites[i].pos.x *= gridWidth;
                                    sprites[i].pos.y *= gridHeight;
                                    sprites[i].texture -= texture.size();
                                }

                                loadFile = false;
                            }
                            if (saveFile){
                                std::vector<Sprite> fileSprites;
                                fileSprites.resize(sprites.size());
                                for (int i = 0; i < sprites.size(); i++){
                                    fileSprites[i].pos.x = sprites[i].pos.x / gridWidth;
                                    fileSprites[i].pos.y = sprites[i].pos.y / gridHeight;
                                    fileSprites[i].texture = sprites[i].texture + texture.size();
                                }

                                writeMapData(filePath, map, fileSprites);
                                saveFile = false;
                            }
                            break;
                        }
                        case SDLK_TAB: {
                            // The user changed his mind and doesn't want to type in a file anymore
                            if (saveFile) saveFile = false;
                            if (loadFile) loadFile = false;
                            break;
                        }
                    }
                    break;
                }
                case SDL_EVENT_KEY_UP: {
                    switch (event.key.key){
                        case SDLK_LSHIFT: {
                            shiftDown = false;
                            break;
                        }
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT: {
                    filePath += event.text.text;
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    SDL_FPoint newTop_left = top_left;
                    double newZoom = zoom;

                    SDL_PumpEvents();

                    float x, y;
                    SDL_GetMouseState(&x, &y);
                    SDL_FPoint mouse = {x, y};

                    SDL_FPoint beforeZoom = screen_to_world(mouse, newZoom, newTop_left);
                    newZoom -= newZoom * event.wheel.y / 20;
                    SDL_FPoint afterZoom = screen_to_world(mouse, newZoom, newTop_left);

                    newTop_left.x += (beforeZoom.x - afterZoom.x);
                    newTop_left.y += (beforeZoom.y - afterZoom.y);

                    // If we want to zoom in then let the user do that and dont check for bounds
                    if (event.wheel.y > 0 || !(newTop_left.x < boundingBox.x || 
                                             newTop_left.x >= (boundingBox.x + boundingBox.w) - WINDOW_WIDTH * zoom ||
                                             newTop_left.y < boundingBox.y || 
                                             newTop_left.y >= (boundingBox.y + boundingBox.h) - WINDOW_HEIGHT * zoom)){

                        zoom = newZoom;
                        top_left = newTop_left;
                    }

                    break;
                }
            }
        }
        SDL_PumpEvents();

        float x, y;
        Uint32 buttons = SDL_GetMouseState(&x, &y);

        const bool* keys = SDL_GetKeyboardState(nullptr);
        
        // Use drag to place the walls
        if (keys[SDL_SCANCODE_LSHIFT] && leftMouseDown){
            SDL_FPoint mouse = {x, y};

            // Get the position where the user clicked
            SDL_FPoint world = screen_to_world(mouse, zoom, top_left);

            int posX = world.x / gridWidth;
            int posY = world.y / gridHeight;

            if (!(posX < 0 || posX >= gridX || posY < 0 || posY >= gridY)){
                Tile_type type = static_cast<Tile_type>(curType);

                // Drag and place walls
                if (type == WALL){
                    map[posX][posY].type = type;
                    map[posX][posY].wallTex = curMaterial+1;
                }
                else if (type == NONE){
                    map[posX][posY].type = type;
                    map[posX][posY].wallTex = 0;
                }
            }
        }

        // Calculate the start and ending points in the world
        SDL_FPoint worldStart = screen_to_world({0, 0}, zoom, top_left);
        SDL_FPoint worldEnd = screen_to_world({(float)WINDOW_WIDTH, (float)WINDOW_HEIGHT}, zoom, top_left);

        if (rightMouseDown){
            SDL_FPoint change = {start_pan.x - x, start_pan.y - y};
            start_pan = {x, y};

            top_left.x += change.x * zoom;
            top_left.y += change.y * zoom;

            // Clamp the top left inside the box of the world
            top_left.x = SDL_clamp(top_left.x, boundingBox.x, (boundingBox.x + boundingBox.w) - WINDOW_WIDTH * zoom);
            top_left.y = SDL_clamp(top_left.y, boundingBox.y, (boundingBox.y + boundingBox.h) - WINDOW_HEIGHT * zoom);
        }

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        // Starting grid positions
        int startX = int(worldStart.x) / gridWidth - 1;
        int startY = int(worldStart.y) / gridHeight - 1;
        int endX = int(worldEnd.x) / gridWidth + 1;
        int endY = int(worldEnd.y) / gridHeight + 1;

        startX = SDL_clamp(startX, 0, gridX);
        startY = SDL_clamp(startY, 0, gridY);
        endX = SDL_clamp(endX, 0, gridX);
        endY = SDL_clamp(endY, 0, gridY);

        // Render points on each grid
        int pointWidth = (gridWidth / 8);
        float offset = (gridWidth - pointWidth) / 2.0f;
        for (int x = startX; x < endX; x++){
            for (int y = startY; y < endY; y++){
                if (map[x][y].type == NONE){
                    SDL_FPoint worldPoint = {x * gridWidth + offset, y * gridHeight + offset};

                    // Top left of our rectangle
                    SDL_FPoint point = world_to_screen(worldPoint, zoom, top_left);

                    SDL_FRect rect;
                    rect.x = point.x;
                    rect.y = point.y;
                    rect.w = pointWidth / zoom;
                    rect.h = pointWidth / zoom;

                    SDL_RenderFillRect(renderer, &rect);
                }
                else if (map[x][y].type == WALL){
                    SDL_FPoint worldPoint = {x * gridWidth, y * gridHeight};

                    // Top left of our rectangle
                    SDL_FPoint point = world_to_screen(worldPoint, zoom, top_left);

                    SDL_FRect rect;
                    rect.x = point.x;
                    rect.y = point.y;
                    rect.w = gridWidth / zoom;
                    rect.h = gridHeight / zoom;

                    int texIndex = map[x][y].wallTex-1;

                    SDL_RenderTexture(renderer, texture[texIndex].texture, NULL, &rect);
                }
            }
        }

        // Render every sprite
        for (Sprite sprite : sprites){
            // Render it as a cube
            float width = 100;

            SDL_FPoint screen = world_to_screen({sprite.pos.x - (width / 2.0f), sprite.pos.y - (width / 2.0f)}, 
                                                zoom, top_left);

            SDL_FRect rect;
            rect.x = screen.x;
            rect.y = screen.y;
            rect.w = width / zoom;
            rect.h = width / zoom;

            SDL_RenderTexture(renderer, spriteTextures[sprite.texture].texture, NULL, &rect);
        }

        // Current texture
        SDL_FRect pos = {10, 10};
        renderText(renderer, font, pos, "Texture (A and D):", {255, 255, 255, 255});

        if (curType == WALL){
            SDL_FRect dstRect = {pos.x + pos.w + 10, 10, pos.h, pos.h};
            SDL_RenderTexture(renderer, texture[curMaterial].texture, NULL, &dstRect);
        }
        else if (curType == SPRITE){
            SDL_FRect dstRect = {pos.x + pos.w + 10, 10, pos.h, pos.h};
            SDL_RenderTexture(renderer, spriteTextures[curSpriteMaterial].texture, NULL, &dstRect);
        }

        // Current type
        pos.y += pos.h + 10;
        renderText(renderer, font, pos, "Tile type (Q and E):", {255, 255, 255, 255});

        SDL_FRect typePos = pos;
        typePos.x += typePos.w + 10;

        switch (curType){
            case 0: {
                renderText(renderer, font, typePos, "None", {255, 255, 255, 255});
                break;
            }
            case 1: {
                renderText(renderer, font, typePos, "Wall", {255, 255, 255, 255});
                break;
            }
            case 2: {
                renderText(renderer, font, typePos, "Sprite", {255, 255, 255, 255});
                break;
            }
        }

        if (loadFile){
            std::string loadStr = "Enter the file you wanna load: " + filePath;
            std::pair<float, float> dimensions = getWidthAndHeight(font, loadStr);
            SDL_FRect bottomLeft = {10, WINDOW_HEIGHT - dimensions.second - 10};

            renderText(renderer, font, bottomLeft, loadStr, {255, 255, 255, 255});
        }
        if (saveFile){
            std::string loadStr = "Enter the file you wanna save: " + filePath;
            std::pair<float, float> dimensions = getWidthAndHeight(font, loadStr);
            SDL_FRect bottomLeft = {10, WINDOW_HEIGHT - dimensions.second - 10};

            renderText(renderer, font, bottomLeft, loadStr, {255, 255, 255, 255});
        }

        SDL_RenderPresent(renderer);

        clock.end();
        double FPS = clock.calculateFPS();
    }

    return 0;
}