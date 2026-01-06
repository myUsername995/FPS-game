#include "networking.hpp"
#include <iostream>

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
                    state.player.playerID = *(int*)event.packet->data;
                }
                // Dimensions
                else if (numPacketsReceived == 1){
                    struct Dimensions {
                        int width, height;
                    };

                    Dimensions mapDimensions;
                    memcpy(&mapDimensions, event.packet->data, event.packet->dataLength);

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
                    int numSprites;
                    memcpy(&numSprites, event.packet->data, event.packet->dataLength);

                    state.numSprites = numSprites;
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

void Client::receiveData(gameState& state, std::atomic<bool>& run){
    while (run.load(std::memory_order_acquire)){
            ENetEvent event;
            while (enet_host_service(client, &event, 50) > 0){
                if (event.type == ENET_EVENT_TYPE_DISCONNECT){
                    run.store(false, std::memory_order_release);
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

                    state.numPlayerSprites = numPlayers;
                    state.sprites.resize(state.numSprites + numPlayers);
                    // Add sprites for the players
                    for (int i = 0; i < numPlayers; i++){
                        Sprite newSprite;
                        newSprite.texture = 11; // Player texture
                        newSprite.pos = state.otherPlayers[i].pos;
                        newSprite.index = i;
                        newSprite.isPlayer = true;

                        state.sprites[i] = newSprite;
                    }

                    break;
                }
            }
        }
}

void Client::sendData(gameState& state){
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

    enet_peer_send(Client::server, 0, packet);

    enet_host_flush(Client::client);
}