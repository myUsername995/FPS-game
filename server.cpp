/*
What this server will be doing:
ON CLIENT CONNECTING:
-Send map data
-Send player data
-Send clients data (if any)

ON CLIENT INPUT:
-Inputs are resolved client-side
-New positions are sent to the server, synchronise with other clients
*/

#include <SDL3/SDL.h>
#include <enet/enet.h>
#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <thread>
#include <atomic>

int maxUsername = 16;

// Where we log server messages
std::vector<std::string> errors;
std::vector<std::string> connections;

std::atomic<bool> running(true);

// Used for sending data over the network
struct Network_player {
    // Metadata
    uint32_t playerID;
    char username[32];

    double ping;

    // Player data
    float posX;
    float posY;
    float lookDirX;
    float lookDirY;

    // Animation related data
    uint8_t isMoving;
    uint8_t animationStep;

    // Only send the health to the other client and the gun frame, so they can change their health and also render our player
    int8_t health;
    int8_t gunFrame;
};

static_assert(sizeof(Network_player) == 72);

struct ClientData {
    uint32_t playerID;
    std::string username;
};

struct sprite {
    float x, y;
    int texture;
};

bool readMapData(std::string fileName, std::vector<int>& worldMap, std::vector<sprite>& sprites, uint32_t& width, uint32_t& height, 
                 uint32_t& numSprites){

    std::ifstream in(fileName, std::ios::binary);

    if (!in){
        std::cout << "File " + fileName + " doesn't exist.\n";
        return false;
    }

    in.read((char*)&width, sizeof(width));
    in.read((char*)&height, sizeof(height));
    in.read((char*)&numSprites, sizeof(numSprites));

    worldMap.resize(width * height);
    sprites.resize(numSprites);

    in.read((char*)worldMap.data(), width * height * sizeof(int));

    for (int i = 0; i < numSprites; i++){
        sprite newSprite;
        in.read((char*)&newSprite.x, sizeof(float));
        in.read((char*)&newSprite.y, sizeof(float));
        in.read((char*)&newSprite.texture, sizeof(int32_t));

        sprites[i] = newSprite;
    }

    return true;
}

int findAvailableID(std::array<bool, 100>& IDs){
    for (uint32_t i = 0; i < 100; i++){
        // true -> available
        if (IDs[i]){
            return i;
        }
    }

    // No available IDs
    return -1;
}

// Return the index into the players array, based on a playerID
int findPlayerID(const std::vector<Network_player>& players, int id){
    for (uint32_t i = 0; i < players.size(); i++){
        if (players[i].playerID == id){
            return i;
        }
    }

    // Couldn't find the player
    return -1;
}

std::string getRandomName(std::vector<std::string>& randNames, int& numNames){
    numNames++;
    if (randNames.size() == 0){
        return "we_ran_out_of_silly_names" + std::to_string(numNames);
    }

    int rand = SDL_rand(randNames.size());

    std::string result = randNames[rand];

    randNames.erase(randNames.begin() + rand);

    return result;
}

// Where we parse inputs from the server owner
/*
print errors -> prints the logged errors
print connections -> prints the logged connections
shutdown -> shuts down the server
*/

void readInput(){

    // Helper functions
    // If the current character is a white space, it goes until it finds a letter and returns that index
    auto skipWhiteSpaces = [](std::string str, int idx) -> int {
        while (str[idx] == ' ') idx++;

        return idx;
    };

    // Keep going until we see a whitespace
    auto findNextWhiteSpace = [](std::string str, int idx) -> int {
        while (str[idx] != ' ') idx++;

        return idx;
    };

    while (running){
        std::string input;
        std::getline(std::cin, input);

        // Parse the first word
        int startWord = skipWhiteSpaces(input, 0);
        int endWord = findNextWhiteSpace(input, startWord);

        std::string firstWord = input.substr(startWord, endWord - startWord);
        if (firstWord == "print"){
            // Parse the second word
            int startWord2 = skipWhiteSpaces(input, endWord);
            int endWord2 = findNextWhiteSpace(input, startWord2);

            std::string secondWord = input.substr(startWord2, endWord2 - startWord2);
            
            if (secondWord == "error"){
                for (const auto& str : errors) std::cout << str;
            }
            else if (secondWord == "connections"){
                for (const auto& str : connections) std::cout << str;
            }
        }
        else if (firstWord == "shutdown"){
            running = false;
            break;
        }
        else {
            std::cout << "Invalid command.\n";
        }
    }

    // Flush the std::getline
    std::cout << std::endl;
}

int main(int argc, char* argv[]){

    if (enet_initialize() < 0){
        std::cerr << "Couldn't initialize enet!\n";
        return 0;
    }
    atexit(enet_deinitialize);

    int numNames = 0;
    std::vector<std::string> randNames = {
        "ben_dover",
        "mike_ox_long",
        "pdf_file",
        "hugh_janus",
        "gabe_itch",
        "jack_king_hoff",
        "dick_enbals"
    };

    int numPlayers = 0;

    std::array<bool, 100> availableIDs;
    availableIDs.fill(true);

    std::vector<sprite> sprites;
    std::vector<int> flattenedMap;
    uint32_t mapWidth, mapHeight, numSprites;

    std::string fileName;
    std::cout << "Enter a filename: \n";
    std::getline(std::cin, fileName);

    if (!readMapData("maps/" + fileName, flattenedMap, sprites, mapWidth, mapHeight, numSprites)) return 0;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = 1234;

    ENetHost* server = enet_host_create(&address, 32, 2, 0, 0);
    
    if (server == NULL){
        std::cerr << "An error occurred while trying to create an ENet server host.\n" << std::endl;
        return 0;
    }

    std::vector<Network_player> players;

    ENetEvent event;

    std::thread inputThread(readInput);
    while (running){
        while (enet_host_service(server, &event, 10) > 0){
            switch (event.type){
                case ENET_EVENT_TYPE_CONNECT: {
                    // Give him an username
                    std::string username = getRandomName(randNames, numNames);

                    if (username.size() > maxUsername){
                        errors.push_back(username + ": Username can't be longer than " + std::to_string(maxUsername) +  " characters.\n");
                        enet_peer_reset(event.peer); break;
                    }

                    // Give him an ID
                    int playerID = findAvailableID(availableIDs);
                    if (playerID == -1){
                        errors.push_back("Too many players, can't assign ID to " + username + ".\n");
                        enet_peer_reset(event.peer); break;
                    }
                    availableIDs[playerID] = false;

                    // Add him to our players array
                    Network_player newPlayer{};
                    newPlayer.playerID = playerID;
                    players.push_back(newPlayer);
                    numPlayers++;
                    connections.push_back(username + " connected to the server. " + std::to_string(numPlayers) + " player(s) online.\n");

                    // Store safe client data
                    event.peer->data = new ClientData{static_cast<uint32_t>(playerID), username};

                    struct playerMetaData {
                        uint32_t playerID;
                        char username[32];
                    };

                    playerMetaData packet;
                    packet.playerID = playerID;
                    strncpy(packet.username, username.c_str(), sizeof(packet.username) - 1);
                    packet.username[sizeof(packet.username)-1] = '\0';

                    // Send ID and username back to the client
                    ENetPacket* idPacket = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, idPacket);

                    // Send our map dimensions first
                    struct Dimensions {
                        int width, height;
                    };
                    Dimensions dims;
                    dims.width = mapWidth;
                    dims.height = mapHeight;

                    ENetPacket* dimensionsPacket = enet_packet_create(&dims, sizeof(dims), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, dimensionsPacket);

                    // Send the map data to the client
                    ENetPacket* mapPacket = enet_packet_create(flattenedMap.data(), mapWidth * mapHeight * sizeof(int), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, mapPacket);

                    // Send the sprites data
                    // Length
                    ENetPacket* spritesLength = enet_packet_create(&numSprites, sizeof(numSprites), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, spritesLength);

                    // Actual data
                    ENetPacket* spritesPacket = enet_packet_create(sprites.data(), numSprites * sizeof(sprite), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, spritesPacket);

                    enet_host_flush(server);

                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    // Assume the client sent their player information
                    if (event.packet->dataLength != sizeof(Network_player)) break;
                    Network_player curPlayer;
                    memcpy(&curPlayer, event.packet->data, sizeof(Network_player));

                    // The player we have to change isn't necessary the player the event.peer client has, so calculate it
                    int playerID = curPlayer.playerID;
                    int index = findPlayerID(players, playerID);
                    if (index == -1) break;

                    players[index] = curPlayer;

                    // Broadcast to all clients
                    ENetPacket* packet = enet_packet_create(players.data(), players.size() * sizeof(Network_player), ENET_PACKET_FLAG_RELIABLE);
                    enet_host_broadcast(server, 0, packet);
                    enet_host_flush(server);

                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT: {
                    numPlayers--;
                    std::string username = static_cast<ClientData*>(event.peer->data)->username;
                    connections.push_back(username + " disconnected from the server. " + std::to_string(numPlayers) + " player(s) online.\n");
                    if (event.peer->data) {
                        // Free the ID and username for later usage by other clients
                        int playerID = static_cast<ClientData*>(event.peer->data)->playerID;
                        availableIDs[playerID] = true;
                        randNames.push_back(username);

                        // Erase the disconnected player from the array
                        int index = findPlayerID(players, playerID);
                        if (index != -1) players.erase(players.begin() + index);

                        delete static_cast<ClientData*>(event.peer->data);
                        event.peer->data = nullptr;
                    }

                    // Send the updated players array to every client so that they're immediately updated
                    ENetPacket* packet = enet_packet_create(players.data(), players.size() * sizeof(Network_player), ENET_PACKET_FLAG_RELIABLE);
                    enet_host_broadcast(server, 0, packet);
                    enet_host_flush(server);
                    
                    break;
                }
            }
        }
    }

    inputThread.join();

    std::cout << "Destroying the server." << std::endl;

    // Broadcast a disconnect message
    ENetPacket* packet = enet_packet_create(nullptr, 0, ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server, 0, packet);
    enet_host_flush(server);

    uint32_t start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 1000) {
        enet_host_service(server, &event, 0);
    }

    enet_host_destroy(server);

    return 0;
}