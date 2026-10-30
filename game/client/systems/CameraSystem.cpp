#include "CameraSystem.h"
#include "game/client/components/CameraComponent.h"
#include "engine/input/InputState.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace irox {

void CameraSystem::update(ecs::World& world, float aspectRatio) {
    auto& input = world.getSingleton<InputState>();
    auto& camPool = world.getPool<CameraComponent>();

    for (ecs::Entity entity : camPool.entities()) {
        auto& cam = camPool.get(entity);

        // Camera rotation without roll
        if (input.isLeftMouseDown) {
            float sens = 0.005f; // Mouse sensitivity in radian
            cam.yaw += input.mouseDelta.x * sens;
            cam.pitch += input.mouseDelta.y * sens;

            // Limited pitch
            constexpr float maxPitch = glm::radians(90.0f);
            constexpr float minPitch = glm::radians(-90.0f);
            cam.pitch = std::clamp(cam.pitch, minPitch, maxPitch);
        }

        // Mosewheel Zoom
        cam.distance = std::clamp(cam.distance - input.mouseWheel * 0.5f, 1.5f, 30.0f);

        // Orientation from Yaw and Pitch
        glm::quat qYaw = glm::angleAxis(cam.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qPitch = glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
        cam.orientation = qPitch * qYaw;

        // ViewMatrix
        cam.viewMatrix =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cam.distance)) *
            glm::mat4_cast(cam.orientation) *
            glm::translate(glm::mat4(1.0f), -cam.target);

        cam.projMatrix = glm::perspective(glm::radians(cam.fov), aspectRatio, cam.nearPlane, cam.farPlane);
        cam.projMatrix[1][1] *= -1.0f; // Inverting Y for Vulkan
    }
}

} // namespace irox