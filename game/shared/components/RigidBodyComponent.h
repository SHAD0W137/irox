#pragma once
#include <box3d/box3d.h>

namespace irox {

enum class BodyType : uint8_t {
    Static = b3_staticBody,
    Kinematic = b3_kinematicBody,
    Dynamic = b3_dynamicBody
};

struct RigidBodyComponent {
    b3BodyId bodyId = b3_nullBodyId;
    b3ShapeId shapeId = b3_nullShapeId;
    BodyType type = BodyType::Dynamic;
};

} // namespace irox