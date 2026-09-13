// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, then mints and drives the real channel
// at runtime.

#include <crucible/fixy/Observe.h>
#include <crucible/permissions/Permission.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

// The round trip must preserve every bit, not land within a tolerance.
// Comparing the bit patterns states that, and gets past the ban on
// comparing two floating-point values with ==.
[[nodiscard]] constexpr bool bit_eq(double a, double b) noexcept {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

}  // namespace

namespace fobs = crucible::fixy::observe;
namespace obs = crucible::observe;

static_assert(std::is_same_v<decltype(&fobs::mint_metrics_writer), decltype(&obs::mint_metrics_writer)>,
              "fixy::observe::mint_metrics_writer must alias "
              "observe::mint_metrics_writer.");

static_assert(std::is_same_v<decltype(&fobs::mint_keeper_metrics_reader), decltype(&obs::mint_keeper_metrics_reader)>,
              "fixy::observe::mint_keeper_metrics_reader must alias "
              "observe::mint_keeper_metrics_reader.");

static_assert(std::is_same_v<decltype(&fobs::mint_canopy_metrics_reader), decltype(&obs::mint_canopy_metrics_reader)>,
              "fixy::observe::mint_canopy_metrics_reader must alias "
              "observe::mint_canopy_metrics_reader.");

static_assert(std::is_same_v<fobs::RuntimeMetrics, obs::RuntimeMetrics>);
static_assert(std::is_same_v<fobs::RuntimeMetricsSample, obs::RuntimeMetricsSample>);
static_assert(std::is_same_v<fobs::RuntimeMetricsChannel, obs::RuntimeMetricsChannel>);
static_assert(std::is_same_v<fobs::RuntimeMetricsWriter, obs::RuntimeMetricsWriter>);
static_assert(std::is_same_v<fobs::RuntimeMetricsReader, obs::RuntimeMetricsReader>);

#define CRUCIBLE_REQUIRE(cond)                                                               \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

int main() {
    fobs::RuntimeMetricsChannel channel;

    auto writer_perm = crucible::safety::mint_permission_root<fobs::RuntimeMetricsWriterTag>();
    auto writer = fobs::mint_metrics_writer(channel, std::move(writer_perm));

    auto keeper_reader_opt = fobs::mint_keeper_metrics_reader(channel);
    CRUCIBLE_REQUIRE(keeper_reader_opt.has_value());

    auto canopy_reader_opt = fobs::mint_canopy_metrics_reader(channel);
    CRUCIBLE_REQUIRE(canopy_reader_opt.has_value());

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

    auto keeper_sample = keeper_reader_opt->load();
    CRUCIBLE_REQUIRE(bit_eq(keeper_sample.peek().meb_lambda_max, 1.5));
    CRUCIBLE_REQUIRE(bit_eq(keeper_sample.peek().ntk_alpha, 0.42));
    CRUCIBLE_REQUIRE(keeper_sample.peek().delta_g_count == 4u);

    auto canopy_sample = canopy_reader_opt->load();
    CRUCIBLE_REQUIRE(bit_eq(canopy_sample.peek().meb_lambda_max, 1.5));

    std::fprintf(stderr, "[test_fixy_observe] OK\n");
    return 0;
}
