// ── test_fixy_canopy — sentinel TU for fixy/Canopy.h ───────────────
//
// Pulls fixy/Canopy.h into a TU compiled under project warning flags
// so the header's static_asserts execute under enforcement.  The
// HLC is DetSafe-critical for Canopy event ordering; this sentinel
// pins the §XXI re-export discipline at the production call site:
//
//   1. fixy::canopy::mint_hlc IS canopy::mint_hlc (pointer identity).
//   2. fixy::canopy::Hlc IS canopy::Hlc (template identity).
//   3. fixy::canopy::HlcTimestamp / HlcClockTimestamp aliases preserve
//      the wire shape and Tagged<source::Hlc> provenance.
//   4. Runtime mint via fixy::, exercise now() / tagged_now() /
//      on_recv(), verify monotonicity and Tagged propagation.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_canopy_*.cpp
// covering wrong cap-tag (param-type mismatch) and missing argument
// (overload-resolution failure).

#include <crucible/effects/Capabilities.h>
#include <crucible/fixy/Canopy.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace fcanopy = crucible::fixy::canopy;
namespace canopy = crucible::canopy;
namespace effects = crucible::effects;

// ─── 1. mint_hlc is the substrate function ────────────────────────

static_assert(std::is_same_v<
    decltype(&fcanopy::mint_hlc),
    decltype(&canopy::mint_hlc)>,
    "fixy::canopy::mint_hlc must alias canopy::mint_hlc "
    "(same function pointer, not a re-declaration).");

// ─── 2. Hlc carrier identity ──────────────────────────────────────

static_assert(std::is_same_v<fcanopy::Hlc, canopy::Hlc>,
    "fixy::canopy::Hlc must alias canopy::Hlc.");

// ─── 3. Wire-timestamp carrier identity ───────────────────────────

static_assert(std::is_same_v<fcanopy::HlcTimestamp,
                             canopy::HlcTimestamp>,
    "fixy::canopy::HlcTimestamp must alias canopy::HlcTimestamp.");

static_assert(std::is_same_v<fcanopy::HlcClockTimestamp,
                             canopy::HlcClockTimestamp>,
    "fixy::canopy::HlcClockTimestamp must alias the substrate "
    "Tagged<HlcTimestamp, source::Hlc>.");

static_assert(std::is_same_v<fcanopy::ExternalHlcTimestamp,
                             canopy::ExternalHlcTimestamp>,
    "fixy::canopy::ExternalHlcTimestamp must alias the substrate "
    "Tagged<HlcTimestamp, source::External>.");

static_assert(std::is_same_v<fcanopy::HlcCounterDelta,
                             canopy::HlcCounterDelta>,
    "fixy::canopy::HlcCounterDelta must alias the substrate "
    "Refined<positive, uint32_t>.");

// ─── 4. Pinned discipline propagates through fixy:: ───────────────

static_assert(!std::is_copy_constructible_v<fcanopy::Hlc>,
    "fixy::canopy::Hlc must be non-copyable.");
static_assert(!std::is_move_constructible_v<fcanopy::Hlc>,
    "fixy::canopy::Hlc must be non-moveable (Pinned CRTP).");

#define CRUCIBLE_REQUIRE(cond)                                             \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "REQUIRE FAILED: %s @ %s:%d\n",          \
                         #cond, __FILE__, __LINE__);                       \
            std::abort();                                                  \
        }                                                                  \
    } while (0)

int main() {
    // (a) Mint via fixy:: with the only legal cap-tag (Init).  Pinned
    //     + RVO: bind to auto&& to extend the prvalue lifetime
    //     without invoking the deleted move/copy.
    auto init = effects::testing::init();
    auto&& clock = fcanopy::mint_hlc(init);

    // (b) now() advances monotonically — two back-to-back calls must
    //     produce strictly ordered HLC pairs (counter bumps even when
    //     physical_ns ties).
    auto t0 = clock.now();
    auto t1 = clock.now();
    CRUCIBLE_REQUIRE(t1 > t0);

    // (c) tagged_now() propagates source::Hlc via the fixy alias.
    auto tagged = clock.tagged_now();
    static_assert(std::is_same_v<decltype(tagged),
                                  fcanopy::HlcClockTimestamp>);
    CRUCIBLE_REQUIRE(tagged.value() > t1);

    // (d) on_recv merges peer timestamp into the local clock —
    //     wrapping the input in the fixy alias proves the surface
    //     accepts substrate-shaped values transparently.
    fcanopy::HlcTimestamp peer{
        .physical_ns = tagged.value().physical_ns + 1'000'000ULL,
        .counter = 0,
    };
    clock.on_recv(peer);
    auto t_after_recv = clock.now();
    CRUCIBLE_REQUIRE(t_after_recv.physical_ns >= peer.physical_ns);

    std::fprintf(stderr, "[test_fixy_canopy] OK\n");
    return 0;
}
