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
print (LOGTYPE) -> prints the logs (connections, errors, tab)
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
#include "time.hpp"

bool run = true;

int maxUsername = 16;

enum connectionType {
    CONNECT,
    DISCONNECT,
    KICK
};

enum errorType {
    TOO_MANY_PLAYERS,
    USERNAME_LIMIT_EXCEEDED
};

struct Connection {
    connectionType type;
    std::string username;
    std::string ip;
    int port;
    int userID;
    double time; // Measure the time of disconnect / kick / connect in MS
};

struct Error {
    errorType type;
    std::string description;
};

// Where we log server messages
std::vector<Connection> connectionLogs; // Stores every connect / disconnect / kick
std::vector<Error> errorLogs;
std::vector<Connection> tabLogs; // Stores the current players

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
    uint8_t hasFired;
    uint8_t isMoving;
    uint8_t isHit;

    uint8_t animationStep;
    uint8_t deathFrame;
    uint8_t gunFrame;

    // Send which player we hit, then the server sends back their health
    int8_t playerHit;
    int8_t health;
    int8_t numKills;
};
static_assert(sizeof(Network_player) == 72);

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

std::string connectionName(connectionType type){
    switch (type){
        case CONNECT: return "CONNECT";
        case DISCONNECT: return "DISCONNECT";
        case KICK: return "KICK";
    }

    return "";
}

std::string errorName(errorType type){
    switch (type){
        case TOO_MANY_PLAYERS: return "TOO_MANY_PLAYERS";
        case USERNAME_LIMIT_EXCEEDED: return "USERNAME_LIMIT_EXCEEDED";
    }

    return "";
}

// Convert to a HOUR\MINUTE\SECOND format
std::string convertDate(double ms){
    std::ostringstream date;

    // Keep as doubles to preserve accuracy
    int totalSeconds = ms / 1000;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    // Convert to ints and format the string
    date << hours << ":" << minutes << ":" << seconds;

    return date.str();
}

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

template<typename... Entry>
void drawEntry(Entry... entry){
    constexpr size_t numArgs = sizeof...(entry);
    std::array<std::string, numArgs> entries = {entry...};

    // Draw each entry at a predetermined location and trust that the user doesn't input more than like 5 entries
    int gap = 24;

    for (int i = 0; i < numArgs; i++){
        int numChars = entries[i].size();
        int skip = gap - numChars;
        if (skip < 0) skip = 1;

        std::cout << entries[i] << std::string(skip, ' ');
    }
    std::cout << "\n";
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

    Connection log;
    log.type = KICK;
    log.username = username;
    log.ip = playerData->ip;
    log.ip += ":" + std::to_string(playerData->port);
    log.userID = playerData->playerID;
    connectionLogs.push_back(log);

    enet_peer_reset(playerToKick);
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
        // Header
        drawEntry("Type", "Description");
        // Content
        for (int i = 0; i < errorLogs.size(); i++){
            drawEntry(errorName(errorLogs[i].type), errorLogs[i].description);
        }
    }
    else if (matchInput(words, "print", "connections")){
        centerText("CONNECTIONS", "=");
        // Header
        drawEntry("Type", "Username", "IP", "ID", "Time");
        for (int i = 0; i < connectionLogs.size(); i++){
            std::ostringstream ip;
            ip << connectionLogs[i].ip << ":" << connectionLogs[i].port;

            drawEntry(connectionName(connectionLogs[i].type), connectionLogs[i].username, ip.str(), 
                      std::to_string(connectionLogs[i].userID), convertDate(connectionLogs[i].time));
        }
    }
    else if (matchInput(words, "print", "tab")){
        centerText("TAB", "=");
        drawEntry("Username", "IP", "ID", "Join time");
        for (int i = 0; i < tabLogs.size(); i++){
            std::ostringstream ip;
            ip << tabLogs[i].ip << ":" << tabLogs[i].port;

            drawEntry(tabLogs[i].username, ip.str(), std::to_string(tabLogs[i].userID), convertDate(connectionLogs[i].time));
        }
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
    // Check if we hit any player
    if (p.playerHit != -1){
        // Only reset the health on the server side, because it would be slower to resolve it clientside because I send all the 
        // players to the clients instead of just one
        players[p.playerHit].health -= p.dmgDealt;

        // The health can't be less than 0
        if (players[p.playerHit].health < 0) players[p.playerHit].health = 0;
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

    // Measure the elapsed time
    Clk serverTime;
    serverTime.begin();

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
                        Error log;
                        log.type = USERNAME_LIMIT_EXCEEDED;
                        log.description = username + " is longer than the allowed limit of " + std::to_string(maxUsername);
                        errorLogs.push_back(log);

                        enet_peer_reset(event.peer); break;
                    }

                    // Give him an ID
                    int playerID = findAvailableID(availableIDs);
                    if (playerID == -1){
                        Error log;
                        log.type = TOO_MANY_PLAYERS;
                        log.description = "Can't assign ID to " + username;
                        errorLogs.push_back(log);

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

                    serverTime.end();

                    Connection log;
                    log.type = CONNECT;
                    log.username = username;
                    log.ip = ip;
                    log.port = port;
                    log.userID = playerID;
                    log.time = serverTime.getTime();
                    connectionLogs.push_back(log);
                    tabLogs.push_back(log);

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

                    // This variable must be set by the server, so don't change it
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

                    serverTime.end();

                    // Add a new connection log
                    Connection log;
                    log.type = DISCONNECT;
                    log.username = cData->username;
                    log.ip = cData->ip;
                    log.port = cData->port;
                    log.userID = cData->playerID;
                    log.time = serverTime.getTime();
                    connectionLogs.push_back(log);

                    // Remove the player from our tab
                    int removeIndex = -1;
                    for (int i = 0; i < tabLogs.size(); i++){
                        if (tabLogs[i].userID == static_cast<int>(cData->playerID)){
                            removeIndex = i;
                            break;
                        }
                    }
                    if (removeIndex != -1) tabLogs.erase(tabLogs.begin() + removeIndex);

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