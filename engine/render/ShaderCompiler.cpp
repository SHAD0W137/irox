#include "ShaderCompiler.h"
#include <iostream>
#include <cassert>

#if defined(IROX_PLATFORM_ANDROID)
#include <SDL3/SDL.h>

VkShaderModule ShaderCompiler::compileShader(
    VkDevice device,
    const std::string& filePath,
    const std::string& entryPointName,
    SlangStage stage
) {
    std::string spvFile = (stage == SLANG_STAGE_VERTEX)
        ? "shaders/cube.vert.spv"
        : "shaders/cube.frag.spv";

    size_t dataSize = 0;
    void* data = SDL_LoadFile(spvFile.c_str(), &dataSize);

    if (!data) {
        SDL_Log("FATAL ERROR: Failed to load shader from APK: %s", spvFile.c_str());
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = dataSize,
        .pCode = reinterpret_cast<const uint32_t*>(data)
    };

    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &createInfo, nullptr, &module);

    SDL_free(data);
    SDL_Log("SUCCESS: Loaded SPV from APK: %s (%zu bytes)", spvFile.c_str(), dataSize);
    return module;
}

ShaderCompiler::ShaderCompiler() {}
ShaderCompiler::~ShaderCompiler() {}

#else

#ifndef IROX_ROOT_DIR
#define IROX_ROOT_DIR "."
#endif

ShaderCompiler::ShaderCompiler() {
    slang::createGlobalSession(m_GlobalSession.writeRef());

    std::string shadersDir = std::string(IROX_ROOT_DIR) + "/assets/shaders";
    const char* searchPaths[] = { shadersDir.c_str() };

    slang::SessionDesc sessionDesc = {};
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = 1;

    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = m_GlobalSession->findProfile("spirv_1_4");

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    m_GlobalSession->createSession(sessionDesc, m_Session.writeRef());
}

ShaderCompiler::~ShaderCompiler() {
    m_Session.setNull();
    m_GlobalSession.setNull();
}

VkShaderModule ShaderCompiler::compileShader(
    VkDevice device,
    const std::string& filePath,
    const std::string& entryPointName,
    SlangStage stage
) {
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = m_Session->loadModule(filePath.c_str(), diagnostics.writeRef());
    if (!module) return VK_NULL_HANDLE;

    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    module->findEntryPointByName(entryPointName.c_str(), entryPoint.writeRef());
    if (!entryPoint) return VK_NULL_HANDLE;

    slang::IComponentType* components[] = { module, entryPoint.get() };
    Slang::ComPtr<slang::IComponentType> program;
    m_Session->createCompositeComponentType(components, 2, program.writeRef(), diagnostics.writeRef());

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    program->link(linkedProgram.writeRef(), diagnostics.writeRef());

    Slang::ComPtr<slang::IBlob> spirvBlob;
    linkedProgram->getEntryPointCode(0, 0, spirvBlob.writeRef(), diagnostics.writeRef());
    if (!spirvBlob) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvBlob->getBufferSize(),
        .pCode = reinterpret_cast<const uint32_t*>(spirvBlob->getBufferPointer())
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    return shaderModule;
}

#endif