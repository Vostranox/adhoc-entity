# adhoc-entity

An archetype-based entity-component system in a single header.

I originally wrote this as the ECS for a student game-engine project.
I've since extracted it from the engine, fixed some bugs, added features, and moved it to C++23.

This is an archetype-based ECS. Entities with the same set of components share storage,
laid out as one contiguous array per component type, so a system sweeps them in a tight,
cache-friendly loop with no indirection. Adding or removing a component moves an entity
to a different archetype, which means copying its components out of one set of arrays and
into another.

## Usage

```cpp
#include <adh/entity.hpp>

struct Position { float x, y; };
struct Velocity { float dx, dy; };

adh::ecs::World world;

auto e = world.create_entity();
world.add<Position, Velocity>(e, Position{0, 0}, Velocity{1, 1});

// Run a system over everything that has both components
world.get_system<Position, Velocity>().for_each([](Position& p, Velocity& v) {
    p.x += v.dx;
    p.y += v.dy;
});

world.remove<Velocity>(e);
world.destroy(e);
```

### Deferred structural changes

Inside a `for_each` callback you may read and write the components you were
given, destroy the entity currently being visited, or remove from it a
component the system iterates. Do this last, the references you were given
are stale afterwards. Every other structural change, destroying other
entities, adding components to any entity, or removing components the system
does not iterate must be deferred through a `CommandBuffer` and flushed
after the loop.

```cpp
adh::ecs::CommandBuffer cbuf;

world.get_system<Position, Health>().for_each([&](adh::ecs::Entity e, Position& pos, Health& hp) {
    if (hp.value <= 0.0f) {
        cbuf.destroy(e);
        auto new_entity = world.create_entity();
        cbuf.add<Position>(new_entity, pos); // applied by flush, after the loop
    }
});

cbuf.flush(world);
```

## Tests

The `test/` folder has a standalone test that exercises the whole public API. From the project root:

```sh
clang++ -Wall -Wextra -std=c++23 -g -O0 -fsanitize=address,undefined -Iinclude test/test.cpp -o run_tests && ./run_tests
```
