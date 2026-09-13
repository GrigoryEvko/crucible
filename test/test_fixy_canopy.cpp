// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, then drives the real clock at runtime.

#include <crucible/effects/Capabilities.h>
#include <crucible/fixy/Canopy.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace fcanopy = crucible::fixy::canopy;
namespace canopy = crucible::canopy;
namespace effects = crucible::effects;

static_assert(std::is_same_v<decltype(&fcanopy::mint_hlc), decltype(&canopy::mint_hlc)>,
              "fixy::canopy::mint_hlc must alias canopy::mint_hlc "
              "(same function pointer, not a re-declaration).");

static_assert(std::is_same_v<fcanopy::Hlc, canopy::Hlc>, "fixy::canopy::Hlc must alias canopy::Hlc.");

static_assert(std::is_same_v<fcanopy::HlcTimestamp, canopy::HlcTimestamp>,
              "fixy::canopy::HlcTimestamp must alias canopy::HlcTimestamp.");

static_assert(std::is_same_v<fcanopy::HlcClockTimestamp, canopy::HlcClockTimestamp>,
              "fixy::canopy::HlcClockTimestamp must alias the substrate "
              "Tagged<HlcTimestamp, source::Hlc>.");

static_assert(std::is_same_v<fcanopy::ExternalHlcTimestamp, canopy::ExternalHlcTimestamp>,
              "fixy::canopy::ExternalHlcTimestamp must alias the substrate "
              "Tagged<HlcTimestamp, source::External>.");

static_assert(std::is_same_v<fcanopy::HlcCounterDelta, canopy::HlcCounterDelta>,
              "fixy::canopy::HlcCounterDelta must alias the substrate "
              "Refined<positive, uint32_t>.");

static_assert(!std::is_copy_constructible_v<fcanopy::Hlc>, "fixy::canopy::Hlc must be non-copyable.");
static_assert(!std::is_move_constructible_v<fcanopy::Hlc>, "fixy::canopy::Hlc must be non-moveable (Pinned CRTP).");

#define CRUCIBLE_REQUIRE(cond)                                                               \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

int main() {
    // The clock is pinned, so its move and copy are deleted. Binding the
    // prvalue to auto&& extends its lifetime without either of them.
    auto init = effects::testing::init();
    auto&& clock = fcanopy::mint_hlc(init);

    // Two back-to-back readings are strictly ordered even when the physical
    // nanosecond ties, because the counter advances on the tie.
    auto t0 = clock.now();
    auto t1 = clock.now();
    CRUCIBLE_REQUIRE(t1 > t0);

    auto tagged = clock.tagged_now();
    static_assert(std::is_same_v<decltype(tagged), fcanopy::HlcClockTimestamp>);
    CRUCIBLE_REQUIRE(tagged.value() > t1);

    // on_recv merges the peer timestamp into the local clock, so the next
    // reading is at least the peer's physical time.
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
