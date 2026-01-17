#include "networking.hpp"
#include <iostream>
#include <cassert>

// Used for sending data over the network
struct Network_player {
    // Metadata
    uint32_t playerID;

    // Player data
    float posX;
    float posY;
    float lookDirX;
    float lookDirY;

    // Animation related data
    uint8_t isMoving;
    uint8_t animationStep;
};

// The number of none player sprites
bool Client::connectToServer(gameState& state, std::string serverIP){
    Client::client = enet_host_create(NULL, 1, 2, 0, 0);
    if (Client::client == NULL){
        std::cerr << "Couldn't set up client.\n";
        return false;
    }

    ENetAddress serverAddress;
    ENetEvent event;
    enet_address_set_host(&serverAddress, serverIP.c_str());
    serverAddress.port = 1234;

    Client::server = enet_host_connect(Client::client, &serverAddress, 2, 0);
    if (Client::server == NULL){
        std::cerr << "Couldn't find the server.\n";
        return false;
    }

    // Try to connect for 5 seconds
    bool connected = false;
    while (enet_host_service(Client::client, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT){

        std::cerr << "Succesfully connected to the server.\n";
        connected = true;
        break;
    }

    if (!connected){
        std::cerr << "Couldn't connect to server.\n";
        enet_peer_reset(Client::server);
        return false;
    }

    // Wait to receive the packets:
    // 1. playerID, 2. map dimensions, 3. map data 4. sprites length 5. sprites data

    bool receivedPacket = false;
    int numPacketsReceived = 0;
    while (numPacketsReceived < 5){
        bool receivedPacket = false;
        while (enet_host_service(Client::client, &event, 5000) > 0){
            if (event.type == ENET_EVENT_TYPE_RECEIVE){
                receivedPacket = true;

                // Set the playerID
                if (numPacketsReceived == 0){
                    struct playerMetaData {
                        uint32_t playerID;
                        char username[32];
                    };

                    playerMetaData received;
                    memcpy(&received, event.packet->data, event.packet->dataLength);

                    state.player.playerID = received.playerID;
                    state.player.username = received.username;

                    std::cout << "Our username: " << state.player.username << std::endl;
                }
                // Dimensions
                else if (numPacketsReceived == 1){
                    struct Dimensions {
                        int width, height;
                    };

                    Dimensions dims;
                    memcpy(&dims, event.packet->data, event.packet->dataLength);

                    state.map.resize(dims.width);
                    for (int i = 0; i < dims.width; i++){
                        state.map[i].resize(dims.height);
                    }
                }
                // Map data
                else if (numPacketsReceived == 2){
                    int width = state.map.size();
                    int height = state.map[0].size();

                    std::vector<int> arr(width * height);

                    memcpy(arr.data(), event.packet->data, event.packet->dataLength);

                    for (int i = 0; i < width * height; i++){
                        int x = i / width;
                        int y = i % width;

                        state.map[x][y] = arr[i];
                    }
                }
                // Sprites metadata
                else if (numPacketsReceived == 3){
                    int numSprites;
                    memcpy(&numSprites, event.packet->data, event.packet->dataLength);

                    state.numSprites = numSprites;
                    state.fullNumSprites = numSprites;
                    state.sprites.resize(numSprites);
                }
                // Sprites data
                else if (numPacketsReceived == 4){
                    struct network_sprite {
                        float x, y;
                        int texture;
                    };

                    std::vector<network_sprite> network_sprites(state.sprites.size());
                    memcpy(network_sprites.data(), event.packet->data, event.packet->dataLength);

                    for (int i = 0; i < state.sprites.size(); i++){
                        state.sprites[i].pos = {network_sprites[i].x, network_sprites[i].y};
                        state.sprites[i].texture = network_sprites[i].texture;
                        state.sprites[i].isPlayer = false;
                    }
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

void Client::disconnectFromServer(){
    // Disconnect from the server
    std::cout << "Disconnecting...\n";
    enet_peer_disconnect(Client::server, 0);

    ENetEvent event;

    // Wait 5 second to disconnect
    bool disconnected = false;
    while (enet_host_service(Client::client, &event, 5000) > 0){
        if (event.type == ENET_EVENT_TYPE_DISCONNECT){
            disconnected = true;
            break;
        }
    }

    if (!disconnected){
        enet_peer_reset(Client::server);
    }

    enet_host_destroy(client);

    std::cout << "Disconnected from the server.\n";
}

void Client::receiveData(gameState& state){
    ENetEvent event;
    while (enet_host_service(Client::client, &event, 0) > 0){
        if (event.type == ENET_EVENT_TYPE_DISCONNECT){
            std::cout << "Disconnected from the server.\n";
            
            return;
        }
        if (event.type == ENET_EVENT_TYPE_RECEIVE){
            // The server sent an std::vector<Network_player>.data() array
            size_t numPlayers = event.packet->dataLength / sizeof(Network_player);

            std::vector<Network_player> network_otherPlayers;
            network_otherPlayers.resize(numPlayers); // allocate space
            state.otherPlayers.resize(numPlayers);

            assert(network_otherPlayers.size() * sizeof(Network_player) == event.packet->dataLength && 
                   "networking.cpp: Network_player size is wrong.");
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
                state.otherPlayers[i].camera = {0, 0}; // Camera doesn't matter for other players
                state.otherPlayers[i].lookDir = {network_otherPlayers[i].lookDirX, network_otherPlayers[i].lookDirY};
                state.otherPlayers[i].playerID = network_otherPlayers[i].playerID;
                state.otherPlayers[i].pos.x = network_otherPlayers[i].posX;
                state.otherPlayers[i].pos.y = network_otherPlayers[i].posY;
                state.otherPlayers[i].isMoving = network_otherPlayers[i].isMoving == 1 ? true : false;
                state.otherPlayers[i].animationStep = network_otherPlayers[i].animationStep;
            }

            state.numPlayers = numPlayers;
            state.fullNumSprites = state.numSprites + state.numPlayers;
            state.sprites.resize(state.fullNumSprites);
            // Add sprites for the players
            for (int i = 0; i < numPlayers; i++){
                Sprite newSprite;
                newSprite.texture = 0;
                newSprite.pos = state.otherPlayers[i].pos;
                newSprite.index = i;
                newSprite.isPlayer = true;

                state.sprites[state.numSprites + i] = newSprite;
            }
        }
    }
}

void Client::sendData(gameState& state){
    // Create a struct instead of a class
    Network_player p;
    p.lookDirX = state.player.lookDir.x;
    p.lookDirY = state.player.lookDir.y;
    p.playerID = state.player.playerID;
    p.posX = state.player.pos.x;
    p.posY = state.player.pos.y;
    p.isMoving = state.player.isMoving;
    p.animationStep = state.player.animationStep;

    ENetPacket* packet = enet_packet_create(&p, sizeof(p), ENET_PACKET_FLAG_RELIABLE);

    enet_peer_send(Client::server, 0, packet);

    enet_host_flush(Client::client);
}