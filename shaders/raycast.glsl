#version 430 core 

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

layout(std430, binding = 0) readonly buffer worldLines {
    vec2 lines[]; // Array of line segment points (pairs of vec2)
};

layout(std430, binding = 1) buffer wallRangesBuffer {
    vec2 wallRanges[];
};

layout(std430, binding = 2) buffer texCoordsBuffer {
    vec4 texCoords[];
};

// The texture index for each line
layout(std430, binding = 3) buffer textureLinesBuffer {
    uint textureLines[];
};

// The output texture for each column
layout(std430, binding = 4) buffer textureColumnsBuffer {
    uint textureColumns[];
};

uniform uint numLines;       // Number of lines on the map
uniform vec2 lookDir;        // Look direction of our player (normalized)
uniform vec2 camera;         // Camera plane vector
uniform vec2 playerPos;      // Position of the player
uniform uint WINDOW_WIDTH;
uniform uint WINDOW_HEIGHT;
uniform float texWidth;
uniform float texHeight;

layout(binding = 0, rgba8) writeonly uniform image2D outImage;  // Output image

void main() {
    uint x = gl_GlobalInvocationID.x;

    if (x >= WINDOW_WIDTH){
        return;
    }

    // Normalized screen x in range [-1, 1]
    float cameraX = (2.0 * x) / float(WINDOW_WIDTH) - 1.0;

    // Ray direction
    vec2 ray = normalize(lookDir + camera * cameraX);

    // Find closest intersection
    float closestDistSq = 1e30;
    uint closestLineIndex = uint(-1);
    vec2 closestIntersection = vec2(0.0);

    for (uint i = 0; i < numLines; i++) {
        vec2 intersectionP;
        vec2 segA = lines[i * 2];
        vec2 segB = lines[i * 2 + 1];

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
    if (closestLineIndex == uint(-1)) {
        wallRanges[x] = vec2(0, 0);         // (0, 0) indicates no collision
        textureColumns[x] = 0;
        return;
    }

    // True distance to wall (optionally fisheye-corrected)
    float dist = sqrt(closestDistSq);
    float perpWallDist = dist * dot(ray, normalize(lookDir)); // ensure lookDir is normalized

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

    wallRanges[x] = vec2(yStart, yEnd);

    uint curTexture = textureLines[closestLineIndex];
    textureColumns[x] = curTexture;

    // Compute texture X coordinate (0..1) along the wall
    vec2 wallStart = lines[closestLineIndex * 2];
    vec2 wallEnd   = lines[closestLineIndex * 2 + 1];
    vec2 wallDir   = wallEnd - wallStart;
    vec2 hitDiff   = closestIntersection - wallStart;

    // Fraction along the segment using projection
    float texX = dot(hitDiff, wallDir) / dot(wallDir, wallDir);
    texX = clamp(texX, 0.0, 1.0) * float(texWidth);

    // Vertical texture mapping
    float stepY = float(texHeight) / lineHeight;
    float texY  = (float(yStart) - startDraw) * stepY;

    texCoords[x] = vec4(texX, 0, texY, stepY);
}