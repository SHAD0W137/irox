#include "game/client/systems/CameraSystem.h"
#include "game/client/components/CameraComponent.h"
#include "engine/input/InputState.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace irox {

void CameraSystem::update(ecs::World& world, float aspectRatio) {
    auto* input = world.GetSingleton<InputState>();
    if (!input) return;

    auto view = world.View<CameraComponent>();

    for (ecs::Entity entity : view) {
        auto& cam = view.get<CameraComponent>(entity);

        // Camera rotation without roll
        if (input->isLeftMouseDown) {
            constexpr float sens = 0.005f; // Mouse sensitivity in radians
            cam.yaw += input->mouseDelta.x * sens;
            cam.pitch += input->mouseDelta.y * sens;

            // Pitch clamping
            constexpr float maxPitch = glm::radians(89.0f);
            constexpr float minPitch = glm::radians(-89.0f);
            cam.pitch = std::clamp(cam.pitch, minPitch, maxPitch); 
        }

        // Mousewheel zoom clamping
        cam.distance = std::clamp(cam.distance - input->mouseWheel * 0.5f, 1.5f, 30.0f);

        // Orientation from Yaw and Pitch
        glm::quat qYaw = glm::angleAxis(cam.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qPitch = glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
        cam.orientation = qPitch * qYaw;

        // View and projection matrices
        cam.viewMatrix =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cam.distance)) *
            glm::mat4_cast(cam.orientation) *
            glm::translate(glm::mat4(1.0f), -cam.target);

        cam.projMatrix = glm::perspective(glm::radians(cam.fov), aspectRatio, cam.nearPlane, cam.farPlane);
        cam.projMatrix[1][1] *= -1.0f; // Invert Y for Vulkan clip space
    }
}

} // namespace irox