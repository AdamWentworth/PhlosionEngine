// src/engine/core/ecs/World.h

#pragma once
#include "engine/core/ecs/Entity.h"
#include "engine/core/ecs/ComponentStorage.h"
#include "engine/core/ecs/View.h"
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <tuple>
#include <utility>

namespace engine { struct CoreServices; }

namespace engine::ecs {

// World owns entity lifetimes and component storages.
// Stage 2.3: Structural-change policy is enforced for World::add/remove/destroy while iterating:
//  - Structural changes are deferred until the end of the outermost iteration.
//  - Deferred ops are generation-checked so they can't accidentally affect recycled entity ids.
class World {
public:
    explicit World(engine::CoreServices* services = nullptr);

    Entity create();
    void destroy(Entity e);
    bool alive(Entity e) const;

    engine::CoreServices* services() { return services_; }
    const engine::CoreServices* services() const { return services_; }

    // Preferred gameplay-facing API (Stage 2.3):
    // - add/remove are safe during iteration (deferred until outermost iteration completes)
    // - has/get observe the currently committed world state (no "peeking" at deferred ops)
    template <class T, class... Args>
    void add(Entity e, Args&&... args) {
        if (iterationDepth_ > 0) {
            // Defer until it is safe to mutate stores (end of outermost iteration).
            // Capture full entity (id+gen) so we can generation-check at apply time.
            auto tup = std::make_tuple(std::forward<Args>(args)...);
            defer([this, e, tup = std::move(tup)]() mutable {
                if (!alive(e)) return;
                std::apply([&](auto&&... a) {
                    components<T>().emplace(e, std::forward<decltype(a)>(a)...);
                }, tup);
            });
            return;
        }
        if (!alive(e)) return;
        components<T>().emplace(e, std::forward<Args>(args)...);
    }

    template <class T>
    void remove(Entity e) {
        if (iterationDepth_ > 0) {
            // Capture full entity so we don't touch recycled ids.
            defer([this, e]() {
                if (!alive(e)) return;
                if (auto* s = try_components<T>()) { s->remove(e); }
            });
            return;
        }
        if (!alive(e)) return;
        if (auto* s = try_components<T>()) { s->remove(e); }
    }

    template <class T>
    bool has(Entity e) const {
        if (!alive(e)) return false;
        const auto* s = try_components<T>();
        return s ? s->has(e) : false;
    }

    template <class T>
    T* get(Entity e) {
        if (!alive(e)) return nullptr;
        auto* s = try_components<T>();
        return s ? s->get(e) : nullptr;
    }

    template <class T>
    const T* get(Entity e) const {
        if (!alive(e)) return nullptr;
        const auto* s = try_components<T>();
        return s ? s->get(e) : nullptr;
    }

    // Transitional API: direct store access (allowed for now, but should not spread).
    template <class T>
    ComponentStorage<T>& components() {
        const std::type_index key(typeid(T));
        auto it = stores_.find(key);
        if (it == stores_.end()) {
            auto ptr = std::make_unique<StoreHolder<T>>();
            auto* raw = &ptr->store;
            stores_.emplace(key, std::move(ptr));
            return *raw;
        }
        return static_cast<StoreHolder<T>*>(it->second.get())->store;
    }

    // Convenience iteration: each component instance of T.
    // Passes (Entity, T&) to fn.
    template <class T, class Fn>
    void each(Fn&& fn) {
        IterationGuard guard(*this);
        auto& store = components<T>();
        for (auto& kv : store.raw()) {
            const std::uint32_t id = kv.first;
            Entity e{ id, id < generations_.size() ? generations_[id] : std::uint8_t{0} };
            if (!alive(e)) continue;
            fn(e, kv.second);
        }
    }

    // Convenience iteration: join of two components (A,B) by entity id.
    // Passes (Entity, A&, B&) to fn.
    template <class A, class B, class Fn>
    void each2(Fn&& fn) {
        IterationGuard guard(*this);
        auto& a = components<A>().raw();
        auto& bStore = components<B>();

        for (auto& kv : a) {
            const std::uint32_t id = kv.first;
            Entity e{ id, id < generations_.size() ? generations_[id] : std::uint8_t{0} };
            if (!alive(e)) continue;
            if (auto* b = bStore.get(e); b) {
                fn(e, kv.second, *b);
            }
        }
    }

    // Canonical query API (starter): iterate entities that have all requested components.
    // This replaces ad-hoc raw-map access patterns over time.
    template <class... Cs, class Fn>
    void for_each(Fn&& fn) {
        IterationGuard guard(*this);
        detail::view_tuple<Cs...> tup{ std::tuple<detail::storage_ref<Cs>...>{ detail::storage_ref<Cs>{ &components<Cs>() }... } };
        View<Cs...> v(tup);
        v.each([&](Entity e, Cs&... comps) {
            if (!alive(e)) return;
            fn(e, comps...);
        });
    }

private:
    struct IStoreHolder {
        virtual ~IStoreHolder() = default;
        virtual void removeEntity(std::uint32_t id) = 0;
    };

    template <class T>
    struct StoreHolder final : IStoreHolder {
        ComponentStorage<T> store;
        void removeEntity(std::uint32_t id) override { store.removeById(id); }
    };

    template <class T>
    ComponentStorage<T>* try_components() {
        const std::type_index key(typeid(T));
        auto it = stores_.find(key);
        if (it == stores_.end()) return nullptr;
        return &static_cast<StoreHolder<T>*>(it->second.get())->store;
    }

    template <class T>
    const ComponentStorage<T>* try_components() const {
        const std::type_index key(typeid(T));
        auto it = stores_.find(key);
        if (it == stores_.end()) return nullptr;
        return &static_cast<const StoreHolder<T>*>(it->second.get())->store;
    }

    // ---- Structural-change policy (Stage 2.3) ----
    void defer(std::function<void()> op) { deferredOps_.push_back(std::move(op)); }

    void beginIteration() { ++iterationDepth_; }

    void endIteration() {
        if (iterationDepth_ == 0) return;
        --iterationDepth_;
        if (iterationDepth_ == 0 && !deferredOps_.empty()) {
            auto ops = std::move(deferredOps_);
            deferredOps_.clear();
            for (auto& op : ops) { op(); }
        }
    }

    struct IterationGuard {
        World& w;
        explicit IterationGuard(World& w_) : w(w_) { w.beginIteration(); }
        ~IterationGuard() { w.endIteration(); }
    };

    void removeAllComponentsFor(std::uint32_t id);
    void destroyImmediate(Entity e);

    engine::CoreServices* services_ = nullptr;

    // Entity bookkeeping
    std::vector<std::uint8_t> generations_;
    std::vector<std::uint32_t> freeIds_;

    // Type-erased component stores
    std::unordered_map<std::type_index, std::unique_ptr<IStoreHolder>> stores_;

    // Iteration / deferral state
    std::uint32_t iterationDepth_ = 0;
    std::vector<std::function<void()>> deferredOps_;
};

} // namespace engine::ecs
