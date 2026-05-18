// fixy-A2-015 sentinel TU.
//
// Witnesses that <crucible/sessions/Sessions.h> — the umbrella header
// — exposes the framework surface a production caller needs.  Three
// claims, one per newly-added include:
//
//   1. SessionMint.h    — `mint_permissioned_session<End>(ctx, &res)`
//                          resolves (CLAUDE.md §XXI Tier-2 ctx-bound
//                          mint hub).
//   2. SessionRowExtraction.h
//                       — `payload_row_t<Computation<Row<Bg>, int>>`
//                          is namable as `Row<Bg>` (every ctx-admission
//                          walk reads this trait).
//   3. MpmcChannelSession.h
//                       — `mpmc_channel_session::ProducerProto<int>`
//                          and the MpmcChannelSessionSurface concept
//                          are reachable (FOUND-A24 MPMC substrate
//                          facade).
//
// Pre-fix: fails to compile (any single missing include reddens this
// TU).  Post-fix: compiles cleanly.  Together with the per-header
// production tests, this guarantees the umbrella's "include me, get
// the framework" contract for the most-commonly-used surfaces.
//
// Discipline notes (CLAUDE.md):
//   - Sentinel pattern, no GoogleTest, no main() body — TU success
//     IS the test, registered via add_executable + add_test.
//   - All claims compile-time only (static_assert).
//   - No transitive third-party header — only the umbrella.

#include <crucible/sessions/Sessions.h>

#include <type_traits>

namespace {

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

// ═════════════════════════════════════════════════════════════════════
// Claim 1 — mint_permissioned_session<End>(ctx, &res) resolves.
// ═════════════════════════════════════════════════════════════════════
//
// Use `decltype` on the mint expression — this proves the factory's
// requires-clause holds (CtxFitsPermissionedProtocol<End, HotFgCtx,
// EmptyPermSet>) and that the returned PSH is well-formed.  No runtime
// invocation needed; the type-system check is sufficient for the
// umbrella-reachability claim.

struct ProbeResource { int value = 0; };

using EmptyMintFromUmbrella = decltype(proto::mint_permissioned_session<proto::End>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<ProbeResource>()));

static_assert(std::is_same_v<typename EmptyMintFromUmbrella::protocol,
                             proto::End>,
              "mint_permissioned_session<End>(ctx, res) must produce "
              "a PSH whose ::protocol == End.");

static_assert(std::is_same_v<typename EmptyMintFromUmbrella::perm_set,
                             proto::EmptyPermSet>,
              "mint_permissioned_session<End>(ctx, res) without perms "
              "must produce a PSH with EmptyPermSet.");

// ═════════════════════════════════════════════════════════════════════
// Claim 2 — payload_row_t<Computation<Row<Bg>, int>> is namable.
// ═════════════════════════════════════════════════════════════════════
//
// This is the canonical row carrier — sending a Computation<R, T> means
// "this payload was produced under row R".  The trait projects the row
// for ctx-admission gates.  Witness: name the projected type and assert
// it equals Row<Bg>.

using BgComputation = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using ProjectedRow  = proto::payload_row_t<BgComputation>;

static_assert(std::is_same_v<ProjectedRow, eff::Row<eff::Effect::Bg>>,
              "payload_row_t<Computation<Row<Bg>, int>> must project "
              "to Row<Bg>.  If this fails the umbrella missed "
              "SessionRowExtraction.h.");

// ═════════════════════════════════════════════════════════════════════
// Claim 3 — MpmcChannelSession surface reachable.
// ═════════════════════════════════════════════════════════════════════
//
// Three witnesses:
//   3a. ProducerProto<int> and ConsumerProto<int> aliases are namable
//       (the protocol shapes used by every MPMC mint factory).
//   3b. The protocols have the documented Loop<Send<...>>/Loop<Recv<...>>
//       shape — guards against rename drift.
//   3c. The MpmcChannelSessionSurface concept rejects `int` (a non-channel
//       type) — proves the concept is declared and operative.

using ProducerInt = proto::mpmc_channel_session::ProducerProto<int>;
using ConsumerInt = proto::mpmc_channel_session::ConsumerProto<int>;

static_assert(std::is_same_v<
    ProducerInt,
    proto::Loop<proto::Send<int, proto::Continue>>>,
    "mpmc_channel_session::ProducerProto<T> must be the canonical "
    "infinite Loop<Send<T, Continue>>.");

static_assert(std::is_same_v<
    ConsumerInt,
    proto::Loop<proto::Recv<int, proto::Continue>>>,
    "mpmc_channel_session::ConsumerProto<T> must be the canonical "
    "infinite Loop<Recv<T, Continue>>.");

static_assert(!proto::mpmc_channel_session::MpmcChannelSessionSurface<int>,
              "MpmcChannelSessionSurface must reject `int` — `int` has "
              "no ProducerHandle / ConsumerHandle / etc. nested types.");

}  // namespace

int main() { return 0; }
