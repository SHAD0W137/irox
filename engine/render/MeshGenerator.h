#pragma once
#include <vector>
#include <cstdint>
#include "RenderTypes.h"

namespace irox {

struct GeneratedMeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Primitive primitive;
};

class MeshGenerator {
public:
    // Creating XZ-plane with UV tiles
    static GeneratedMeshData createPlane(float width, float depth, float uvTileX = 10.0f, float uvTileY = 10.0f);

    // Creating capsule 3D grid
    static GeneratedMeshData createCapsule(float radius, float halfHeight, uint32_t segments = 16, uint32_t rings = 8);
};

} // namespace irox