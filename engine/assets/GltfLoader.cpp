#include "GltfLoader.h"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <iostream>

namespace irox {

bool GltfLoader::loadFromFile(const std::filesystem::path& filePath, GltfRawModel& outModel) {
    if (!std::filesystem::exists(filePath)) {
        std::cerr << "[GltfLoader] File not found: " << filePath << "\n";
        return false;
    }

    auto file = fastgltf::MappedGltfFile::FromPath(filePath);
    if (!file) return false;

    constexpr auto options =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble |
        fastgltf::Options::LoadGLBBuffers |
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::GenerateMeshIndices;

    fastgltf::Parser parser(fastgltf::Extensions::KHR_texture_basisu);
    auto asset = parser.loadGltf(file.get(), filePath.parent_path(), options);
    if (asset.error() != fastgltf::Error::None) return false;

    fastgltf::Asset& gltf = asset.get();

    // Geometry
    for (auto& mesh : gltf.meshes) {
        for (auto& p : mesh.primitives) {
            if (p.type != fastgltf::PrimitiveType::Triangles) continue;

            auto* posIt = p.findAttribute("POSITION");
            if (posIt == p.attributes.end()) continue;

            Primitive primitive {
                .firstIndex = static_cast<uint32_t>(outModel.indices.size()),
                .vertexOffset = static_cast<int32_t>(outModel.vertices.size()),
                .materialIndex = p.materialIndex.has_value() ? static_cast<int32_t>(p.materialIndex.value()) : -1
            };

            size_t vtxStart = outModel.vertices.size();
            auto& posAccessor = gltf.accessors[posIt->accessorIndex];
            outModel.vertices.resize(vtxStart + posAccessor.count);

            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, posAccessor, [&](fastgltf::math::fvec3 pos, size_t idx) {
                outModel.vertices[vtxStart + idx].pos = { pos.x(), pos.y(), pos.z() };
                outModel.vertices[vtxStart + idx].normal = { 0.0f, 1.0f, 0.0f };
                outModel.vertices[vtxStart + idx].uv = { 0.0f, 0.0f };
            });

            auto* normIt = p.findAttribute("NORMAL");
            if (normIt != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, gltf.accessors[normIt->accessorIndex], [&](fastgltf::math::fvec3 n, size_t idx) {
                    outModel.vertices[vtxStart + idx].normal = { n.x(), n.y(), n.z() };
                });
            }

            auto* uvIt = p.findAttribute("TEXCOORD_0");
            if (uvIt != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(gltf, gltf.accessors[uvIt->accessorIndex], [&](fastgltf::math::fvec2 uv, size_t idx) {
                    outModel.vertices[vtxStart + idx].uv = { uv.x(), uv.y() };
                });
            }

            if (p.indicesAccessor.has_value()) {
                auto& idxAccessor = gltf.accessors[p.indicesAccessor.value()];
                primitive.indexCount = static_cast<uint32_t>(idxAccessor.count);
                fastgltf::iterateAccessor<uint32_t>(gltf, idxAccessor, [&](uint32_t i) {
                    outModel.indices.push_back(i);
                });
            }

            outModel.primitives.push_back(primitive);
        }
    }

    // Textures
    for (auto& img : gltf.images) {
        GltfTextureData texData;
        std::visit(fastgltf::visitor {
            [](auto&) {},
            [&](fastgltf::sources::URI& uri) {
                texData.uri = (filePath.parent_path() / std::string(uri.uri.path().begin(), uri.uri.path().end())).string();
            },
            [&](fastgltf::sources::Vector& vec) {
                const auto* ptr = reinterpret_cast<const uint8_t*>(vec.bytes.data());
                texData.bufferData.assign(ptr, ptr + vec.bytes.size());
            },
            [&](fastgltf::sources::ByteView& view) {
                const auto* ptr = reinterpret_cast<const uint8_t*>(view.bytes.data());
                texData.bufferData.assign(ptr, ptr + view.bytes.size());
            },
            [&](fastgltf::sources::BufferView& view) {
                auto& bufferView = gltf.bufferViews[view.bufferViewIndex];
                auto& buffer = gltf.buffers[bufferView.bufferIndex];
                std::visit(fastgltf::visitor {
                    [](auto&) {},
                    [&](const fastgltf::sources::Vector& byteContainer) {
                        const auto* ptr = reinterpret_cast<const uint8_t*>(byteContainer.bytes.data() + bufferView.byteOffset);
                        texData.bufferData.assign(ptr, ptr + bufferView.byteLength);
                    },
                    [&](const fastgltf::sources::ByteView& byteContainer) {
                        const auto* ptr = reinterpret_cast<const uint8_t*>(byteContainer.bytes.data() + bufferView.byteOffset);
                        texData.bufferData.assign(ptr, ptr + bufferView.byteLength);
                    }
                }, buffer.data);
            }
        }, img.data);
        outModel.textures.push_back(std::move(texData));
    }

    // Material matching
    for (auto& mat : gltf.materials) {
        int32_t imgIdx = -1;
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t texIdx = mat.pbrData.baseColorTexture->textureIndex;
            if (texIdx < gltf.textures.size() && gltf.textures[texIdx].imageIndex.has_value()) {
                imgIdx = static_cast<int32_t>(gltf.textures[texIdx].imageIndex.value());
            }
        }
        outModel.materialTextureIndices.push_back(imgIdx);
    }

    std::cout << "[GltfLoader] Loaded " << outModel.vertices.size() << " verts, "
        << outModel.indices.size() << " indices, "
        << outModel.textures.size() << " textures.\n";
    return true;
}

} // namespace irox