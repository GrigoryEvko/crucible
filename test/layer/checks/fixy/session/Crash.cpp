// The compile-time checks of fixy/session/Crash.h.

#include <fixy/session/Crash.h>

// The traits of the armed cells of the header are aliases, and the roster
// walk of foundation/contracts/Armed.h finds class templates only.  These
// assertions read their cells.
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_payload>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_branch>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_reliable_set>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_well_formed>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_covered>);

// ── The walk, checked ────────────────────────────────────────────────
//
// Each answer of the walk, against a protocol that obeys and one that
// breaks the rule.  The keyed payloads are defined in
// fixy/session/Projection.h, so test/fixy/test_session_crash_stop.cpp
// checks the role of a keyed step.

namespace fixy::session::detail::crash::walk_self_test {
using armed_witness::Alice;
using armed_witness::Bob;
using armed_witness::Guarded;
using armed_witness::Msg;
template <typename Proto, typename Peer, typename Reliable>
inline constexpr verdict answers = verdict_of(^^Proto, ^^Peer, ^^Reliable);

// The watch reads the sender of an Offer.
static_assert(answers<Guarded, Alice, ReliableSet<>>.is_watched);
static_assert(!answers<Offer<Sender<Bob>, Recv<Msg, End>, Recv<Crash<Bob>, End>>, Alice, ReliableSet<>>.is_watched);
static_assert(answers<Offer<Sender<Bob>, Recv<Msg, End>>, Alice, ReliableSet<Bob>>.is_watched);

// A vendor pin passes each answer to the protocol it pins.
using Pinned = VendorPinned<VendorBackend::NV, Guarded>;
static_assert(answers<Pinned, Alice, ReliableSet<>>.is_structured && answers<Pinned, Alice, ReliableSet<>>.is_covered
              && answers<Pinned, Alice, ReliableSet<>>.is_watched
              && answers<Pinned, Alice, ReliableSet<>>.is_delegation_free);

// A step whose payload is a protocol, and a combinator that is not plain,
// fail every answer.
static_assert(!answers<Delegate<End, End>, Alice, ReliableSet<>>.is_delegation_free);
static_assert(!answers<Delegate<End, End>, Alice, ReliableSet<>>.is_structured);
static_assert(!answers<Commit<End>, Alice, ReliableSet<>>.is_structured);
static_assert(!answers<int, Alice, ReliableSet<>>.is_structured);
}  // namespace fixy::session::detail::crash::walk_self_test
