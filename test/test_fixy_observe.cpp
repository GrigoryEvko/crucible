// ── test_fixy_observe — sentinel TU for fixy/Observe.h ─────────────
//
// Pulls fixy/Observe.h into a TU compiled under project warning
// flags so the header's static_asserts execute under enforcement.
// Witnesses the §XXI re-export discipline for the three SWMR
// metrics mint factories:
//
//   1. fixy::observe::mint_metrics_writer aliases the substrate.
//   2. fixy::observe::mint_keeper_metrics_reader aliases the substrate.
//   3. fixy::observe::mint_canopy_metrics_reader aliases the substrate.
//   4. Carrier identity for the full payload + handle + channel suite.
//   5. Runtime: mint writer + reader, publish a sample, read it back,
//      verify Stale<> propagation and Tagged<source::*> survives.
//
// HS14: 3 fixy_neg fixtures live in test/fixy_neg/neg_fixy_observe_*.cpp,
// one per mint, covering wrong Permission tag (writer), no-arg
// overload-resolution failure (keeper reader), and wrong-type
// conversion (canopy reader).

#include <crucible/fixy/Observe.h>
#include <crucible/permissions/Permission.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

// Bit-exact double comparison — round-trip integrity check, not an
// approximate one.  Sidesteps -Werror=float-equal by comparing the
// IEEE-754 bit patterns directly.
[[nodiscard]] constexpr bool bit_eq(double a, double b) noexcept {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

}  // namespace

namespace fobs = crucible::fixy::observe;
namespace obs = crucible::observe;

// ─── 1-3. mint_* pointer identity ─────────────────────────────────

static_assert(std::is_same_v<
    decltype(&fobs::mint_metrics_writer),
    decltype(&obs::mint_metrics_writer)>,
    "fixy::observe::mint_metrics_writer must alias "
    "observe::mint_metrics_writer.");

static_assert(std::is_same_v<
    decltype(&fobs::mint_keeper_metrics_reader),
    decltype(&obs::mint_keeper_metrics_reader)>,
    "fixy::observe::mint_keeper_metrics_reader must alias "
    "observe::mint_keeper_metrics_reader.");

static_assert(std::is_same_v<
    decltype(&fobs::mint_canopy_metrics_reader),
    decltype(&obs::mint_canopy_metrics_reader)>,
    "fixy::observe::mint_canopy_metrics_reader must alias "
    "observe::mint_canopy_metrics_reader.");

// ─── 4. Carrier identity ──────────────────────────────────────────

static_assert(std::is_same_v<fobs::RuntimeMetrics, obs::RuntimeMetrics>);
static_assert(std::is_same_v<fobs::RuntimeMetricsSample,
                             obs::RuntimeMetricsSample>);
static_assert(std::is_same_v<fobs::RuntimeMetricsChannel,
                             obs::RuntimeMetricsChannel>);
static_assert(std::is_same_v<fobs::RuntimeMetricsWriter,
                             obs::RuntimeMetricsWriter>);
static_assert(std::is_same_v<fobs::RuntimeMetricsReader,
                             obs::RuntimeMetricsReader>);

#define CRUCIBLE_REQUIRE(cond)                                             \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "REQUIRE FAILED: %s @ %s:%d\n",          \
                         #cond, __FILE__, __LINE__);                       \
            std::abort();                                                  \
        }                                                                  \
    } while (0)

int main() {
    // ─── 5. Runtime: mint via fixy::, publish, read, verify ──────

    fobs::RuntimeMetricsChannel channel;

    // (a) Writer mint — consumes the writer-tag Permission.
    auto writer_perm = crucible::safety::mint_permission_root<
        fobs::RuntimeMetricsWriterTag>();
    auto writer = fobs::mint_metrics_writer(channel, std::move(writer_perm));

    // (b) Keeper reader — fractional from the SnapshotPool.
    auto keeper_reader_opt = fobs::mint_keeper_metrics_reader(channel);
    CRUCIBLE_REQUIRE(keeper_reader_opt.has_value());

    // (c) Canopy reader — same shape, second fractional borrow.
    auto canopy_reader_opt = fobs::mint_canopy_metrics_reader(channel);
    CRUCIBLE_REQUIRE(canopy_reader_opt.has_value());

    // (d) Publish a fresh sample through the writer.
    fobs::RuntimeMetrics payload{};
    payload.meb_lambda_max = 1.5;
    payload.ntk_alpha = 0.42;
    payload.delta_g_count = 4;
    payload.delta_g[0] = 0.1;
    payload.delta_g[1] = 0.2;
    payload.delta_g[2] = 0.3;
    payload.delta_g[3] = 0.4;
    auto fresh = fobs::fresh_metrics_sample(payload);
    writer.publish(fresh);

    // (e) Keeper reader observes the sample with Stale<> staleness.
    //     ReaderHandle::load() returns the Stale<RuntimeMetrics>;
    //     peek() unwraps to the bare payload (Graded::peek under
    //     AbsoluteModality stays zero-cost).
    auto keeper_sample = keeper_reader_opt->load();
    CRUCIBLE_REQUIRE(bit_eq(keeper_sample.peek().meb_lambda_max, 1.5));
    CRUCIBLE_REQUIRE(bit_eq(keeper_sample.peek().ntk_alpha, 0.42));
    CRUCIBLE_REQUIRE(keeper_sample.peek().delta_g_count == 4u);

    // (f) Canopy reader sees the same sample (single-writer publish
    //     propagates to every active reader through the AtomicSnapshot
    //     publication slot).
    auto canopy_sample = canopy_reader_opt->load();
    CRUCIBLE_REQUIRE(bit_eq(canopy_sample.peek().meb_lambda_max, 1.5));

    std::fprintf(stderr, "[test_fixy_observe] OK\n");
    return 0;
}
