/*
What this server will be doing:
ON CLIENT CONNECTING:
-Send map data
-Send player data
-Send clients data (if any)

ON CLIENT INPUT:
-Change our server data
-Send the unchanged data to other clients
-WARNING: No validation is performed for data sent by clients, other than length validation. (cuz hell yeh)

Commands:
print (LOGTYPE) -> prints the logs
shutdown -> sends shutdown packets to clients, and destroy's the server
kick (USERNAME) -> kicks an user.

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
#include <sstream>
#include <conio.h>
#include <algorithm>
#include <thread>

bool run = true;

int maxUsername = 16;

// Where we log server messages
std::ostringstream errors;
std::ostringstream connections;
int numPlayers = 0;

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

// These two arrays are synchronised, but they server different purposes
std::vector<Network_player> players;
std::vector<ENetPeer*> peerData;

struct ClientData {
    uint32_t playerID;
    std::string username;
    std::string ip;
    uint32_t port;
};

struct sprite {
    float x, y;
    int texture;
};

// Append the type information to the front of the packet, length must be specified in bytes
std::vector<uint8_t> convertData(PacketType type, uint32_t sendID, void* ptr, size_t length){
    int segment1Size = sizeof(uint8_t);
    int segment2Size = sizeof(uint32_t);

    std::vector<uint8_t> buffer(segment1Size + segment2Size + length);

    memcpy(buffer.data(), &type, segment1Size);
    memcpy(buffer.data() + segment1Size, &sendID, segment2Size);
    memcpy(buffer.data() + segment1Size + segment2Size, ptr, length);

    return buffer;
}

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
int findPlayerID(int id){
    for (uint32_t i = 0; i < players.size(); i++){
        if (players[i].playerID == id){
            return i;
        }
    }

    // Couldn't find the player
    return -1;
}

// Return the index into the players array, based on an username
int findPlayerUsername(const std::string& username){
    for (uint32_t i = 0; i < players.size(); i++){
        if (players[i].username == username){
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

// Helper functions
// If the current character is a white space, it goes until it finds a letter and returns that index
int skipWhiteSpaces(std::string str, int idx){
    while (idx < str.size() && str[idx] == ' ') idx++;

    return idx;
}

// Keep going until we see a whitespace
int findNextWhiteSpace(std::string str, int idx){
    while (idx < str.size() && str[idx] != ' ') idx++;

    return idx;
}

#include <windows.h>

int getConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

// Str -> the string to center; fillStr -> a single character to fill the empty spaces
void centerText(const std::string& str, const std::string& fillStr){
    int width = getConsoleWidth();
    int padding = (width - str.size()) / 2;
    if (padding < 0) padding = 0;

    std::cout << std::string(padding, fillStr[0]) << str << std::string(padding, fillStr[0]) << "\n";
}

// Parses a word starting at some index "idx" in the string "input", returns the next word, and writes the index of the end of that word 
// into "idx"
std::string parseWord(const std::string& input, int& idx, bool toLower){
    // Parse the first word
    int startWord = skipWhiteSpaces(input, idx);

    // Check if there's any non-space character
    if (startWord >= input.size()) {
        return "";
    }

    int endWord = findNextWhiteSpace(input, startWord);
    int length = endWord - startWord;

    // Make sure length is positive
    if (length <= 0) {
        return "";
    }

    idx = endWord;

    std::string word = input.substr(startWord, length);

    if (toLower) std::transform(word.begin(), word.end(), word.begin(), ::tolower);

    return word;
}

// Parses a command into words
void parseWords(const std::string& input, std::vector<std::string>& outWords, int wordsToParse){
    int idx = 0;
    int numWords = 0;
    while (idx < input.size() && numWords < wordsToParse){
        std::string word = parseWord(input, idx, true);
        if (word == "") return;

        outWords.push_back(word);
    }
}

// Kick a player based on an username
void kickPlayer(ENetHost* server, const std::string& username){
    std::cout << "Kicking player: " << username << "...\n";
    int index = findPlayerUsername(username);
    if (index == -1){
        std::cout << "No player named " << username << ".\n";
        return;
    }

    // Send a kick packet to this player
    std::vector<uint8_t> buffer = convertData(PACKET_KICK, 0, nullptr, 0);
    ENetPeer* playerToKick = peerData[index];
    ClientData* playerData = static_cast<ClientData*>(playerToKick->data);

    // Send the disconnect packet to the client
    ENetPacket* kickPacket = enet_packet_create(buffer.data(), buffer.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(playerToKick, 0, kickPacket);
    enet_host_flush(server);

    numPlayers--;
    connections << "Type: (kicked), ";
    connections << "Username: (" << username << "), ";
    connections << "IP Address: (" << playerData->ip << ":" << playerData->port << "), ";
    connections << "Number of players: (" << numPlayers << ")\n";
    enet_peer_reset(playerToKick);
}

template<typename... Args>
bool matchInput(const std::vector<std::string>& words, Args... args){
    constexpr int numArguements = sizeof...(args);
    // Not enough words
    if (words.size() < numArguements){
        return false;
    }

    std::string arr[numArguements] = {args...};

    for (int i = 0; i < numArguements; i++){
        if (words[i] != arr[i]){
            return false;
        }
    }

    return true;
}

void parseInput(ENetHost* server, const std::string& input){
    // Parse the command
    std::vector<std::string> words;
    parseWords(input, words, 3);

    if (matchInput(words, "shutdown")){
        run = false;
    }
    else if (matchInput(words, "print", "errors")){
        centerText("ERRORS", "=");
        std::cout << errors.str();
    }
    else if (matchInput(words, "print", "connections")){
        centerText("CONNECTIONS", "=");
        std::cout << connections.str();
    }
    else if (matchInput(words, "kick")){
        // See which username was typed in
        int idx = 0;
        parseWord(input, idx, true); // parse the "kick" command but discard the result
        std::string username = parseWord(input, idx, true); // parse the username

        kickPlayer(server, username);
    }
    else {
        std::cout << "Invalid command.\n";
    }

    std::cout << "\n";
}

void readInput(ENetHost* server, std::string& input){
    if (_kbhit()){
        char c = _getch();

        // Backspace
        if (c == '\b'){
            if (!input.empty()) input.pop_back();

            std::cout << "\b \b";
        }
        // Enter
        else if (c == '\r'){
            std::cout << "\n";

            parseInput(server, input);
            input.clear();
        }
        else {
            input += c;

            std::cout << c;
        }
    }
}

void resolveShot(Network_player& p){
    if (p.playerHit != -1){
        // Change the other player's health
        players[p.playerHit].health -= p.dmgDealt;
        if (players[p.playerHit].health <= 0){
            players[p.playerHit].health = 100;
        }
    }
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

    std::string input;
    SDL_Event SDLEvent;
    ENetEvent event;
    while (run){
        readInput(server, input);
        while (enet_host_service(server, &event, 10) > 0){
            readInput(server, input);
            switch (event.type){
                case ENET_EVENT_TYPE_CONNECT: {
                    // Give him an username
                    std::string username = getRandomName(randNames, numNames);

                    if (username.size() > maxUsername){
                        errors << username << ": Username can't be longer than " << maxUsername << " characters.\n";
                        enet_peer_reset(event.peer); break;
                    }

                    // Give him an ID
                    int playerID = findAvailableID(availableIDs);
                    if (playerID == -1){
                        errors << "Too many players, can't assign ID to " << username << ".\n";
                        enet_peer_reset(event.peer); break;
                    }
                    availableIDs[playerID] = false;

                    // Add him to our players array
                    Network_player newPlayer{};
                    newPlayer.playerID = playerID;
                    newPlayer.health = 100;
                    players.push_back(newPlayer);
                    peerData.push_back(event.peer);
                    numPlayers++;

                    char ip[32]; // for IPv4
                    enet_address_get_host_ip(&event.peer->address, ip, sizeof(ip));
                    uint32_t port = event.peer->address.port;

                    connections << "Type: (connect), ";
                    connections << "Username: (" << username << "), ";
                    connections << "IP Address: (" << ip << ":" << port << "), ";
                    connections << "Number of players: (" << numPlayers << ")\n";

                    // Store safe client data
                    event.peer->data = new ClientData{static_cast<uint32_t>(playerID), username, ip, port};

                    struct playerMetaData {
                        uint32_t playerID;
                        char username[32];
                    };

                    playerMetaData packet;
                    packet.playerID = playerID;
                    strncpy(packet.username, username.c_str(), sizeof(packet.username) - 1);
                    packet.username[sizeof(packet.username)-1] = '\0';

                    struct Dimensions {
                        int width, height;
                    };

                    Dimensions dims;
                    dims.width = mapWidth;
                    dims.height = mapHeight;

                    ENetPeer* peer = event.peer;
                    auto sendData = [peer](PacketType type, void* data, size_t length){
                        std::vector<uint8_t> buffer = convertData(type, 0, data, length);
                        ENetPacket* sendPacket = enet_packet_create(buffer.data(), buffer.size(), ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(peer, 0, sendPacket);
                    };

                    sendData(PACKET_PLAYER_METADATA, &packet, sizeof(packet));
                    sendData(PACKET_MAP_METADATA, &dims, sizeof(dims));
                    sendData(PACKET_MAP_DATA, flattenedMap.data(), mapWidth * mapHeight * sizeof(int));
                    sendData(PACKET_SPRITES_METADATA, &numSprites, sizeof(numSprites));
                    sendData(PACKET_SPRITES_DATA, sprites.data(), numSprites * sizeof(sprite));

                    enet_host_flush(server);

                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE: {
                    ClientData* cData = static_cast<ClientData*>(event.peer->data);

                    // Assume the client sent their player information
                    if (event.packet->dataLength != sizeof(Network_player)) break;
                    Network_player curPlayer;
                    memcpy(&curPlayer, event.packet->data, sizeof(Network_player));

                    // The player we have to change isn't necessary the player the event.peer client has, so calculate it
                    int index = findPlayerID(curPlayer.playerID);
                    if (index == -1) break;

                    resolveShot(curPlayer);

                    // Don't change the health
                    int prevHealth = players[index].health;

                    // Store on the server side for easy access
                    players[index] = curPlayer;
                    players[index].health = prevHealth;

                    // Convert our data
                    std::vector<uint8_t> buffer = convertData(PACKET_DATA, cData->playerID, players.data(), players.size() * sizeof(Network_player));

                    // Broadcast to all clients
                    ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), ENET_PACKET_FLAG_RELIABLE);
                    enet_host_broadcast(server, 0, packet);
                    enet_host_flush(server);

                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT: {
                    numPlayers--;
                    ClientData* cData = static_cast<ClientData*>(event.peer->data);
                    connections << "Type: (disconnect), ";
                    connections << "Username: (" << cData->username << "), ";
                    connections << "IP Address: (" << cData->ip << ":" << cData->port << "), ";
                    connections << "Number of players: (" << numPlayers << ")\n";

                    if (event.peer->data) {
                        // Free the ID and username for later usage by other clients
                        int playerID = cData->playerID;
                        availableIDs[playerID] = true;
                        randNames.push_back(cData->username);

                        // Erase the disconnected player from the array
                        int index = findPlayerID(playerID);
                        if (index != -1){
                            players.erase(players.begin() + index);
                            peerData.erase(peerData.begin() + index);
                        }

                        delete static_cast<ClientData*>(event.peer->data);
                        event.peer->data = nullptr;
                    }

                    std::vector<uint8_t> buffer = convertData(PACKET_DATA, cData->playerID, players.data(), players.size() * sizeof(Network_player));

                    // Send the updated players array to every client so that they're immediately updated
                    ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), ENET_PACKET_FLAG_RELIABLE);
                    enet_host_broadcast(server, 0, packet);
                    enet_host_flush(server);
                    
                    break;
                }
            }
        }
    }

    std::cout << "Destroying the server." << std::endl;

    // Broadcast a disconnect message
    std::vector<uint8_t> shutdownMsg = convertData(PACKET_SHUTDOWN, 0, nullptr, 0);

    ENetPacket* packet = enet_packet_create(shutdownMsg.data(), shutdownMsg.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server, 0, packet);
    enet_host_flush(server);

    enet_host_destroy(server);

    return 0;
}