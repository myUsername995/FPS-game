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

enum PacketID : uint8_t {
    PACKET_PLAYER_ID,
    PACKET_PLAYER_USERNAME,

    PACKET_MAP_METADATA,
    PACKET_MAP_DATA,
    PACKET_SPRITE_METADATA,
    PACKET_SPRITE_DATA,
};

// The data we will send over the network
struct PacketData {
    PacketID id;
    void* data;
};

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

struct sprite {
    float x, y;
    int texture;
};

struct ClientData {
    int playerID;
    std::string username;
};

#include <fstream>

bool readMapData(std::string fileName, int** worldMap, sprite** sprites, uint32_t& width, uint32_t& height, uint32_t& numSprites){
    std::ifstream in(fileName, std::ios::binary);

    if (!in){
        std::cout << "File " + fileName + " doesn't exist.\n";
        return false;
    }

    in.read((char*)&width, sizeof(width));
    in.read((char*)&height, sizeof(height));
    in.read((char*)&numSprites, sizeof(numSprites));

    *worldMap = new int[width * height];
    *sprites  = new sprite[numSprites];

    in.read((char*)*worldMap, width * height * sizeof(int));

    for (uint32_t i = 0; i < numSprites; i++) {
        sprite curSprite = (*sprites)[i];

        in.read((char*)&curSprite.x, sizeof(float));
        in.read((char*)&curSprite.y, sizeof(float));
        in.read((char*)&curSprite.texture, sizeof(int32_t));

        (*sprites)[i] = curSprite;
    }

    return true;
}

int findAvailableID(std::array<bool, 100>& IDs){
    for (int i = 0; i < 100; i++){
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
    for (int i = 0; i < players.size(); i++){
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

// Just append the ID to the start of the pointer
ENetPacket* createPacket(void* data, size_t size, PacketID id) {
    uint8_t packetID = static_cast<uint8_t>(id);

    // Create vector with 1 + size bytes
    std::vector<uint8_t> bytes(size+1);
    bytes[0] = id;
    // Append data
    if (size > 0 && data)
        bytes.insert(bytes.end(),
                     reinterpret_cast<uint8_t*>(data),
                     reinterpret_cast<uint8_t*>(data) + size);

    // Create ENet packet
    return enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
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

    sprite* sprites;
    int* flattenedMap;
    uint32_t mapWidth, mapHeight, numSprites;

    std::string fileName;
    std::cout << "Enter a filename: \n";
    std::cin >> fileName;

    if (!readMapData("maps/" + fileName, &flattenedMap, &sprites, mapWidth, mapHeight, numSprites)) return 0;

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
    while (true){
        while (enet_host_service(server, &event, 1000) > 0){
            switch (event.type){
                case ENET_EVENT_TYPE_CONNECT: {
                    // Give him an username
                    std::string username = getRandomName(randNames, numNames);

                    // Give him an ID
                    int playerID = findAvailableID(availableIDs);
                    if (playerID == -1) { enet_peer_reset(event.peer); break; }
                    availableIDs[playerID] = false;

                    // Add him to our players array
                    Network_player newPlayer{};
                    newPlayer.playerID = playerID;
                    players.push_back(newPlayer);
                    numPlayers++;
                    std::cout << username << " connected to the server. " << numPlayers << " player(s) online." << std::endl;

                    // Store safe client data
                    event.peer->data = new ClientData{playerID, username};

                    // Send ID back to client
                    ENetPacket* idPacket = enet_packet_create(&playerID, sizeof(playerID), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, idPacket);

                    // Send with and height
                    struct Dimensions {
                        int width, height;
                    };

                    Dimensions mapDimensions;
                    mapDimensions.width = mapWidth;
                    mapDimensions.height = mapHeight;

                    ENetPacket* dimensionsPacket = enet_packet_create(&mapDimensions, sizeof(mapDimensions), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, dimensionsPacket);

                    // Send the map data to the client
                    ENetPacket* mapPacket = enet_packet_create(flattenedMap, mapWidth * mapHeight * sizeof(int), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, mapPacket);

                    // Send the sprites data
                    // Length
                    ENetPacket* spritesLength = enet_packet_create(&numSprites, sizeof(numSprites), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, spritesLength);

                    // Actual data
                    ENetPacket* spritesPacket = enet_packet_create(sprites, numSprites * sizeof(sprite), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, spritesPacket);

                    enet_host_flush(server);

                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    // Assume the client sent their player information
                    if (event.packet->dataLength != sizeof(Network_player)) break;

                    int playerID = static_cast<ClientData*>(event.peer->data)->playerID;
                    int index = findPlayerID(players, playerID);
                    if (index == -1) break;

                    Network_player temp;
                    memcpy(&temp, event.packet->data, sizeof(Network_player));
                    players[index] = temp;

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
                    std::cout << username << " disconnected from the server. " << numPlayers << " player(s) online.\n";
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

    enet_host_destroy(server);

    return 0;
}