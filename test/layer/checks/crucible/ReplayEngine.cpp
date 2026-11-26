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
// apart from the copy and the move.  It holds no borrow, so a body of the
// compiled scope gives it back.
static_assert(sizeof(ReplayStep) == 8 && std::is_trivially_copyable_v<ReplayStep>);
static_assert(!std::is_default_constructible_v<ReplayStep>);
static_assert(!std::is_constructible_v<ReplayStep, ReplayStatus, OpIndex>);
static_assert(::foundation::core::HoldsNoBorrow<ReplayStep>);

// The proof of a matched op: only the engine makes one.  It is a borrow of
// the region, so no body of a compiled scope gives it back.  Its copy is
// trivial, so its Option is a plain Option that a loop reads.
static_assert(!std::is_default_constructible_v<MatchedOp>);
static_assert(!std::is_constructible_v<MatchedOp, ::fixy::Ref<const TraceEntry>>);
static_assert(std::is_trivially_copy_constructible_v<MatchedOp> && sizeof(MatchedOp) == 8);
static_assert(!::foundation::core::HoldsNoBorrow<MatchedOp>);
static_assert(requires(::fixy::Option<MatchedOp> const& proof) {
    proof.begin();
    proof.end();
});

}  // namespace crucible
