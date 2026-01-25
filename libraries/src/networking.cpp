/*
The packets have 3 segments:
1. Identifier (PacketType)
2. Sender ID (uint32_t)
3. Data (std::vector<uint8_t>)

Use the convertData() function to acquire these segments from the raw bytes received over the network.
*/

#include "networking.hpp"
#include <iostream>
#include <cassert>
#include <cstring>

enum PacketType : uint8_t {
    // Sent during gameplay
    PACKET_KICK,
    PACKET_SHUTDOWN,
    PACKET_DATA,
    
    // Sent on initialisation
    PACKET_PLAYER_METADATA,
    PACKET_MAP_METADATA,
    PACKET_MAP_DATA,
    PACKET_SPRITES_METADATA,
    PACKET_SPRITES_DATA
};

// Used for sending data over the network
struct Network_player {
    // Metadata
    uint32_t playerID;
    char username[32];

    float ping;
    float dmgDealt;

    // Player data
    float posX;
    float posY;
    float lookDirX;
    float lookDirY;

    // Animation related data
    uint8_t isMoving;
    uint8_t animationStep;
    int8_t gunFrame;

    // Send which player we hit, then the server sends back their health
    int8_t playerHit;
    int8_t health;
};
static_assert(sizeof(Network_player) == 68);

// Convert the bytes sent from the network into the 3 segments
void convertData(const std::vector<uint8_t>& buffer, PacketType& type, uint32_t& sendID, std::vector<uint8_t>& data){
    size_t segment1Size = sizeof(uint8_t);
    size_t segment2Size = sizeof(uint32_t);
    data.resize(buffer.size() - segment1Size - segment2Size);

    // Remove the bytes from the first two segments
    int dataSize = buffer.size() - (segment1Size + segment2Size);

    memcpy(&type, buffer.data(), segment1Size);
    memcpy(&sendID, buffer.data() + segment1Size, segment2Size);
    memcpy(data.data(), buffer.data() + segment1Size + segment2Size, dataSize);
}

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

    // Try to connect for 3 seconds
    bool connected = false;
    int elapsed = 0;
    int timeout = 3000;
    while (elapsed < timeout) {
        int serviceResult = enet_host_service(Client::client, &event, 100);
        if (serviceResult > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
            std::cout << "Successfully connected to the server.\n";
            connected = true;
            break;
        }
        elapsed += 100;
    }

    if (!connected){
        std::cerr << "Couldn't connect to server.\n";
        enet_peer_reset(Client::server);
        return false;
    }

    // Wait to receive the packets
    bool receivedPlayerMetadata = false;
    bool receivedMapMetadata = false;
    bool receivedMapData = false;
    bool receivedSpritesMetadata = false;
    bool receivedSpritesData = false;

    bool receivedAll = false;

    timeout = 3000;
    elapsed = 0;
    while (elapsed < timeout){
        int receivedPacket = enet_host_service(client, &event, 100);
        elapsed += 100;

        receivedAll = receivedPlayerMetadata && receivedMapMetadata && receivedMapData && receivedSpritesMetadata && receivedSpritesData;
        if (receivedAll) break;
        if (!receivedPacket) continue;

        if (event.type == ENET_EVENT_TYPE_RECEIVE){
            // The buffer of bytes we receive from the network
            std::vector<uint8_t> buffer(event.packet->dataLength);
            memcpy(buffer.data(), event.packet->data, event.packet->dataLength);

            PacketType type;
            uint32_t sendID; // Send ID doesn't matter here, because it's sent by the server
            std::vector<uint8_t> data;

            convertData(buffer, type, sendID, data);

            // Handle each kind of data differently
            switch (type){
                case PACKET_PLAYER_METADATA: {
                    receivedPlayerMetadata = true;
                    struct playerMetaData {
                        uint32_t playerID;
                        char username[32];
                    };

                    playerMetaData received;
                    memcpy(&received, data.data(), data.size());

                    state.player.playerID = received.playerID;
                    state.player.username = received.username;
                    break;
                }
                case PACKET_MAP_METADATA: {
                    receivedMapMetadata = true;
                    struct Dimensions {
                        int width, height;
                    };

                    Dimensions dims;
                    memcpy(&dims, data.data(), data.size());

                    state.map.resize(dims.width);
                    for (int i = 0; i < dims.width; i++){
                        state.map[i].resize(dims.height);
                    }
                    break;
                }
                case PACKET_MAP_DATA: {
                    receivedMapData = true;
                    int width = state.map.size();
                    int height = state.map[0].size();

                    std::vector<int> arr(width * height);
                    memcpy(arr.data(), data.data(), width * height * sizeof(int));

                    for (int i = 0; i < width * height; i++){
                        int x = i / width;
                        int y = i % width;

                        state.map[x][y] = arr[i];
                    }
                    break;
                }
                case PACKET_SPRITES_METADATA: {
                    receivedSpritesMetadata = true;
                    int numSprites;
                    memcpy(&numSprites, data.data(), data.size());

                    state.numPlayers = 0;
                    state.numSprites = numSprites;
                    state.fullNumSprites = numSprites;
                    state.sprites.resize(numSprites);
                    break;
                }
                case PACKET_SPRITES_DATA: {
                    receivedSpritesData = true;
                    struct network_sprite {
                        float x, y;
                        int texture;
                    };

                    std::vector<network_sprite> network_sprites(state.sprites.size());
                    memcpy(network_sprites.data(), data.data(), state.numSprites * sizeof(network_sprite));

                    for (int i = 0; i < state.sprites.size(); i++){
                        state.sprites[i].pos = {network_sprites[i].x, network_sprites[i].y};
                        state.sprites[i].texture = network_sprites[i].texture;
                        state.sprites[i].isPlayer = false;
                    }
                    break;
                }
            }

            enet_packet_destroy(event.packet);
    }
    }

    // Show which packets we didn't receive
    if (!receivedAll){
        std::cout << "Connection timed out.\n";

        if (!receivedPlayerMetadata){
            std::cerr << "Didn't receive the player metadata from the server.\n";
        }
        if (!receivedMapMetadata){
            std::cerr << "Didn't receive the map dimensions from the server.\n";
        }
        if (!receivedMapData){
            std::cerr << "Didn't receive the map data from the server.\n";
        }
        if (!receivedSpritesMetadata){
            std::cerr << "Didn't receive the sprites length data from the server.\n";
        }
        if (!receivedSpritesData){
            std::cerr << "Didn't receive the sprites data from the server.\n";
        }

        return false;
    }
    
    std::cout << "Received all initialisation packets.\n";

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

// Receives data from the server
// ReceivedRoundTrip is used to measure ping like this -> we send data and start a timer, the server sends us back our own data with 
// our own sendID, we detect that the server just sent back our own data, so we end the timer and thats our ping.
void Client::receiveData(gameState& state, bool& receivedRoundTrip, bool& shutdown, bool& kicked, bool& corruptedData){
    shutdown = false;
    kicked = false;
    corruptedData = false;
    receivedRoundTrip = false;

    ENetEvent event;
    while (enet_host_service(Client::client, &event, 0) > 0){
        if (event.type == ENET_EVENT_TYPE_RECEIVE){
            // Read the data sent by the server
            std::vector<uint8_t> buffer(event.packet->dataLength);
            memcpy(buffer.data(), event.packet->data, event.packet->dataLength);

            PacketType type;
            uint32_t sendID;
            std::vector<uint8_t> data;

            convertData(buffer, type, sendID, data);
            switch (type){
                case PACKET_SHUTDOWN: {
                    enet_packet_destroy(event.packet);

                    shutdown = true;
                    return;
                }
                case PACKET_KICK: {
                    enet_packet_destroy(event.packet);

                    kicked = true;
                    return;
                }
                case PACKET_DATA: {
                    // If the sendID is the same as our ID, that means we sent our packet to ourselves, so set this value to true
                    // and then discard it
                    if (sendID == state.player.playerID){
                        receivedRoundTrip = true;
                        return;
                    }

                    if (data.size() % sizeof(Network_player) != 0) {
                        std::cerr << "Corrupted packet: size mismatch\n";
                        enet_packet_destroy(event.packet);
                        corruptedData = true;
                        return;
                    }

                    // Read player data into our vector
                    int numPlayers = data.size() / sizeof(Network_player);
                    std::vector<Network_player> network_allPlayers(numPlayers);
                    memcpy(network_allPlayers.data(), data.data(), data.size());

                    // Organise player data
                    int numOtherPlayers = numPlayers - 1;
                    if (numOtherPlayers < 0) numOtherPlayers = 0;
                    Network_player network_player;
                    std::vector<Network_player> network_otherPlayers(numOtherPlayers);

                    // Remove our own player from the array so it only contains other players
                    int removeIndex = -1;
                    for (size_t i = 0; i < network_allPlayers.size(); i++){
                        if (network_allPlayers[i].playerID == state.player.playerID){
                            network_player = network_allPlayers[i];
                            removeIndex = i;
                            break;
                        }
                    }

                    if (removeIndex == -1){
                        std::cout << "Player was not sent from the server.\n";
                        enet_packet_destroy(event.packet);

                        corruptedData = true;
                        return;
                    }

                    network_allPlayers.erase(network_allPlayers.begin() + removeIndex);
                    network_otherPlayers = network_allPlayers;

                    auto copyNetworkStruct = [](Player& p, const Network_player& netP){
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

                    // Only copy the health for the player
                    state.player.health = network_player.health;

                    state.otherPlayers.resize(numOtherPlayers);
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

                    enet_packet_destroy(event.packet);
                    break;
                }
            }
        }
    }
}

// Send's one player struct of data to the server, and give the server and ID to let it know which player's data to modify
void Client::sendData(const Player& player, int playerID){
    // Create a struct instead of a class
    Network_player p;
    p.lookDirX = player.lookDir.x;
    p.lookDirY = player.lookDir.y;
    p.playerID = playerID;
    std::strncpy(p.username, player.username.c_str(), sizeof(p.username) - 1);
    p.username[sizeof(p.username) - 1] = '\0';
    p.posX = player.pos.x;
    p.posY = player.pos.y;
    p.isMoving = player.isMoving;
    p.animationStep = player.animationStep;
    p.ping = player.ping;
    p.playerHit = player.playerHit;
    p.dmgDealt = player.dmgDealt;
    p.health = player.health;
    p.gunFrame = player.gunFrame;

    ENetPacket* packet = enet_packet_create(&p, sizeof(p), ENET_PACKET_FLAG_RELIABLE);

    enet_peer_send(Client::server, 0, packet);

    enet_host_flush(Client::client);
}