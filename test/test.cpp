#include <cstdint>
#include <memory>
#include <print>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <adh/entity.hpp>

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
    static_assert(std::is_copy_constructible_v<SparseSetPage<64U>> && std::is_move_constructible_v<SparseSetPage<64U>>,
                  "SparseSetPage is vector-backed: copyable and movable");

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
    test_has_component_is_valid();
    test_remove();
    test_destroy_recycle();
    test_reset_and_clear();
    test_move_semantics();
    test_systems_and_for_each();
    test_multi_archetype();
    test_system_multi_add_and_refresh();
    test_system_lifetime();
    test_clone();
    test_clone_non_copyable();
    test_sparse_set_stress();
    test_stress_integrity();

    if (s_fail != 0) {
        std::println("FAILED: {} of {} checks", s_fail, s_checks);
        return 1;
    }
    std::println("OK: all {} checks passed", s_checks);
    return 0;
}
