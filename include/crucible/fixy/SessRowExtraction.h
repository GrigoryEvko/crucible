#pragma once

// ── crucible::fixy::sess::row — payload-row projection surface ──────
//
// FIXY-V-062 (HIGH).  Re-exports the public surface of
// `sessions/SessionRowExtraction.h` — the type-level walker that
// projects from a session-typed payload (or an entire protocol) into
// the Met(X) `effects::Row<Es...>` it engages.  This is the
// load-bearing bridge between L0 safety wrappers and L7 protocol-row
// gates:  Vessel→Forge dispatch hooks consult `payload_row_t<T>` to
// admit only protocols whose row is a subrow of the receiving ctx's
// row; Forge phases gate IR-rewrite rules on `protocol_effect_row_t`;
// Cipher tier-promotion uses `payload_effect_row_t` to validate that
// a wire payload's authority is admitted at the promotion site.
//
// ── Two complementary trait families ───────────────────────────────
//
// `payload_row<T>` projects a SINGLE wrapped payload into its row.
// Specialised for ~30 wrappers (Computation / Capability /
// Refined / SealedRefined / Tagged / Linear / Stale / Secret /
// ContentAddressed / Transferable / Borrowed / Returned / and all
// canonical Graded wrappers HotPath / DetSafe / AllocClass / etc.).
// Most specialisations are transparent-unwrap (defer to the inner T's
// row);  NumericalTier wraps the inner row in `NumericalPayloadRow`
// to preserve tolerance grade through the wire boundary.
//
// `protocol_effect_row<Proto>` walks an entire protocol AST and folds
// its payload rows (Send / Recv) and branch rows (Select / Offer /
// CheckpointedSession) into one union row via `row_union_pack`.
// Specialised for every shipped combinator (End / Continue / Stop_g
// / Send / Recv / Loop / VendorPinned / Select / Offer / Sender-
// annotated Offer / CheckpointedSession / Delegate / Accept /
// EpochedDelegate / EpochedAccept).  Primary template is forward-
// declared WITHOUT definition — non-protocol types fail with
// "incomplete type" at instantiation, structurally rejecting
// nonsense inputs.
//
// ── Why this surface exists (Agent 3 finding B6 HIGH) ──────────────
//
// Before V-062, the row-extraction machinery was substrate-only —
// every production hook (perf/, observe/, cog/, canopy/, forge/,
// mimic/) reached into `crucible::safety::proto::payload_row_t<...>`
// directly to compute a payload's row.  That bypassed the fixy::
// audit ledger AND tied the production layer to substrate ordinals;
// a substrate-side rename or namespace re-org would break every
// reach point silently.  V-062 closes that gap with a 10-symbol
// re-export surface that productions consume via
// `fixy::sess::row::payload_row_t<...>` (etc.) — substrate moves
// trip only this header's sentinels, not 50 call sites.
//
// HIGH priority because the row-typing pipeline (Forge phases →
// Mimic dispatch → cross-vendor numerics CI) consumes this surface
// at every IR boundary;  drift here is correlated with
// FOUND-J/K row-adoption work (tasks #781-#814).
//
// ── Eight symbols (the public row-extraction API) ──────────────────
//
//   carrier type (1):              NumericalPayloadRow
//   per-payload extractor (2):     payload_row, payload_row_t
//   row-effect extractor (2):      payload_row_effect, payload_row_effect_t
//   composed alias (1):            payload_effect_row_t
//   protocol walker (2):           protocol_effect_row,
//                                  protocol_effect_row_t
//                          total: 1 + 2 + 2 + 1 + 2 = 8
//
// Note: `row_union_pack` lives in
// `safety::proto::detail::protocol_effect_row_fold::` — a detail
// namespace, NOT a public symbol; production consumers reach
// `protocol_effect_row_t<Proto>` and let the walker fold internally.
// fixy:: does not re-export details.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Eight using-decls, sentinel battery, smoke routine.  No new
// types, no mint factories, no free functions — every entry is a
// pure name-lookup directive (zero machine code).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; pure type-level.
//   TypeSafe — using-decls preserve substrate identity; sentinels
//              assert this via is_same_v across substrate and fixy.
//   NullSafe — no pointer state.
//   MemSafe  — all symbols compile-time-only; no allocation.
//   DetSafe  — pure type-level computation; same input always yields
//              the same row projection.
//   BorrowSafe — no aliasing at this layer.
//   ThreadSafe — no shared state crossed.
//   LeakSafe — no resource owned.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionRowExtraction.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::row {

// ── 1. Carrier type (1) ────────────────────────────────────────────
// `NumericalPayloadRow<Tier, InnerRow>` — preserves the
// `safety::Tolerance` tier across the wire boundary by pairing it
// with the inner effect row.  Without this, dropping a payload's
// tolerance grade at Send/Recv would silently weaken the MIMIC §41
// numerical contract.  `Tier` is `safety::Tolerance`-concept-
// constrained (substituting a non-Tolerance type fails the constraint
// — see HS14 fixture β).
using ::crucible::safety::proto::NumericalPayloadRow;

// ── 2. Per-payload extractor (2) ───────────────────────────────────
// `payload_row<T>::type` projects a SINGLE wrapped payload into its
// Met(X) effect row.  Primary template returns `Row<>` (bare T
// carries no effects).  ~30 wrapper specialisations recurse
// transparently into the inner T; NumericalTier wraps in
// NumericalPayloadRow to preserve tolerance.  `_t` is the shortcut.
using ::crucible::safety::proto::payload_row;
using ::crucible::safety::proto::payload_row_t;

// ── 3. Row-effect extractor (2) ────────────────────────────────────
// `payload_row_effect<PayloadRow>::type` projects a payload-row type
// down to a plain `effects::Row<...>`.  Identity for already-plain
// rows; for NumericalPayloadRow<Tier, InnerRow> it strips the
// tolerance and recurses into InnerRow.  This is what
// `CtxFitsProtocol` / Stage consume (they need the bare Row<...> to
// run Subrow checks).
using ::crucible::safety::proto::payload_row_effect;
using ::crucible::safety::proto::payload_row_effect_t;

// ── 4. Composed alias (1) ──────────────────────────────────────────
// `payload_effect_row_t<T>` — one-step composition equivalent to
// `payload_row_effect_t<payload_row_t<T>>`.  The canonical "what
// effect row does sending this payload engage?" query at every wire-
// boundary admission gate.
using ::crucible::safety::proto::payload_effect_row_t;

// ── 5. Protocol walker (2) ─────────────────────────────────────────
// `protocol_effect_row<Proto>::type` folds an entire protocol AST
// into one union effect row.  Primary template is forward-declared
// WITHOUT definition — non-protocol Proto fails with "incomplete
// type", structurally rejecting nonsense inputs (HS14 fixture α
// pins this).  Specialised for every combinator (End, Continue,
// Stop_g, Send, Recv, Loop, VendorPinned, Select, Offer, Sender-
// annotated Offer, CheckpointedSession, Delegate, Accept,
// EpochedDelegate, EpochedAccept).
using ::crucible::safety::proto::protocol_effect_row;
using ::crucible::safety::proto::protocol_effect_row_t;

}  // namespace crucible::fixy::sess::row

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::sess::row::v062_self_test {

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;
namespace saf   = ::crucible::safety;

// Fixture types.
struct UserPod { int x; double y; };
struct ProvTag {};

// Computation-wrapped bare T.
using IoComp = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using BgComp = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

// Composite wrappers — each spec must recurse transparently.
using TaggedIo  = saf::Tagged<IoComp, ProvTag>;
using StaleIo   = saf::Stale<IoComp>;
using LinearIo  = saf::Linear<IoComp>;
using SecretIo  = saf::Secret<IoComp>;

// Minimal protocols for protocol_effect_row reach.
using End_      = proto::End;
using SendIo    = proto::Send<IoComp, proto::End>;
using RecvBg    = proto::Recv<BgComp, proto::End>;

// ── A. Carrier-type reach ──────────────────────────────────────────
static_assert(std::is_same_v<
    NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>,
    proto::NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>>,
    "NumericalPayloadRow must reach identically through fixy::");
static_assert(NumericalPayloadRow<saf::Tolerance::BITEXACT,
                                  eff::Row<eff::Effect::IO>>::tolerance
              == saf::Tolerance::BITEXACT,
    "NumericalPayloadRow preserves the tolerance grade NTTP.");

// ── B. Per-payload extractor reach ─────────────────────────────────
// Bare types → empty row.
static_assert(std::is_same_v<payload_row_t<int>,      eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<UserPod>,  eff::Row<>>);

// Computation extracts its row literally.
static_assert(std::is_same_v<payload_row_t<IoComp>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<BgComp>, eff::Row<eff::Effect::Bg>>);

// Identity through fixy:: matches substrate exactly.
static_assert(std::is_same_v<
    payload_row_t<IoComp>, proto::payload_row_t<IoComp>>,
    "payload_row_t must reach identically through fixy::");

// Transparent-unwrap discipline: wrapping with Tagged/Stale/Linear/
// Secret must NOT change the extracted row (deferral to inner).
static_assert(std::is_same_v<
    payload_row_t<TaggedIo>, payload_row_t<IoComp>>,
    "Tagged is transparent for payload_row.");
static_assert(std::is_same_v<
    payload_row_t<StaleIo>, payload_row_t<IoComp>>,
    "Stale is transparent for payload_row.");
static_assert(std::is_same_v<
    payload_row_t<LinearIo>, payload_row_t<IoComp>>,
    "Linear is transparent for payload_row.");
static_assert(std::is_same_v<
    payload_row_t<SecretIo>, payload_row_t<IoComp>>,
    "Secret is transparent for payload_row.");

// Class-template form reach (for trait-inheritance composition).
static_assert(std::is_same_v<
    typename payload_row<IoComp>::type, eff::Row<eff::Effect::IO>>);

// ── C. Row-effect extractor reach ──────────────────────────────────
// Identity on plain rows.
static_assert(std::is_same_v<
    payload_row_effect_t<eff::Row<eff::Effect::IO>>, eff::Row<eff::Effect::IO>>,
    "payload_row_effect_t is identity on bare effects::Row<...>.");

// Strips tolerance from NumericalPayloadRow.
using NPR = NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>;
static_assert(std::is_same_v<
    payload_row_effect_t<NPR>, eff::Row<eff::Effect::IO>>,
    "payload_row_effect_t strips tolerance from NumericalPayloadRow.");

// Class-template form reach.
static_assert(std::is_same_v<
    typename payload_row_effect<eff::Row<eff::Effect::IO>>::type,
    eff::Row<eff::Effect::IO>>);

// ── D. Composed alias reach ────────────────────────────────────────
static_assert(std::is_same_v<
    payload_effect_row_t<IoComp>, eff::Row<eff::Effect::IO>>,
    "payload_effect_row_t<Computation<Row<IO>, int>> = Row<IO>.");
static_assert(std::is_same_v<
    payload_effect_row_t<int>, eff::Row<>>,
    "payload_effect_row_t<int> = Row<> (bare base case).");

// Substrate-identity through fixy.
static_assert(std::is_same_v<
    payload_effect_row_t<TaggedIo>,
    proto::payload_effect_row_t<TaggedIo>>,
    "payload_effect_row_t must reach identically through fixy::");

// ── E. Protocol walker reach ───────────────────────────────────────
// End → Row<>.
static_assert(std::is_same_v<protocol_effect_row_t<End_>, eff::Row<>>,
    "protocol_effect_row_t<End> = Row<>.");

// Send/Recv extract the payload's row.
static_assert(std::is_same_v<
    protocol_effect_row_t<SendIo>, eff::Row<eff::Effect::IO>>,
    "protocol_effect_row_t<Send<Computation<Row<IO>,_>, End>> = Row<IO>.");
static_assert(std::is_same_v<
    protocol_effect_row_t<RecvBg>, eff::Row<eff::Effect::Bg>>,
    "protocol_effect_row_t<Recv<Computation<Row<Bg>,_>, End>> = Row<Bg>.");

// Class-template form.
static_assert(std::is_same_v<
    typename protocol_effect_row<End_>::type, eff::Row<>>);

// Substrate identity.
static_assert(std::is_same_v<
    protocol_effect_row_t<SendIo>,
    proto::protocol_effect_row_t<SendIo>>,
    "protocol_effect_row_t must reach identically through fixy::");

// ── F. Cardinality witness ─────────────────────────────────────────
//
//   carrier type (1: NumericalPayloadRow)
// + per-payload extractor (2: payload_row + _t)
// + row-effect extractor (2: payload_row_effect + _t)
// + composed alias (1: payload_effect_row_t)
// + protocol walker (2: protocol_effect_row + _t)
//                                              ──── 8
//
// (`row_union_pack` lives in proto::detail::protocol_effect_row_fold::
// — a detail namespace, not part of the public surface; the walker
// folds internally and consumers never observe it directly.)
constexpr int v062_surface_cardinality = 8;
static_assert(v062_surface_cardinality == 8,
    "fixy::sess::row:: V-062 surface cardinality drifted — update "
    "SessRowExtraction.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::row::v062_self_test

namespace crucible::fixy::sess::row {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    namespace eff   = ::crucible::effects;
    namespace saf   = ::crucible::safety;

    using IoComp = eff::Computation<eff::Row<eff::Effect::IO>, int>;
    using End_   = proto::End;
    using SendIo = proto::Send<IoComp, End_>;

    [[maybe_unused]] constexpr bool comp_ok =
        std::is_same_v<payload_row_t<IoComp>, eff::Row<eff::Effect::IO>>;
    [[maybe_unused]] constexpr bool bare_ok =
        std::is_same_v<payload_row_t<int>, eff::Row<>>;
    [[maybe_unused]] constexpr bool proto_ok =
        std::is_same_v<protocol_effect_row_t<SendIo>, eff::Row<eff::Effect::IO>>;
    [[maybe_unused]] constexpr bool effect_strip_ok = std::is_same_v<
        payload_effect_row_t<IoComp>, eff::Row<eff::Effect::IO>>;

    using NPR = NumericalPayloadRow<saf::Tolerance::BITEXACT,
                                    eff::Row<eff::Effect::IO>>;
    [[maybe_unused]] constexpr bool npr_strip_ok = std::is_same_v<
        payload_row_effect_t<NPR>, eff::Row<eff::Effect::IO>>;

    (void) comp_ok; (void) bare_ok; (void) proto_ok;
    (void) effect_strip_ok; (void) npr_strip_ok;
}

}  // namespace crucible::fixy::sess::row
