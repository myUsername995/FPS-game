# =========================
# Compiler & flags
# =========================
CXX      = g++
CXXFLAGS = -O3 -g

# =========================
# Common include paths
# =========================
COMMON_INCLUDES = \
	-I"C:/Files/.vscode/myLibraries/include" \
	-I"C:/Files/.vscode/myLibraries/libraries/header"

# =========================
# Common libraries
# =========================
COMMON_LIBS = \
	-L"C:/Files/.vscode/myLibraries/lib" \
	-lSDL3 -lSDL3_ttf -lSDL3_image \

# =========================
# ENet (used by raycast + server)
# =========================
ENET_INCLUDES = -I./enet-1.3.18/include
ENET_LIBS     = -L"./enet-1.3.18" -lenet64 -lws2_32 -lwinmm

# =========================
# Shared source
# =========================
TIME_SRC = C:/Files/.vscode/myLibraries/libraries/src/time.cpp

# =========================
# Targets
# =========================
.PHONY: all editor raycast server clean

all: editor raycast server

# -------- editor --------
editor: editor.exe

mapEditor.exe: editor.cpp $(TIME_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ \
	$(COMMON_INCLUDES) \
	$(COMMON_LIBS)

# -------- raycast --------
raycast: raycast.exe

raycast.exe: raycast.cpp $(TIME_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ \
	$(COMMON_INCLUDES) \
	$(ENET_INCLUDES) \
	$(COMMON_LIBS) \
	$(ENET_LIBS)

# -------- server --------
server: server.exe

raycastServer.exe: server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ \
	$(COMMON_INCLUDES) \
	$(ENET_INCLUDES) \
	$(COMMON_LIBS) \
	$(ENET_LIBS)

# -------- clean --------
clean:
	del /Q *.exe 2>nul || exit 0
