#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <print>
#include <queue>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <adh/entity.hpp>

namespace {
    std::int32_t s_allocations_before_failure{ -1 };
} // namespace

void* operator new(std::size_t size) {
    if (s_allocations_before_failure >= 0 && s_allocations_before_failure-- == 0) {
        throw std::bad_alloc{};
    }
    if (void* p{ std::malloc(size != 0U ? size : 1U) }) {
        return p;
    }
    throw std::bad_alloc{};
}

#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#endif

namespace {

    struct Position {
        float x, y, z;
    };
    struct Velocity {
        float dx, dy, dz;
    };
    struct Health {
        float hp;
    };
    struct Tag {
        std::uint32_t v;
    };
    struct MoveOnly {
        std::unique_ptr<std::int32_t> p;
    };
    struct Inventory {
        std::vector<std::unique_ptr<std::int32_t>> items;
    };
    struct Tree {
        using value_type = Tree;
        using iterator = Tree*;
        std::int32_t value;
        std::vector<Tree> children;
    };
    struct PropertyTree {
        using value_type = std::pair<const std::string, PropertyTree>;
        using iterator = value_type*;
        std::string data;
        std::map<std::string, PropertyTree> children;
    };
    struct Forest : std::map<std::string, std::vector<Forest>> {
        std::int32_t value{};
    };
    struct OwnerTree : std::map<std::string, std::unique_ptr<OwnerTree>> {};

} // namespace

template <>
struct adh::ecs::is_cloneable<Inventory> : std::false_type {};

namespace {

    std::int32_t s_checks{};
    std::int32_t s_fail{};

    void check(bool cond, std::source_location loc = std::source_location::current()) {
        ++s_checks;
        if (!cond) {
            std::println("  FAIL  {}:{}", loc.file_name(), loc.line());
            ++s_fail;
        }
    }

    using namespace adh::ecs;

    static_assert(!std::is_copy_constructible_v<World> && !std::is_copy_assignable_v<World>,
                  "World must be non-copyable");
    static_assert(std::is_move_constructible_v<World> && std::is_move_assignable_v<World>, "World must be movable");
    static_assert(!std::is_copy_constructible_v<CommandBuffer> && !std::is_copy_assignable_v<CommandBuffer>,
                  "CommandBuffer must be non-copyable");
    static_assert(std::is_nothrow_move_constructible_v<CommandBuffer> &&
                      std::is_nothrow_move_assignable_v<CommandBuffer>,
                  "CommandBuffer must be nothrow movable");
    static_assert(std::is_copy_constructible_v<SparseSetPage<64U>> && std::is_move_constructible_v<SparseSetPage<64U>>,
                  "SparseSetPage is vector-backed: copyable and movable");

    template <typename T>
    concept storable = requires { typename Container<T>; };
    static_assert(storable<Position> && storable<std::int32_t>, "components are stored in a Container");
    static_assert(!storable<bool> && !storable<const bool>, "bool components are rejected at compile time");

    void test_sparse_set() {
        SparseSet<std::int32_t, 4096U> s;
        check(s.empty());
        check(s.size() == 0);
        check(!s.contains(7U));

        s.add(7U, 11);
        s.add(9U, 22);
        check(!s.empty());
        check(s.size() == 2);
        check(s.contains(7U));
        check(s.contains(9U));
        check(!s.contains(8U));

        check(s.get(7U) == 11);
        s.get(7U) = 100;
        check(s[7U] == 100);

        const auto& cs = s;
        check(cs.get(9U) == 22);
        check(cs[9U] == 22);
        check(cs.contains(7U));
        check(cs.size() == 2);
        check(!cs.empty());

        check(s.add(7U, -1) == 100);
        check(s.get(7U) == 100);

        s.remove(7U);
        check(!s.contains(7U));
        check(s.size() == 1);
        check(s.get(9U) == 22);

        SparseSet<std::int32_t, 4096U> r;
        r.reserve(128);
        r.add(3U, 33);
        check(r.contains(3U) && r.get(3U) == 33);

        SparseSet<std::int32_t, 4096U> hi;
        hi.add(5U, 50);
        hi.add(1000U, 1000);
        hi.reserve(16);
        check(hi.contains(1000U) && hi.get(1000U) == 1000);
        hi.remove(5U);
        check(hi.size() == 1 && hi.contains(1000U) && hi.get(1000U) == 1000 && !hi.contains(5U));
    }

    void test_add_get_arity() {
        World w;
        auto e = w.create_entity();

        w.add<Position, Velocity>(e, Position{ 1, 2, 3 }, Velocity{ 4, 5, 6 });
        Health& h = w.add<Health>(e, Health{ 50.f });
        check(h.hp == 50.f);
        h.hp = 75.f;

        check(w.get<Health>(e).hp == 75.f);
        w.get<Position>(e).x = 9.f;
        check(w.get<Position>(e).x == 9.f);

        auto&& [p, v] = w.get<Position, Velocity>(e);
        check(p.y == 2 && p.z == 3);
        check(v.dx == 4 && v.dz == 6);
        p.y = 20;
        check(w.get<Position>(e).y == 20);

        auto e2 = w.create_entity();
        w.add<Position>(e2, 7.f, 8.f, 9.f);
        check(w.get<Position>(e2).x == 7 && w.get<Position>(e2).y == 8 && w.get<Position>(e2).z == 9);
    }

    struct Checked {
        explicit Checked(std::int32_t value)
            : v{ value } {
            if (value < 0) {
                throw std::invalid_argument{ "negative" };
            }
        }
        std::int32_t v;
    };

    struct Home {
        Position p;
    };

    void test_add_argument_aliases_world() {
        {
            World w;
            auto a = w.create_entity();
            auto b = w.create_entity();
            w.add<Position>(a, Position{ 1, 1, 1 });
            w.add<Position>(b, Position{ 2, 2, 2 });
            w.add<Home>(a, w.get<Position>(a));
            check(w.get<Home>(a).p.x == 1 && w.get<Position>(a).x == 1 && w.get<Position>(b).x == 2);
        }
        {
            World w;
            auto a = w.create_entity();
            auto b = w.create_entity();
            w.add<Position, Home>(a, Position{ 5, 5, 5 }, Home{ Position{ 0, 0, 0 } });
            w.add<Position>(b, Position{ 7, 7, 7 });
            w.add<Home>(b, w.get<Position>(a));
            check(w.get<Home>(b).p.x == 5 && w.get<Position>(b).x == 7 && w.get<Position>(a).x == 5);
        }
    }

    void test_add_throwing_constructor() {
        {
            World w;
            auto a = w.create_entity();
            auto b = w.create_entity();
            w.add<Position>(a, Position{ 1, 1, 1 });
            w.add<Position>(b, Position{ 2, 2, 2 });
            bool threw{};
            try {
                w.add<Checked>(a, -1);
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            check(threw);
            check(w.has_component<Position>(a) && !w.has_component<Checked>(a) && w.get<Position>(a).x == 1);
            check(w.get<Position>(b).x == 2);
            w.add<Checked>(a, 3);
            check(w.get<Checked>(a).v == 3 && w.get<Position>(a).x == 1);
            w.destroy(b);
            w.destroy(a);
            check(w.get_entity_count() == 0);
        }
        {
            World w;
            auto a = w.create_entity();
            bool threw{};
            try {
                w.add<Position, Checked>(a, Position{ 1, 1, 1 }, -1);
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            check(threw);
            check(!w.has_component<Position>(a));
            auto b = w.create_entity();
            w.add<Position, Checked>(b, Position{ 2, 2, 2 }, 7);
            check(w.get<Position>(b).x == 2 && w.get<Checked>(b).v == 7);
            std::int32_t visits{};
            w.get_system<Position, Checked>().for_each([&](Entity ent, Position&, Checked&) {
                check(ent == b);
                ++visits;
            });
            check(visits == 1);
        }
    }

    void test_add_allocation_failure() {
        bool intact{ true };
        std::int32_t failures{};
        for (std::int32_t n{};; ++n) {
            World w;
            auto a = w.create_entity();
            auto b = w.create_entity();
            auto c = w.create_entity();
            w.add<Position>(a, Position{ 1, 1, 1 });
            w.add<Position>(b, Position{ 2, 2, 2 });
            w.add<Position, Health>(c, Position{ 3, 3, 3 }, Health{ 3.f });

            bool threw{};
            s_allocations_before_failure = n;
            try {
                w.add<Health>(a, Health{ 1.f });
                w.add<Velocity, Tag>(b, Velocity{ 2, 2, 2 }, Tag{ 2U });
            } catch (const std::bad_alloc&) {
                threw = true;
            }
            s_allocations_before_failure = -1;

            intact = intact && w.get<Position>(a).x == 1 && w.get<Position>(b).x == 2 && w.get<Position>(c).x == 3 &&
                     w.get<Health>(c).hp == 3.f && (!w.has_component<Health>(a) || w.get<Health>(a).hp == 1.f) &&
                     w.has_component<Velocity>(b) == w.has_component<Tag>(b) &&
                     (!w.has_component<Velocity>(b) || (w.get<Velocity>(b).dx == 2 && w.get<Tag>(b).v == 2U));

            if (!w.has_component<Health>(a)) {
                w.add<Health>(a, Health{ 1.f });
            }
            if (!w.has_component<Velocity>(b)) {
                w.add<Velocity, Tag>(b, Velocity{ 2, 2, 2 }, Tag{ 2U });
            }
            float health{};
            w.get_system<Position, Health>().for_each([&](Position& p, Health& h) {
                intact = intact && p.x == h.hp;
                health += h.hp;
            });
            std::int32_t moving{};
            w.get_system<Position, Velocity, Tag>().for_each([&](Entity e, Position& p, Velocity&, Tag&) {
                intact = intact && e == b && p.x == 2;
                ++moving;
            });
            intact = intact && health == 4.f && moving == 1;
            w.destroy(c);
            w.destroy(b);
            w.destroy(a);
            intact = intact && w.get_entity_count() == 0;

            if (!threw) {
                break;
            }
            ++failures;
        }
        check(intact);
        check(failures > 0);
    }

    void test_has_component_is_valid() {
        World w;
        auto e = w.create_entity();
        w.add<Position, Velocity>(e, Position{ 1, 1, 1 }, Velocity{ 1, 1, 1 });

        check(w.has_component<Position>(e));
        check(w.has_component<Velocity>(e));
        check((w.has_component<Position, Velocity>(e)));
        check(!w.has_component<Health>(e));
        check(!(w.has_component<Position, Health>(e)));

        check(w.is_valid(e));
        check(!w.is_valid(NULL_ENTITY));
        check(!w.is_valid(static_cast<Entity>(0xDEADBEEFU)));
        check(!w.has_component<Position>(NULL_ENTITY));
        check(w.get_entity_count() == 1);
    }

    void test_remove() {
        World w;
        auto e = w.create_entity();
        w.add<Position, Velocity, Health>(e, Position{ 1, 2, 3 }, Velocity{ 4, 5, 6 }, Health{ 99.f });

        w.remove<Velocity>(e);
        check(!w.has_component<Velocity>(e));
        check((w.has_component<Position, Health>(e)));
        check(w.get<Position>(e).x == 1 && w.get<Position>(e).z == 3);
        check(w.get<Health>(e).hp == 99.f);

        w.remove_all(e);
        check(!w.has_component<Position>(e));
        check(!w.has_component<Health>(e));
        check(w.is_valid(e));

        World multi;
        auto m = multi.create_entity();
        multi.add<Position, Velocity, Health>(m, Position{ 1, 2, 3 }, Velocity{ 4, 5, 6 }, Health{ 9.f });
        multi.remove<Velocity, Health>(m);
        check(multi.has_component<Position>(m));
        check(!multi.has_component<Velocity>(m));
        check(!multi.has_component<Health>(m));
        check(multi.get<Position>(m).y == 2);
    }

    void test_destroy_recycle() {
        World w;
        auto e = w.create_entity();
        w.add<Position>(e, Position{ 1, 1, 1 });
        check(w.is_valid(e));

        w.destroy(e);
        check(!w.is_valid(e));
        check(!w.has_component<Position>(e));
        check(w.get_entity_count() == 0);

        auto e2 = w.create_entity();
        check(w.is_valid(e2));
        check(!w.is_valid(e));
    }

    void test_reset_and_clear() {
        World w;
        for (std::int32_t i{}; i < 10; ++i) {
            w.add<Position>(w.create_entity(), Position{ float(i), 0, 0 });
        }
        check(w.get_entity_count() == 10);

        w.reset();
        check(w.get_entity_count() == 0);
        auto e = w.create_entity();
        w.add<Position>(e, Position{ 1, 1, 1 });
        check(w.has_component<Position>(e));

        w.destroy();
        check(w.get_entity_count() == 0);
    }

    void test_move_semantics() {
        World original;
        auto e = original.create_entity();
        original.add<Position, Velocity>(e, Position{ 1, 2, 3 }, Velocity{ 4, 5, 6 });

        World move_constructed{ std::move(original) };
        check((move_constructed.has_component<Position, Velocity>(e)));
        check(move_constructed.get<Position>(e).x == 1);

        World move_assigned;
        move_assigned.add<Health>(move_assigned.create_entity(), Health{ 1.f });
        move_assigned = std::move(move_constructed);
        check(move_assigned.has_component<Position>(e));
        check(move_assigned.get<Velocity>(e).dy == 5);
    }

    void test_moved_from_world_is_usable() {
        const auto exercise = [](World& w) {
            auto e = w.create_entity();
            w.add<Position>(e, Position{ 3, 4, 5 });
            w.add<Velocity>(e, Velocity{ 1, 1, 1 });
            std::int32_t visits{};
            w.get_system<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
                p.x += v.dx;
                ++visits;
            });
            auto copy = w.clone();
            return visits == 1 && w.get<Position>(e).x == 4 && w.get_entity_count() == 1 && copy.has_value() &&
                   copy->get<Position>(e).x == 4;
        };

        World source;
        source.add<Health>(source.create_entity(), Health{ 1.f });
        World constructed{ std::move(source) };
        check(source.clone().has_value());
        check(exercise(source));

        World assigned;
        assigned = std::move(constructed);
        check(exercise(constructed));
        check(assigned.get_entity_count() == 1);
    }

    void test_systems_and_for_each() {
        World w;
        auto e1 = w.create_entity();
        auto e2 = w.create_entity();
        auto e3 = w.create_entity();
        w.add<Position, Velocity>(e1, Position{ 0, 0, 0 }, Velocity{ 1, 1, 1 });
        w.add<Position, Velocity>(e2, Position{ 0, 0, 0 }, Velocity{ 1, 1, 1 });
        w.add<Position>(e3, Position{ 0, 0, 0 });

        std::int32_t pos_count{};
        w.get_system<Position>().for_each([&](Position& p) {
            p.x += 1.f;
            ++pos_count;
        });
        check(pos_count == 3);
        check(w.get<Position>(e1).x == 1.f);

        std::int32_t pv_count{};
        w.get_system<Position, Velocity>().for_each([&](Entity ent, Position& p, Velocity& v) {
            (void)ent;
            p.x += v.dx;
            ++pv_count;
        });
        check(pv_count == 2);
        check(w.get<Position>(e1).x == 2.f);
        check(w.get<Position>(e3).x == 1.f);

        std::int32_t total{};
        w.for_each([&](Entity) {
            ++total;
        });
        check(total == 3);
    }

    void test_world_for_each_stable_handle() {
        World w;
        std::vector<Entity> parents;
        for (std::int32_t i{}; i < 4; ++i) {
            parents.push_back(w.create_entity());
        }

        std::vector<Entity> visited;
        std::vector<Entity> children;
        w.for_each([&](const Entity& e) {
            for (std::int32_t i{}; i < 8; ++i) {
                children.push_back(w.create_entity());
            }
            visited.push_back(e);
        });
        check(visited.size() == parents.size());
        check(std::ranges::is_permutation(visited, parents));

        bool unchanged{ true };
        w.for_each([&](const Entity& e) {
            const Entity before{ e };
            w.destroy(e);
            unchanged = unchanged && e == before;
        });
        check(unchanged);
        check(w.get_entity_count() == 0);
    }

    void test_multi_archetype() {
        World w;
        constexpr std::int32_t N{ 60 };
        for (std::int32_t i{}; i < N; ++i) {
            auto e = w.create_entity();
            w.add<Position>(e, Position{ float(i), 0, 0 });
            if (i % 2 == 0) {
                w.add<Velocity>(e, Velocity{ 1, 1, 1 });
            }
            if (i % 3 == 0) {
                w.add<Health>(e, Health{ 1.f });
            }
        }

        std::int32_t exp_pv{};
        std::int32_t exp_pvh{};
        for (std::int32_t i{}; i < N; ++i) {
            if (i % 2 == 0) {
                ++exp_pv;
            }
            if (i % 2 == 0 && i % 3 == 0) {
                ++exp_pvh;
            }
        }

        std::int32_t got_pos{};
        std::int32_t got_pv{};
        std::int32_t got_pvh{};
        w.get_system<Position>().for_each([&](Position&) {
            ++got_pos;
        });
        w.get_system<Position, Velocity>().for_each([&](Position&, Velocity&) {
            ++got_pv;
        });
        w.get_system<Position, Velocity, Health>().for_each([&](Position&, Velocity&, Health&) {
            ++got_pvh;
        });
        check(got_pos == N);
        check(got_pv == exp_pv);
        check(got_pvh == exp_pvh);
    }

    void test_system_multi_add_and_refresh() {
        World w;
        auto sys = w.get_system<Position, Velocity>();

        std::int32_t before{};
        sys.for_each([&](Position&, Velocity&) {
            ++before;
        });
        check(before == 0);

        for (std::int32_t i{}; i < 20; ++i) {
            auto e = w.create_entity();
            w.add<Position, Velocity, Health, Tag>(e, Position{ float(i), 0, 0 }, Velocity{ 1, 1, 1 }, Health{ 1.f },
                                                   Tag{ 0 });
        }
        for (std::int32_t i{}; i < 10; ++i) {
            auto e = w.create_entity();
            w.add<Position, Velocity>(e, Position{ 0, 0, 0 }, Velocity{ 1, 1, 1 });
        }

        std::int32_t pv{};
        sys.for_each([&](Position&, Velocity&) {
            ++pv;
        });
        check(pv == 30);

        std::int32_t p{};
        w.get_system<Position>().for_each([&](Position&) {
            ++p;
        });
        check(p == 30);
    }

    void test_system_lifetime() {
        {
            World w;
            auto e = w.create_entity();
            w.add<Position>(e, Position{ 1, 0, 0 });
            auto sys = w.get_system<Position>();
            std::int32_t before{};
            sys.for_each([&](Position&) {
                ++before;
            });
            check(before == 1);

            w.reset();
            std::int32_t after{};
            sys.for_each([&](Position&) {
                ++after;
            });
            check(after == 0);
        }

        {
            World w1;
            auto e = w1.create_entity();
            w1.add<Position>(e, Position{ 2, 0, 0 });
            auto stale = w1.get_system<Position>();

            World w2{ std::move(w1) };
            std::int32_t stale_count{};
            stale.for_each([&](Position&) {
                ++stale_count;
            });
            check(stale_count == 0);

            std::int32_t fresh_count{};
            w2.get_system<Position>().for_each([&](Position&) {
                ++fresh_count;
            });
            check(fresh_count == 1);
        }
    }

    template <std::uint32_t N>
    struct Marker {
        std::uint32_t v;
    };

    template <std::uint32_t... Is>
    void spawn_markers(World& w, std::integer_sequence<std::uint32_t, Is...>) {
        (w.add<Position, Marker<Is>>(w.create_entity(), Position{ 0, 0, 0 }, Marker<Is>{ Is }), ...);
    }

    void test_for_each_skips_entities_moved_into_empty_archetype() {
        World w;
        std::vector<Entity> entities;
        for (std::int32_t i{}; i < 3; ++i) {
            auto e = w.create_entity();
            w.add<Position>(e, Position{ float(i), 0, 0 });
            entities.push_back(e);
        }
        auto tmp = w.create_entity();
        w.add<Position, Health>(tmp, Position{ 0, 0, 0 }, Health{ 0.f });
        w.destroy(tmp);

        std::int32_t visits{};
        w.get_system<Position>().for_each([&](Entity ent, Position&) {
            ++visits;
            if (!w.has_component<Health>(ent)) {
                w.add<Health>(ent, Health{ 2.f });
            }
        });
        check(visits == 3);

        bool all_moved{ true };
        for (std::size_t i{}; i < entities.size(); ++i) {
            all_moved = all_moved && w.get<Health>(entities[i]).hp == 2.f && w.get<Position>(entities[i]).x == float(i);
        }
        check(all_moved);
    }

    void test_for_each_nested_same_system() {
        World w;
        w.add<Position>(w.create_entity(), Position{ 0, 0, 0 });
        w.add<Position, Velocity>(w.create_entity(), Position{ 0, 0, 0 }, Velocity{ 0, 0, 0 });

        std::int32_t outer{};
        std::int32_t inner{};
        w.get_system<Position>().for_each([&](Position&) {
            if (++outer == 1) {
                spawn_markers(w, std::make_integer_sequence<std::uint32_t, 16U>{});
                w.get_system<Position>().for_each([&](Position&) {
                    ++inner;
                });
            }
        });
        check(outer == 2);
        check(inner == 18);

        std::int32_t after{};
        w.get_system<Position>().for_each([&](Position&) {
            ++after;
        });
        check(after == 18);
    }

    class DropCounter {
      public:
        explicit DropCounter(std::int32_t* drops)
            : m_drops{ drops } {}
        DropCounter(const DropCounter&) = delete;
        DropCounter(DropCounter&& other) noexcept
            : m_drops{ std::exchange(other.m_drops, nullptr) } {}
        DropCounter& operator=(const DropCounter&) = delete;
        DropCounter& operator=(DropCounter&& other) noexcept {
            std::swap(m_drops, other.m_drops);
            return *this;
        }
        ~DropCounter() {
            if (m_drops != nullptr) {
                ++*m_drops;
            }
        }

      private:
        std::int32_t* m_drops;
    };

    void test_command_buffer() {
        World w;
        auto e1 = w.create_entity();
        auto e2 = w.create_entity();
        auto e3 = w.create_entity();
        w.add<Position, Velocity>(e1, Position{ 1, 0, 0 }, Velocity{ 1, 1, 1 });
        w.add<Position>(e2, Position{ 2, 0, 0 });
        w.add<Position>(e3, Position{ 3, 0, 0 });

        CommandBuffer cbuf;
        check(cbuf.empty());
        check(cbuf.size() == 0);
        cbuf.flush(w);
        check(w.get_entity_count() == 3);

        w.get_system<Position>().for_each([&](Entity ent, Position&) {
            if (ent == e1) {
                cbuf.remove<Velocity>(ent);
            } else if (ent == e2) {
                cbuf.destroy(ent);
            } else {
                cbuf.add<Health, MoveOnly>(ent, Health{ 5.f }, MoveOnly{ std::make_unique<std::int32_t>(42) });
            }
        });
        check(cbuf.size() == 3);
        check(!cbuf.empty());
        check(w.has_component<Velocity>(e1));
        check(w.is_valid(e2));
        check(!w.has_component<Health>(e3));

        cbuf.flush(w);
        check(cbuf.empty());
        check(w.has_component<Position>(e1) && !w.has_component<Velocity>(e1));
        check(!w.is_valid(e2));
        check((w.has_component<Position, Health, MoveOnly>(e3)));
        check(w.get<Health>(e3).hp == 5.f);
        check(*w.get<MoveOnly>(e3).p == 42);

        constexpr std::int32_t N{ 64 };
        std::vector<Entity> spawned;
        for (std::int32_t i{}; i < N; ++i) {
            auto e = w.create_entity();
            spawned.push_back(e);
            cbuf.add<MoveOnly>(e, MoveOnly{ std::make_unique<std::int32_t>(i) });
        }
        auto e4 = w.create_entity();
        cbuf.add<Position>(e4, 7.f, 8.f, 9.f);
        CommandBuffer moved{ std::move(cbuf) };
        check(moved.size() == static_cast<std::size_t>(N) + 1U);
        moved.flush(w);
        check(moved.empty());
        bool all_applied{ true };
        for (std::int32_t i{}; i < N; ++i) {
            all_applied = all_applied && *w.get<MoveOnly>(spawned[static_cast<std::size_t>(i)]).p == i;
        }
        check(all_applied);
        check(w.get<Position>(e4).y == 8.f);
    }

    void test_command_buffer_argument_lifetime() {
        std::int32_t drops{};
        {
            World w;
            auto e1 = w.create_entity();
            auto e2 = w.create_entity();
            auto e3 = w.create_entity();
            {
                CommandBuffer cbuf;
                cbuf.add<DropCounter>(e1, DropCounter{ &drops });
                cbuf.add<DropCounter>(e2, &drops);
                CommandBuffer moved;
                moved = std::move(cbuf);
                check(drops == 0);
                moved.flush(w);
                check(drops == 0);
                check(w.has_component<DropCounter>(e1) && w.has_component<DropCounter>(e2));

                moved.add<DropCounter>(e3, DropCounter{ &drops });
            }
            check(drops == 1);
            check(!w.has_component<DropCounter>(e3));
            w.destroy(e1);
            check(drops == 2);
        }
        check(drops == 3);

        World w;
        auto e = w.create_entity();
        CommandBuffer cbuf;
        Position pos{ 1, 2, 3 };
        cbuf.add<Position>(e, pos);
        pos.x = 99;
        cbuf.flush(w);
        check(w.get<Position>(e).x == 1);
    }

    void test_clone() {
        World src;
        auto e1 = src.create_entity();
        auto e2 = src.create_entity();
        src.add<Position, Velocity>(e1, Position{ 1, 2, 3 }, Velocity{ 4, 5, 6 });
        src.add<Position>(e2, Position{ 7, 8, 9 });

        auto copy_opt = src.clone();
        check(copy_opt.has_value());
        World copy = std::move(copy_opt).value();
        check(copy.get_entity_count() == 2);
        check((copy.has_component<Position, Velocity>(e1)));
        check(copy.has_component<Position>(e2));
        check(!copy.has_component<Velocity>(e2));
        check(copy.get<Position>(e1).x == 1);
        check(copy.get<Velocity>(e1).dz == 6);
        check(copy.get<Position>(e2).x == 7);

        copy.get<Position>(e1).x = 999;
        check(src.get<Position>(e1).x == 1);
        src.get<Position>(e2).y = 555;
        check(copy.get<Position>(e2).y == 8);

        copy.add<Health>(e1, Health{ 1.f });
        check(copy.has_component<Health>(e1));
        check(!src.has_component<Health>(e1));

        auto e3 = src.create_entity();
        src.add<Position>(e3, Position{ 0, 0, 0 });
        check(src.get_entity_count() == 3);
        check(copy.get_entity_count() == 2);
    }

    void test_clone_non_copyable() {
        World w;
        auto e = w.create_entity();
        w.add<MoveOnly>(e, MoveOnly{ std::make_unique<std::int32_t>(7) });

        auto copy = w.clone();
        check(!copy.has_value());
        check(*w.get<MoveOnly>(e).p == 7);
    }

    using MoveOnlyVector = std::vector<std::unique_ptr<std::int32_t>>;
    static_assert(is_cloneable_v<Position> && is_cloneable_v<std::vector<Position>> &&
                      is_cloneable_v<std::map<std::int32_t, std::vector<std::int32_t>>> && is_cloneable_v<Tree>,
                  "copyable types are cloneable");
    static_assert(!is_cloneable_v<MoveOnly> && !is_cloneable_v<MoveOnlyVector> &&
                      !is_cloneable_v<std::map<std::int32_t, std::unique_ptr<std::int32_t>>> &&
                      !is_cloneable_v<std::optional<MoveOnlyVector>> && !is_cloneable_v<std::queue<MoveOnly>> &&
                      !is_cloneable_v<std::tuple<std::int32_t, MoveOnlyVector>> && !is_cloneable_v<Inventory>,
                  "containers of move-only elements are not cloneable");

    void test_clone_move_only_elements() {
        World w;
        auto e = w.create_entity();
        MoveOnlyVector items;
        items.push_back(std::make_unique<std::int32_t>(1));
        w.add<MoveOnlyVector>(e, std::move(items));
        w.add<std::map<std::int32_t, std::unique_ptr<std::int32_t>>>(e);
        w.add<std::optional<MoveOnlyVector>>(e);
        Inventory inventory;
        inventory.items.push_back(std::make_unique<std::int32_t>(2));
        w.add<Inventory>(e, std::move(inventory));
        check(*w.get<MoveOnlyVector>(e)[0] == 1);
        check(*w.get<Inventory>(e).items[0] == 2);
        check(!w.clone().has_value());

        World trees;
        auto t = trees.create_entity();
        trees.add<Tree>(t, Tree{ 1, { Tree{ 2, {} } } });
        auto copy = trees.clone();
        check(copy.has_value() && copy->get<Tree>(t).children.at(0).value == 2);
    }

    using MoveOnlySpan = std::span<std::unique_ptr<std::int32_t>>;
    static_assert(is_cloneable_v<PropertyTree> && is_cloneable_v<Forest> && is_cloneable_v<std::vector<Forest>> &&
                      is_cloneable_v<MoveOnlySpan>,
                  "self-containing types and views are cloneable");
    static_assert(!is_cloneable_v<OwnerTree> && !is_cloneable_v<std::vector<Inventory>> &&
                      !is_cloneable_v<std::pair<std::int32_t, Inventory>>,
                  "move-only elements are found inside self-containing types, and specialisations apply to elements");

    void test_clone_self_containing_types_and_views() {
        World w;
        auto e = w.create_entity();
        PropertyTree config;
        config.children["window"].data = "800x600";
        w.add<PropertyTree>(e, config);
        Forest forest;
        forest["oak"].emplace_back().value = 3;
        w.add<Forest>(e, forest);
        auto items = std::make_unique<std::unique_ptr<std::int32_t>[]>(2);
        items[1] = std::make_unique<std::int32_t>(5);
        w.add<MoveOnlySpan>(e, MoveOnlySpan{ items.get(), 2U });
        auto copy = w.clone();
        check(copy.has_value() && copy->get<PropertyTree>(e).children.at("window").data == "800x600" &&
              copy->get<Forest>(e).at("oak").at(0).value == 3 && *copy->get<MoveOnlySpan>(e)[1] == 5);
    }

    void test_sparse_set_stress() {
        constexpr std::uint32_t N{ 4000U };
        SparseSet<std::int32_t, 64U> set;
        for (std::uint32_t id{}; id < N; ++id) {
            set.add(id, static_cast<std::int32_t>(id) * 2);
        }
        check(set.size() == N);

        bool all_present{ true };
        for (std::uint32_t id{}; id < N; ++id) {
            all_present = all_present && set.contains(id) && set.get(id) == static_cast<std::int32_t>(id) * 2;
        }
        check(all_present);

        for (std::uint32_t id{ 1U }; id < N; id += 2U) {
            set.remove(id);
        }
        check(set.size() == N / 2U);

        bool survivors_ok{ true };
        bool removed_gone{ true };
        for (std::uint32_t id{}; id < N; ++id) {
            if (id % 2U == 0U) {
                survivors_ok = survivors_ok && set.contains(id) && set.get(id) == static_cast<std::int32_t>(id) * 2;
            } else {
                removed_gone = removed_gone && !set.contains(id);
            }
        }
        check(survivors_ok);
        check(removed_gone);

        for (std::uint32_t id{ 1U }; id < N; id += 2U) {
            set.add(id, static_cast<std::int32_t>(id) * 2);
        }
        check(set.size() == N);
    }

    void test_stress_integrity() {
        constexpr std::int32_t N{ 4000 };
        World w;

        struct Entry {
            Entity entity;
            std::int32_t id;
            bool alive;
            bool has_velocity;
            bool has_health;
        };
        std::vector<Entry> entries;
        entries.reserve(N);

        for (std::int32_t i{}; i < N; ++i) {
            Entity e = w.create_entity();
            w.add<Position>(e, Position{ float(i), float(i) + 1, float(i) + 2 });
            const bool has_velocity{ i % 2 == 0 };
            const bool has_health{ i % 3 == 0 };
            if (has_velocity) {
                w.add<Velocity>(e, Velocity{ float(i), 0, 0 });
            }
            if (has_health) {
                w.add<Health>(e, Health{ float(i) });
            }
            if (i % 5 == 0) {
                w.add<Tag>(e, Tag{ static_cast<std::uint32_t>(i) });
            }
            entries.push_back(Entry{ e, i, true, has_velocity, has_health });
        }
        check(w.get_entity_count() == static_cast<std::size_t>(N));

        const auto verify = [&entries](World& world) {
            bool ok{ true };
            for (const Entry& en : entries) {
                if (!en.alive) {
                    continue;
                }
                ok = ok && world.get<Position>(en.entity).x == float(en.id);
                if (en.has_velocity) {
                    ok = ok && world.get<Velocity>(en.entity).dx == float(en.id);
                }
                if (en.has_health) {
                    ok = ok && world.get<Health>(en.entity).hp == float(en.id);
                }
            }
            return ok;
        };
        check(verify(w));

        for (Entry& en : entries) {
            if (en.has_velocity) {
                w.remove<Velocity>(en.entity);
                en.has_velocity = false;
            }
        }
        check(verify(w));

        std::int32_t destroyed{};
        for (Entry& en : entries) {
            if (en.id % 7 == 0) {
                w.destroy(en.entity);
                en.alive = false;
                ++destroyed;
            }
        }
        check(w.get_entity_count() == static_cast<std::size_t>(N - destroyed));
        check(verify(w));

        auto clone_opt = w.clone();
        check(clone_opt.has_value());
        World clone = std::move(clone_opt).value();
        check(clone.get_entity_count() == w.get_entity_count());
        check(verify(clone));

        for (const Entry& en : entries) {
            if (en.alive) {
                clone.get<Position>(en.entity).x = -1.f;
            }
        }
        check(verify(w));

        bool clone_mutated{ true };
        for (const Entry& en : entries) {
            if (en.alive) {
                clone_mutated = clone_mutated && clone.get<Position>(en.entity).x == -1.f;
            }
        }
        check(clone_mutated);
    }

} // namespace

int main() {
    test_sparse_set();
    test_add_get_arity();
    test_add_argument_aliases_world();
    test_add_throwing_constructor();
    test_add_allocation_failure();
    test_has_component_is_valid();
    test_remove();
    test_destroy_recycle();
    test_reset_and_clear();
    test_move_semantics();
    test_moved_from_world_is_usable();
    test_systems_and_for_each();
    test_world_for_each_stable_handle();
    test_multi_archetype();
    test_system_multi_add_and_refresh();
    test_system_lifetime();
    test_for_each_skips_entities_moved_into_empty_archetype();
    test_for_each_nested_same_system();
    test_command_buffer();
    test_command_buffer_argument_lifetime();
    test_clone();
    test_clone_non_copyable();
    test_clone_move_only_elements();
    test_clone_self_containing_types_and_views();
    test_sparse_set_stress();
    test_stress_integrity();

    if (s_fail != 0) {
        std::println("FAILED: {} of {} checks", s_fail, s_checks);
        return 1;
    }
    std::println("OK: all {} checks passed", s_checks);
    return 0;
}
