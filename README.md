# irox

**irox** is planned to be a high-performance, cross-platform game engine built with an Overwatch-inspired ECS architecture, deterministic rollback networking, and open-world streaming support.

## Key Architecture
- **Stateless ECS Systems & Pure POD Components:** Modeled after Blizzard's *Overwatch* architecture.
- **Unified Singleplayer / Host Simulation:** Single-world execution eliminating redundant simulation passes for local play and co-op hosting.
- **Cross-Platform:** Native support for Windows PC and Android via Vulkan.