#pragma once

// ── crucible::fixy::canopy — Canopy primitives under fixy:: ────────
//
// FIXY-V-213.  Re-exports the Hybrid Logical Clock (canopy/Hlc.h)
// substrate under `fixy::canopy::` so callers who include only the
// fixy umbrella never have to descend into canopy/ to mint an HLC.
//
// The Hlc is a DetSafe-critical primitive: every distributed event
// ordering decision in Canopy (Raft log apply, KernelCache publish,
// reshard barriers, observe drift epoch) reads through it.  The
// §XXI ctx-bound mint takes `effects::Init` by value — only init-
// tier code can construct one, and the substrate's `safety::Pinned<Hlc>`
// CRTP base ensures the returned instance cannot be moved or copied
// (guaranteed-copy-elision at the call site is the only legal path).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   canopy::mint_hlc(effects::Init)                — ctx-bound mint
//   canopy::Hlc                                    — main clock (Pinned, 64 B)
//   canopy::HlcTimestamp                           — wire (physical_ns, counter)
//   canopy::HlcClockTimestamp                      — Tagged<HlcTs, source::Hlc>
//   canopy::ExternalHlcTimestamp                   — Tagged<HlcTs, source::External>
//   canopy::HlcCounterDelta                        — Refined<positive, uint32_t>
//   canopy::HlcTimestampChannel<Capacity, UserTag> — SPSC alias for streaming
//   canopy::try_push_hlc_timestamp / try_pop_*     — channel helpers
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Each `using canopy::*;` is a name-lookup directive only;
// the call resolves to the same substrate function with the same
// param-type gate.

#include <crucible/canopy/Hlc.h>

#include <type_traits>

namespace crucible::fixy::canopy {

// ── §XXI ctx-bound mint ────────────────────────────────────────────
using ::crucible::canopy::mint_hlc;

// ── Carrier types ──────────────────────────────────────────────────
using ::crucible::canopy::Hlc;
using ::crucible::canopy::HlcTimestamp;
using ::crucible::canopy::HlcClockTimestamp;
using ::crucible::canopy::ExternalHlcTimestamp;
using ::crucible::canopy::HlcCounterDelta;

// ── Streaming channel surface ──────────────────────────────────────
using ::crucible::canopy::HlcTimestampChannel;
using ::crucible::canopy::try_push_hlc_timestamp;
using ::crucible::canopy::try_pop_hlc_timestamp;

}  // namespace crucible::fixy::canopy

// ── Self-test ──────────────────────────────────────────────────────
//
// Five witnesses ride this header:
//
//   (1) `mint_hlc` is the substrate function, not a re-declaration.
//       Function-pointer identity proves the using-decl does NOT
//       introduce a new overload — the call resolves through the
//       same `effects::Init` parameter gate as the bare substrate.
//   (2) `Hlc` template-identity to substrate's `canopy::Hlc`.
//   (3) `HlcTimestamp` carrier identity — bit-exact wire shape.
//   (4) `HlcClockTimestamp` Tagged<source::Hlc> identity preserved.
//   (5) Pinned discipline survives the re-export — Hlc remains
//       non-copyable AND non-moveable through the fixy:: surface.

namespace crucible::fixy::canopy::self_test {

// (1) mint_hlc pointer identity — non-template free function, direct.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::canopy::mint_hlc),
    decltype(&::crucible::canopy::mint_hlc)>,
    "FIXY-V-213: fixy::canopy::mint_hlc must alias canopy::mint_hlc — "
    "the using-decl did not introduce a new overload.");

// (2) Hlc type-carrier identity.
static_assert(std::is_same_v<
    ::crucible::fixy::canopy::Hlc,
    ::crucible::canopy::Hlc>,
    "FIXY-V-213: fixy::canopy::Hlc must alias canopy::Hlc.");

// (3) HlcTimestamp wire-shape identity.
static_assert(std::is_same_v<
    ::crucible::fixy::canopy::HlcTimestamp,
    ::crucible::canopy::HlcTimestamp>,
    "FIXY-V-213: fixy::canopy::HlcTimestamp must alias canopy::HlcTimestamp.");

// (4) HlcClockTimestamp Tagged<source::Hlc> identity.
static_assert(std::is_same_v<
    ::crucible::fixy::canopy::HlcClockTimestamp,
    ::crucible::canopy::HlcClockTimestamp>,
    "FIXY-V-213: fixy::canopy::HlcClockTimestamp must alias "
    "canopy::HlcClockTimestamp (Tagged<HlcTimestamp, source::Hlc>).");

// (5) Pinned discipline survives the re-export — re-exported Hlc
//     IS the substrate's Pinned Hlc; the static_assert fires here
//     for the fixy:: spelling so reviewers see the witness at the
//     re-export point.
static_assert(!std::is_copy_constructible_v<
                  ::crucible::fixy::canopy::Hlc>,
    "FIXY-V-213: fixy::canopy::Hlc must be non-copyable (Pinned CRTP).");
static_assert(!std::is_move_constructible_v<
                  ::crucible::fixy::canopy::Hlc>,
    "FIXY-V-213: fixy::canopy::Hlc must be non-moveable (Pinned CRTP).");

}  // namespace crucible::fixy::canopy::self_test
