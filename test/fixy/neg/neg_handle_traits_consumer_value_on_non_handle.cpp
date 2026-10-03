// consumer_handle_value_t recovers the payload a consumer handle pops.
// It is constrained on is_consumer_handle_v, so asking it about a type
// with no try_pop is refused at the alias.  Without the constraint the
// alias would name the primary shape's `payload`, which is void, and a
// stage over such a handle would silently carry a void element type.
//
// The probe is the hybrid rather than a plain int, which is the sharper
// case: it HAS a try_pop of exactly the right signature, and is refused
// only because it also has a try_push, so nothing decides its pole.  A
// plain int would be refused by the shape match alone and would not
// witness the exclusion.
//
// VIOLATION: a TU asks a type with both channel members for its consumer
// payload.
//
// Expected diagnostic: the is_consumer_handle_v constraint on
// consumer_handle_value_t fails.

#include <fixy/concurrent/HandleTraits.h>

#include <optional>

namespace {

struct both_poles {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

// The requirement is the one use of the alias.  An alias declaration
// gives a second error at the declaration, and a second use gives a
// second error at the use.  A fixture that rejects at two places cannot
// say which rejection its regexes witnessed.
static_assert(requires { typename ::fixy::concurrent::consumer_handle_value_t<both_poles>; });

}  // namespace

int main() { return 0; }
