#pragma once
#include "ecs/ECS.h"
#include "game/client/components/CameraComponent.h"
#include "engine/input/InputState.h"

namespace irox {
    class CameraControllerSystem {
    public:
        static void update(ecs::World& world, float aspectRatio) {
            auto& input = world.getSingleton<InputState>();
            auto& cameraPool = world.getPool<CameraComponent>();

            for (Entity entity : cameraPool.entities()) {
                auto& cam = cameraPool.get(entity);

                // Rotate Camera
                if (input.isLeftMouseDown) {
                    float sensitivity = 0.2f;
                    glm::quat rotYaw = glm::angleAxis(glm::radians(input.mouseDelta.x * sensitivity), glm::vec3(0.0f, 1.0f, 0.0f));
                    glm::quat rotPitch = glm::angleAxis(glm::radians(input.mouseDelta.y * sensitivity), glm::vec3(1.0f, 0.0f, 0.0f));
                    cam.orientation = glm::normalize(rotPitch * rotYaw * cam.orientation);
                }

                // Pan Camera
                if (input.isRightMouseDown) {
                    glm::quat invOrientation = glm::conjugate(cam.orientation);
                    glm::vec3 camRight = invOrientation * glm::vec3(1.0f, 0.0f, 0.0f);
                    glm::vec3 camUp = invOrientation * glm::vec3(0.0f, 1.0f, 0.0f);

                    float panSpeed = 0.002f * cam.distance;
                    cam.target += (-camRight * input.mouseDelta.x + camUp * input.mouseDelta.y) * panSpeed;
                }

                // Zoom Camera
                cam.distance = std::clamp(cam.distance - input.mouseWheel * 0.5f, 0.5f, 50.0f);

                // Recompute Matrices
                cam.viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cam.distance)) *
                    glm::mat4_cast(cam.orientation) *
                    glm::translate(glm::mat4(1.0f), -cam.target);

                cam.projMatrix = glm::perspective(glm::radians(cam.fov), aspectRatio, cam.nearPlane, cam.farPlane);
                cam.projMatrix[1][1] *= -1.0f; // Invert for Vulkan clip space
            }
        }
    };
}