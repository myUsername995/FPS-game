#include "networking.hpp"
#include <iostream>
#include <cassert>
#include <cstring>

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
    uint8_t gunFrame;
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

    std::cout << "Connecting to " << serverIP << "..." << std::endl;

    // Try to connect for 5 seconds
    bool connected = false;
    while (enet_host_service(Client::client, &event, 5000) > 0){
        if (event.type == ENET_EVENT_TYPE_CONNECT){
            std::cerr << "Succesfully connected to the server.\n";
            connected = true;
        }
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

// Returns whether data was received or not
bool Client::receiveData(gameState& state){
    bool received = false;
    ENetEvent event;
    while (enet_host_service(Client::client, &event, 0) > 0){
        received = true;
        if (event.type == ENET_EVENT_TYPE_DISCONNECT){
            std::cout << "Disconnected from the server.\n";
            
            return false;
        }
        if (event.type == ENET_EVENT_TYPE_RECEIVE){
            size_t numOtherPlayers = event.packet->dataLength / sizeof(Network_player) - 1;

            Network_player network_player;
            std::vector<Network_player> network_otherPlayers;
            network_otherPlayers.reserve(numOtherPlayers);
            state.otherPlayers.resize(numOtherPlayers);
            
            std::vector<Network_player> network_allPlayers;
            network_allPlayers.resize(numOtherPlayers+1); // allocate space
            assert((numOtherPlayers + 1) * sizeof(Network_player) == event.packet->dataLength && "networking.cpp: Network_player size is wrong.");
            memcpy(network_allPlayers.data(), event.packet->data, event.packet->dataLength);

            // Remove our own player from the array so it only contains other players
            int removeIndex = -1;
            for (int i = 0; i < numOtherPlayers + 1; i++){
                if (network_allPlayers[i].playerID == state.player.playerID){
                    network_player = network_allPlayers[i];
                    removeIndex = i;
                    break;
                }
            }

            if (removeIndex == -1){
                std::cout << "Player was not sent from the server.\n";
                return false;
            }

            network_allPlayers.erase(network_allPlayers.begin() + removeIndex);
            network_otherPlayers = network_allPlayers;

            auto copyNetworkStruct = [](Player& p, const Network_player& netP) {
                p.camera = {0, 0}; // Camera doesn't matter for other players
                p.lookDir = {netP.lookDirX, netP.lookDirY};
                p.playerID = netP.playerID;
                p.username = netP.username;
                p.pos.x = netP.posX;
                p.pos.y = netP.posY;
                p.isMoving = (netP.isMoving == 1);
                p.animationStep = netP.animationStep;
                p.ping = netP.ping;
                p.health = netP.health;
                p.gunFrame = netP.gunFrame;
            };

            // Handle our player differently
            copyNetworkStruct(state.player, network_player);

            // Copy into the actual otherPlayers array
            for (int i = 0; i < numOtherPlayers; i++){
                copyNetworkStruct(state.otherPlayers[i], network_otherPlayers[i]);
            }

            state.numPlayers = numOtherPlayers;
            state.fullNumSprites = state.numSprites + state.numPlayers;
            state.sprites.resize(state.fullNumSprites);
            // Add sprites for the other players (not for ourselves, as we don't see our own sprite)
            for (int i = 0; i < numOtherPlayers; i++){
                Sprite newSprite;
                newSprite.texture = 0;
                newSprite.pos = state.otherPlayers[i].pos;
                newSprite.index = i;
                newSprite.isPlayer = true;

                state.sprites[state.numSprites + i] = newSprite;
            }
        }
    }

    return received;
}

void Client::sendData(gameState& state){
    // Create a struct instead of a class
    Network_player p;
    p.lookDirX = state.player.lookDir.x;
    p.lookDirY = state.player.lookDir.y;
    p.playerID = state.player.playerID;
    std::strncpy(p.username, state.player.username.c_str(), sizeof(p.username) - 1);
    p.username[sizeof(p.username) - 1] = '\0';
    p.posX = state.player.pos.x;
    p.posY = state.player.pos.y;
    p.isMoving = state.player.isMoving;
    p.animationStep = state.player.animationStep;
    p.ping = state.player.ping;
    p.health = state.player.health;
    p.gunFrame = state.player.gunFrame;

    ENetPacket* packet = enet_packet_create(&p, sizeof(p), ENET_PACKET_FLAG_RELIABLE);

    enet_peer_send(Client::server, 0, packet);

    enet_host_flush(Client::client);
}