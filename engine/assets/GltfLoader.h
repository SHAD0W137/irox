#include <filesystem>
#include <vector>
#include "engine/render/RenderTypes.h"

namespace irox {

struct GltfTextureData {
    std::string uri;
    std::vector<uint8_t> bufferData;
};

struct GltfRawModel {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Primitive> primitives;
    std::vector<GltfTextureData> textures;
    std::vector<int32_t> materialTextureIndices;
};

class GltfLoader {
public:
    static bool loadFromFile(const std::filesystem::path& filePath, GltfRawModel& outModel);
};

} // namespace irox