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

int WINDOW_HEIGHT = 800;
int WINDOW_WIDTH = 800;

constexpr float PI = 3.14159;
constexpr const char* serverIP = "192.168.0.99";

constexpr int MAX_WINDOW_HEIGHT = 1017;
constexpr int MAX_WINDOW_WIDTH = 1920;

bool run = true;

Uint32 buffer[MAX_WINDOW_HEIGHT][MAX_WINDOW_WIDTH]; // y-coordinate first because it works per scanline

class Vector {
    public:
        Vector(){}
        Vector(float x, float y){
           Vector::x = x;
           Vector::y = y; 
        }
        ~Vector(){}

        double length(){
            return std::sqrt(Vector::x * Vector::x + Vector::y * Vector::y);
        }

        Vector normalize(){
            double length = Vector::length();

            return Vector(x / length, y / length);
        }

        Vector rotate90CW(){
            Vector newVec(Vector::y, -Vector::x);

            return newVec;
        }

        void rotate(double angle){
            double oldX = x;
            double oldY = y;

            x = oldX * cos(angle) - oldY * sin(angle);
            y = oldX * sin(angle) + oldY * cos(angle);
        }


        std::string to_string() const {
            return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
        }

        float x, y;
};

class Player {
    public:
        Player(SDL_FPoint pos, Vector lookDir, Vector camera){
            Player::pos = pos;
            Player::lookDir = lookDir.normalize();
            Player::camera = camera.normalize();
        }
        Player(SDL_FPoint pos, Vector lookDir, double FOVRadians){
            Player::pos = pos;
            Player::lookDir = lookDir.normalize();

            // Convert the FOV to the right plane vector
            double planeLen = tan(FOVRadians / 2.0f);

            // Camera plane
            Vector perp = Player::lookDir.rotate90CW();
            Player::camera = Vector(perp.x * planeLen, perp.y * planeLen);
        }
        Player(){}
        ~Player(){}

        // Decided by the server
        int playerID;

        // Player data
        SDL_FPoint pos;
        Vector lookDir;
        Vector camera;

        // Use this to animate players
        bool isMoving = false;
        int animationStep = 0;
};

Vector operator-(Vector v1, Vector v2){
    return Vector(v1.x - v2.x, v1.y - v2.y);
}

Vector operator-(SDL_FPoint p1, SDL_FPoint p2){
    return Vector(p1.x - p2.x, p1.y - p2.y);
}

// Used for sending data over the network
struct Network_player {
    // Metadata
    int playerID;

    // Player data
    SDL_FPoint pos;
    float lookDirX, lookDirY;
    float cameraX, cameraY;

    // Animation related dta
    bool isMoving;
    int animationStep;
};

struct Sprite {
    SDL_FPoint pos;
    int texture;

    // Show other players as sprites
    bool isPlayer = false;
    int index = 0;
};

struct gameState {
    Player player;
    std::vector<Player> otherPlayers;

    // The server sends the map to each client on start-up
    int mapWidth, mapHeight;
    std::vector<std::vector<int>> map;
    std::vector<Sprite> sprites;        // Sprites are also part of the world
};

struct Texture {
    std::vector<Uint32> texels;
    int width, height;
};

// Textures of the sprites
std::vector<Texture> texture;
int numSprites = 0;
int numPlayerSprites = 0;

// Store the player textures in a separate array
std::array<Texture, 8> playerTextures;
std::array<std::array<Texture, 8>, 4> playerRunTextures;

std::vector<double> ZBuffer;

// Arrays used to sort the sprites
std::vector<int> spriteOrder;
std::vector<double> spriteDistance;

// Networking functions

// Connect to the server and send the first packet (metadata, just username for now) and also receive a packet containing our playerID
bool connectToServer(ENetHost** client, ENetPeer** server, gameState& state){
    *client = enet_host_create(NULL, 1, 2, 0, 0);
    if (*client == NULL){
        std::cerr << "Couldn't set up client.\n";
        return false;
    }

    ENetAddress serverAddress;
    ENetEvent event;
    enet_address_set_host(&serverAddress, serverIP);
    serverAddress.port = 1234;

    *server = enet_host_connect(*client, &serverAddress, 2, 0);
    if (*server == NULL){
        std::cerr << "Couldn't find the server.\n";
        return false;
    }

    // Try to connect for 5 seconds
    bool connected = false;
    while (enet_host_service(*client, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT){

        std::cerr << "Succesfully connected to the server.\n";
        connected = true;
        break;
    }

    if (!connected){
        std::cerr << "Couldn't connect to server.\n";
        enet_peer_reset(*server);
        return false;
    }

    // Wait to receive the packets:
    // 1. playerID, 2. map dimensions, 3. map data

    bool receivedPacket = false;
    int numPacketsReceived = 0;
    while (numPacketsReceived < 5){
        bool receivedPacket = false;
        while (enet_host_service(*client, &event, 5000) > 0){
            if (event.type == ENET_EVENT_TYPE_RECEIVE){
                receivedPacket = true;

                // Set the playerID
                if (numPacketsReceived == 0){
                    state.player.playerID = *(int*)event.packet->data;
                }
                // Dimensions
                else if (numPacketsReceived == 1){
                    struct Dimensions {
                        int width, height;
                    };

                    Dimensions mapDimensions;
                    memcpy(&mapDimensions, event.packet->data, event.packet->dataLength);

                    state.mapWidth = mapDimensions.width;
                    state.mapHeight = mapDimensions.height;

                    state.map.resize(mapDimensions.width);
                    for (int i = 0; i < mapDimensions.width; i++){
                        state.map[i].resize(mapDimensions.height);
                    }
                }
                // Map data
                else if (numPacketsReceived == 2){
                    int width = state.map.size();
                    int height = state.map[0].size();

                    int* arr = new int[width * height];

                    memcpy(arr, event.packet->data, event.packet->dataLength);

                    for (int i = 0; i < width * height; i++){
                        int x = i % width;
                        int y = i / width;

                        state.map[x][y] = arr[i];
                    }

                    delete[] arr;
                }
                // Sprites metadata
                else if (numPacketsReceived == 3){
                    memcpy(&numSprites, event.packet->data, event.packet->dataLength);
                    state.sprites.resize(numSprites);
                }
                // Sprites data
                else if (numPacketsReceived == 4){
                    struct network_sprite {
                        float x, y;
                        int texture;
                    };

                    network_sprite* network_sprites = new network_sprite[state.sprites.size()];
                    memcpy(network_sprites, event.packet->data, event.packet->dataLength);

                    for (int i = 0; i < state.sprites.size(); i++){
                        state.sprites[i].pos = {network_sprites[i].x, network_sprites[i].y};
                        state.sprites[i].texture = network_sprites[i].texture;
                        state.sprites[i].isPlayer = false;
                    }

                    delete[] network_sprites;
                }

                enet_packet_destroy(event.packet);

                numPacketsReceived++;
                break;
            }
        }

        if (!receivedPacket && numPacketsReceived == 0){
            std::cerr << "Didn't receive the playerID from the server.\n";
            return false;
        }
        if (!receivedPacket && numPacketsReceived == 1){
            std::cerr << "Didn't receive the map dimensions from the server.\n";
            return false;
        }
        if (!receivedPacket && numPacketsReceived == 2){
            std::cerr << "Didn't receive the map data from the server.\n";
            return false;
        }
        if (!receivedPacket && numPacketsReceived == 3){
            std::cerr << "Didn't receive the sprites length data from the server.\n";
            return false;
        }
        if (!receivedPacket && numPacketsReceived == 4){
            std::cerr << "Didn't receive the sprites data from the server.\n";
            return false;
        }
    }

    return true;
}

void disconnectFromServer(ENetHost* client, ENetPeer* server){
    // Disconnect from the server
    std::cout << "Disconnecting...\n";
    enet_peer_disconnect(server, 0);

    ENetEvent event;

    // Wait 5 second to disconnect
    bool disconnected = false;
    while (enet_host_service(client, &event, 5000) > 0){
        if (event.type == ENET_EVENT_TYPE_DISCONNECT){
            disconnected = true;
            break;
        }
    }

    if (!disconnected){
        enet_peer_reset(server);
    }

    std::cout << "Disconnected from the server.\n";
}

void sendInputs(ENetHost* client, ENetPeer* server, const gameState& state){
    // Create a struct instead of a class
    Network_player p;
    p.cameraX = state.player.camera.x;
    p.cameraY = state.player.camera.y;
    p.lookDirX = state.player.lookDir.x;
    p.lookDirY = state.player.lookDir.y;
    p.playerID = state.player.playerID;
    p.pos = state.player.pos;
    p.isMoving = state.player.isMoving;
    p.animationStep = state.player.animationStep;

    ENetPacket* packet = enet_packet_create(&p, sizeof(p), ENET_PACKET_FLAG_RELIABLE);

    enet_peer_send(server, 0, packet);

    enet_host_flush(client);
}

// This function will run on a separate thread
void receiveInputs(ENetHost* client, ENetPeer* server, gameState& state){
    while (run){
        ENetEvent event;
        while (enet_host_service(client, &event, 50) > 0){
            if (event.type == ENET_EVENT_TYPE_DISCONNECT){
                run = false;
                std::cout << "Disconnected from the server.\n";
                
                return;
            }
            if (event.type == ENET_EVENT_TYPE_RECEIVE){
                // The server sent an std::vector<Player>.data() array
                size_t numPlayers = event.packet->dataLength / sizeof(Network_player);

                std::vector<Network_player> network_otherPlayers;
                network_otherPlayers.resize(numPlayers); // allocate space
                state.otherPlayers.resize(numPlayers);

                memcpy(network_otherPlayers.data(), event.packet->data, event.packet->dataLength);

                // Remove our own player from the otherPlayers array
                for (int i = 0; i < numPlayers; i++){
                    if (network_otherPlayers[i].playerID == state.player.playerID){
                        network_otherPlayers.erase(network_otherPlayers.begin() + i);
                        break;
                    }
                }
                numPlayers--;

                // Copy into the actual otherPlayers array
                for (int i = 0; i < numPlayers; i++){
                    state.otherPlayers[i].camera = {network_otherPlayers[i].cameraX, network_otherPlayers[i].cameraY};
                    state.otherPlayers[i].lookDir = {network_otherPlayers[i].lookDirX, network_otherPlayers[i].lookDirY};
                    state.otherPlayers[i].playerID = network_otherPlayers[i].playerID;
                    state.otherPlayers[i].pos = network_otherPlayers[i].pos;
                    state.otherPlayers[i].isMoving = network_otherPlayers[i].isMoving;
                    state.otherPlayers[i].animationStep = network_otherPlayers[i].animationStep;
                }

                state.sprites.resize(numSprites + numPlayers);
                numPlayerSprites = numPlayers;
                // Add sprites for the players
                for (int i = 0; i < numPlayers; i++){
                    Sprite newSprite;
                    newSprite.texture = 11; // Player texture
                    newSprite.pos = state.otherPlayers[i].pos;
                    newSprite.index = i;
                    newSprite.isPlayer = true;

                    state.sprites[numSprites + i] = newSprite;
                }

                break;
            }
        }
    }
}

// Loads an image into the textures array
bool loadImage(int index, const std::string& path) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface){
        std::cerr << "Couldn't load file: " << path << std::endl;
        return false;
    }

    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surface);
    if (!converted){
        std::cerr << "Couldn't convert file to ARGB8888: " << path << std::endl;
        return false;
    }

    Texture tex;
    tex.width = converted->w;
    tex.height = converted->h;
    tex.texels.resize(tex.width * tex.height);

    Uint8* pixels = (Uint8*)converted->pixels;
    int pitch = converted->pitch; // bytes per row

    for (int y = 0; y < tex.height; y++) {
        Uint32* srcRow = (Uint32*)(pixels + y * pitch);
        for (int x = 0; x < tex.width; x++) {
            tex.texels[y * tex.width + x] = srcRow[x];
        }
    }

    SDL_DestroySurface(converted);

    if (index >= texture.size()) {
        texture.resize(index + 1); // ensure vector is large enough
    }

    texture[index] = std::move(tex);
    return true;
}

// Helper function to parse the player.png picture into the playerTextures array
void parsePlayerTextures(){
    SDL_Surface* surface = IMG_Load("pics/player.png");
    if (!surface) {
        SDL_Log("IMG_Load failed: %s", SDL_GetError());
        return;
    }

    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    if (!converted) {
        SDL_Log("SDL_ConvertSurface failed: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return;
    }

    SDL_DestroySurface(surface);
    surface = converted;

    // Every picture is 64 by 64 pixels, we want the first row of 8 pictures. Additionally there is a 1 pixel gap between each picture

    // Go through 5 rows -> 1st row: standing player, 1st-5th rows: running player (animation)
    for (int j = 0; j < 5; j++){
        int startY = j * 65;
        for (int i = 0; i < 8; i++){
            // Account for the one pixel gap
            int x = i * 65;

            if (j == 0){
                playerTextures[i].width = 64;
                playerTextures[i].height = 64;

                playerTextures[i].texels.resize(64 * 64);

                for (int y = 0; y < 64; y++){
                    Uint32* row = (Uint32*)((Uint8*)surface->pixels + (startY + y) * surface->pitch) + x;
                    memcpy(&playerTextures[i].texels[y * 64], row, 64 * sizeof(Uint32));
                }
            }
            else {
                playerRunTextures[j-1][i].width = 64;
                playerRunTextures[j-1][i].height = 64;

                playerRunTextures[j-1][i].texels.resize(64 * 64);

                for (int y = 0; y < 64; y++){
                    Uint32* row = (Uint32*)((Uint8*)surface->pixels + (startY + y) * surface->pitch) + x;
                    memcpy(&playerRunTextures[j-1][i].texels[y * 64], row, 64 * sizeof(Uint32));
                }
            }
        }
    }
}

// Sort algorithm
// Sort the sprites based on distance
void sortSprites(std::vector<int>& order, std::vector<double>& dist, int amount){
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

double dotProduct(Vector v1, Vector v2){
    return v2.x * v1.x + v2.y * v1.y;
}

Uint32 darkenColor(Uint32 color, double factor){
    Uint32 a = (color >> 24) & 0xFF;  // Alpha
    Uint8 r = (color >> 16) & 0xFF;   // Red
    Uint8 g = (color >> 8) & 0xFF;    // Green
    Uint8 b = color & 0xFF;           // Blue

    // Darken by some factor
    r *= factor;
    g *= factor;
    b *= factor;

    // Recombine ARGB
    color = (a << 24) | (r << 16) | (g << 8) | b;

    return color;
}

void renderWalls(SDL_Renderer* renderer, const gameState& state){
    Player p = state.player;
    // Go through all the X values on the screen
    for (int x = 0; x < WINDOW_WIDTH; x++){
        // When X is 0, cameraX is -1, when X is in the middle, cameraX is 0, when X is to the right, camera X is 1
        double cameraX = 2 * x / (double)WINDOW_WIDTH - 1;
        Vector rayDir;
        rayDir.x = p.lookDir.x + p.camera.x * cameraX;
        rayDir.y = p.lookDir.y + p.camera.y * cameraX;

        // Which box of the map we're in
        SDL_Point map = {(int)p.pos.x, (int)p.pos.y};

        // Length of ray from current position to next x or y-side
        double sideDistX;
        double sideDistY;

        // Length of ray from one x or y-side to next x or y-side
        double deltaDistX = (rayDir.x == 0) ? 1e30 : std::abs(1 / rayDir.x);
        double deltaDistY = (rayDir.y == 0) ? 1e30 : std::abs(1 / rayDir.y);
        double perpWallDist;

        // What direction to step in x or y-direction (either +1 or -1)
        int stepX;
        int stepY;

        int hit = 0; //was there a wall hit?
        int side; //was a NS or a EW wall hit?
        
        // Calculate step and initial sideDist
        if (rayDir.x < 0){
            stepX = -1;
            sideDistX = (p.pos.x - map.x) * deltaDistX;
        }
        else {
            stepX = 1;
            sideDistX = (map.x + 1.0 - p.pos.x) * deltaDistX;
        }
        if (rayDir.y < 0){
            stepY = -1;
            sideDistY = (p.pos.y - map.y) * deltaDistY;
        }
        else {
            stepY = 1;
            sideDistY = (map.y + 1.0 - p.pos.y) * deltaDistY;
        }

        
        // Perform DDA
        while (hit == 0){
            // Jump to next map square, either in x-direction, or in y-direction
            if (sideDistX < sideDistY){
                sideDistX += deltaDistX;
                map.x += stepX;
                side = 0;
            }
            else {
                sideDistY += deltaDistY;
                map.y += stepY;
                side = 1;
            }
            // Check if ray has hit a wall
            if (state.map[map.x][map.y] > 0) hit = 1;
        }

        
        // Calculate distance projected on camera direction (Euclidean distance would give fisheye effect!)
        if (side == 0) perpWallDist = (sideDistX - deltaDistX);
        else          perpWallDist = (sideDistY - deltaDistY);

        
        // Calculate height of line to draw on screen
        int lineHeight = (int)(WINDOW_HEIGHT / perpWallDist);

        // Calculate lowest and highest pixel to fill in current stripe
        int drawStart = -lineHeight / 2 + WINDOW_HEIGHT / 2;
        if(drawStart < 0)drawStart = 0;
        int drawEnd = lineHeight / 2 + WINDOW_HEIGHT / 2;
        if(drawEnd >= WINDOW_HEIGHT)drawEnd = WINDOW_HEIGHT - 1;

        // Choose wall color
        SDL_Color color;
        switch(state.map[map.x][map.y]){
            case 1:  color = {255, 0, 0, 255};  break; //red
            case 2:  color = {0, 255, 0, 255};  break; //green
            case 3:  color = {0, 0, 255, 255};   break; //blue
            case 4:  color = {255, 255, 255, 255};  break; //white
            default: color = {127, 255, 0, 255}; break; //yellow
        }

        // Give x and y sides different brightness
        if (side == 1) {
            color.r /= 2;
            color.g /= 2;
            color.b /= 2;
        }
        
        // Texturing calculations
        int texNum = state.map[map.x][map.y] - 1; // 1 subtracted from it so that texture 0 can be used!
        int curTexWidth = texture[texNum].width;
        int curTexHeight = texture[texNum].height;

        // Calculate value of wallX
        double wallX; // Where exactly the wall was hit
        if (side == 0) wallX = p.pos.y + perpWallDist * rayDir.y;
        else           wallX = p.pos.x + perpWallDist * rayDir.x;
        wallX -= floor((wallX));

        // X coordinate on the texture
        int texX = int(wallX * double(curTexWidth));
        if (side == 0 && rayDir.x > 0) texX = curTexWidth - texX - 1;
        if (side == 1 && rayDir.y < 0) texX = curTexWidth - texX - 1;

        // How much to increase the texture coordinate per screen pixel
        double step = 1.0 * curTexHeight / lineHeight;
        // Starting texture coordinate
        double texPos = (drawStart - WINDOW_HEIGHT / 2 + lineHeight / 2) * step;
        for (int y = drawStart; y < drawEnd; y++){
            // Cast the texture coordinate to integer, and mask with (texHeight - 1) in case of overflow
            int texY = (int)texPos & (curTexHeight - 1);
            texPos += step;
            Uint32 color = texture[texNum].texels[curTexHeight * texY + texX];
            // Make color darker for y-sides: R, G and B byte
            if (side == 1) {
                color = darkenColor(color, 1.0/2.0);
            }
            buffer[y][x] = color;

            ZBuffer[x] = perpWallDist;
        }
    }
}

void renderSky(const gameState& state){
    int skyTexture = 12;
    const Texture& sky = texture[skyTexture];

    int texW = sky.width;
    int texH = sky.height;

    int horizon = WINDOW_HEIGHT / 2;
    for (int x = 0; x < WINDOW_WIDTH; x++) {
        // cameraX in range [-1, 1]
        double cameraX = 2.0 * x / (double)WINDOW_WIDTH - 1.0;

        // Ray direction for this column
        double rayDirX = state.player.lookDir.x + state.player.camera.x * cameraX;
        double rayDirY = state.player.lookDir.y + state.player.camera.y * cameraX;

        // Convert ray direction to angle
        double angle = atan2(rayDirY, rayDirX);  // [-PI, PI]

        // Normalize to [0, 1]
        double u = (angle + PI) / (2.0 * PI);

        int texX = (int)(u * texW) % texW;
        for (int y = 0; y < horizon; y++) {
            int texY = (y * texH) / horizon;

            buffer[y][x] = sky.texels[texY * texW + texX];
        }
    }
}

void renderFloorAndCeiling(SDL_Renderer* renderer, const gameState& state){
    // FLOOR CASTING
    Player player = state.player;

    // Choose a texture
    int floorTexture = 3;
    int ceilingTexture = 8;

    for (int y = WINDOW_HEIGHT / 2; y < WINDOW_HEIGHT; y++){
      // rayDir for leftmost ray (x = 0) and rightmost ray (x = w)
      float rayDirX0 = player.lookDir.x - player.camera.x;
      float rayDirY0 = player.lookDir.y - player.camera.y;
      float rayDirX1 = player.lookDir.x + player.camera.x;
      float rayDirY1 = player.lookDir.y + player.camera.y;

      // Current y position compared to the center of the screen (the horizon)
      int p = y - WINDOW_HEIGHT / 2;

      // Vertical position of the camera.
      float posZ = 0.5 * WINDOW_HEIGHT;

      // Horizontal distance from the camera to the floor for the current row.
      // 0.5 is the z position exactly in the middle between floor and ceiling.
      float rowDistance = posZ / p;

      // calculate the real world step vector we have to add for each x (parallel to camera plane)
      // adding step by step avoids multiplications with a weight in the inner loop
      float floorStepX = rowDistance * (rayDirX1 - rayDirX0) / WINDOW_WIDTH;
      float floorStepY = rowDistance * (rayDirY1 - rayDirY0) / WINDOW_WIDTH;

      // real world coordinates of the leftmost column. This will be updated as we step to the right.
      float floorX = player.pos.x + rowDistance * rayDirX0;
      float floorY = player.pos.y + rowDistance * rayDirY0;

      for (int x = 0; x < WINDOW_WIDTH; ++x){
        // the cell coord is simply got from the integer parts of floorX and floorY
        int cellX = (int)(floorX);
        int cellY = (int)(floorY);

        // get the texture coordinate from the fractional part
        int floorWidth = texture[floorTexture].width;
        int floorHeight = texture[floorTexture].height;

        int ceilWidth = texture[ceilingTexture].width;
        int ceilHeight = texture[ceilingTexture].height;

        // Fractional world coordinate
        float fx = std::fmod(std::fabs(floorX), 1.0f);
        float fy = std::fmod(std::fabs(floorY), 1.0f);

        // Floor
        int txFloor = std::clamp((int)(fx * floorWidth), 0, floorWidth - 1);
        int tyFloor = std::clamp((int)(fy * floorHeight), 0, floorHeight - 1);

        // Ceiling
        int txCeil = std::clamp((int)(fx * ceilWidth), 0, ceilWidth - 1);
        int tyCeil = std::clamp((int)(fy * ceilHeight), 0, ceilHeight - 1);

        floorX += floorStepX;
        floorY += floorStepY;

        // Draw the pixel
        Uint32 color;

        // Ceiling (symmetrical, at screenHeight - y - 1 instead of y)
        color = texture[ceilingTexture].texels[ceilWidth * tyCeil + txCeil];
        color = darkenColor(color, 1.0/2.0);
        //buffer[WINDOW_HEIGHT - y - 1][x] = color;

        // Floor
        color = texture[floorTexture].texels[floorWidth * tyFloor + txFloor];
        color = darkenColor(color, 1.0/2.0);
        buffer[y][x] = color;
      }
    }
}

void renderSprites(SDL_Renderer* renderer, const gameState& state){
    Player player = state.player;
    std::vector<Player> otherPlayers = state.otherPlayers;

    int fullNumSprites = numSprites + numPlayerSprites;
    spriteOrder.resize(fullNumSprites);
    spriteDistance.resize(fullNumSprites);

    // SPRITE CASTING
    // Sort sprites from far to close
    for(int i = 0; i < fullNumSprites; i++){
        spriteOrder[i] = i;
        spriteDistance[i] = ((player.pos.x - state.sprites[i].pos.x) * (player.pos.x - state.sprites[i].pos.x) + 
                            (player.pos.y - state.sprites[i].pos.y) * (player.pos.y - state.sprites[i].pos.y)); //sqrt not taken, unneeded
    }

    sortSprites(spriteOrder, spriteDistance, fullNumSprites);

    // After sorting the sprites, do the projection and draw them
    for(int i = 0; i < fullNumSprites; i++){
        int spriteIndex = spriteOrder[i];
        int spriteTexWidth, spriteTexHeight;

        std::vector<Uint32>* textureArr; // The current texture
        SDL_FPoint spritePos; // The position of the sprite
        int invisColor; // The player sprite and other sprites use different colors (cuz I pulled them from different sources)

        if (state.sprites[spriteIndex].isPlayer){
            // Construct the player class from the struct
            Player otherPlayer = state.otherPlayers[state.sprites[spriteIndex].index];

            Vector spriteDir = otherPlayer.lookDir.normalize();
            Vector toCamera  = (player.pos - otherPlayer.pos).normalize();

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

                textureArr = &playerRunTextures[step][playerTexture].texels;
                spriteTexWidth  = playerRunTextures[step][playerTexture].width;
                spriteTexHeight = playerRunTextures[step][playerTexture].height;
            }
            // Standing texture
            else {
                textureArr = &playerTextures[playerTexture].texels;
                spriteTexWidth  = playerTextures[playerTexture].width;
                spriteTexHeight = playerTextures[playerTexture].height;
            }

            spritePos = otherPlayer.pos;

            // Weird purple thingy
            invisColor = 0x980088;
        }
        else {
            int tex = state.sprites[spriteIndex].texture;
            textureArr = &texture[tex].texels;
            spriteTexWidth  = texture[tex].width;
            spriteTexHeight = texture[tex].height;
            spritePos = state.sprites[spriteIndex].pos;

            // Black
            invisColor = 0;
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

        int spriteScreenX = int((WINDOW_WIDTH / 2) * (1 + transformX / transformY));

        // Calculate height of the sprite on screen
        int spriteHeight = abs(int(WINDOW_HEIGHT / (transformY))); // Using 'transformY' instead of the real distance prevents fisheye
        // Calculate lowest and highest pixel to fill in current stripe
        int drawStartY = -spriteHeight / 2 + WINDOW_HEIGHT / 2;
        if (drawStartY < 0) drawStartY = 0;
        int drawEndY = spriteHeight / 2 + WINDOW_HEIGHT / 2;
        if (drawEndY >= WINDOW_HEIGHT) drawEndY = WINDOW_HEIGHT - 1;

        // Calculate width of the sprite
        int spriteWidth = abs( int (WINDOW_HEIGHT / (transformY)));
        int drawStartX = -spriteWidth / 2 + spriteScreenX;
        if(drawStartX < 0) drawStartX = 0;
        int drawEndX = spriteWidth / 2 + spriteScreenX;
        if(drawEndX >= WINDOW_WIDTH) drawEndX = WINDOW_WIDTH - 1;

        // Loop through every vertical stripe of the sprite on screen
        for (int stripe = drawStartX; stripe < drawEndX; stripe++){
            int texX = (stripe + spriteWidth / 2 - spriteScreenX) * spriteTexWidth / spriteWidth;
            // The conditions in the if are:
            //1) it's in front of camera plane so you don't see things behind you
            //2) it's on the screen (left)
            //3) it's on the screen (right)
            //4) ZBuffer, with perpendicular distance
            if (transformY > 0 && stripe > 0 && stripe < WINDOW_WIDTH && transformY < ZBuffer[stripe]){
                // For every pixel of the current stripe
                for (int y = drawStartY; y < drawEndY; y++){
                    int d = y - WINDOW_HEIGHT / 2 + spriteHeight / 2;
                    int texY = ((d * spriteTexHeight) / spriteHeight);
                    Uint32 color = (*textureArr)[spriteTexWidth * texY + texX]; // Get current color from the texture
                    if ((color & 0x00FFFFFF) != invisColor){
                        buffer[y][stripe] = color; // Paint pixel if it isn't black, black is the invisible color
                    }
                }
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

    SDL_Renderer* renderer;
    SDL_Window* window;

    SDL_CreateWindowAndRenderer("Multiplayer FPS game", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer);

    SDL_Texture* tex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        MAX_WINDOW_WIDTH,
        MAX_WINDOW_HEIGHT
    );

    double FOV = 90.0;

    gameState state;

    Player player = Player({5, 5}, Vector(1, 1), FOV * (PI / 180.0));
    state.player = player;

    ENetHost* client;
    ENetPeer* server;
    // Connect to the server
    if (!connectToServer(&client, &server, state)) return 0;
    player = state.player;
    std::cout << "Our ID: " << player.playerID << std::endl;

    // On connection, send our player info immediately to the other clients
    sendInputs(client, server, state);

    ZBuffer.resize(WINDOW_WIDTH);
    texture.resize(12);

    // Wall textures
    loadImage(0, "pics/eagle.png");
    loadImage(1, "pics/redbrick.png");
    loadImage(2, "pics/purplestone.png");
    loadImage(3, "pics/greystone.png");
    loadImage(4, "pics/bluestone.png");
    loadImage(5, "pics/mossy.png");
    loadImage(6, "pics/wood.png");
    loadImage(7, "pics/colorstone.png");
    loadImage(8, "pics/sky.jpg");
    
    // Sprite textures
    loadImage(9, "pics/barrel.png");
    loadImage(10, "pics/pillar.png");
    loadImage(11, "pics/greenlight.png");

    // Sky
    loadImage(12, "pics/doomSky.png");

    parsePlayerTextures();

    TTF_Font* font = TTF_OpenFont("Roboto_Condensed-Black.ttf", 20);

    double FPSCap = 60;
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

    // The thread where we will receive updates from the server
    std::thread receiveThread(receiveInputs, client, server, std::ref(state));
    while (run){
        clock.begin();

        // Clear the buffer
        for (int y = 0; y < WINDOW_HEIGHT; y++){
            for(int x = 0; x < WINDOW_WIDTH; x++){
                buffer[y][x] = 0;
            }
        }

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

                    ZBuffer.resize(WINDOW_WIDTH);

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
        SDL_FPoint pos = player.pos;
        Vector dir = player.lookDir;
        Vector velocity(0, 0);
        int numKeysPressed = 0;

        // Forward
        if (keyboardState[SDL_SCANCODE_W]){
            numKeysPressed++;
            // Move forward if no wall
            if (state.map[int(pos.x + dir.x * speed)][int(pos.y)] == false) velocity.x += player.lookDir.x * speed;
            if (state.map[int(pos.x)][int(pos.y + dir.y * speed)] == false) velocity.y += player.lookDir.y * speed;
        }

        // Backward
        if (keyboardState[SDL_SCANCODE_S]){
            numKeysPressed++;
            if (state.map[int(pos.x - dir.x * speed)][int(pos.y)] == false) velocity.x -= player.lookDir.x * speed;
            if (state.map[int(pos.x)][int(pos.y - dir.y * speed)] == false) velocity.y -= player.lookDir.y * speed;
        }

        // Strafe right
        if (keyboardState[SDL_SCANCODE_A]){
            numKeysPressed++;
            if (state.map[int(pos.x - dir.y * speed)][int(pos.y)] == false) velocity.x -= player.lookDir.y * speed;
            if (state.map[int(pos.x)][int(pos.y + dir.x * speed)] == false) velocity.y += player.lookDir.x * speed;
        }
    
        if (keyboardState[SDL_SCANCODE_D]){
            numKeysPressed++;
            // Strafe left
            if (state.map[int(pos.x + dir.y * speed)][int(pos.y)] == false) velocity.x += player.lookDir.y * speed;
            if (state.map[int(pos.x)][int(pos.y - dir.x * speed)] == false) velocity.y -= player.lookDir.x * speed;
        }

        double adjustment = 1.0;
        if (numKeysPressed >= 2){
            adjustment = 1.0 / sqrt(2);
        }

        player.pos.x += velocity.x * adjustment;
        player.pos.y += velocity.y * adjustment;

        // Get the mouse state
        float x, y;
        SDL_GetMouseState(&x, &y);

        bool cameraChanged = rightMouseButtonDown;
        if (rightMouseButtonDown){
            double changeX = start_pan.x - x;
            if (changeX == 0) cameraChanged = false;
            start_pan = {x, y};

            Vector dir = player.lookDir;
            Vector plane = player.camera;

            // Both camera direction and camera plane must be rotated
            double rotSpeed = rotationSpeed * changeX;

            dir.rotate(rotSpeed);
            plane.rotate(rotSpeed);

            player.lookDir = dir;
            player.camera = plane;
        }

        if (numKeysPressed > 0){
            animationAccumulate = fmod(animationAccumulate + animationSpeed, 4);

            player.isMoving = true;
            player.animationStep = (int)animationAccumulate;
        }
        else {
            if (state.player.isMoving){
                player.isMoving = false;
                player.animationStep = 0;
                animationAccumulate = 0;

                // Send our input, because it wouldn't get sent otherwise because we didn't move. But we should still send it to update
                // the client that we stopped moving
                state.player = player;
                sendInputs(client, server, state);
            }
        }

        state.player = player;

        // Only send packets if something changed
        if (cameraChanged || numKeysPressed > 0){
            // Send the new state of the player to the server
            sendInputs(client, server, state);
        }

        // The rendering process
        renderSky(state);
        renderFloorAndCeiling(renderer, state);
        renderWalls(renderer, state);
        renderSprites(renderer, state);

        SDL_Rect updateRect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
        SDL_UpdateTexture(
            tex,
            &updateRect,
            buffer,
            MAX_WINDOW_WIDTH * sizeof(uint32_t)
        );

        SDL_FRect texRect = {0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
        SDL_RenderTexture(renderer, tex, &texRect, &texRect);

        SDL_FRect rect = {10, 10, 0, 0};
        renderText(renderer, font, rect, "FPS: " + std::to_string(FPS), {255, 255, 255, 255});

        rect.y += rect.h + 10;
        renderText(renderer, font, rect, "Speed: " + std::to_string(playerSpeed), {255, 255, 255, 255});

        SDL_RenderPresent(renderer);

        clock.end();

        clock.capFPS(FPSCap);

        FPS = clock.calculateFPS();
        dt = clock.getTime();
    }

    receiveThread.join();

    // Disconnect from the server
    disconnectFromServer(client, server);

    enet_host_destroy(client);

    return 0;
}