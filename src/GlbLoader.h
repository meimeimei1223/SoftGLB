#ifndef GLB_LOADER_H
#define GLB_LOADER_H

#include "SoftBodyPhysics.h"
#include <vector>
#include <string>

struct GlbResult {
    SoftBodyPhysics::MeshData meshData;

    std::vector<unsigned char> pixels;
    int textureWidth    = 0;
    int textureHeight   = 0;
    int textureChannels = 0;
    bool hasTexture     = false;
};

class GlbLoader {
public:
    static GlbResult load(const std::string& filePath);
};

#endif
