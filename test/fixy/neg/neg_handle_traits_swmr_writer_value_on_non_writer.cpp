// swmr_writer_value_t recovers the payload a single-writer handle
// publishes, and is constrained on is_swmr_writer_v.  The single-writer
// pair is a separate exclusion from the queue pair — publish against
// load rather than try_push against try_pop — so it needs its own
// fixture: a bug that dropped the has_load check would leave the
// consumer fixture green.
//
// The probe again has publish with exactly the right signature, and is
// refused for holding load as well.
//
// VIOLATION: a TU asks a type with both single-writer members for its
// published payload.
//
// Expected diagnostic: the is_swmr_writer_v constraint on
// swmr_writer_value_t fails.

#include <fixy/concurrent/HandleTraits.h>

namespace {

struct both_swmr_poles {
    void publish(int const&) noexcept {}
    [[nodiscard]] int load() const noexcept { return 0; }
};

// Declared and never used, for the reason given in the sibling fixture.
using forged = ::fixy::concurrent::swmr_writer_value_t<both_swmr_poles>;

}  // namespace

int main() { return 0; }
