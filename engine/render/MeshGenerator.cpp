#include "MeshGenerator.h"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace irox {

GeneratedMeshData MeshGenerator::createPlane(float width, float depth, float uvTileX, float uvTileY) {
    GeneratedMeshData mesh;
    float hx = width * 0.5f;
    float hz = depth * 0.5f;

    mesh.vertices = {
        {.pos = { -hx, 0.0f, -hz }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 0.0f, 0.0f } },
        {.pos = {  hx, 0.0f, -hz }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { uvTileX, 0.0f } },
        {.pos = {  hx, 0.0f,  hz }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { uvTileX, uvTileY } },
        {.pos = { -hx, 0.0f,  hz }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 0.0f, uvTileY } }
    };

    mesh.indices = { 0, 2, 1, 2, 0, 3 };

    mesh.primitive = {
        .firstIndex = 0,
        .indexCount = static_cast<uint32_t>(mesh.indices.size()),
        .vertexOffset = 0,
        .materialIndex = 0
    };

    return mesh;
}

GeneratedMeshData MeshGenerator::createCapsule(float radius, float halfHeight, uint32_t segments, uint32_t rings) {
    GeneratedMeshData mesh;

    const float hemisphereArc = glm::half_pi<float>() * radius;
    const float cylinderHeight = halfHeight * 2.0f;
    const float totalSurfaceLength = hemisphereArc * 2.0f + cylinderHeight;

    const float uTile = 4.0f;
    const float vTile = 4.0f;

    uint32_t cylinderRings = rings;

    for (uint32_t i = 0; i <= rings; ++i) {
        float phi = glm::half_pi<float>() * (1.0f - static_cast<float>(i) / rings);
        float y = halfHeight + radius * std::sin(phi);
        float r = radius * std::cos(phi);

        float currentArc = (glm::half_pi<float>() - phi) * radius;
        float v = (currentArc / totalSurfaceLength) * vTile;

        for (uint32_t j = 0; j <= segments; ++j) {
            float theta = glm::two_pi<float>() * (static_cast<float>(j) / segments);
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(glm::vec3(x, y - halfHeight, z));
            glm::vec2 uv((static_cast<float>(j) / segments) * uTile, v);

            mesh.vertices.push_back({ .pos = pos, .normal = normal, .uv = uv });
        }
    }

    for (uint32_t k = 1; k < cylinderRings; ++k) {
        float t = static_cast<float>(k) / cylinderRings;
        float y = halfHeight - t * cylinderHeight;

        float currentArc = hemisphereArc + t * cylinderHeight;
        float v = (currentArc / totalSurfaceLength) * vTile;

        for (uint32_t j = 0; j <= segments; ++j) {
            float theta = glm::two_pi<float>() * (static_cast<float>(j) / segments);
            float x = radius * std::cos(theta);
            float z = radius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal(std::cos(theta), 0.0f, std::sin(theta));
            glm::vec2 uv((static_cast<float>(j) / segments) * uTile, v);

            mesh.vertices.push_back({ .pos = pos, .normal = normal, .uv = uv });
        }
    }

    for (uint32_t i = 0; i <= rings; ++i) {
        float phi = -glm::half_pi<float>() * (static_cast<float>(i) / rings);
        float y = -halfHeight + radius * std::sin(phi);
        float r = radius * std::cos(phi);

        float currentArc = hemisphereArc + cylinderHeight + (-phi) * radius;
        float v = (currentArc / totalSurfaceLength) * vTile;

        for (uint32_t j = 0; j <= segments; ++j) {
            float theta = glm::two_pi<float>() * (static_cast<float>(j) / segments);
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(glm::vec3(x, y + halfHeight, z));
            glm::vec2 uv((static_cast<float>(j) / segments) * uTile, v);

            mesh.vertices.push_back({ .pos = pos, .normal = normal, .uv = uv });
        }
    }

    uint32_t totalRings = (rings + 1) + (cylinderRings - 1) + (rings + 1);
    for (uint32_t r = 0; r < totalRings - 1; ++r) {
        for (uint32_t s = 0; s < segments; ++s) {
            uint32_t i0 = r * (segments + 1) + s;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (r + 1) * (segments + 1) + s;
            uint32_t i3 = i2 + 1;

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);

            mesh.indices.push_back(i1);
            mesh.indices.push_back(i3);
            mesh.indices.push_back(i2);
        }
    }

    mesh.primitive = {
        .firstIndex = 0,
        .indexCount = static_cast<uint32_t>(mesh.indices.size()),
        .vertexOffset = 0,
        .materialIndex = 0
    };

    return mesh;
}

} // namespace irox