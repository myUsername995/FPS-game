#pragma once
#include <enet/enet.h>
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cmath>

extern float PI;
extern std::string serverIP;

class Vector {
    public:
        Vector(){}
        Vector(float x, float y){
           Vector::x = x;
           Vector::y = y; 
        }
        ~Vector(){}

        static double dot(const Vector& v1, const Vector& v2){
            return v1.x * v2.x + v1.y * v2.y;
        }
        static double cross(const Vector& v1, const Vector& v2){
            return v1.x * v2.y - v1.y * v2.x;
        }

        double length() const {
            return std::sqrt(Vector::x * Vector::x + Vector::y * Vector::y);
        }

        Vector normalize() const {
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
        std::string username;

        // Player data
        SDL_FPoint pos;
        Vector lookDir;
        Vector camera;

        // Use this to animate players
        bool isMoving = false;
        int animationStep = 0;

        int health = 100;
        int gunFrame = 0;
        int gunType = -1;        // 0 -> pistol 1 -> bigger gun thingy
        bool fired;

        double ping;
};

struct Sprite {
    // General sprite attributes
    SDL_FPoint pos;
    int texture;

    bool isPlayer;      // Determine if the current sprite is a player, then we need to find the player infos in the ohterPlayers array
    int index;          // An index into the otherPlayers array
};

struct Line {
    SDL_FPoint p1;
    SDL_FPoint p2;
    int texture;
};

struct Texture {
    SDL_Surface* texture;
    int width, height;
};

// The state of our game
struct gameState {
    Player player;
    std::vector<Player> otherPlayers;

    // The server sends the map to each client on start-up
    std::vector<Line> lineMap;
    std::vector<std::vector<int>> map;          // An array of X cordinates which store Y cordinates
    std::vector<Sprite> sprites;                // The data about sprites
    int numSprites;
    int numPlayers;
    int fullNumSprites; // numSprites + numPlayers

    // The textures of sprites
    std::vector<Texture> wallTextures;
    std::vector<Texture> spriteTextures;        // Sprite textures include players too
    std::vector<Texture> screenTextures;
};