#pragma once

// ── crucible::fixy::simd — SIMD-width pinning grant surface (FIXY-V-259) ─
//
// The enum-typed SIMD vector-width grant for Agent 11 §3.5.  Where
// V-257's `grant::hw::simd_width<uint16_t WidthBits>` is a raw-integer
// width minted through `mint_simd_width(ctx)` (the ctx-authorized path),
// V-259's `grant::simd::width<WidthBits W>` is a STRONG-ENUM-typed direct
// pinning grant for `#if`-arm declarations and std::simd / intrinsic call
// sites alike (the V-264 SwissTable consumer).
//
// FIXY-FOUND-039 closure — duplicate-engagement on SimdIsa axis:
//   Both grants ARE STRUCTURALLY DISTINCT TYPES so they may coexist
//   side-by-side in the codebase (different headers, different mint
//   surfaces, different NTTP shapes — raw uint16 vs WidthBits enum).
//   BUT they BOTH route to `DimensionAxis::SimdIsa` via the per-grant
//   `which_dim` specializations (Hw.h:380, Simd.h:134).  A `fixy::fn`
//   Grants pack that contains BOTH a `grant::hw::simd_width<W1>` AND
//   a `grant::simd::width<W2>` engages SimdIsa twice — the wrapper's
//   tier-4 `UniqueEngagementPerAxis` gate REJECTS it with the
//   `FixyDuplicate_SimdIsa` diagnostic tag (Reject.h:1284).  Exactly
//   one SimdIsa pin per binding.  The structural witness pinning
//   this lives in this header's `v039_simd_isa_duplicate_witness`
//   sentinel block (below) plus the dedicated neg-compile fixture
//   `test/fixy_neg/neg_fixy_v_039_simd_isa_duplicate.cpp`.
//
// ── WidthBits — SIMD vector width in BITS ─────────────────────────────
//
//   Scalar   = 0     — no vector register (scalar fallback)
//   Bits128  = 128   — SSE / NEON
//   Bits256  = 256   — AVX2
//   Bits512  = 512   — AVX-512
//   Bits1024 = 1024  — SVE (pre-declared, forward-compat)
//   Bits2048 = 2048  — SVE2 wide (pre-declared, forward-compat)
//
// The 1024/2048 SVE-wide widths are pre-declared so a future ARM-SVE
// kernel pins them without an enum edit; they are KNOWN widths today
// (the `is_known_width` gate accepts them) but ship no convenience alias.
//
// ── width<W> — strong-typed SIMD-width pinning grant ──────────────────
//
//   template <WidthBits W>
//       requires is_known_width_v<W>
//   struct width final : grant_base {};
//
// The `requires` clause is the strong-typing gate: a value outside the
// six enumerated widths (reachable only via an explicit
// `static_cast<WidthBits>(...)`) is ill-formed at the grant template-id.
// `which_dim<width<W>> = DimensionAxis::SimdIsa` (V-253 axis).
//
// ── Composition with V-258 / V-260 (forward-looking) ──────────────────
//
// `width<W>` composes with `vendor::intrinsic<V, I>` (V-258) in a
// binding's Grants pack.  The CONSISTENCY check between width and ISA —
// e.g. `simd::width_512 + vendor::avx2_intrinsic` reds because AVX-512
// width cannot run on the AVX2 family — is V-260's S001 collision rule,
// NOT this grant.  V-259 ships the individually-well-formed grant; both
// `width_512` and `avx2_intrinsic` are valid grants on their own today
// (the sentinel TU witnesses this), and S001 will reject the COMPOSITION
// once V-260 lands.  Fixture 4.2 (width<512>-on-AVX2 reds) therefore
// ships WITH V-260.
//
// Per Grant.h's namespace-purity discipline (CR-09), the which_dim
// specialization reopens `crucible::fixy::grant`; this header is
// allowlisted in scripts/check-fixy-grant-namespace-purity.sh alongside
// Hw.h / Vendor.h / Fp.h / Fs.h.
//
// ── Axiom coverage (CLAUDE.md §II) ────────────────────────────────────
//
//   InitSafe   — `width` is a `final` empty struct; WidthBits has explicit
//                ordinals; is_known_width is total over the enum.
//   TypeSafe   — strong scoped enum (WidthBits : uint16_t); the
//                is_known_width gate forbids out-of-enum widths.
//   NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe — zero-state
//                grant tag, no resources.
//   DetSafe    — same W → same grant type on any platform.
//
// ── HS14 fixtures (test/fixy_neg/) ────────────────────────────────────
//
//   neg_fixy_v_259_width_out_of_enum  — width<WidthBits{777}>  (garbage)
//   neg_fixy_v_259_width_unlisted_64  — width<WidthBits{64}>   (plausible
//                                       but not an enumerated width class)
//   (The width<512>-on-AVX2 composition fixture 4.2 ships with V-260 S001.)

#include <crucible/fixy/Grant.h>            // grant_base, which_dim, IsGrantTag
#include <crucible/fixy/Dim.h>              // dim::DimensionAxis
#include <crucible/fixy/Hw.h>               // FOUND-039: hw::simd_width for sentinel

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::fixy::simd {

// ── WidthBits — SIMD vector width in bits ─────────────────────────────
enum class WidthBits : std::uint16_t {
    Scalar   = 0,
    Bits128  = 128,
    Bits256  = 256,
    Bits512  = 512,
    Bits1024 = 1024,  // SVE — pre-declared, forward-compat
    Bits2048 = 2048,  // SVE2 wide — pre-declared, forward-compat
};

// ── is_known_width — the strong-typing gate ───────────────────────────
//
// True iff W is one of the six enumerated register-width classes; an
// out-of-enum value (reachable only via explicit static_cast) reds.
[[nodiscard]] constexpr bool is_known_width(WidthBits width_value) noexcept {
    switch (width_value) {
        case WidthBits::Scalar:
        case WidthBits::Bits128:
        case WidthBits::Bits256:
        case WidthBits::Bits512:
        case WidthBits::Bits1024:
        case WidthBits::Bits2048:
            return true;
        default:
            return false;
    }
}

template <WidthBits W>
inline constexpr bool is_known_width_v = is_known_width(W);

}  // namespace crucible::fixy::simd

// ═════════════════════════════════════════════════════════════════════
// ── width grant (crucible::fixy::grant::simd) ─────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::simd {

// width<W> — SIMD-width pinning grant.  EBO-collapsible (sizeof == 1
// standalone, 0 inside an aggregator).  Gated to the six known widths.
template <::crucible::fixy::simd::WidthBits W>
    requires ::crucible::fixy::simd::is_known_width_v<W>
struct width final : grant_base {};

}  // namespace crucible::fixy::grant::simd

// ── which_dim routing — CR-09 locked namespace ───────────────────────

namespace crucible::fixy::grant {

template <::crucible::fixy::simd::WidthBits W>
    requires ::crucible::fixy::simd::is_known_width_v<W>
struct which_dim<simd::width<W>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SimdIsa> {};

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── Canonical aliases (crucible::fixy::simd) ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::simd {

namespace gs = ::crucible::fixy::grant::simd;

using width_scalar = gs::width<WidthBits::Scalar>;
using width_128    = gs::width<WidthBits::Bits128>;
using width_256    = gs::width<WidthBits::Bits256>;
using width_512    = gs::width<WidthBits::Bits512>;

}  // namespace crucible::fixy::simd

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::simd::detail::v259_self_test {

namespace gs = ::crucible::fixy::grant::simd;
using ::crucible::fixy::grant::IsGrantTag;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── Layer 1: is_known_width — total + correct ─────────────────────────
static_assert( is_known_width(WidthBits::Scalar));
static_assert( is_known_width(WidthBits::Bits128));
static_assert( is_known_width(WidthBits::Bits256));
static_assert( is_known_width(WidthBits::Bits512));
static_assert( is_known_width(WidthBits::Bits1024));
static_assert( is_known_width(WidthBits::Bits2048));
static_assert(!is_known_width(static_cast<WidthBits>(64)));
static_assert(!is_known_width(static_cast<WidthBits>(777)));

// ── Layer 2: width is a valid grant tag routing to SimdIsa ────────────
static_assert(IsGrantTag<gs::width<WidthBits::Bits256>>);
static_assert(IsGrantTag<gs::width<WidthBits::Bits1024>>);  // forward-compat SVE
static_assert(which_dim_v<gs::width<WidthBits::Scalar>>  == D::SimdIsa);
static_assert(which_dim_v<gs::width<WidthBits::Bits512>> == D::SimdIsa);

// ── Layer 3: sizeof — EBO-collapsible (1 byte standalone) ─────────────
static_assert(sizeof(gs::width<WidthBits::Scalar>)   == 1);
static_assert(sizeof(gs::width<WidthBits::Bits512>)  == 1);
static_assert(sizeof(gs::width<WidthBits::Bits2048>) == 1);

// ── Layer 4: NTTP distinctness — different W → different type ──────────
static_assert(!std::is_same_v<gs::width<WidthBits::Bits128>, gs::width<WidthBits::Bits256>>);
static_assert(!std::is_same_v<gs::width<WidthBits::Bits512>, gs::width<WidthBits::Bits1024>>);
static_assert( std::is_same_v<gs::width<WidthBits::Bits256>, gs::width<WidthBits::Bits256>>);

// ── Layer 5: cv-ref rejection (fixy-A4-033) ───────────────────────────
static_assert(!::crucible::fixy::grant::IsGrantTag_v<const gs::width<WidthBits::Bits256>>);
static_assert(!::crucible::fixy::grant::IsGrantTag_v<gs::width<WidthBits::Bits512>&>);

// ── Layer 6: the 4 canonical aliases resolve to the documented widths ─
static_assert(std::is_same_v<width_scalar, gs::width<WidthBits::Scalar>>);
static_assert(std::is_same_v<width_128,    gs::width<WidthBits::Bits128>>);
static_assert(std::is_same_v<width_256,    gs::width<WidthBits::Bits256>>);
static_assert(std::is_same_v<width_512,    gs::width<WidthBits::Bits512>>);
static_assert(IsGrantTag<width_scalar>);
static_assert(IsGrantTag<width_512>);

// ── Runtime smoke test — non-constant args defeat consteval folding ───
inline void runtime_smoke_test() {
    WidthBits w = WidthBits::Bits256;
    [[maybe_unused]] bool known   = is_known_width(w);
    [[maybe_unused]] bool unknown = is_known_width(static_cast<WidthBits>(48));

    [[maybe_unused]] width_256 a{};
    [[maybe_unused]] width_512 b{};
    [[maybe_unused]] gs::width<WidthBits::Bits1024> sve{};  // forward-compat
}

}  // namespace crucible::fixy::simd::detail::v259_self_test

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-FOUND-039 — SimdIsa duplicate-engagement structural witness ─
// ═════════════════════════════════════════════════════════════════════
//
// Pin the invariant that BOTH grant families (`hw::simd_width<W>` and
// `simd::width<W>`) route to the SAME DimensionAxis (SimdIsa).  The
// transitive consequence — that `fixy::fn<...>` rejects a Grants pack
// containing one of each — falls out of the wrapper's tier-4
// UniqueEngagementPerAxis gate (Reject.h §1284).  The dedicated
// neg-compile fixture in test/fixy_neg/ proves the rejection actually
// fires; this sentinel pins the structural premise (same-axis routing).
//
// Cardinality discipline (FIXY-FOUND-029 / FOUND-035 precedent):
// the count "2 grant families × 1 shared axis" is structurally
// witnessed below — adding a third SimdIsa-routing grant in the future
// must add a row here too so this block diverges if anyone introduces
// a SILENT third channel.

namespace crucible::fixy::simd::detail::v039_simd_isa_duplicate_witness {

namespace gh = ::crucible::fixy::grant::hw;
namespace gs = ::crucible::fixy::grant::simd;
using ::crucible::fixy::grant::which_dim_v;
using D = ::crucible::fixy::dim::DimensionAxis;

// Layer 1: each grant individually routes to SimdIsa (already tested
// in Hw.h:593 and Simd.h:178 above; restated here to anchor FOUND-039).
static_assert(which_dim_v<gh::simd_width<256>>               == D::SimdIsa,
    "FIXY-FOUND-039 anchor: hw::simd_width<W> MUST route to SimdIsa.");
static_assert(which_dim_v<gs::width<WidthBits::Bits256>>     == D::SimdIsa,
    "FIXY-FOUND-039 anchor: simd::width<W> MUST route to SimdIsa.");

// Layer 2: the load-bearing FOUND-039 invariant — both grants engage
// the SAME axis.  A user who passes both will fail tier-4
// UniqueEngagementPerAxis on SimdIsa.  This static_assert reds if a
// future audit accidentally re-routes one of the two grants to a new
// axis (which would silently re-admit the previously-rejected pack
// shape).
static_assert(which_dim_v<gh::simd_width<256>>
              == which_dim_v<gs::width<WidthBits::Bits256>>,
    "FIXY-FOUND-039: hw::simd_width AND simd::width MUST share an "
    "axis so a Grants pack containing both fails "
    "UniqueEngagementPerAxis (FixyDuplicate_SimdIsa).");

// Layer 3: NTTP-distinct types (so the codebase can hold both
// definitions and use them at different call sites — they are
// type-incompatible even when comparing the same width value).
static_assert(!std::is_same_v<gh::simd_width<256>,
                              gs::width<WidthBits::Bits256>>,
    "FIXY-FOUND-039: the two grants are STRUCTURALLY DISTINCT types "
    "(different NTTP shapes); they coexist at TYPE level but cannot "
    "coexist within a single Grants pack.");

// Layer 4: width-value parity sanity — `gh::simd_width<256>` and
// `gs::width<Bits256>` represent the SAME width concept.  Reviewer
// reads this and understands the rejection isn't a "different widths
// don't compose" bug — it's a "same axis, one slot" rule.
static_assert(static_cast<std::uint16_t>(WidthBits::Bits256) == 256u,
    "FIXY-FOUND-039 sanity: WidthBits::Bits256 underlying value is "
    "256 — confirms the two grant families address the same width "
    "physical concept when both name 256-bit.");

}  // namespace crucible::fixy::simd::detail::v039_simd_isa_duplicate_witness
