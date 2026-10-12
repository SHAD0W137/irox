cmake_minimum_required(VERSION 3.22)
project(irox LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Platform Detection
# -----------------------------------------------------------------------------
if(WIN32)
    message(STATUS "[irox] Configuring for Windows PC")
    add_compile_definitions(IROX_PLATFORM_WINDOWS)
elseif(ANDROID)
    message(STATUS "[irox] Configuring for Android (ABI: ${ANDROID_ABI})")
    add_compile_definitions(IROX_PLATFORM_ANDROID)
else()
    message(STATUS "[irox] Configuring for Linux / Headless")
    add_compile_definitions(IROX_PLATFORM_LINUX)
endif()

# Global output paths
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# -----------------------------------------------------------------------------
# Third-Party Dependencies (Vulkan, Math, etc.)
# -----------------------------------------------------------------------------
if(NOT ANDROID AND NOT IROX_HEADLESS)
    find_package(Vulkan REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Module: engine
# -----------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES 
    "engine/core/*.cpp"
    "engine/ecs/*.cpp"
    "engine/spatial/*.cpp"
    "engine/network/*.cpp"
    "engine/input/*.cpp"
)

# Platform-specific engine sources
if(WIN32)
    file(GLOB_RECURSE PLATFORM_SOURCES "engine/platform/windows/*.cpp")
elseif(ANDROID)
    file(GLOB_RECURSE PLATFORM_SOURCES "engine/platform/android/*.cpp")
endif()
list(APPEND ENGINE_SOURCES ${PLATFORM_SOURCES})

# Graphics sources (omit for headless servers)
if(NOT IROX_HEADLESS)
    file(GLOB_RECURSE RENDER_SOURCES "engine/render/*.cpp")
    list(APPEND ENGINE_SOURCES ${RENDER_SOURCES})
endif()

add_library(irox_engine STATIC ${ENGINE_SOURCES})
target_include_directories(irox_engine PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

if(WIN32 AND NOT IROX_HEADLESS)
    target_link_libraries(irox_engine PUBLIC Vulkan::Vulkan)
elseif(ANDROID)
    target_link_libraries(irox_engine PUBLIC android log vulkan)
endif()

# -----------------------------------------------------------------------------
# Module: game
# -----------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SHARED_SOURCES "game/shared/*.cpp")
file(GLOB_RECURSE GAME_CLIENT_SOURCES "game/client/*.cpp")
file(GLOB_RECURSE GAME_SERVER_SOURCES "game/server/*.cpp")

# Game Shared (All targets)
add_library(irox_game_shared STATIC ${GAME_SHARED_SOURCES})
target_include_directories(irox_game_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/game/shared)
target_link_libraries(irox_game_shared PUBLIC irox_engine)

# Game Client (Client + Standalone)
add_library(irox_game_client STATIC ${GAME_CLIENT_SOURCES})
target_include_directories(irox_game_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/game/client)
target_link_libraries(irox_game_client PUBLIC irox_game_shared)

# Game Server (Server + Standalone)
add_library(irox_game_server STATIC ${GAME_SERVER_SOURCES})
target_include_directories(irox_game_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/game/server)
target_link_libraries(irox_game_server PUBLIC irox_game_shared)

# -----------------------------------------------------------------------------
# Apps / Executables
# -----------------------------------------------------------------------------
if(WIN32)
    # 1. Standalone Singleplayer / Host App
    add_executable(irox_standalone "apps/standalone/main.cpp")
    target_link_libraries(irox_standalone PRIVATE 
        irox_engine 
        irox_game_shared 
        irox_game_client 
        irox_game_server
    )

    # 2. Dedicated Server App (Headless)
    add_executable(irox_server "apps/dedicated_server/main.cpp")
    target_link_libraries(irox_server PRIVATE 
        irox_engine 
        irox_game_shared 
        irox_game_server
    )

    # 3. Remote Client App
    add_executable(irox_client "apps/remote_client/main.cpp")
    target_link_libraries(irox_client PRIVATE 
        irox_engine 
        irox_game_shared 
        irox_game_client
    )

elseif(ANDROID)
    # Android uses a native shared library (.so) packaged into the APK
    add_library(irox_android SHARED 
        "apps/android/app/src/main/cpp/android_main.cpp"
    )
    target_link_libraries(irox_android PRIVATE 
        irox_engine 
        irox_game_shared 
        irox_game_client 
        irox_game_server
        android
        log
        native_app_glue
    )
endif()