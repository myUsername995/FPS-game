#pragma once
#include "GPU.hpp"
#include "gameState.hpp"

int initShaders(SDL_Window* window, const std::vector<Line>& inLines, const std::vector<Texture>& wallTex, std::array<Texture, 8> playerTexs, 
                std::array<std::array<Texture, 8>, 4> playerRunTexs, std::vector<Texture> spriteTexs);
void resizeShaders(SDL_Window* window);
void renderMap(SDL_Window* window, const gameState& state);