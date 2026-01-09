#pragma once
#include "GPU.hpp"
#include "gameState.hpp"

int initShaders(std::vector<Line>& lines, int window_width, int window_height);
void resizeShaders(int window_width, int window_height);
void renderWallz(Vector lookDir, Vector camera, SDL_FPoint playerPosition);