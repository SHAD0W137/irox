#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <cassert>

namespace irox::ecs {

    using Entity = uint32_t;
    constexpr Entity NULL_ENTITY = 0xFFFFFFFF;

    // Type-erased base pool for Sparse Sets
    class IComponentPool {
    public:
        virtual ~IComponentPool() = default;
        virtual void remove(Entity entity) = 0;
        virtual bool has(Entity entity) const = 0;
    };

    template<typename T>
    class ComponentPool : public IComponentPool {
    private:
        std::vector<T> m_denseData;
        std::vector<Entity> m_denseToEntity;
        std::unordered_map<Entity, size_t> m_entityToDense;

    public:
        T& add(Entity entity, const T& component) {
            assert(!has(entity) && "Entity already has component!");
            size_t index = m_denseData.size();
            m_entityToDense[entity] = index;
            m_denseToEntity.push_back(entity);
            m_denseData.push_back(component);
            return m_denseData.back();
        }

        T& get(Entity entity) {
            assert(has(entity) && "Entity does not have component!");
            return m_denseData[m_entityToDense[entity]];
        }

        bool has(Entity entity) const override {
            return m_entityToDense.contains(entity);
        }

        void remove(Entity entity) override {
            if (!has(entity)) return;
            size_t index = m_entityToDense[entity];
            size_t lastIndex = m_denseData.size() - 1;

            if (index != lastIndex) {
                m_denseData[index] = std::move(m_denseData[lastIndex]);
                Entity lastEntity = m_denseToEntity[lastIndex];
                m_denseToEntity[index] = lastEntity;
                m_entityToDense[lastEntity] = index;
            }

            m_denseData.pop_back();
            m_denseToEntity.pop_back();
            m_entityToDense.erase(entity);
        }

        const std::vector<T>& data() const { return m_denseData; }
        const std::vector<Entity>& entities() const { return m_denseToEntity; }
    };

    class World {
    private:
        Entity m_nextEntity = 0;
        std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> m_pools;
        std::unordered_map<std::type_index, std::shared_ptr<void>> m_singletons;

    public:
        Entity createEntity() {
            return m_nextEntity++;
        }

        void destroyEntity(Entity e) {
            for (auto& [type, pool] : m_pools) {
                pool->remove(e);
            }
        }

        template<typename T>
        ComponentPool<T>& getPool() {
            auto type = std::type_index(typeid(T));
            if (!m_pools.contains(type)) {
                m_pools[type] = std::make_unique<ComponentPool<T>>();
            }
            return *static_cast<ComponentPool<T>*>(m_pools[type].get());
        }

        template<typename T>
        T& addComponent(Entity e, const T& c) {
            return getPool<T>().add(e, c);
        }

        template<typename T>
        T& getComponent(Entity e) {
            return getPool<T>().get(e);
        }

        template<typename T>
        bool hasComponent(Entity e) {
            return getPool<T>().has(e);
        }

        template<typename T>
        bool hasSingleton() const {
            return m_singletons.contains(std::type_index(typeid(T)));
        }

        // Overwatch-style Singletons (Global services stored inside the ECS)
        template<typename T, typename... Args>
        T& setSingleton(Args&&... args) {
            auto type = std::type_index(typeid(T));
            auto instance = std::make_shared<T>(std::forward<Args>(args)...);
            m_singletons[type] = instance;
            return *instance;
        }

        template<typename T>
        T& getSingleton() {
            auto type = std::type_index(typeid(T));
            assert(m_singletons.contains(type) && "Singleton not registered!");
            return *static_cast<T*>(m_singletons[type].get());
        }
    };

} // namespace irox::ecs