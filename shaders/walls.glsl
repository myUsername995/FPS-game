# RETURNS AN IMAGE TO BE RENDERED

layout(local_size_x = 256, local_size_y = 1) in;

// Returns true if ray intersects segment, optionally outputs intersection
bool raySegmentIntersect(vec2 rayOrigin, vec2 rayDir, vec2 segA, vec2 segB, out vec2 intersection){
    vec2 v1 = rayOrigin - segA;
    vec2 v2 = segB - segA;
    vec2 v3 = vec2(-rayDir.y, rayDir.x); // perpendicular to ray

    float denom = dot(v2, v3);
    if (abs(denom) < 1e-6)
        return false; // parallel, no intersection

    float t1 = cross(v2, v1) / denom; // distance along ray
    float t2 = dot(v1, v3) / denom;   // position along segment

    if (t1 >= 0.0 && t2 >= 0.0 && t2 <= 1.0)
    {
        intersection = rayOrigin + t1*rayDir;
        return true;
    }

    return false;
}

float pointLineDistance(vec2 P, vec2 A, vec2 B){
    vec2 AB = B - A;
    vec2 AP = P - A;
    float area = abs(AB.x*AP.y - AB.y*AP.x); // |AB × AP|
    float len = length(AB);                  // |AB|
    return area / len;
}

// Helper: 2D cross product
float cross(vec2 a, vec2 b){
    return a.x*b.y - a.y*b.x;
};

layout(std430, binding = 0) readonly lines {
    vec2 lines[];
};

layout(binding=0) uniform image2D outImage;
layout(binding=1) uniform sampler2D testTex;

uniform uint numLines;       # Number of lines on the map
uniform vec2 ray;            # The ray at the X cordinate of our window
uniform vec2 playerPos;
uniform uint WINDOW_HEIGHT;
uniform uint WINDOW_WIDTH;

# THE ARRAYS WE HAVE
# lines<numLines, (vec2, vec2)>

void main(){
    uint x = gl_GlobalInvocationID.x;

    # Go through all the lines in the world and check for intersections
    float dist = 1e+300;
    uint closestLineIndex = 0;
    for (uint i = 0; i < numLines; i++){
        vec2 intersectionP;

        # We found an intersection, but make sure we choose the closest intersection
        if (raySegmentIntersect(playerPos, ray, lines[i*2], lines[i*2+1], intersectionP)){
            # Don't need to take the sqrt, because the distances are relative
            float newDistance = (playerPos.x - intersectionP.x) * (playerPos.x - intersectionP.x) +
                                (playerPos.y - intersectionP.y) * (playerPos.y - intersectionP.y);

            if (newDistance < dist){
                dist = newDistance;
                closestLineIndex = i;
            }
        }
    }

    float perpWallDist = pointLineDistance(playerPos, lines[closestLineIndex*2], lines[closestLineIndex*2+1]);

    float lineHeight = float(WINDOW_HEIGHT) / perpWallDist;
    lineHeight = clamp(lineHeight, 0.0, float(WINDOW_HEIGHT));

    float startDraw = float(WINDOW_HEIGHT)/2.0 - lineHeight/2.0;
    float endDraw   = float(WINDOW_HEIGHT)/2.0 + lineHeight/2.0;

    // compute horizontal texture coordinate
    vec2 wallStart = lines[closestLineIndex*2];
    vec2 wallEnd   = lines[closestLineIndex*2+1];
    vec2 wallDir   = wallEnd - wallStart;
    float hitX = (diff.x * wallDir.x + diff.y * wallDir.y) / (wallDir.x * wallDir.x + wallDir.y * wallDir.y);
    hitX = clamp(hitX, 0.0, 1.0);
    int texX = int(hitX * texWidth);

    uint yStart = uint(max(startDraw, 0.0));
    uint yEnd   = uint(min(endDraw, float(WINDOW_HEIGHT-1)));

    float step = texHeight / lineHeight;
    float texPos = 0.0;

    for (uint y = yStart; y <= yEnd; y++) {
        int texY = int(texPos);
        texY = clamp(texY, 0, int(texHeight)-1);
        texPos += step;

        vec4 color = texelFetch(testTex, ivec2(texX, texY), 0);
        imageStore(outImage, ivec2(x, y), color);
    }

    return;
}