// The compile-time checks of crucible/ReplayEngine.h.

#include <crucible/ReplayEngine.h>

namespace crucible {

// The guard values, the cyclic cursor, the matched op and the slot table take
// the first 64 bytes, and the count of the ops and its pad take the last 8.
static_assert(sizeof(ReplayEngine) == 72,
              "ReplayEngine: 16 guard + 24 cursor + 8 matched + 16 table + 4 count + 4 pad = 72");

// Only the context builds an engine: no code outside it constructs one, and no
// copy of bytes makes one.
static_assert(!std::is_default_constructible_v<ReplayEngine>);
static_assert(!std::is_copy_constructible_v<ReplayEngine> && !std::is_move_constructible_v<ReplayEngine>);
static_assert(!std::is_trivially_copyable_v<ReplayEngine> && !std::is_implicit_lifetime_v<ReplayEngine>);

// A view must not outlive the frame that minted it, so storing one in a field
// would let it escape.
static_assert(::fixy::no_scoped_view_field_check<ReplayEngine>());

// A step is eight trivially copyable bytes, so an advance gives it back in
// one register.  Only the engine makes a step: it has no public constructor
// apart from the copy and the move.
static_assert(sizeof(ReplayStep) == 8 && std::is_trivially_copyable_v<ReplayStep>);
static_assert(!std::is_default_constructible_v<ReplayStep>);
static_assert(!std::is_constructible_v<ReplayStep, ReplayStatus, OpIndex>);

}  // namespace crucible
