#pragma once
#include <vulkan/vulkan.h>
#include <string>

#if !defined(IROX_PLATFORM_ANDROID)
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>
#else
// Android uses precompiled shaders
enum SlangStage {
    SLANG_STAGE_VERTEX = 0,
    SLANG_STAGE_FRAGMENT = 1
};
#endif

class ShaderCompiler {
#if !defined(IROX_PLATFORM_ANDROID)
    Slang::ComPtr<slang::IGlobalSession> m_GlobalSession;
    Slang::ComPtr<slang::ISession> m_Session;
#endif

public:
    ShaderCompiler();
    ~ShaderCompiler();

    VkShaderModule compileShader(
        VkDevice device,
        const std::string& filePath,
        const std::string& entryPointName,
        SlangStage stage
    );
};