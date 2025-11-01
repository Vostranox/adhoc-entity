#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace adh::ecs {
    struct Archetype;

    using EntityID = std::uint64_t;
    using ComponentID = std::uint32_t;
    using ArchetypeID = std::vector<ComponentID>;

    enum class Entity : EntityID {};

    inline constexpr std::uint64_t EMPTY_SLOT{ std::numeric_limits<std::uint64_t>::max() };
    inline constexpr Entity NULL_ENTITY{ std::numeric_limits<EntityID>::max() };
    inline constexpr std::uint32_t ENTITY_SHIFT{ 32U };
    inline constexpr std::uint64_t COMPONENT_PAGE_BYTES{ 4096U };

    inline ComponentID next_component_id() noexcept {
        static std::atomic<ComponentID> next{ 0U };
        return next.fetch_add(1U, std::memory_order_relaxed);
    }

    template <typename T>
    [[nodiscard]] ComponentID get_id() noexcept {
        static const ComponentID id{ next_component_id() };
        return id;
    }

    template <std::uint64_t S>
    struct SparseSetPage {
        static constexpr std::uint64_t PAGE_MAX_SIZE{ S / sizeof(std::uint64_t) };
        SparseSetPage() {
            data.fill(EMPTY_SLOT);
        }
        auto&& operator[](this auto&& self, std::uint64_t index) {
            return self.data[index];
        }
        std::array<std::uint64_t, PAGE_MAX_SIZE> data;
    };

    template <typename T, std::uint64_t S>
    class SparseSet {
      public:
        template <typename... Args>
        auto& add(std::uint32_t id, Args&&... args) {
            const std::uint64_t page{ get_page(id) };
            const std::uint64_t offset{ get_offset(id) };
            if (page >= m_sparse.size()) {
                m_sparse.resize(page + 1U);
            }
            std::uint64_t& slot{ m_sparse[page][offset] };
            if (slot == EMPTY_SLOT) {
                auto& ret{ m_dense.emplace_back(std::forward<Args>(args)...) };
                m_dense_index.emplace_back(id);
                slot = m_dense.size() - 1U;
                return ret;
            }
            return m_dense[slot];
        }

        void remove(std::uint32_t id) noexcept {
            const std::uint64_t hole{ m_sparse[get_page(id)][get_offset(id)] };
            const std::uint64_t last{ m_dense.size() - 1U };
            if (hole != last) {
                const std::uint32_t moved_id{ m_dense_index[last] };
                m_dense[hole] = std::move(m_dense[last]);
                m_dense_index[hole] = moved_id;
                m_sparse[get_page(moved_id)][get_offset(moved_id)] = hole;
            }
            m_sparse[get_page(id)][get_offset(id)] = EMPTY_SLOT;
            m_dense.pop_back();
            m_dense_index.pop_back();
        }

        [[nodiscard]] bool contains(std::uint32_t id) const noexcept {
            if (get_page(id) < m_sparse.size()) {
                return m_sparse[get_page(id)][get_offset(id)] != EMPTY_SLOT;
            }
            return false;
        }

        [[nodiscard]] auto&& get(this auto&& self, std::uint32_t id) noexcept {
            assert(self.contains(id) && "SparseSet::get: id not present");
            return self.m_dense[self.m_sparse[self.get_page(id)][self.get_offset(id)]];
        }

        auto&& operator[](this auto&& self, std::uint32_t id) noexcept {
            return self.get(id);
        }

        [[nodiscard]] auto size() const noexcept {
            return m_dense.size();
        }

        [[nodiscard]] auto empty() const noexcept {
            return m_dense.empty();
        }

        void reserve(std::size_t n) {
            m_dense.reserve(n);
            m_dense_index.reserve(n);
            m_sparse.resize((n / SparseSetPage<S>::PAGE_MAX_SIZE) + 1U);
        }

      private:
        [[nodiscard]] constexpr auto get_page(std::uint32_t id) const noexcept {
            return id / SparseSetPage<S>::PAGE_MAX_SIZE;
        }

        [[nodiscard]] constexpr auto get_offset(std::uint32_t id) const noexcept {
            return id % SparseSetPage<S>::PAGE_MAX_SIZE;
        }

        std::vector<SparseSetPage<S>> m_sparse;
        std::vector<T> m_dense;
        std::vector<std::uint32_t> m_dense_index;
    };

    struct BaseContainer {
        BaseContainer() = default;
        BaseContainer(const BaseContainer&) = default;
        BaseContainer(BaseContainer&&) = default;
        BaseContainer& operator=(const BaseContainer&) = default;
        BaseContainer& operator=(BaseContainer&&) = default;
        virtual ~BaseContainer() = default;
        [[nodiscard]] virtual std::unique_ptr<BaseContainer> create() const = 0;
        [[nodiscard]] virtual std::unique_ptr<BaseContainer> clone() const = 0;
        virtual void move(std::uint32_t index, BaseContainer* ptr) = 0;
        virtual void erase(std::uint32_t index) noexcept = 0;
    };

    template <typename T>
    class Container : public BaseContainer {
      public:
        [[nodiscard]] std::unique_ptr<BaseContainer> create() const override {
            return std::make_unique<Container>();
        }

        [[nodiscard]] std::unique_ptr<BaseContainer> clone() const override {
            if constexpr (std::is_copy_constructible_v<T>) {
                return std::make_unique<Container>(*this);
            } else {
                return nullptr;
            }
        }

        void move(std::uint32_t from, BaseContainer* dst) override {
            static_cast<Container*>(dst)->m_data.emplace_back(std::move(m_data[from]));
            erase(from);
        }

        void erase(std::uint32_t row) noexcept override {
            const std::uint32_t last{ static_cast<std::uint32_t>(m_data.size() - 1U) };
            if (row != last) {
                m_data[row] = std::move(m_data[last]);
            }
            m_data.pop_back();
        }

        template <typename... Args>
        decltype(auto) emplace_back(Args&&... args) {
            return m_data.emplace_back(std::forward<Args>(args)...);
        }

        [[nodiscard]] auto&& get(this auto&& self, std::uint32_t row) {
            return self.m_data[row];
        }

      private:
        std::vector<T> m_data;
    };

    struct Record {
        Archetype* archetype;
        std::uint32_t row;
    };

    struct Edge {
        Archetype* next;
    };

    struct Archetype {
        std::vector<Entity> entities;
        ArchetypeID type;
        std::unordered_map<ComponentID, Edge> edges;
        std::unordered_map<ComponentID, Archetype*> add_edges;
        SparseSet<std::unique_ptr<BaseContainer>, COMPONENT_PAGE_BYTES> components;
    };

    struct CachedSystem {
        std::vector<Archetype*> matched;
        std::size_t scanned;
    };

    template <typename... Components>
    class System;

    class World {
        template <typename... C>
        friend class System;

      public:
        World()
            : m_root_archetype{ std::make_unique<Archetype>() } {}

        World(const World&) = delete;
        World& operator=(const World&) = delete;
        World(World&&) = default;
        World& operator=(World&&) = default;
        ~World() = default;

        [[nodiscard]] std::optional<World> clone() const {
            World out;
            std::unordered_map<const Archetype*, Archetype*> remap;
            remap.emplace(m_root_archetype.get(), out.m_root_archetype.get());

            for (const auto& up : m_archetypes) {
                const Archetype* src{ up.get() };
                auto dst{ std::make_unique<Archetype>() };
                dst->entities = src->entities;
                dst->type = src->type;
                for (const ComponentID cid : src->type) {
                    auto cloned{ src->components[cid]->clone() };
                    if (cloned == nullptr) {
                        return std::nullopt;
                    }
                    dst->components.add(cid, std::move(cloned));
                }
                remap.emplace(src, dst.get());
                out.m_archetypes.emplace_back(std::move(dst));
            }

            const auto remap_graph = [&remap](const Archetype* src, Archetype* dst) {
                for (const auto& [cid, e] : src->edges) {
                    dst->edges.emplace(cid, Edge{ e.next ? remap.at(e.next) : nullptr });
                }
                for (const auto& [cid, a] : src->add_edges) {
                    dst->add_edges.emplace(cid, a ? remap.at(a) : nullptr);
                }
            };
            remap_graph(m_root_archetype.get(), out.m_root_archetype.get());
            for (const auto& up : m_archetypes) {
                remap_graph(up.get(), remap.at(up.get()));
            }

            out.m_entities = m_entities;
            out.m_recycled_entities = m_recycled_entities;
            out.m_retired = m_retired;
            out.m_entity_archetype.reserve(m_entity_archetype.size());
            for (const Record& rec : m_entity_archetype) {
                out.m_entity_archetype.emplace_back(rec.archetype != nullptr ? remap.at(rec.archetype) : nullptr,
                                                    rec.row);
            }
            return out;
        }

        void reset() {
            clear_to_empty();
        }

        [[nodiscard]] Entity create_entity() {
            if (m_recycled_entities.empty()) {
                return generate_id();
            }
            return recycle_id();
        }

        template <typename... T, typename... Args>
        decltype(auto) add(Entity entity, Args&&... args) {
            Record& r{ record_of(entity) };
            assert((!has_component<T>(entity) && ...) && "add: entity already has the component(s)");
            Archetype* node = nullptr;
            if constexpr (sizeof...(T) == 1) {
                node = add_target(r.archetype, get_id<T...>());
            } else {
                ArchetypeID archetype_id{ get_archetype_id<T...>(r.archetype) };
                node = find_archetype(archetype_id);
            }

            const std::uint32_t old_row{ r.row };
            if (r.archetype) {
                pop_entity(entity, r.archetype);
            }

            const bool fresh_node{ node->components.empty() };
            if (r.archetype != nullptr) {
                transfer_row(r.archetype, old_row, node, r.archetype->type);
            }
            if (fresh_node) {
                (add_component<T>(node), ...);
            }

            if constexpr (sizeof...(Args) == sizeof...(T)) {
                (emplace_data<T>(node, std::forward<Args>(args)), ...);
            } else if constexpr (sizeof...(T) == 1) {
                get_pointer<T...>(node)->emplace_back(std::forward<Args>(args)...);
            } else {
                static_assert(false,
                              "add: pass one argument per component, or a single component with its constructor args");
            }
            r.archetype = node;
            r.archetype->entities.emplace_back(entity);
            r.row = static_cast<std::uint32_t>(r.archetype->entities.size() - 1U);
            if constexpr (sizeof...(T) == 1) {
                return get_pointer<T...>(r.archetype)->get(r.row);
            } else {
                return std::forward_as_tuple(get_pointer<T>(r.archetype)->get(r.row)...);
            }
        }

        template <typename... T>
        void remove(Entity entity) {
            Record& r{ record_of(entity) };
            assert(r.archetype != nullptr && "remove: entity has no components");
            assert(has_component<T...>(entity) && "remove: entity does not have the component(s)");
            if (r.archetype->type.size() == sizeof...(T)) {
                remove_all(entity);
                return;
            }

            const std::uint32_t row{ r.row };
            ArchetypeID to_remove_ids{ get_id<T>()... };
            std::ranges::sort(to_remove_ids);
            ArchetypeID archetype_id{ r.archetype->type };
            for (const auto cid : to_remove_ids) {
                r.archetype->components[cid]->erase(row);
                archetype_id.erase(std::ranges::find(archetype_id, cid));
            }
            Archetype* node = find_archetype(archetype_id);

            pop_entity(entity, r.archetype);
            transfer_row(r.archetype, row, node, node->type);

            r.archetype = node;
            r.archetype->entities.emplace_back(entity);
            r.row = static_cast<std::uint32_t>(r.archetype->entities.size() - 1U);
        }

        void remove_all(Entity entity) noexcept {
            Record& r{ record_of(entity) };
            if (r.archetype == nullptr) {
                return;
            }
            const std::uint32_t row{ r.row };
            for (const auto cid : r.archetype->type) {
                r.archetype->components[cid]->erase(row);
            }
            pop_entity(entity, r.archetype);
            r.archetype = nullptr;
        }

        template <typename... T>
        [[nodiscard]] bool has_component(Entity entity) const noexcept {
            if (!is_valid(entity)) {
                return false;
            }
            const Record& r{ record_of(entity) };
            return r.archetype != nullptr && (r.archetype->components.contains(get_id<T>()) && ...);
        }

        template <typename... T>
        [[nodiscard]] decltype(auto) get(Entity entity) {
            Record& r{ record_of(entity) };
            assert(has_component<T...>(entity) && "get: entity does not have the component(s)");
            if constexpr (sizeof...(T) == 1) {
                return get_pointer<T...>(r.archetype)->get(r.row);
            } else {
                return std::forward_as_tuple(get_pointer<T>(r.archetype)->get(r.row)...);
            }
        }

        void destroy(Entity entity) {
            if (is_valid(entity)) {
                remove_all(entity);
                delete_id(entity);
            }
        }

        void destroy() {
            clear_to_empty();
        }

        [[nodiscard]] bool is_valid(Entity entity) const noexcept {
            std::uint32_t pos{ get_index(get_type(entity)) };
            return (pos < m_entities.size() && m_entities[pos] == entity);
        }

        template <typename... T>
        System<T...> get_system();

        template <typename T>
        void for_each(T func) {
            for (std::int64_t i = static_cast<std::int64_t>(m_entities.size()) - 1; i >= 0; --i) {
                const auto idx{ static_cast<std::size_t>(i) };
                if (is_valid(m_entities[idx])) {
                    func(m_entities[idx]);
                }
            }
        }

        [[nodiscard]] std::size_t get_entity_count() const noexcept {
            return m_entities.size() - m_recycled_entities.size() - m_retired;
        }

      private:
        void clear_to_empty() {
            m_archetypes.clear();
            m_cached_systems.clear();
            m_entity_archetype.clear();
            m_entities.clear();
            m_recycled_entities = {};
            m_retired = 0U;
            m_root_archetype = std::make_unique<Archetype>();
        }

        Entity generate_id() {
            const Entity id{ create_id(static_cast<std::uint32_t>(m_entities.size() + 1), 0U) };
            m_entities.emplace_back(id);
            m_entity_archetype.emplace_back(Record{ .archetype = nullptr, .row = 0U });
            return id;
        }

        Entity recycle_id() {
            const Entity temp{ m_recycled_entities.front() };
            m_recycled_entities.pop();
            const Entity id = create_id(get_index(get_type(temp)) + 1U, get_version(get_type(temp)) + 1U);
            m_entities[get_index(get_type(temp))] = id;
            m_entity_archetype[get_index(get_type(temp))] = Record{ .archetype = nullptr, .row = 0U };
            return id;
        }

        static Entity create_id(std::uint32_t index, std::uint32_t version) noexcept {
            return static_cast<Entity>((static_cast<EntityID>(index) << ENTITY_SHIFT) | version);
        }

        void delete_id(Entity entity) {
            if (get_version(get_type(entity)) < std::numeric_limits<std::uint32_t>::max()) {
                m_recycled_entities.emplace(entity);
            } else {
                ++m_retired;
            }
            m_entities[get_index(get_type(entity))] = NULL_ENTITY;
            m_entity_archetype[get_index(get_type(entity))] = Record{ .archetype = nullptr, .row = 0U };
        }

        static EntityID get_type(Entity entity) noexcept {
            return static_cast<EntityID>(entity);
        }

        static std::uint32_t get_index(EntityID entity) noexcept {
            return static_cast<std::uint32_t>(entity >> ENTITY_SHIFT) - 1U;
        }

        static std::uint32_t get_version(EntityID entity) noexcept {
            return static_cast<std::uint32_t>(entity);
        }

        template <typename... T>
        decltype(auto) get_archetype_id(Archetype* node) {
            ArchetypeID archetype_id{ get_id<T>()... };
            if (node) {
                for (const auto cid : node->type) {
                    archetype_id.emplace_back(cid);
                }
            }
            std::ranges::sort(archetype_id);
            const auto dup{ std::ranges::unique(archetype_id) };
            archetype_id.erase(dup.begin(), dup.end());
            return archetype_id;
        }

        Archetype* add_target(Archetype* source, ComponentID cid) {
            Archetype* src{ (source != nullptr) ? source : m_root_archetype.get() };
            if (auto it{ src->add_edges.find(cid) }; it != src->add_edges.end()) {
                return it->second;
            }
            ArchetypeID archetype_id;
            if (source != nullptr) {
                archetype_id = source->type;
            }
            archetype_id.emplace_back(cid);
            std::ranges::sort(archetype_id);
            const auto dup{ std::ranges::unique(archetype_id) };
            archetype_id.erase(dup.begin(), dup.end());
            Archetype* node{ find_archetype(archetype_id) };
            src->add_edges.emplace(cid, node);
            return node;
        }

        Archetype* find_archetype(const ArchetypeID& archetype_id) {
            Archetype* node{ m_root_archetype.get() };
            for (std::size_t i{}; i != archetype_id.size(); ++i) {
                Edge& edge{ node->edges[archetype_id[i]] };
                if (edge.next == nullptr) {
                    auto owned{ std::make_unique<Archetype>() };
                    Archetype* new_archetype{ owned.get() };
                    new_archetype->edges.emplace(archetype_id[i], Edge{ nullptr });
                    for (std::size_t j{}; j <= i; ++j) {
                        new_archetype->type.emplace_back(archetype_id[j]);
                    }
                    edge.next = new_archetype;
                    m_archetypes.emplace_back(std::move(owned));
                }
                node = edge.next;
            }
            return node;
        }

        void pop_entity(Entity entity, Archetype* node) noexcept {
            if (node->entities.empty()) {
                return;
            }
            std::uint32_t row{ record_of(entity).row };
            const Entity last{ node->entities.back() };
            node->entities[row] = last;
            record_of(last).row = row;
            node->entities.pop_back();
        }

        void transfer_row(Archetype* from, std::uint32_t from_row, Archetype* to, const ArchetypeID& which) {
            const bool fresh{ to->components.empty() };
            for (const ComponentID cid : which) {
                if (fresh) {
                    to->components.add(cid, from->components[cid]->create());
                }
                from->components[cid]->move(from_row, to->components[cid].get());
            }
        }

        template <typename T>
        void add_component(Archetype* node) {
            node->components.add(get_id<T>(), std::make_unique<Container<T>>());
        }

        template <typename T, typename... Args>
        void emplace_data(Archetype* node, Args&&... args) {
            get_pointer<T>(node)->emplace_back(std::forward<Args>(args)...);
        }

        template <typename T>
        auto get_pointer(Archetype* node) noexcept {
            return static_cast<Container<T>*>(node->components[get_id<T>()].get());
        }

        Record& record_of(Entity entity) noexcept {
            return m_entity_archetype[get_index(get_type(entity))];
        }

        [[nodiscard]] const Record& record_of(Entity entity) const noexcept {
            return m_entity_archetype[get_index(get_type(entity))];
        }

        std::vector<std::unique_ptr<Archetype>> m_archetypes;
        std::map<ArchetypeID, CachedSystem> m_cached_systems;
        std::vector<Record> m_entity_archetype;
        std::vector<Entity> m_entities;
        std::queue<Entity> m_recycled_entities;
        std::size_t m_retired{};
        std::unique_ptr<Archetype> m_root_archetype;
    };

    namespace detail {
        template <typename...>
        struct unique_types : std::true_type {};

        template <typename T, typename... Rest>
        struct unique_types<T, Rest...>
            : std::bool_constant<(!std::is_same_v<T, Rest> && ...) && unique_types<Rest...>::value> {};
    } // namespace detail

    template <typename... Components>
    class System {
        static_assert(detail::unique_types<Components...>::value, "System: component types must be distinct");

      public:
        System(World* world, ArchetypeID ids)
            : m_world{ world }
            , m_ids{ std::move(ids) } {}

        template <typename T>
        void for_each(T func) {
            const std::vector<std::unique_ptr<Archetype>>& archetypes{ m_world->m_archetypes };
            CachedSystem& cached{ m_world->m_cached_systems[m_ids] };
            for (std::size_t i{ cached.scanned }; i < archetypes.size(); ++i) {
                if (matches(archetypes[i].get())) {
                    cached.matched.emplace_back(archetypes[i].get());
                }
            }
            cached.scanned = archetypes.size();

            for (Archetype* node : cached.matched) {
                if (node->entities.empty()) {
                    continue;
                }
                auto ptrs{ std::make_tuple(get_pointer<Components>(node)...) };
                for (std::int64_t i = static_cast<std::int64_t>(node->entities.size()) - 1; i >= 0; --i) {
                    const auto row{ static_cast<std::uint32_t>(i) };
                    if constexpr (std::is_invocable_v<T, decltype(std::declval<Components&>())...>) {
                        std::apply(func, std::forward_as_tuple(std::get<Container<Components>*>(ptrs)->get(row)...));
                    } else {
                        std::apply(func, std::tuple_cat(std::forward_as_tuple(node->entities[row]),
                                                        std::forward_as_tuple(
                                                            std::get<Container<Components>*>(ptrs)->get(row)...)));
                    }
                }
            }
        }

      private:
        template <typename T>
        auto get_pointer(Archetype* node) noexcept {
            return static_cast<Container<T>*>(node->components[get_id<T>()].get());
        }

        bool matches(const Archetype* node) const {
            return std::ranges::all_of(m_ids, [node](ComponentID id) {
                return std::ranges::contains(node->type, id);
            });
        }

        World* m_world;
        ArchetypeID m_ids;
    };

    template <typename... T>
    System<T...> World::get_system() {
        ArchetypeID ids{ get_id<T>()... };
        std::ranges::sort(ids);
        return System<T...>{ this, std::move(ids) };
    }
} // namespace adh::ecs
