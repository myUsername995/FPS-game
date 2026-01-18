#pragma once
#include <enet/enet.h>
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <cmath>
#include <atomic>
#include <cstdint>
#include "gameState.hpp"

class Client {
    private:
    ENetHost* client;
    ENetPeer* server;

    public:
    Client(){};

    bool connectToServer(gameState& state, std::string serverIP);
    void disconnectFromServer();
    bool receiveData(gameState& state);
    void sendData(gameState& state);
};