// The vector-width probe family of the hardware-capability ledger: the size
// of the streaming buffer, and whether the probe answers or declines.
// test_ledger_probes tests the contract that every probe family shares.
//
// In an uninstrumented build the probe streams a buffer of eight times the
// L3 instance of one core, in four runs.  The production settings give each
// run at least kMinSampleCount samples.  This test checks the structure of
// the probe and not a number worth storing, so it gives each streaming run
// two samples.  An instrumented build declines at once.

#include <crucible/ledger/probes/VectorWidth.h>

#include "test_assert.h"
#include "test_ledger_probes_fixtures.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>

using namespace crucible;
using crucible::ledger::LedgerError;
using ledger_probe_fixtures::fit_host;
using ledger_probe_fixtures::probe_ctx;

namespace {

// The streaming shape is bound by DRAM only when its buffer is several
// times the last-level cache that the measuring thread can fill.  So a
// sized buffer is at least kMinStreamCacheMultiple times that cache, and
// a cache that the scratch ceiling cannot outgrow is refused.
void test_stream_buffer_outgrows_the_reachable_cache() {
    using ledger::probes::kMaxStreamBytes;
    using ledger::probes::kMinStreamBytes;
    using ledger::probes::kMinStreamCacheMultiple;
    using ledger::probes::stream_bytes_for;
    constexpr std::size_t MiB = std::size_t{1} << 20;

    constexpr std::array<std::size_t, 9> reaches{0,         MiB,       32 * MiB,  96 * MiB,       128 * MiB,
                                                 129 * MiB, 504 * MiB, 768 * MiB, ~std::size_t{0}};
    for (const std::size_t reach : reaches) {
        const std::expected<std::size_t, LedgerError> sized = stream_bytes_for(reach);
        if (sized.has_value()) {
            assert(*sized >= kMinStreamBytes && *sized <= kMaxStreamBytes);
            assert(*sized / kMinStreamCacheMultiple >= reach);
        } else {
            assert(sized.error() == LedgerError::NotApplicableOnThisHost);
            assert(reach > kMaxStreamBytes / kMinStreamCacheMultiple);
        }
    }

    // The bench host: one L3 instance of 32 MiB, and a buffer eight times
    // that.
    assert(stream_bytes_for(32 * MiB) == 256 * MiB);

    // Break it: a part whose one L3 is 504 MiB.  The ceiling would give a
    // buffer of 512 MiB, which that cache holds almost whole, so the pass
    // would measure the cache and not DRAM.
    assert(!stream_bytes_for(504 * MiB).has_value());

    // This host, through the same rule.
    const std::size_t host_reach = ledger::probes::reachable_last_level_bytes(::fixy::concurrent::Topology::instance());
    const std::expected<std::size_t, LedgerError> host_sized = ledger::probes::stream_bytes_for_host();
    assert(host_sized.has_value() == stream_bytes_for(host_reach).has_value());
    if (host_sized.has_value()) {
        assert(*host_sized / kMinStreamCacheMultiple >= host_reach);
    }

    crucible::test::pass("  test_stream_buffer_outgrows_the_reachable_cache: PASSED\n");
}

// The sample count of a streaming run.  The default settings derive it from
// sample_count and keep it at or above the floor of the ledger, and a
// positive stream_sample_count replaces it.
void test_stream_sample_count_follows_the_settings() {
    ledger::set_probe_settings(ledger::ProbeSettings{});
    assert(ledger::probes::stream_sample_count() == 64u);
    ledger::set_probe_settings(ledger::ProbeSettings{.sample_count = 64, .pin_core = -1});
    assert(ledger::probes::stream_sample_count() == ledger::kMinSampleCount);
    ledger::set_probe_settings(ledger::ProbeSettings{.sample_count = 64, .pin_core = -1, .stream_sample_count = 2});
    assert(ledger::probes::stream_sample_count() == 2u);
    crucible::test::pass("  test_stream_sample_count_follows_the_settings: PASSED\n");
}

void test_vector_width_probe_answers_or_declines() {
    // Cheap settings: the probe is exercised for its structure — the two
    // shapes, the memo, the guard that keeps a 512-bit kernel off a host
    // without one — and not for a number worth storing. The numbers this
    // host actually produces come from crucible-hwprobe, which measures at
    // the sample counts the verdicts want.
    ledger::set_probe_settings(ledger::ProbeSettings{.sample_count = 64, .pin_core = -1, .stream_sample_count = 2});
    ledger::probes::vector_width_detail::g_memo.forget();

    const auto preferred = ledger::probes::probe_vector_width_preferred_bits(probe_ctx, fit_host());

    if constexpr (ledger::kBuildIsInstrumented) {
        // The claim: an instrumented build declines rather than reporting
        // numbers about its own instrumentation. This is not a tolerated
        // skip, it is the assertion — the same probe under
        // AddressSanitizer once reported the streaming shape at 190% where
        // an ordinary build reports 101%, because a sanitizer charges one
        // shadow check per load and a 512-bit load moves twice the bytes
        // of a 256-bit one.
        assert(!preferred.has_value());
        assert(preferred.error() == LedgerError::NotApplicableOnThisHost);
        assert(!ledger::probes::probe_vector_width_compute_gain(probe_ctx, fit_host()).has_value());
        assert(!ledger::probes::probe_vector_width_memory_gain(probe_ctx, fit_host()).has_value());
        crucible::test::pass("  test_vector_width_probe_answers_or_declines: PASSED (instrumented build declines)\n");
        return;
    }

    if (!preferred.has_value()) {
        // The only declines an uninstrumented host produces: no wide unit
        // to compare against or a last-level cache too large for the
        // streaming buffer, no memory for the buffer, or a streaming run
        // whose pin failed.
        assert(preferred.error() == LedgerError::NotApplicableOnThisHost
               || preferred.error() == LedgerError::StorePathUnavailable
               || preferred.error() == LedgerError::ConfidenceBelowBar);
        crucible::test::pass("  test_vector_width_probe_answers_or_declines: PASSED (declined)\n");
        return;
    }

    const std::uint64_t width = preferred->value.raw();
    assert(width == ledger::probes::kNarrowWidthBits || width == ledger::probes::kWideWidthBits);

    // The two margins come from the SAME measurement, because the memo is
    // what shares it. If they did not, each verdict would describe a
    // different moment of a machine that had not changed.
    const auto compute = ledger::probes::probe_vector_width_compute_gain(probe_ctx, fit_host());
    const auto memory = ledger::probes::probe_vector_width_memory_gain(probe_ctx, fit_host());
    assert(compute.has_value());
    assert(memory.has_value());

    // The rule the probe applies, restated: the wide width is preferred
    // only when it won the compute shape outright.
    if (width == ledger::probes::kWideWidthBits) {
        assert(compute->value.raw() > 100u + ledger::kPracticalMarginPercent);
    }
    crucible::test::pass("  test_vector_width_probe_answers_or_declines: PASSED ({} bits, compute={}%, memory={}%)\n",
                         static_cast<unsigned long long>(width), static_cast<unsigned long long>(compute->value.raw()),
                         static_cast<unsigned long long>(memory->value.raw()));
}

}  // namespace

namespace crucible::ledger::probes {

namespace vector_width_detail::self_test {

// The conservative width is the narrow one, and a default-constructed
// measurement — which is what a host with no wide unit produces — already
// says so without anything having run.
static_assert(VectorWidthMeasurement{}.preferred_bits == kNarrowWidthBits);
static_assert(!VectorWidthMeasurement{}.is_usable());
static_assert(VectorWidthMeasurement{}.fault == LedgerError::NotApplicableOnThisHost);
static_assert(kNarrowWidthBits < kWideWidthBits);

// The compute buffer must fit the smallest L1d this tree supports, or the
// compute-bound shape is not compute-bound on that host.
static_assert(kComputeBytes < ::fixy::concurrent::conservative_l1d_per_core);

// No constant can be past each last-level cache, so the buffer is sized
// against the measured cache at run time, and stream_bytes_for refuses a
// cache that the ceiling cannot outgrow by kMinStreamCacheMultiple.
static_assert(kMinStreamBytes <= kMaxStreamBytes);
static_assert(kMinStreamCacheMultiple > 1 && kMinStreamCacheMultiple <= kStreamCacheMultiple);
static_assert(stream_bytes_for(std::size_t{32} << 20) == std::size_t{256} << 20);
static_assert(stream_bytes_for(kMaxStreamBytes / kMinStreamCacheMultiple) == kMaxStreamBytes);
static_assert(!stream_bytes_for(kMaxStreamBytes / kMinStreamCacheMultiple + 1).has_value());

}  // namespace vector_width_detail::self_test

}  // namespace crucible::ledger::probes

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_ledger_probes_vector_width:\n");
    test_stream_buffer_outgrows_the_reachable_cache();
    test_stream_sample_count_follows_the_settings();
    test_vector_width_probe_answers_or_declines();
    crucible::test::pass("test_ledger_probes_vector_width: 3 groups, all passed\n");
    return 0;
}
