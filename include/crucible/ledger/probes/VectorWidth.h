#pragma once

// Is the wide vector unit actually faster on THIS host?
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

#include <crucible/ledger/ProbeSupport.h>
#include <fixy/concurrent/Topology.h>
#include <fixy/concurrent/WorkingSet.h>
#include <foundation/Saturate.h>
#include <foundation/Simd.h>

#include <algorithm>
#include <bit>
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

// The streaming buffer is sized against the last-level cache that the
// measuring thread can fill, so the pass is bound by DRAM.  The target
// is kStreamCacheMultiple times that cache.  The floor serves a machine
// whose cache probe failed.  The ceiling keeps a part with a very large
// last-level cache from asking for a multi-gigabyte scratch region.  A
// buffer that the ceiling holds below kMinStreamCacheMultiple times the
// cache is not bound by DRAM, and the probe refuses the measurement.
inline constexpr std::size_t kMinStreamBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxStreamBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::size_t kStreamCacheMultiple = 8;
inline constexpr std::size_t kMinStreamCacheMultiple = 4;

// The last-level cache that the streaming pass can fill.  The bench
// harness pins the measuring thread to one core, and a pinned thread
// fills only the L3 instance of that core.  Topology reads the size of
// one instance from sysfs, and it assumes that each instance has that
// size.  The whole L3 of a machine is larger: 24 instances of 32 MiB on
// the two-socket bench host.  A thread that is not pinned can fill more
// than one instance, so measure() refuses a streaming run whose pin
// failed.
[[nodiscard]] inline std::size_t reachable_last_level_bytes(::fixy::concurrent::Topology const& topology) noexcept {
    return topology.l3_total_bytes();
}

// The size of the streaming buffer for a reachable last-level cache, or
// NotApplicableOnThisHost when the ceiling holds the buffer below
// kMinStreamCacheMultiple times that cache.
[[nodiscard]] constexpr std::expected<std::size_t, LedgerError> stream_bytes_for(std::size_t reachable_bytes) noexcept {
    const std::size_t wanted = ::foundation::sat::mul_sat(reachable_bytes, kStreamCacheMultiple);
    const std::size_t sized = std::clamp(wanted, kMinStreamBytes, kMaxStreamBytes);
    if (sized / kMinStreamCacheMultiple < reachable_bytes) {
        return std::unexpected(LedgerError::NotApplicableOnThisHost);
    }
    return sized;
}

[[nodiscard]] inline std::expected<std::size_t, LedgerError> stream_bytes_for_host() noexcept {
    return stream_bytes_for(reachable_last_level_bytes(::fixy::concurrent::Topology::instance()));
}

// The compute shape's call is microseconds, so it takes the configured
// sample count. The streaming shape's call is milliseconds, so taking the
// same number would run for an hour; it is divided down and then floored
// at the ledger's own minimum, because a sample count below that bar is
// refused at admission anyway and measuring it would be wasted time.  A
// positive stream_sample_count of the settings replaces that derived count.
[[nodiscard]] inline std::size_t stream_sample_count() noexcept {
    const ProbeSettings settings = probe_settings();
    if (settings.stream_sample_count > 0u) {
        return settings.stream_sample_count;
    }
    return std::clamp<std::size_t>(settings.sample_count / 64u, kMinSampleCount, 256u);
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
//
// The kernels call GCC builtins and vector operations, so this header
// includes no intrinsics header.  Each register type is a vector of float.
// The may_alias attribute lets a load or a store reach a float buffer.

using Floats256 [[gnu::vector_size(32), gnu::may_alias]] = float;
using Floats512 [[gnu::vector_size(64), gnu::may_alias]] = float;
using Floats128 = ::foundation::simd::vec<float, 4>::raw_type;
using Doubles256 = ::foundation::simd::vec<double, 4>::raw_type;
using Doubles512 = ::foundation::simd::vec<double, 8>::raw_type;
using Ints128 = ::foundation::simd::vec<int, 4>::raw_type;

// Each operation of a kernel is a function call.  GCC evaluates the
// arguments of a call from right to left, and that order sets the register
// allocation of the kernel.  A change of the machine code of a kernel
// changes what the probe measures.

// An aligned load or store of one register.  The buffer of a probe region
// is page aligned, and each kernel steps whole registers.
[[nodiscard]] CRUCIBLE_INLINE Floats256 load_floats256(const float* source) noexcept {
    return *static_cast<const Floats256*>(static_cast<const void*>(source));
}

[[nodiscard]] CRUCIBLE_INLINE Floats512 load_floats512(const float* source) noexcept {
    return *static_cast<const Floats512*>(static_cast<const void*>(source));
}

CRUCIBLE_INLINE void store_floats256(float* target, Floats256 value) noexcept {
    *static_cast<Floats256*>(static_cast<void*>(target)) = value;
}

[[nodiscard]] CRUCIBLE_INLINE Floats256 add_floats256(Floats256 lhs, Floats256 rhs) noexcept { return lhs + rhs; }

[[nodiscard]] CRUCIBLE_INLINE Floats512 add_floats512(Floats512 lhs, Floats512 rhs) noexcept { return lhs + rhs; }

[[gnu::target("avx2,fma")]] [[nodiscard]] CRUCIBLE_INLINE Floats256 fmadd_floats256(Floats256 factor, Floats256 scale,
                                                                                    Floats256 addend) noexcept {
    return __builtin_ia32_vfmaddps256(factor, scale, addend);
}

// The write mask of all ones updates each lane, and the rounding operand
// keeps the current rounding direction.
[[gnu::target("avx512f")]] [[nodiscard]] CRUCIBLE_INLINE Floats512 fmadd_floats512(Floats512 factor, Floats512 scale,
                                                                                   Floats512 addend) noexcept {
    constexpr short all_lanes = -1;
    constexpr int current_rounding = 4;
    return __builtin_ia32_vfmaddps512_mask(factor, scale, addend, all_lanes, current_rounding);
}

// One 256-bit half of a 512-bit register.  The extract builtin takes a
// merge source, and the write mask of all ones takes no lane from it.
[[gnu::target("avx512f")]] [[nodiscard]] CRUCIBLE_INLINE Floats256 half_of_floats512(Floats512 value,
                                                                                     int half) noexcept {
    constexpr unsigned char all_lanes = 0xFF;
    return std::bit_cast<Floats256>(
        __builtin_ia32_extractf64x4_mask(std::bit_cast<Doubles512>(value), half, Doubles256{}, all_lanes));
}

// The sum of the 16 lanes.  The two 256-bit halves add first, then the two
// 128-bit halves, then the two 64-bit halves, then the two lanes.
[[gnu::target("avx512f")]] [[nodiscard]] CRUCIBLE_INLINE float sum_floats512(Floats512 value) noexcept {
    const Floats256 upper = half_of_floats512(value, 1);
    const Floats256 lower = half_of_floats512(value, 0);
    const Floats256 halves = upper + lower;
    const Floats128 upper_quarter = __builtin_ia32_vextractf128_ps256(halves, 1);
    const Floats128 lower_quarter = __builtin_ia32_vextractf128_ps256(halves, 0);
    const Floats128 quarters = upper_quarter + lower_quarter;
    const Floats128 swapped = __builtin_shuffle(quarters, Ints128{2, 3, 0, 1});
    const Floats128 pairs = quarters + swapped;
    return pairs[0] + pairs[1];
}

[[gnu::target("avx2,fma"), gnu::noinline]] inline float compute_fma_256(const float* buffer, std::size_t float_count,
                                                                        std::size_t passes) noexcept {
    Floats256 lane0{};
    Floats256 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    Floats256 lane4 = lane0, lane5 = lane0, lane6 = lane0, lane7 = lane0;
    const Floats256 multiplier = ::foundation::simd::vec<float, 8>{1.0000001f}.v_;
    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t index = 0; index + 64 <= float_count; index += 64) {
            lane0 = fmadd_floats256(load_floats256(buffer + index + 0), multiplier, lane0);
            lane1 = fmadd_floats256(load_floats256(buffer + index + 8), multiplier, lane1);
            lane2 = fmadd_floats256(load_floats256(buffer + index + 16), multiplier, lane2);
            lane3 = fmadd_floats256(load_floats256(buffer + index + 24), multiplier, lane3);
            lane4 = fmadd_floats256(load_floats256(buffer + index + 32), multiplier, lane4);
            lane5 = fmadd_floats256(load_floats256(buffer + index + 40), multiplier, lane5);
            lane6 = fmadd_floats256(load_floats256(buffer + index + 48), multiplier, lane6);
            lane7 = fmadd_floats256(load_floats256(buffer + index + 56), multiplier, lane7);
        }
    }
    lane0 = add_floats256(add_floats256(lane0, lane1), add_floats256(lane2, lane3));
    lane4 = add_floats256(add_floats256(lane4, lane5), add_floats256(lane6, lane7));
    lane0 = add_floats256(lane0, lane4);
    alignas(32) float out[8]{};
    store_floats256(out, lane0);
    return out[0] + out[1] + out[2] + out[3] + out[4] + out[5] + out[6] + out[7];
}

[[gnu::target("avx512f"), gnu::noinline]] inline float compute_fma_512(const float* buffer, std::size_t float_count,
                                                                       std::size_t passes) noexcept {
    Floats512 lane0{};
    Floats512 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    Floats512 lane4 = lane0, lane5 = lane0, lane6 = lane0, lane7 = lane0;
    const Floats512 multiplier = ::foundation::simd::vec<float, 16>{1.0000001f}.v_;
    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t index = 0; index + 128 <= float_count; index += 128) {
            lane0 = fmadd_floats512(load_floats512(buffer + index + 0), multiplier, lane0);
            lane1 = fmadd_floats512(load_floats512(buffer + index + 16), multiplier, lane1);
            lane2 = fmadd_floats512(load_floats512(buffer + index + 32), multiplier, lane2);
            lane3 = fmadd_floats512(load_floats512(buffer + index + 48), multiplier, lane3);
            lane4 = fmadd_floats512(load_floats512(buffer + index + 64), multiplier, lane4);
            lane5 = fmadd_floats512(load_floats512(buffer + index + 80), multiplier, lane5);
            lane6 = fmadd_floats512(load_floats512(buffer + index + 96), multiplier, lane6);
            lane7 = fmadd_floats512(load_floats512(buffer + index + 112), multiplier, lane7);
        }
    }
    lane0 = add_floats512(add_floats512(lane0, lane1), add_floats512(lane2, lane3));
    lane4 = add_floats512(add_floats512(lane4, lane5), add_floats512(lane6, lane7));
    return sum_floats512(add_floats512(lane0, lane4));
}

[[gnu::target("avx2"), gnu::noinline]] inline float stream_sum_256(const float* buffer,
                                                                   std::size_t float_count) noexcept {
    Floats256 lane0{};
    Floats256 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    for (std::size_t index = 0; index + 32 <= float_count; index += 32) {
        lane0 = add_floats256(lane0, load_floats256(buffer + index + 0));
        lane1 = add_floats256(lane1, load_floats256(buffer + index + 8));
        lane2 = add_floats256(lane2, load_floats256(buffer + index + 16));
        lane3 = add_floats256(lane3, load_floats256(buffer + index + 24));
    }
    lane0 = add_floats256(add_floats256(lane0, lane1), add_floats256(lane2, lane3));
    alignas(32) float out[8]{};
    store_floats256(out, lane0);
    return out[0] + out[1] + out[2] + out[3] + out[4] + out[5] + out[6] + out[7];
}

[[gnu::target("avx512f"), gnu::noinline]] inline float stream_sum_512(const float* buffer,
                                                                      std::size_t float_count) noexcept {
    Floats512 lane0{};
    Floats512 lane1 = lane0, lane2 = lane0, lane3 = lane0;
    for (std::size_t index = 0; index + 64 <= float_count; index += 64) {
        lane0 = add_floats512(lane0, load_floats512(buffer + index + 0));
        lane1 = add_floats512(lane1, load_floats512(buffer + index + 16));
        lane2 = add_floats512(lane2, load_floats512(buffer + index + 32));
        lane3 = add_floats512(lane3, load_floats512(buffer + index + 48));
    }
    lane0 = add_floats512(add_floats512(lane0, lane1), add_floats512(lane2, lane3));
    return sum_floats512(lane0);
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

[[nodiscard]] inline VectorWidthMeasurement measure([[maybe_unused]] LedgerIoCtx const& ctx) noexcept {
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

    auto compute_region = ProbeRegion::create(ctx, kComputeBytes, PagePolicy::BasePages);
    if (!compute_region.has_value()) {
        result.fault = compute_region.error();
        return result;
    }
    // Base pages, not huge ones, for the streaming shape as well. The
    // question is which vector width wins, and huge pages would fold the
    // host's page-table coverage into the answer. Base pages are also the
    // worse case, so a tie measured here is a tie under the more
    // demanding of the two page policies.
    const std::expected<std::size_t, LedgerError> sized_stream = stream_bytes_for_host();
    if (!sized_stream.has_value()) {
        result.fault = sized_stream.error();
        return result;
    }
    const std::size_t stream_bytes = *sized_stream;
    auto stream_region = ProbeRegion::create(ctx, stream_bytes, PagePolicy::BasePages);
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

    // One streaming pass is milliseconds, so a batch of one pass is far above
    // the floor of the timer.  The explicit batch skips the pilot of the
    // harness, which would time one hundred passes to find the same batch.
    auto stream_run = [&](const char* name, bool use_wide) {
        bench::Run run{name};
        (void)run.samples(stream_sample_count()).warmup(2).batch(1).max_wall_ms(20000);
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

    // The buffer is sized for the L3 instance of one core.  A run whose
    // pin failed can move across instances and fill more than one, so its
    // pass is not known to be bound by DRAM.
    const bool is_every_stream_run_pinned =
        memory_narrow_first.pinned_cpu.is_valid() && memory_wide_first.pinned_cpu.is_valid()
        && memory_narrow_second.pinned_cpu.is_valid() && memory_wide_second.pinned_cpu.is_valid();
    if (!is_every_stream_run_pinned) {
        result.fault = LedgerError::ConfidenceBelowBar;
        return result;
    }

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

[[nodiscard]] inline VectorWidthMeasurement const& shared_measurement(LedgerIoCtx const& ctx) noexcept {
    return g_memo.get_or_measure([&ctx] { return measure(ctx); });
}

}  // namespace vector_width_detail

// ── The three ProbeFunctions ──────────────────────────────────────────

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_vector_width_preferred_bits(LedgerIoCtx const& ctx, CompetenceReport const&) noexcept {
    VectorWidthMeasurement const& measured = vector_width_detail::shared_measurement(ctx);
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.preferred_bits}, .evidence = measured.compute_evidence};
}

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_vector_width_compute_gain(LedgerIoCtx const& ctx, CompetenceReport const&) noexcept {
    VectorWidthMeasurement const& measured = vector_width_detail::shared_measurement(ctx);
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
probe_vector_width_memory_gain(LedgerIoCtx const& ctx, CompetenceReport const&) noexcept {
    VectorWidthMeasurement const& measured = vector_width_detail::shared_measurement(ctx);
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
