// A Box ends its object only where the type of the object is complete.
// The end of a Box of a type that is only declared is refused.

#include <foundation/core/Ref.h>

#include <utility>

struct Engine;

[[maybe_unused]] static void drop(::foundation::core::Box<Engine>&& engine) noexcept {
    ::foundation::core::Box<Engine> const taken{std::move(engine)};
}

int main() { return 0; }
