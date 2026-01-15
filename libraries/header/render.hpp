#pragma once
#include "GPU.hpp"
#include "gameState.hpp"

int initShaders(SDL_Window* window, const gameState& state);
void resizeShaders(SDL_Window* window);
void renderMap(SDL_Window* window, const gameState& state);