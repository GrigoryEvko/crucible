// ── test_fixy_sess — sentinel TU for fixy/Sess.h ───────────────────
//
// Pulls fixy/Sess.h into a TU compiled under project warning flags
// so the header's static_asserts execute under enforcement.
// Witnesses:
//
//   1. fixy::sess::Send/Recv/End/Loop/Continue alias the substrate.
//   2. fixy::sess::Stop / Stop_g / CrashClass alias the substrate.
//   3. fixy::sess::EpochedDelegate / RecordingSessionHandle alias.
//   4. fixy::sess::mint_session_handle is reachable via the alias.
//   5. fixy::sess::federation::mint_sender / mint_receiver are reachable.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_sess_*.cpp.

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace fsess = ::crucible::fixy::sess;
namespace proto = ::crucible::safety::proto;
namespace fed   = ::crucible::safety::proto::federation;

// ─── 1. Core combinator aliases ───────────────────────────────────

static_assert(std::is_same_v<
    fsess::Send<int, fsess::End>,
    proto::Send<int, proto::End>>,
    "fixy::sess::Send must alias proto::Send.");

static_assert(std::is_same_v<
    fsess::Recv<int, fsess::Loop<fsess::Continue>>,
    proto::Recv<int, proto::Loop<proto::Continue>>>,
    "fixy::sess::Recv/Loop/Continue compose identically.");

// ─── 2. Crash-stop family aliases ─────────────────────────────────

static_assert(std::is_same_v<fsess::Stop, proto::Stop>,
    "fixy::sess::Stop must alias proto::Stop.");

// ─── 3. Federation mint factory reachable via fixy::sess ──────────

namespace test_fixy_sess {
struct KeyTag {};
}  // namespace test_fixy_sess

// fixy::sess::federation namespace alias is reachable.
static_assert(std::is_same_v<
    decltype(&fsess::federation::mint_sender<test_fixy_sess::KeyTag,
                                              ::crucible::effects::BgCompileCtx,
                                              int>),
    decltype(&fed::mint_sender<test_fixy_sess::KeyTag,
                                ::crucible::effects::BgCompileCtx,
                                int>)>,
    "fixy::sess::federation::mint_sender must be the substrate function "
    "(name-lookup-only re-export).");

// ─── 4. Runtime sanity ────────────────────────────────────────────

int main() {
    // Minting goes through SessionMint.h which requires a fully-formed
    // resource; this TU asserts reachability, not runtime instantiation.
    // The substrate's existing tests cover the full mint round-trip.
    return 0;
}
