#pragma once

namespace irox {

struct CapsuleColliderComponent {
    float radius = 0.4f;      
    float halfHeight = 0.5f;

    float getTotalHeight() const { return 2.0f * (halfHeight + radius); }
};

} // namespace irox