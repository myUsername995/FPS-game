#version 430 

layout(local_size_x = 256, local_size_y = 1) in;

// Helper: 2D cross product
float cross2(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

// Ray–segment intersection
bool raySegmentIntersect(vec2 rayOrigin, vec2 rayDir, vec2 segA, vec2 segB, out vec2 intersection) {
    vec2 v1 = rayOrigin - segA;
    vec2 v2 = segB - segA;
    vec2 v3 = vec2(-rayDir.y, rayDir.x); // Perpendicular to ray

    float denom = dot(v2, v3);
    if (abs(denom) < 1e-6) {
        return false; // Parallel, no intersection
    }

    float t1 = cross2(v2, v1) / denom; // Distance along the ray
    float t2 = dot(v1, v3) / denom;    // Position along the segment

    if (t1 >= 0.0 && t2 >= 0.0 && t2 <= 1.0) {
        intersection = rayOrigin + t1 * rayDir;
        return true;
    }

    return false;
}

struct columnData {
    vec4 texCoord;
    ivec2 wallRange;
    int texture;
};

struct lineData {
    vec2 p1;
    vec2 p2;
    int texture;
};

struct atlasUV {
    float x;
    float y;

    float width;
    float height;
};

layout(std430, binding = 0) buffer lineBuf {
    lineData lines[];
};

layout(std430, binding = 1) buffer columnBuf {
    columnData columns[];
};

layout(std430, binding = 2) buffer depthBufferBuf {
    float depthBuffer[];
};

layout(std430, binding = 3) buffer atlasUVBuf {
    atlasUV atlasUVs[];
};

uniform uint numLines;       // Number of lines on the map
uniform vec2 lookDir;        // Look direction of our player (normalized)
uniform vec2 camera;         // Camera plane vector
uniform vec2 playerPos;      // Position of the player
uniform int WINDOW_WIDTH;
uniform int WINDOW_HEIGHT;

void main() {
    int x = int(gl_GlobalInvocationID.x);

    if (x >= WINDOW_WIDTH){
        return;
    }

    // Normalized screen x in range [-1, 1]
    float cameraX = (2.0 * x) / float(WINDOW_WIDTH) - 1.0;

    // Ray direction
    vec2 ray = normalize(lookDir + camera * cameraX);

    // Find closest intersection
    float closestDistSq = 1e30;
    int closestLineIndex = -1;
    vec2 closestIntersection = vec2(0.0);

    for (int i = 0; i < numLines; i++) {
        lineData line = lines[i];

        vec2 intersectionP;
        vec2 segA = line.p1;
        vec2 segB = line.p2;

        if (raySegmentIntersect(playerPos, ray, segA, segB, intersectionP)) {
            vec2 diff = intersectionP - playerPos;
            float newDistSq = dot(diff, diff);

            if (newDistSq < closestDistSq) {
                closestDistSq = newDistSq;
                closestLineIndex = i;
                closestIntersection = intersectionP;
            }
        }
    }

    // If no hit, just clear column (e.g., black)
    if (closestLineIndex == -1) {
        columns[x].wallRange = ivec2(0, 0);         // (0, 0) indicates no collision
        columns[x].texture = -1;
        return;
    }
    
    lineData curLine = lines[closestLineIndex];

    // True distance to wall
    float dist = sqrt(closestDistSq);
    float perpWallDist = dist * dot(ray, normalize(lookDir)); // ensure lookDir is normalized

    // Store the distance in a depth buffer
    depthBuffer[x] = dist;

    // Avoid division by zero
    if (perpWallDist < 1e-4) {
        perpWallDist = 1e-4;
    }

    // Wall height on screen
    float lineHeight = float(WINDOW_HEIGHT) / perpWallDist;

    float startDraw = float(WINDOW_HEIGHT) / 2.0 - lineHeight / 2.0;
    float endDraw   = float(WINDOW_HEIGHT) / 2.0 + lineHeight / 2.0;

    uint yStart = uint(max(startDraw, 0.0));
    uint yEnd   = uint(min(endDraw, float(WINDOW_HEIGHT - 1)));

    columns[x].wallRange = ivec2(yStart, yEnd);

    int curTexture = curLine.texture;
    columns[x].texture = curTexture;

    // Compute texture X coordinate (0..1) along the wall
    vec2 wallStart = curLine.p1;
    vec2 wallEnd   = curLine.p2;
    vec2 wallDir   = wallEnd - wallStart;
    vec2 hitDiff   = closestIntersection - wallStart;

    float texWidth = atlasUVs[curTexture].width;
    float texHeight = atlasUVs[curTexture].height;

    // Fraction along the segment using projection
    float texX = dot(hitDiff, wallDir) / dot(wallDir, wallDir);
    texX = clamp(texX, 0.0, 1.0) * float(texWidth);

    // Vertical texture mapping
    float stepY = float(texHeight) / lineHeight;
    float texY  = (float(yStart) - startDraw) * stepY;

    columns[x].texCoord = vec4(texX, 0, texY, stepY);
}