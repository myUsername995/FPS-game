# =========================
# Compiler & flags
# =========================
CXX      = g++
CXXFLAGS = -O3 -g

# =========================
# Common include paths
# =========================
COMMON_INCLUDES = \
	-I"C:/Files/myLibraries/include" \
	-I"C:/Files/myLibraries/libraries/header"

RAYCAST_INCLUDES = \
	-I"./libraries/header" \
	-I"C:/Files/myLibraries"

# =========================
# Common libraries
# =========================
COMMON_LIBS = \
	-L"C:/Files/myLibraries/lib" \
	-lSDL3 -lSDL3_ttf -lSDL3_image \

# =========================
# ENet (used by raycast + server)
# =========================
ENET_INCLUDES = -I./enet-1.3.18/include
ENET_LIBS     = -L"./enet-1.3.18" -lenet64 -lws2_32 -lwinmm

# =========================
# Shared source
# =========================
TIME_SRC = C:/Files/myLibraries/libraries/src/time.cpp
NETWORKING_SRC = ./libraries/src/networking.cpp
RENDERING_SRC = ./libraries/src/render.cpp C:/Files/myLibraries/libraries/src/GPU.cpp \
				C:/Files/myLibraries/glad/glad.c

# =========================
# Targets
# =========================
.PHONY: all editor raycast server clean

all: editor raycast server

# -------- editor --------
editor: editor.exe

editor.exe: editor.cpp $(TIME_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ \
	$(COMMON_INCLUDES) \
	$(COMMON_LIBS)

# -------- raycast --------
raycast: raycast.exe

raycast.exe: raycast.cpp $(TIME_SRC) $(NETWORKING_SRC) $(RENDERING_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ \
	$(COMMON_INCLUDES) \
	$(ENET_INCLUDES) \
	$(RAYCAST_INCLUDES) \
	$(COMMON_LIBS) \
	$(ENET_LIBS)

# -------- server --------
server: server.exe

server.exe: server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ \
	$(COMMON_INCLUDES) \
	$(ENET_INCLUDES) \
	$(COMMON_LIBS) \
	$(ENET_LIBS)