cmake_minimum_required(VERSION 3.25)
project(irox LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(IROX_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_compile_definitions(
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if (MSVC)
    add_compile_options(/fp:precise)
    # Ensure standard FMA generation without arbitrary fused contractions
    add_compile_options(/fp:except-)
else()
    # Clang on Windows and Android NDK
    add_compile_options(-fno-fast-math -ffp-contract=off)
endif()

# Box3D Physics Engine 
include(FetchContent)
set(FETCHCONTENT_QUIET TRUE)

# No demos or samples
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    box3d
    URL https://github.com/erincatto/box3d/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(box3d)

# Platform-specific dependencies
if(ANDROID)
    find_package(Vulkan REQUIRED)
    find_package(SDL3 REQUIRED CONFIG)

    add_compile_definitions(IROX_PLATFORM_ANDROID)
    add_compile_options(-Wno-nullability-completeness)

    include(FetchContent)
    set(FETCHCONTENT_QUIET TRUE)

    # GLM / VMA
    FetchContent_Declare(glm URL https://github.com/g-truc/glm/releases/download/1.0.3/glm-1.0.3.zip)
    FetchContent_Declare(vma URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.1.0.zip)

    # Valve GNS headers
    FetchContent_Declare(
        gns_headers
        URL https://github.com/ValveSoftware/GameNetworkingSockets/archive/refs/tags/v1.4.1.zip
    )
    FetchContent_GetProperties(gns_headers)
    if(NOT gns_headers_POPULATED)
        FetchContent_Populate(gns_headers)
    endif()

    FetchContent_MakeAvailable(glm vma)

    # ARM64 .so Valve GNS library
    add_library(GameNetworkingSockets SHARED IMPORTED)
    set_target_properties(GameNetworkingSockets PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/apps/android/app/src/main/jniLibs/arm64-v8a/libGameNetworkingSockets.so"
    )

else()
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}/lib/cmake/SDL3")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}/lib/cmake")
    endif()

    find_package(Vulkan REQUIRED)
    find_package(SDL3 REQUIRED)

    find_library(SLANG_LIB NAMES slang PATHS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib" REQUIRED)
    add_library(Slang::Slang UNKNOWN IMPORTED)
    set_target_properties(Slang::Slang PROPERTIES
        IMPORTED_LOCATION "${SLANG_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/Include"
    )

    find_package(glm CONFIG REQUIRED)
    find_package(fastgltf CONFIG REQUIRED)
    find_package(KTX CONFIG REQUIRED)
    find_package(Stb REQUIRED)
    find_package(GameNetworkingSockets CONFIG REQUIRED)

    add_library(stb_image INTERFACE)
    target_include_directories(stb_image INTERFACE ${Stb_INCLUDE_DIR})
endif()

# Engine source
set(ENGINE_SOURCES
    engine/render/GPUMemoryManager.cpp
    engine/render/ShaderCompiler.cpp
    engine/render/VulkanContext.cpp
    engine/render/MeshGenerator.cpp
    engine/network/NetworkManager.cpp   
    game/client/systems/CameraSystem.cpp
    game/client/systems/RenderSystem.cpp
    game/shared/systems/CharacterMovementSystem.cpp
    game/shared/systems/PhysicsSystem.cpp  
    game/scenes/SandboxScene.cpp
)

# Model loaders only for PC
if(NOT ANDROID)
    list(APPEND ENGINE_SOURCES
        engine/assets/KtxTextureLoader.cpp
        engine/assets/GltfLoader.cpp
    )
endif()

add_library(irox_engine STATIC ${ENGINE_SOURCES})

target_include_directories(irox_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/game
    ${Vulkan_INCLUDE_DIRS}
)

if(ANDROID)
    target_include_directories(irox_engine PUBLIC
        ${glm_SOURCE_DIR}
        ${vma_SOURCE_DIR}/include
        ${gns_headers_SOURCE_DIR}/include
    )
    target_link_libraries(irox_engine PUBLIC
        Vulkan::Vulkan
        SDL3::SDL3
        GameNetworkingSockets
        android
        log
        box3d::box3d
    )
else()
    target_include_directories(irox_engine PUBLIC "$ENV{VULKAN_SDK}/Include")
    target_link_libraries(irox_engine PUBLIC
        Vulkan::Vulkan
        SDL3::SDL3
        Slang::Slang
        glm::glm
        fastgltf::fastgltf
        KTX::ktx
        stb_image
        GameNetworkingSockets::GameNetworkingSockets
        box3d::box3d
    )
    if(WIN32)
        target_link_libraries(irox_engine PUBLIC ws2_32 crypt32)
    endif()
endif()

# Targets
if(ANDROID)
    add_library(main SHARED apps/standalone/main.cpp)
    target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(main PRIVATE irox_engine)
else()
    add_executable(irox apps/standalone/main.cpp)
    target_include_directories(irox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(irox PRIVATE irox_engine)

    add_executable(irox_server apps/dedicated_server/main.cpp)
    target_include_directories(irox_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(irox_server PRIVATE irox_engine)
endif()