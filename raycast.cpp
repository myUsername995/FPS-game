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
#include "networking.hpp"
#include "wallRender.hpp"

int WINDOW_HEIGHT = 800;
int WINDOW_WIDTH = 800;

constexpr float PI = 3.14159;
std::string serverIP = "192.168.0.99";

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

struct Sprite {
    SDL_FPoint pos;
    int texture;

    int isPlayer;
    int index = 0;
};

struct gameState {
    Player player;
    std::vector<Player> otherPlayers;

    // The server sends the map to each client on start-up
    std::vector<std::vector<int>> map;
    std::vector<Sprite> sprites;
    int numSprites, numPlayerSprites;
};

struct Lines {
    SDL_FPoint p1;
    SDL_FPoint p2;
    int texture;
};

struct Texture {
    SDL_Surface* texture;
    int width, height;
};

// Textures of the sprites and walls
std::vector<Texture> wallTextures;
std::vector<Texture> spriteTextures;
std::array<Texture, 8> playerTextures;
std::array<std::array<Texture, 8>, 4> playerRunTextures;
Texture skyTexture;

int numWallTextures = 0;
int numSpriteTextures = 0;
int numPlayerTextures = 0;

std::vector<double> ZBuffer;

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
                playerTextures[i].width = 64;
                playerTextures[i].height = 64;

                // Where to copy it from, from the image of images
                SDL_Rect srcRect = {x, y, 64, 64};
                SDL_BlitSurface(surface, &srcRect, playerTextures[i].texture, NULL);
            }
            else {
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
            wallTextures.resize(id);
        }
        wallTextures[id] = tex;
    }
    else if (type == TEXTURE_SPRITE){
        if (id >= spriteTextures.size()){
            spriteTextures.resize(id);
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

void DarkenSurface(SDL_Surface* surface){
    if (SDL_MUSTLOCK(surface))
        SDL_LockSurface(surface);

    Uint8* pixels = static_cast<Uint8*>(surface->pixels);
    SDL_PixelFormat format = surface->format;
    int bpp = SDL_BYTESPERPIXEL(format);

    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            Uint8* p = pixels + y * surface->pitch + x * bpp;

            Uint32 pixel;
            memcpy(&pixel, p, bpp);

            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(format), NULL, &r, &g, &b, &a);

            r >>= 1;
            g >>= 1;
            b >>= 1;

            Uint32 newPixel = SDL_MapRGBA(SDL_GetPixelFormatDetails(format), NULL, r, g, b, a);
            memcpy(p, &newPixel, bpp);
        }
    }

    if (SDL_MUSTLOCK(surface))
        SDL_UnlockSurface(surface);
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
        int curTexWidth = wallTextures[texNum].width;
        int curTexHeight = wallTextures[texNum].height;

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

            ZBuffer[x] = perpWallDist;
        }
    }
}

void renderSky(const gameState& state){
    int texW = skyTexture.width;
    int texH = skyTexture.height;

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
        int floorWidth = wallTextures[floorTexture].width;
        int floorHeight = wallTextures[floorTexture].height;

        int ceilWidth = wallTextures[ceilingTexture].width;
        int ceilHeight = wallTextures[ceilingTexture].height;

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

        // // Ceiling (symmetrical, at screenHeight - y - 1 instead of y)
        // color = texture[ceilingTexture].texels[ceilWidth * tyCeil + txCeil];
        // color = darkenColor(color, 1.0/2.0);
        // //buffer[WINDOW_HEIGHT - y - 1][x] = color;

        // // Floor
        // color = texture[floorTexture].texels[floorWidth * tyFloor + txFloor];
        // color = darkenColor(color, 1.0/2.0);
        // buffer[y][x] = color;
      }
    }
}

void renderSprites(SDL_Renderer* renderer, const gameState& state){
    Player player = state.player;
    std::vector<Player> otherPlayers = state.otherPlayers;

    // Arrays used to sort the sprites
    std::vector<int> spriteOrder;
    std::vector<double> spriteDistance;

    int fullNumSprites = state.numSprites + state.numPlayerSprites;
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
    for(int i = 0; i < numSpriteTextures; i++){
        int spriteIndex = spriteOrder[i];
        int spriteTexWidth, spriteTexHeight;

        SDL_Surface** texture; // The current texture
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

                *texture = playerRunTextures[step][playerTexture].texture,
                spriteTexWidth  = playerRunTextures[step][playerTexture].width;
                spriteTexHeight = playerRunTextures[step][playerTexture].height;
            }
            // Standing texture
            else {
                *texture = playerTextures[playerTexture].texture;
                spriteTexWidth  = playerTextures[playerTexture].width;
                spriteTexHeight = playerTextures[playerTexture].height;
            }

            spritePos = otherPlayer.pos;

            // Weird purple thingy
            invisColor = 0x980088;
        }
        else {
            int tex = state.sprites[spriteIndex].texture;
            *texture = wallTextures[tex].texture;
            spriteTexWidth  = wallTextures[tex].width;
            spriteTexHeight = wallTextures[tex].height;
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
                    // Uint32 color = (*textureArr)[spriteTexWidth * texY + texX]; // Get current color from the texture
                    // if ((color & 0x00FFFFFF) != invisColor){
                    //     buffer[y][stripe] = color; // Paint pixel if it isn't black, black is the invisible color
                    // }
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

    double FOV = 90.0;
    gameState state;
    state.player = Player({5, 5}, Vector(1, 1), FOV * (PI / 180.0));

    Client connection;
    if (!connection.connectToServer(state, serverIP)) return 0;
    std::cout << "Our ID: " << state.player.playerID << std::endl;

    // On connection, send our player info immediately to the other clients
    connection.sendData(state);

    ZBuffer.resize(WINDOW_WIDTH);

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
    loadImage(TEXTURE_SKY, "pics/doomSky.png", 0);

    // Player
    loadImage(TEXTURE_PLAYER, "pics/player.png", 0);

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
    std::atomic<bool> run;
    run.store(true, std::memory_order_release);
    // The thread where we will receive updates from the server
    std::thread receiveThread(&Client::receiveData, &connection, std::ref(state), std::ref(run));
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
        if (cameraChanged || state.player.isMoving){
            // Send the new state of the player to the server
            connection.sendData(state);
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

        // The rendering process
        renderSky(state);
        renderFloorAndCeiling(renderer, state);
        renderWalls(renderer, state);
        renderSprites(renderer, state);

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
    connection.disconnectFromServer();

    return 0;
}