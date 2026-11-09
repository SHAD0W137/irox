#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <bitset>
#include <memory>
#include <cassert>
#include <type_traits>
#include <limits>
#include <utility>

namespace irox::ecs {

// Entity Architecture 20-bit Index / 12-bit Generation
using Entity = uint32_t;
constexpr Entity NULL_ENTITY = 0xFFFFFFFF;

namespace detail {
constexpr uint32_t ENTITY_INDEX_BITS = 20;
constexpr uint32_t ENTITY_INDEX_MASK = (1u << ENTITY_INDEX_BITS) - 1; // 0x000FFFFF (1,048,575 entities)
constexpr uint32_t ENTITY_GEN_BITS = 12;
constexpr uint32_t ENTITY_GEN_MASK = (1u << ENTITY_GEN_BITS) - 1;   // 0x00000FFF (4,095 generations)
}

[[nodiscard]] constexpr inline uint32_t getEntityIndex(Entity e) noexcept {
    return e & detail::ENTITY_INDEX_MASK;
}

[[nodiscard]] constexpr inline uint32_t getEntityGeneration(Entity e) noexcept {
    return (e >> detail::ENTITY_INDEX_BITS) & detail::ENTITY_GEN_MASK;
}

[[nodiscard]] constexpr inline Entity makeEntity(uint32_t index, uint32_t generation) noexcept {
    return (index & detail::ENTITY_INDEX_MASK) |
        ((generation & detail::ENTITY_GEN_MASK) << detail::ENTITY_INDEX_BITS);
}

// Sequential Type Identifiers & Bitset Masks 
using ComponentID = uint32_t;
constexpr size_t MAX_COMPONENTS = 64; // Used as instant bitmaks. Next MAX should be 128 (if there are more that 64 components in the game)
using ComponentMask = std::bitset<MAX_COMPONENTS>;

inline ComponentID getNextComponentID() noexcept {
    static ComponentID counter = 0;
    assert(counter < MAX_COMPONENTS && "Exceeded MAX_COMPONENTS capacity (64)!");
    return counter++;
}

template<typename T>
inline ComponentID getComponentID() noexcept {
    static const ComponentID id = getNextComponentID();
    return id;
}

// Singletons are designed for standalone singleton systems. Do not contribute towards MAX_COMPOENNTS count
using SingletonID = uint32_t;
inline SingletonID getNextSingletonID() noexcept {
    static SingletonID counter = 0;
    return counter++;
}

template<typename T>
inline SingletonID getSingletonID() noexcept {
    static const SingletonID id = getNextSingletonID();
    return id;
}

// Paged Sparse Set Component Storage
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity entity) = 0;
    [[nodiscard]] virtual bool has(Entity entity) const noexcept = 0;
    [[nodiscard]] virtual size_t size() const noexcept = 0;
};

template<typename T>
class ComponentPool final : public IComponentPool {
    // Enforce зure POD Components
    static_assert(std::is_trivially_copyable_v<T>,
        "Component violation: T must be Trivially Copyable (no std::vector, std::string, or custom destructors)!");

private:
    static constexpr size_t PAGE_SIZE = 1024; // 1024 * 4B = 4 KB / Matches virtual memory page
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

    std::vector<T> m_denseData;
    std::vector<Entity> m_denseToEntity;
    std::vector<std::unique_ptr<std::array<uint32_t, PAGE_SIZE>>> m_sparsePages;

    [[nodiscard]] inline uint32_t getSparse(uint32_t entityIndex) const noexcept {
        const size_t page = entityIndex / PAGE_SIZE;
        const size_t offset = entityIndex % PAGE_SIZE;
        if (page >= m_sparsePages.size() || !m_sparsePages[page]) {
            return INVALID_INDEX;
        }
        return (*m_sparsePages[page])[offset];
    }

    inline void setSparse(uint32_t entityIndex, uint32_t denseIndex) {
        const size_t page = entityIndex / PAGE_SIZE;
        const size_t offset = entityIndex % PAGE_SIZE;
        if (page >= m_sparsePages.size()) {
            m_sparsePages.resize(page + 1);
        }
        if (!m_sparsePages[page]) {
            m_sparsePages[page] = std::make_unique<std::array<uint32_t, PAGE_SIZE>>();
            m_sparsePages[page]->fill(INVALID_INDEX);
        }
        (*m_sparsePages[page])[offset] = denseIndex;
    }

public:
    void reserve(size_t capacity) {
        m_denseData.reserve(capacity);
        m_denseToEntity.reserve(capacity);
    }

    T& add(Entity entity, const T& component) {
        assert(!has(entity) && "Entity already has this component!");

#ifndef NDEBUG
        // Trap any reallocation occurring after initialization
        if (m_denseData.size() >= m_denseData.capacity() && m_denseData.capacity() > 0) {
            assert(false && "CRITICAL: Component budget exceeded! Pool triggered dynamic heap reallocation.");
        }
#endif
        const uint32_t denseIndex = static_cast<uint32_t>(m_denseData.size());
        const uint32_t entityIndex = getEntityIndex(entity);

        setSparse(entityIndex, denseIndex);
        m_denseToEntity.push_back(entity);
        m_denseData.push_back(component);

        return m_denseData.back();
    }

    [[nodiscard]] T& get(Entity entity) noexcept {
        assert(has(entity) && "Entity does not possess this component!");
        const uint32_t denseIdx = getSparse(getEntityIndex(entity));
        return m_denseData[denseIdx];
    }

    [[nodiscard]] const T& get(Entity entity) const noexcept {
        assert(has(entity) && "Entity does not possess this component!");
        const uint32_t denseIdx = getSparse(getEntityIndex(entity));
        return m_denseData[denseIdx];
    }

    [[nodiscard]] bool has(Entity entity) const noexcept override {
        const uint32_t denseIdx = getSparse(getEntityIndex(entity));
        return (denseIdx != INVALID_INDEX) &&
            (denseIdx < m_denseToEntity.size()) &&
            (m_denseToEntity[denseIdx] == entity);
    }

    void remove(Entity entity) override {
        if (!has(entity)) return;

        const uint32_t entityIndex = getEntityIndex(entity);
        const uint32_t denseIdx = getSparse(entityIndex);
        const uint32_t lastDenseIdx = static_cast<uint32_t>(m_denseData.size() - 1);

        // Swap-and-Pop: Preserve packed dense memory contiguous layout
        if (denseIdx != lastDenseIdx) {
            m_denseData[denseIdx] = m_denseData[lastDenseIdx];
            const Entity lastEntity = m_denseToEntity[lastDenseIdx];
            m_denseToEntity[denseIdx] = lastEntity;
            setSparse(getEntityIndex(lastEntity), denseIdx);
        }

        m_denseData.pop_back();
        m_denseToEntity.pop_back();
        setSparse(entityIndex, INVALID_INDEX);
    }

    [[nodiscard]] size_t size() const noexcept override { return m_denseData.size(); }
    [[nodiscard]] const std::vector<T>& data() const noexcept { return m_denseData; }
    [[nodiscard]] std::vector<T>& data() noexcept { return m_denseData; }
    [[nodiscard]] const std::vector<Entity>& entities() const noexcept { return m_denseToEntity; }
};

// Forward declaration of World for View
class World;

// Lead-Pool Multi-Component View Query
template<typename... Comps>
class View {
private:
    World& m_world;
    ComponentMask m_mask;
    const IComponentPool* m_leadPool { nullptr };
    const std::vector<Entity>* m_leadEntities { nullptr };

public:
    explicit View(World& world);

    template<typename T>
    [[nodiscard]] T& get(Entity e);

    template<typename T>
    [[nodiscard]] const T& get(Entity e) const;

    class Iterator {
    private:
        World& m_world;
        const ComponentMask& m_mask;
        const std::vector<Entity>* m_entities;
        size_t m_index;

        void advanceToNextValid() noexcept;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entity;
        using difference_type = std::ptrdiff_t;
        using pointer = const Entity*;
        using reference = const Entity&;

        Iterator(World& world, const ComponentMask& mask, const std::vector<Entity>* entities, size_t index) noexcept
            : m_world(world), m_mask(mask), m_entities(entities), m_index(index) {
            if (m_entities) {
                advanceToNextValid();
            }
        }

        Entity operator*() const noexcept { return (*m_entities)[m_index]; }
        Iterator& operator++() noexcept {
            ++m_index;
            advanceToNextValid();
            return *this;
        }

        bool operator==(const Iterator& other) const noexcept { return m_index == other.m_index; }
        bool operator!=(const Iterator& other) const noexcept { return m_index != other.m_index; }
    };

    [[nodiscard]] Iterator begin() const noexcept {
        if (!m_leadEntities || m_leadEntities->empty()) return end();
        return Iterator(m_world, m_mask, m_leadEntities, 0);
    }

    [[nodiscard]] Iterator end() const noexcept {
        const size_t endIdx = m_leadEntities ? m_leadEntities->size() : 0;
        return Iterator(m_world, m_mask, m_leadEntities, endIdx);
    }
};

// ECS World: Central Registry, Recycler & Singleton Container
class World {
private:
    std::vector<uint16_t> m_generations;
    std::vector<uint32_t> m_freeList;
    std::vector<ComponentMask> m_masks;

    // Flat array of pools indexed directly by ComponentID
    std::vector<std::unique_ptr<IComponentPool>> m_pools;

    // Flat array of singletons indexed directly by SingletonID
    std::vector<std::shared_ptr<void>> m_singletons;

public:
    World() = default;
    ~World() = default;

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    // Entity Lifecycle Management
    [[nodiscard]] Entity createEntity() {
        uint32_t index = 0;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();
        } else {
            index = static_cast<uint32_t>(m_generations.size());
            assert(index <= detail::ENTITY_INDEX_MASK && "ECS entity capacity overflow!");
            m_generations.push_back(1); // Generation starts at 1
            m_masks.emplace_back();
        }

        m_masks[index].reset();
        return makeEntity(index, m_generations[index]);
    }

    [[nodiscard]] bool isAlive(Entity e) const noexcept {
        if (e == NULL_ENTITY) return false;
        const uint32_t index = getEntityIndex(e);
        const uint32_t gen = getEntityGeneration(e);
        return index < m_generations.size() && m_generations[index] == gen;
    }

    void destroyEntity(Entity e) {
        if (!isAlive(e)) return;

        const uint32_t index = getEntityIndex(e);
        const ComponentMask& mask = m_masks[index];

        // Only dispatch removals to pools the entity actually owns
        for (size_t compId = 0; compId < m_pools.size(); ++compId) {
            if (mask.test(compId) && m_pools[compId]) {
                m_pools[compId]->remove(e);
            }
        }
        m_masks[index].reset();

        // Increment generation to invalidate stale dangling references
        uint16_t nextGen = (m_generations[index] + 1) & detail::ENTITY_GEN_MASK;
        if (nextGen == 0) nextGen = 1; // Preserve 0 as invalid
        m_generations[index] = nextGen;

        m_freeList.push_back(index);
    }

    // Component Pool Access
    template<typename T>
    [[nodiscard]] ComponentPool<T>& getPool() {
        const ComponentID id = getComponentID<T>();
        if (id >= m_pools.size()) {
            m_pools.resize(id + 1);
        }
        if (!m_pools[id]) {
            m_pools[id] = std::make_unique<ComponentPool<T>>();
        }
        return *static_cast<ComponentPool<T>*>(m_pools[id].get());
    }

    template<typename T>
    [[nodiscard]] const ComponentPool<T>& getPool() const {
        const ComponentID id = getComponentID<T>();
        assert(id < m_pools.size() && m_pools[id] && "Component pool not allocated!");
        return *static_cast<const ComponentPool<T>*>(m_pools[id].get());
    }

    // Component Operations
    template<typename T>
    T& addComponent(Entity e, const T& component) {
        assert(isAlive(e) && "Cannot add component to a dead or invalid entity!");
        const ComponentID id = getComponentID<T>();
        m_masks[getEntityIndex(e)].set(id);
        return getPool<T>().add(e, component);
    }

    template<typename T>
    [[nodiscard]] T& getComponent(Entity e) {
        assert(isAlive(e) && "Cannot get component from a dead or invalid entity!");
        return getPool<T>().get(e);
    }

    template<typename T>
    [[nodiscard]] const T& getComponent(Entity e) const {
        assert(isAlive(e) && "Cannot get component from a dead or invalid entity!");
        return getPool<T>().get(e);
    }

    template<typename T>
    [[nodiscard]] bool hasComponent(Entity e) const noexcept {
        if (!isAlive(e)) return false;
        const ComponentID id = getComponentID<T>();
        return m_masks[getEntityIndex(e)].test(id);
    }

    template<typename T>
    void removeComponent(Entity e) {
        if (!isAlive(e)) return;
        const ComponentID id = getComponentID<T>();
        m_masks[getEntityIndex(e)].reset(id);
        getPool<T>().remove(e);
    }

    template<typename... Comps>
    [[nodiscard]] bool hasComponents(Entity e) const noexcept {
        if (!isAlive(e)) return false;
        ComponentMask expected;
        (expected.set(getComponentID<Comps>()), ...);
        return (m_masks[getEntityIndex(e)] & expected) == expected;
    }

    [[nodiscard]] const ComponentMask& getMask(Entity e) const noexcept {
        assert(isAlive(e) && "Entity is dead or invalid!");
        return m_masks[getEntityIndex(e)];
    }

    // Multi-Component Queries
    template<typename... Comps>
    [[nodiscard]] View<Comps...> View() {
        return irox::ecs::View<Comps...>(*this);
    }

    // Singletons: Global services registered within the ECS World
    template<typename T, typename... Args>
    T& setSingleton(Args&&... args) {
        const SingletonID id = getSingletonID<T>();
        if (id >= m_singletons.size()) {
            m_singletons.resize(id + 1);
        }
        auto instance = std::make_shared<T>(std::forward<Args>(args)...);
        m_singletons[id] = instance;
        return *instance;
    }

    template<typename T>
    [[nodiscard]] bool hasSingleton() const noexcept {
        const SingletonID id = getSingletonID<T>();
        return id < m_singletons.size() && m_singletons[id] != nullptr;
    }

    template<typename T>
    [[nodiscard]] T* GetSingleton() noexcept {
        const SingletonID id = getSingletonID<T>();
        if (id >= m_singletons.size() || !m_singletons[id]) {
            return nullptr;
        }
        return static_cast<T*>(m_singletons[id].get());
    }

    template<typename T>
    [[nodiscard]] T& getSingleton() {
        T* s = GetSingleton<T>();
        assert(s != nullptr && "Requested singleton has not been registered in the World!");
        return *s;
    }

    // Enforing memory limitations
    template<typename T>
    void setComponentBudget(size_t maxExpectedInstances) {
        getPool<T>().reserve(maxExpectedInstances);
    }

    void reserveEntities(size_t maxExpectedEntities) {
        m_generations.reserve(maxExpectedEntities);
        m_masks.reserve(maxExpectedEntities);
        m_freeList.reserve(maxExpectedEntities / 2);
    }
};

// View Template Implementations
template<typename... Comps>
View<Comps...>::View(World& world) : m_world(world) {
    (m_mask.set(getComponentID<Comps>()), ...);

    size_t minSize = std::numeric_limits<size_t>::max();
    auto inspectLeadPool = [&]<typename T>() {
        auto& pool = m_world.template getPool<T>();
        if (pool.size() < minSize) {
            minSize = pool.size();
            m_leadPool = &pool;
            m_leadEntities = &pool.entities();
        }
    };

    (inspectLeadPool.template operator() < Comps > (), ...);
}

template<typename... Comps>
template<typename T>
T& View<Comps...>::get(Entity e) {
    return m_world.template getComponent<T>(e);
}

template<typename... Comps>
template<typename T>
const T& View<Comps...>::get(Entity e) const {
    return m_world.template getComponent<T>(e);
}

template<typename... Comps>
void View<Comps...>::Iterator::advanceToNextValid() noexcept {
    while (m_index < m_entities->size()) {
        const Entity e = (*m_entities)[m_index];
        if (m_world.isAlive(e) && (m_world.getMask(e) & m_mask) == m_mask) {
            return;
        }
        ++m_index;
    }
}

} // namespace irox::ecs