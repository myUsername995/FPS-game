#pragma once
#include <enet/enet.h>
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cmath>
#include <atomic>
#include "gameState.hpp"

class Client {
    private:
    ENetHost* client;
    ENetPeer* server;

    public:
    Client(){};

    bool connectToServer(gameState& state, std::string serverIP);
    void disconnectFromServer();
    void receiveData(gameState& state, std::atomic<bool>& run);
    void sendData(gameState& state);
};

class Server {
    public:
    void receiveData(ENetHost* server);
    void sendData(ENetPeer* client);
};