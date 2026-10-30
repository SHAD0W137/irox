#include "KtxTextureLoader.h"
#include <ktx.h>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace irox {

static bool uploadRawPixels(
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    irox::GPUMemoryManager& memoryManager,
    VkCommandBuffer transferCmd,
    VkSemaphore timelineSemaphore,
    uint64_t& timelineValue,
    irox::AllocatedImage& outImage
) {
    VkDeviceSize dataSize = width * height * 4;

    VkImageCreateInfo imgInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    outImage = memoryManager.createImage(imgInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    memoryManager.sendStagingImageData(
        transferCmd,
        timelineSemaphore,
        ++timelineValue,
        outImage.image,
        { width, height, 1 },
        pixels,
        dataSize
    );

    return true;
}

bool KtxTextureLoader::loadFromFile(
    const std::string& filePath,
    irox::GPUMemoryManager& memoryManager,
    VkCommandBuffer transferCmd,
    VkSemaphore timelineSemaphore,
    uint64_t& timelineValue,
    irox::AllocatedImage& outImage
) {
    // Trying to open as KTX
    ktxTexture* kTexture = nullptr;
    KTX_error_code res = ktxTexture_CreateFromNamedFile(
        filePath.c_str(),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &kTexture
    );

    if (res == KTX_SUCCESS && kTexture) {
        if (ktxTexture_NeedsTranscoding(kTexture)) {
            auto* kTex2 = reinterpret_cast<ktxTexture2*>(kTexture);
            ktxTexture2_TranscodeBasis(kTex2, KTX_TTF_RGBA32, 0);
        }

        bool ok = uploadRawPixels(
            ktxTexture_GetData(kTexture),
            kTexture->baseWidth,
            kTexture->baseHeight,
            memoryManager,
            transferCmd,
            timelineSemaphore,
            timelineValue,
            outImage
        );
        ktxTexture_Destroy(kTexture);
        return ok;
    }

    // stb_image fallback if non-ktx
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(filePath.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (pixels) {
        bool ok = uploadRawPixels(pixels, w, h, memoryManager, transferCmd, timelineSemaphore, timelineValue, outImage);
        stbi_image_free(pixels);
        return ok;
    }

    std::cerr << "[TextureLoader] Failed to load image: " << filePath << "\n";
    return false;
}

bool KtxTextureLoader::loadFromMemory(
    const uint8_t* data,
    size_t size,
    irox::GPUMemoryManager& memoryManager,
    VkCommandBuffer transferCmd,
    VkSemaphore timelineSemaphore,
    uint64_t& timelineValue,
    irox::AllocatedImage& outImage
) {
    // As KTX
    ktxTexture* kTexture = nullptr;
    KTX_error_code res = ktxTexture_CreateFromMemory(
        data,
        size,
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &kTexture
    );

    if (res == KTX_SUCCESS && kTexture) {
        if (ktxTexture_NeedsTranscoding(kTexture)) {
            auto* kTex2 = reinterpret_cast<ktxTexture2*>(kTexture);
            ktxTexture2_TranscodeBasis(kTex2, KTX_TTF_RGBA32, 0);
        }

        bool ok = uploadRawPixels(
            ktxTexture_GetData(kTexture),
            kTexture->baseWidth,
            kTexture->baseHeight,
            memoryManager,
            transferCmd,
            timelineSemaphore,
            timelineValue,
            outImage
        );
        ktxTexture_Destroy(kTexture);
        return ok;
    }

    // As PNG/JPEG
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, STBI_rgb_alpha);
    if (pixels) {
        bool ok = uploadRawPixels(pixels, w, h, memoryManager, transferCmd, timelineSemaphore, timelineValue, outImage);
        stbi_image_free(pixels);
        return ok;
    }

    return false;
}

irox::AllocatedImage KtxTextureLoader::createFallbackTexture(
    irox::GPUMemoryManager& memoryManager,
    VkCommandBuffer transferCmd,
    VkSemaphore timelineSemaphore,
    uint64_t& timelineValue
) {
    const uint32_t W = 16, H = 16;
    std::vector<uint32_t> pixels(W * H);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            bool even = ((x / 2) + (y / 2)) % 2 == 0;
            pixels[y * W + x] = even ? 0xFF202020 : 0xFFEE10EE;
        }
    }
    irox::AllocatedImage outImage;
    uploadRawPixels(reinterpret_cast<uint8_t*>(pixels.data()), W, H, memoryManager, transferCmd, timelineSemaphore, timelineValue, outImage);
    return outImage;
}

} // namespace irox