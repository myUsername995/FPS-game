#pragma once
#include "GPU.hpp"
#include "gameState.hpp"

int initShaders(SDL_Window* window, const std::vector<Line>& inLines, const std::vector<Texture>& wallTex);
void resizeShaders(SDL_Window* window);
void renderWallz(SDL_Window* window, Vector lookDir, Vector camera, SDL_FPoint playerPosition);