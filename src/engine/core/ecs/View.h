// src/engine/core/ecs/View.h

#pragma once
#include "engine/core/ecs/Entity.h"
#include "engine/core/ecs/ComponentStorage.h"
#include <tuple>
#include <type_traits>
#include <cstdint>

namespace engine::ecs {

// Minimal "view" / join helper for the starter ECS.
// This is correctness-first and intentionally simple:
// - chooses the first component as the "driver" store
// - checks presence of the remaining components by id lookups
// Later upgrades can choose the smallest store as driver and add safe iterators.
//
// Usage:
//   world.for_each<Pos, Vel>([](Entity e, Pos& p, Vel& v){ ... });
//
// This is header-only to keep integration easy in early migration stages.

namespace detail {

template <class T>
struct storage_ref { ComponentStorage<T>* s; };

template <class... Ts>
struct view_tuple {
    std::tuple<storage_ref<Ts>...> stores;
};

template <class T, class... Ts>
ComponentStorage<T>& get_store(view_tuple<T, Ts...>& v) {
    return *std::get<storage_ref<T>>(v.stores).s;
}

template <class T, class... Ts>
ComponentStorage<T>& get_store(view_tuple<Ts...>& v) {
    return *std::get<storage_ref<T>>(v.stores).s;
}

} // namespace detail

template <class... Ts>
class View {
public:
    explicit View(detail::view_tuple<Ts...> stores) : stores_(stores) {}

    template <class Fn>
    void each(Fn&& fn) {
        static_assert(sizeof...(Ts) >= 1, "View requires at least one component type");

        // Drive on the first component's storage.
        using First = std::tuple_element_t<0, std::tuple<Ts...>>;
        auto& first = detail::get_store<First>(stores_);
        for (auto& kv : first.raw()) {
            const std::uint32_t id = kv.first;
            Entity e{ id, std::uint8_t{0} }; // generation validation is handled by World::alive() if needed

            // Only call fn if all components exist for this id.
            if (has_all_except_first<First>(id)) {
                call_fn<First>(fn, e, id);
            }
        }
    }

private:
    template <class First>
    bool has_all_except_first(std::uint32_t id) {
        return has_all_impl<First, Ts...>(id);
    }

    template <class First, class T0>
    bool has_all_impl(std::uint32_t id) {
        // Only First is present; always true (we're iterating it).
        (void)id;
        return true;
    }

    template <class First, class T0, class T1, class... Rest>
    bool has_all_impl(std::uint32_t id) {
        if constexpr (std::is_same_v<T1, First>) {
            return has_all_impl<First, T1, Rest...>(id);
        } else {
            auto& s = detail::get_store<T1>(stores_);
            if (s.get(Entity{id, 0}) == nullptr) return false;
            return has_all_impl<First, T1, Rest...>(id);
        }
    }

    template <class First, class Fn>
    void call_fn(Fn&& fn, Entity e, std::uint32_t id) {
        call_fn_impl<First, Fn, Ts...>(std::forward<Fn>(fn), e, id);
    }

    template <class First, class Fn, class T0>
    void call_fn_impl(Fn&& fn, Entity e, std::uint32_t /*id*/) {
        // only First
        auto& first = detail::get_store<First>(stores_);
        auto* c0 = first.get(Entity{e.id, 0});
        fn(e, *c0);
    }

    template <class First, class Fn, class T0, class T1, class... Rest>
    void call_fn_impl(Fn&& fn, Entity e, std::uint32_t id) {
        auto& s1 = detail::get_store<T1>(stores_);
        auto* c1 = s1.get(Entity{id, 0});
        // Build a tuple of references and then invoke.
        call_fn_tuple<First, Fn, T0, T1, Rest...>(std::forward<Fn>(fn), e, id, *c1);
    }

    template <class First, class Fn, class T0, class T1, class... Rest, class... Accum>
    void call_fn_tuple(Fn&& fn, Entity e, std::uint32_t id, Accum&... accum) {
        if constexpr (sizeof...(Rest) == 0) {
            auto& first = detail::get_store<First>(stores_);
            auto* c0 = first.get(Entity{id, 0});
            fn(e, *c0, accum...);
        } else {
            using Next = std::tuple_element_t<2 + sizeof...(Accum), std::tuple<T0, T1, Rest...>>;
            auto& sn = detail::get_store<Next>(stores_);
            auto* cn = sn.get(Entity{id, 0});
            call_fn_tuple<First, Fn, T0, T1, Rest...>(std::forward<Fn>(fn), e, id, accum..., *cn);
        }
    }

    detail::view_tuple<Ts...> stores_;
};

} // namespace engine::ecs
