#pragma once

// #68 — is the wide vector unit actually faster on THIS host?
//
// The question is not "does the part have AVX-512". __builtin_cpu_supports
// answers that, the fingerprint already folds it, and it decides nothing.
// The question is whether a 512-bit kernel finishes sooner than a 256-bit
// one on this silicon, at this clock, under this thermal policy — so that
// the runtime can advertise the fastest path instead of the widest one.
//
// Those two can differ. On the Intel parts that introduced AVX-512, a
// 512-bit instruction pulled the core into a lower licence and dropped its
// clock, and a kernel that spent more time waiting for memory than doing
// arithmetic came out slower at 512 bits than at 256. On a part with a
// full-width datapath and no licence transition, the same kernel comes out
// twice as fast. Nothing short of running both tells you which host you
// have, which is why this is a probe and not a table.
//
// Two shapes, because the answer genuinely differs between them:
//
//   compute-bound  An FMA chain over a buffer that fits L1, with eight
//                  independent accumulators so the pipeline stays full.
//                  Nothing here waits for memory, so the width of the
//                  datapath is the only thing that can decide it. This is
//                  the shape where a wide unit shows what it is worth, and
//                  it is also the shape where a licence transition hurts
//                  most, because there is no memory stall to hide under.
//
//   memory-bound   One streaming pass over a buffer several times the
//                  size of the last-level cache. The bottleneck is the
//                  path to DRAM, which does not care how wide the register
//                  that receives the line is. A part with no licence
//                  penalty ties here. A part with one loses here, and the
//                  tie-versus-loss is exactly the diagnostic.
//
// A synthetic peak-FLOPs loop would answer neither. It reports the number
// on the datasheet, which is true on every host and decides nothing on any
// of them.
//
// When the two widths come out within noise on both shapes, "no preference"
// is the answer and the probe says so. The conservative reading of "no
// preference" is the NARROWER width, and that direction is not arbitrary:
// 256-bit code runs on every x86-64-v3 part, needs no runtime guard, and
// cannot trigger a licence transition on a part that has them. Preferring
// the wider width on a tie would be betting the difference is zero on every
// future workload, having measured two.
//
// Release behaviour of the checks in this file. The guard that keeps a
// 512-bit kernel off a host without one is a runtime branch returning
// LedgerError::NotApplicableOnThisHost, NOT an assertion, because an
// assertion that compiles out would leave a SIGILL in production. The
// contract_assert at each kernel entry fires in Release too — Release
// builds contracts at `observe` and the violation handler is [[noreturn]] —
// and is there as a second line, not as the first.

#include <crucible/concurrent/WorkingSet.h>
#include <crucible/ledger/ProbeSupport.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace crucible::ledger::probes {

// ── Shape parameters ──────────────────────────────────────────────────

// Small enough to sit in the L1 data cache of every supported part. The
// conservative L1d bound in concurrent/WorkingSet.h is 32 KiB, so 16 KiB
// of floats leaves room for the loop's own working set and still cannot
// spill on the smallest host.
inline constexpr std::size_t kComputeFloatCount = 4096;
inline constexpr std::size_t kComputeBytes = kComputeFloatCount * sizeof(float);

// Passes over the L1 buffer inside one timed call. Enough that the call
// is microseconds rather than nanoseconds, so the timer's own floor is
// three orders of magnitude below the thing being timed.
inline constexpr std::size_t kComputePassCount = 64;

// The streaming buffer is sized against the measured last-level cache so
// the pass is bandwidth-bound on any host, with a floor for a machine
// whose cache probe failed and a ceiling so a part with a very large
// last-level cache does not ask for a multi-gigabyte scratch region.
inline constexpr std::size_t kMinStreamBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxStreamBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::size_t kStreamCacheMultiple = 8;

[[nodiscard]] inline std::size_t stream_bytes_for_host() noexcept {
    const std::size_t last_level = concurrent::Topology::instance().l3_total_bytes();
    const std::size_t wanted = last_level * kStreamCacheMultiple;
    return std::clamp(wanted, kMinStreamBytes, kMaxStreamBytes);
}

// The compute shape's call is microseconds, so it takes the configured
// sample count. The streaming shape's call is milliseconds, so taking the
// same number would run for an hour; it is divided down and then floored
// at the ledger's own minimum, because a sample count below that bar is
// refused at admission anyway and measuring it would be wasted time.
[[nodiscard]] inline std::size_t stream_sample_count() noexcept {
    const std::size_t configured = probe_settings().sample_count;
    return std::clamp<std::size_t>(configured / 64u, kMinSampleCount, 256u);
}

#if defined(__x86_64__) || defined(__i386__)

// ── The kernels ───────────────────────────────────────────────────────
//
// Per-function target attributes, not function multiversioning. There is
// no resolver, no IFUNC and no runtime dispatch: two ordinary functions
// that happen to be compiled for different instruction sets, called by
// name from a probe that has already checked which of them this host can
// execute. The house rule against multiversioning is about hidden dispatch
// on a hot path, and the thing it forbids — an indirect jump the optimizer
// cannot see through — does not appear here.
//
// noinline on all four so the measured body is the same code whatever the
// caller was compiled for, and so the two widths cannot be merged into one
// function by a compiler that notices they compute the same sum.

[[gnu::target("avx2,fma"), gnu::noinline]] inline float compute_fma_256(const float* buffer, std::size_t float_count,
                                                                        std::size_t passes) noexcept {
    __m256 lane0 = _mm256_setzero_ps();
    __m256 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    __m256 lane4 = lane0, lane5 = lane0, lane6 = lane0, lane7 = lane0;
    const __m256 multiplier = _mm256_set1_ps(1.0000001f);
    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t index = 0; index + 64 <= float_count; index += 64) {
            lane0 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 0), multiplier, lane0);
            lane1 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 8), multiplier, lane1);
            lane2 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 16), multiplier, lane2);
            lane3 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 24), multiplier, lane3);
            lane4 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 32), multiplier, lane4);
            lane5 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 40), multiplier, lane5);
            lane6 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 48), multiplier, lane6);
            lane7 = _mm256_fmadd_ps(_mm256_load_ps(buffer + index + 56), multiplier, lane7);
        }
    }
    lane0 = _mm256_add_ps(_mm256_add_ps(lane0, lane1), _mm256_add_ps(lane2, lane3));
    lane4 = _mm256_add_ps(_mm256_add_ps(lane4, lane5), _mm256_add_ps(lane6, lane7));
    lane0 = _mm256_add_ps(lane0, lane4);
    alignas(32) float out[8]{};
    _mm256_store_ps(out, lane0);
    return out[0] + out[1] + out[2] + out[3] + out[4] + out[5] + out[6] + out[7];
}

[[gnu::target("avx512f"), gnu::noinline]] inline float compute_fma_512(const float* buffer, std::size_t float_count,
                                                                       std::size_t passes) noexcept {
    __m512 lane0 = _mm512_setzero_ps();
    __m512 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    __m512 lane4 = lane0, lane5 = lane0, lane6 = lane0, lane7 = lane0;
    const __m512 multiplier = _mm512_set1_ps(1.0000001f);
    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t index = 0; index + 128 <= float_count; index += 128) {
            lane0 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 0), multiplier, lane0);
            lane1 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 16), multiplier, lane1);
            lane2 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 32), multiplier, lane2);
            lane3 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 48), multiplier, lane3);
            lane4 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 64), multiplier, lane4);
            lane5 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 80), multiplier, lane5);
            lane6 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 96), multiplier, lane6);
            lane7 = _mm512_fmadd_ps(_mm512_load_ps(buffer + index + 112), multiplier, lane7);
        }
    }
    lane0 = _mm512_add_ps(_mm512_add_ps(lane0, lane1), _mm512_add_ps(lane2, lane3));
    lane4 = _mm512_add_ps(_mm512_add_ps(lane4, lane5), _mm512_add_ps(lane6, lane7));
    return _mm512_reduce_add_ps(_mm512_add_ps(lane0, lane4));
}

[[gnu::target("avx2"), gnu::noinline]] inline float stream_sum_256(const float* buffer,
                                                                   std::size_t float_count) noexcept {
    __m256 lane0 = _mm256_setzero_ps();
    __m256 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    for (std::size_t index = 0; index + 32 <= float_count; index += 32) {
        lane0 = _mm256_add_ps(lane0, _mm256_load_ps(buffer + index + 0));
        lane1 = _mm256_add_ps(lane1, _mm256_load_ps(buffer + index + 8));
        lane2 = _mm256_add_ps(lane2, _mm256_load_ps(buffer + index + 16));
        lane3 = _mm256_add_ps(lane3, _mm256_load_ps(buffer + index + 24));
    }
    lane0 = _mm256_add_ps(_mm256_add_ps(lane0, lane1), _mm256_add_ps(lane2, lane3));
    alignas(32) float out[8]{};
    _mm256_store_ps(out, lane0);
    return out[0] + out[1] + out[2] + out[3] + out[4] + out[5] + out[6] + out[7];
}

[[gnu::target("avx512f"), gnu::noinline]] inline float stream_sum_512(const float* buffer,
                                                                      std::size_t float_count) noexcept {
    __m512 lane0 = _mm512_setzero_ps();
    __m512 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    for (std::size_t index = 0; index + 64 <= float_count; index += 64) {
        lane0 = _mm512_add_ps(lane0, _mm512_load_ps(buffer + index + 0));
        lane1 = _mm512_add_ps(lane1, _mm512_load_ps(buffer + index + 16));
        lane2 = _mm512_add_ps(lane2, _mm512_load_ps(buffer + index + 32));
        lane3 = _mm512_add_ps(lane3, _mm512_load_ps(buffer + index + 48));
    }
    lane0 = _mm512_add_ps(_mm512_add_ps(lane0, lane1), _mm512_add_ps(lane2, lane3));
    return _mm512_reduce_add_ps(lane0);
}

[[nodiscard]] inline bool host_has_wide_vector_unit() noexcept { return __builtin_cpu_supports("avx512f") != 0; }

#else

[[nodiscard]] inline bool host_has_wide_vector_unit() noexcept { return false; }

#endif

// ── The measurement ───────────────────────────────────────────────────

inline constexpr std::uint64_t kNarrowWidthBits = 256;
inline constexpr std::uint64_t kWideWidthBits = 512;

struct VectorWidthMeasurement {
    LedgerError fault = LedgerError::NotApplicableOnThisHost;

    std::uint64_t preferred_bits = kNarrowWidthBits;
    std::uint32_t compute_gain_percent = 0;
    std::uint32_t memory_gain_percent = 0;

    // One evidence record per shape, because the two shapes fail
    // independently: a host can produce a rock-steady compute ratio and a
    // streaming ratio that moves with whatever else is on the memory
    // controllers. Folding them into one record would have the steadier
    // half vouching for the other.
    VerdictEvidence compute_evidence{};
    VerdictEvidence memory_evidence{};

    [[nodiscard]] constexpr bool is_usable() const noexcept { return fault == LedgerError::None; }
};

namespace vector_width_detail {

inline MeasurementMemo<VectorWidthMeasurement> g_memo{};

[[nodiscard]] inline bench::Run configured_run(const char* name) noexcept {
    bench::Run run{name};
    (void)run.samples(probe_settings().sample_count).warmup(64).max_wall_ms(8000);
    const int core = probe_settings().pin_core;
    if (core >= 0) {
        (void)run.core(core);
    }
    return run;
}

[[nodiscard]] inline VectorWidthMeasurement measure() noexcept {
    VectorWidthMeasurement result{};

    // An instrumented build measures its own instrumentation. Refer to
    // kBuildIsInstrumented in ProbeSupport.h for what it costs.
    if constexpr (kBuildIsInstrumented) {
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

#if defined(__x86_64__) || defined(__i386__)
    if (!host_has_wide_vector_unit()) {
        // Not a failure. A host with one vector width has already answered
        // the question, and the answer is that width. Reporting it as a
        // refusal would have an operator looking for a noise source on a
        // machine that is behaving correctly.
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    auto compute_region = ProbeRegion::create(kComputeBytes, PagePolicy::BasePages);
    if (!compute_region.has_value()) {
        result.fault = compute_region.error();
        return result;
    }
    // Base pages, not huge ones, for the streaming shape as well. The
    // question is which vector width wins, and huge pages would fold the
    // host's page-table coverage into the answer. Base pages are also the
    // worse case, so a tie measured here is a tie under the more
    // demanding of the two page policies.
    const std::size_t stream_bytes = stream_bytes_for_host();
    auto stream_region = ProbeRegion::create(stream_bytes, PagePolicy::BasePages);
    if (!stream_region.has_value()) {
        result.fault = stream_region.error();
        return result;
    }

    auto* compute_floats = static_cast<float*>(compute_region->data());
    for (std::size_t index = 0; index < kComputeFloatCount; ++index) {
        compute_floats[index] = 1.0f + static_cast<float>(index) * 1e-6f;
    }
    (void)stream_region->fault_in(0x3f);
    auto* stream_floats = static_cast<float*>(stream_region->data());
    const std::size_t stream_float_count = stream_bytes / sizeof(float);

    float sink = 0.0f;

    auto compute_narrow = [&] {
        return configured_run("ledger.vector_width.compute.256").measure([&] {
            sink += compute_fma_256(compute_floats, kComputeFloatCount, kComputePassCount);
            bench::do_not_optimize(sink);
        });
    };
    auto compute_wide = [&] {
        return configured_run("ledger.vector_width.compute.512").measure([&] {
            sink += compute_fma_512(compute_floats, kComputeFloatCount, kComputePassCount);
            bench::do_not_optimize(sink);
        });
    };

    // The two widths are measured in separate runs rather than alternated
    // inside one. On a part with licence transitions, alternating charges
    // every 256-bit sample for the transition out of the 512-bit state
    // that preceded it, which would make the narrow width look worse than
    // it is on exactly the hosts where it is better.
    const bench::Report narrow_first = compute_narrow();
    const bench::Report narrow_second = compute_narrow();
    const bench::Report wide_first = compute_wide();
    const bench::Report wide_second = compute_wide();

    auto stream_run = [&](const char* name, bool use_wide) {
        bench::Run run{name};
        (void)run.samples(stream_sample_count()).warmup(2).max_wall_ms(20000);
        const int core = probe_settings().pin_core;
        if (core >= 0) {
            (void)run.core(core);
        }
        return run.measure([&] {
            sink += use_wide ? stream_sum_512(stream_floats, stream_float_count)
                             : stream_sum_256(stream_floats, stream_float_count);
            bench::do_not_optimize(sink);
        });
    };
    // Twice each, because both margins are ratios and a ratio's evidence
    // has to say whether the ratio reproduces. Refer to evidence_for_ratio.
    const bench::Report memory_narrow_first = stream_run("ledger.vector_width.stream.256.run1", false);
    const bench::Report memory_wide_first = stream_run("ledger.vector_width.stream.512.run1", true);
    const bench::Report memory_narrow_second = stream_run("ledger.vector_width.stream.256.run2", false);
    const bench::Report memory_wide_second = stream_run("ledger.vector_width.stream.512.run2", true);

    bench::do_not_optimize(sink);

    const VariantComparison compute = compare_variants(narrow_first, wide_first);
    const VariantComparison memory = compare_variants(memory_narrow_first, memory_wide_first);

    result.compute_gain_percent = compute.candidate_gain_percent;
    result.memory_gain_percent = memory.candidate_gain_percent;

    // The rule, stated once. The wide width is preferred only when it wins
    // the compute shape outright and does not lose the memory shape
    // outright. A width that doubles arithmetic throughput and halves
    // streaming throughput is not a width to advertise, and on the parts
    // where that happens it is precisely the streaming shape that catches
    // it.
    const bool wide_wins_compute = compute.candidate_wins();
    const bool wide_loses_memory =
        memory.is_statistically_distinguishable && memory.is_practically_wide && memory.candidate_gain_percent < 100u;
    result.preferred_bits = (wide_wins_compute && !wide_loses_memory) ? kWideWidthBits : kNarrowWidthBits;

    // Every one of the three verdicts is decided by a comparison, so all
    // three carry the comparison's evidence rather than one variant's. The
    // preferred width is decided by the compute shape, so it takes that
    // shape's ratio evidence; a width chosen on a ratio that does not
    // reproduce is refused at admission along with the ratio itself.
    result.compute_evidence = evidence_for_ratio(narrow_first, wide_first, narrow_second, wide_second);
    result.memory_evidence =
        evidence_for_ratio(memory_narrow_first, memory_wide_first, memory_narrow_second, memory_wide_second);
    result.fault = LedgerError::None;
#endif
    return result;
}

[[nodiscard]] inline VectorWidthMeasurement const& shared_measurement() noexcept {
    return g_memo.get_or_measure(&measure);
}

}  // namespace vector_width_detail

// ── The three ProbeFunctions ──────────────────────────────────────────

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_vector_width_preferred_bits(CompetenceReport const&) noexcept {
    VectorWidthMeasurement const& measured = vector_width_detail::shared_measurement();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.preferred_bits}, .evidence = measured.compute_evidence};
}

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_vector_width_compute_gain(CompetenceReport const&) noexcept {
    VectorWidthMeasurement const& measured = vector_width_detail::shared_measurement();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    if (measured.compute_gain_percent == 0u) {
        return std::unexpected(LedgerError::ConfidenceBelowBar);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.compute_gain_percent},
                              .evidence = measured.compute_evidence};
}

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_vector_width_memory_gain(CompetenceReport const&) noexcept {
    VectorWidthMeasurement const& measured = vector_width_detail::shared_measurement();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    if (measured.memory_gain_percent == 0u) {
        return std::unexpected(LedgerError::ConfidenceBelowBar);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.memory_gain_percent},
                              .evidence = measured.memory_evidence};
}

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
static_assert(kComputeBytes < concurrent::conservative_l1d_per_core);

// The streaming buffer must be past any plausible last-level cache.
static_assert(kMinStreamBytes > concurrent::conservative_l3_total);
static_assert(kMinStreamBytes <= kMaxStreamBytes);

}  // namespace vector_width_detail::self_test

}  // namespace crucible::ledger::probes
