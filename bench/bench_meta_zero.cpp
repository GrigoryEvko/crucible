// What the descriptor zeroing costs, and which half of it is removable.
//
// The recording kernel of an operator with an exact bound puts a list of
// Capacity descriptors on its stack per intercepted operation, and then fills
// the descriptors it needs.  (A kernel with no exact bound takes its
// descriptors from the record stack of the Vigil.)  Two separate zeroings
// happen on that path and only one of them can be taken away:
//
//   1. Default-construction of the descriptor array.  TensorDimArray holds
//      `int64_t lanes_[kMaxTensorNDim]{}` (TensorMeta.h) and TensorMeta gives
//      `sizes` and `strides` non-static data-member initialisers, so a
//      descriptor zeroes itself whenever it is default-constructed.  The
//      holder's own braces are not what causes this and removing them
//      removes nothing.  Scenario A prices it per descriptor count.
//
//   2. A second `meta = {}` on each descriptor that is about to be filled.
//      This one is redundant with (1) for every slot that gets filled, and
//      it is the single plausibly-removable zero on the path.  Scenario B
//      prices it alone.
//
// Why the tail lanes are zeroed at all: not for the background dimension
// hash, which reduces under a prefix mask and never reads past ndim, but for
// Serialize.h's write_meta, which writes `sizeof(m.sizes)` verbatim to the
// wire.  A wire format reader is the constraint, so (1) cannot be dropped
// without changing that format.
//
// This translation unit names TensorMeta.h and no recording header, so its
// numbers do not move when the recording path is rewritten.  It needs no
// symbol from the library either, which is what let it be measured while
// that rewrite was mid-flight.

#include <crucible/TensorMeta.h>

#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

// One sample is the average over a batch, which amortises the clock read
// without letting a single outlier vanish into a mean over everything.
constexpr int kBatch = 1000;
constexpr std::size_t kSamples = 2000;
constexpr std::size_t kRuns = 10;

// The allocation capability of the samples of a run.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

struct Percentiles {
    double p50 = 0, p99 = 0, p999 = 0, max = 0;
};

// The value at PerMille thousandths of the samples in ascending order, as the
// index PerMille * (n - 1) / 1000 rounded down.  For the counts of the bench,
// the index is the index of the fraction in a double.
template <std::size_t PerMille>
    requires(PerMille <= 1000)
[[nodiscard]] double at_per_mille(::fixy::HeapArray<double> const& sorted) noexcept {
    const std::size_t idx = PerMille * (sorted.size() - 1) / 1000;
    return *sorted.get(idx).expect("a fraction in [0, 1] names a sample");
}

[[nodiscard]] Percentiles percentiles_of(::fixy::HeapArray<double> samples) {
    ::fixy::HeapArray<double> const sorted = bench::sorted_samples(std::move(samples));
    return {at_per_mille<500>(sorted), at_per_mille<990>(sorted), at_per_mille<999>(sorted),
            at_per_mille<1000>(sorted)};
}

// Scenario A: default-construct Count descriptors, which is the zeroing the
// non-static data-member initialisers perform.
template <std::size_t Count>
[[nodiscard]] double construct_batch_ns() {
    const std::uint64_t start = bench::detail::read_monotonic_ns();
    for (int i = 0; i < kBatch; i++) {
        ::fixy::FixedArray<crucible::TensorMeta, Count> metas{};
        bench::keep_in_memory(metas);
    }
    const std::uint64_t stop = bench::detail::read_monotonic_ns();
    return static_cast<double>(stop - start) / kBatch;
}

}  // namespace

// The raw floor of scenario A: the same descriptors in a C array.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a C array of descriptors is the raw floor of the zeroing of scenario A")
namespace {

template <std::size_t Count>
[[nodiscard]] double construct_c_array_batch_ns() {
    const std::uint64_t start = bench::detail::read_monotonic_ns();
    for (int i = 0; i < kBatch; i++) {
        crucible::TensorMeta metas[Count]{};
        bench::keep_in_memory(metas);
    }
    const std::uint64_t stop = bench::detail::read_monotonic_ns();
    return static_cast<double>(stop - start) / kBatch;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// Scenario B: the redundant second zero on one already-live descriptor.
[[nodiscard]] double reassign_batch_ns() {
    crucible::TensorMeta meta{};
    bench::keep_in_memory(meta);
    const std::uint64_t start = bench::detail::read_monotonic_ns();
    for (int i = 0; i < kBatch; i++) {
        meta = {};
        bench::keep_in_memory(meta);
    }
    const std::uint64_t stop = bench::detail::read_monotonic_ns();
    return static_cast<double>(stop - start) / kBatch;
}

// The runs of one scenario of Descriptors descriptors, and their line.
template <std::size_t Descriptors, typename Batch>
void report(::fixy::TextView label, Batch const& one_batch) {
    // Each mint calls its init in the order of the positions, so the runs and
    // their samples come one after the other, as in a loop.
    Percentiles last{};
    ::fixy::HeapArray<double> run_p50s =
        ::fixy::mint_heap_array_with<double>(kAlloc, kRuns, [&one_batch, &last](std::size_t) noexcept {
            last = percentiles_of(::fixy::mint_heap_array_with<double>(
                kAlloc, kSamples, [&one_batch](std::size_t) noexcept { return one_batch(); }));
            return last.p50;
        });
    ::fixy::HeapArray<double> const sorted_p50s = bench::sorted_samples(std::move(run_p50s));
    const double median_p50 = *sorted_p50s.get(kRuns / 2).expect("the middle run is below kRuns");
    const double spread = (at_per_mille<1000>(sorted_p50s) - at_per_mille<0>(sorted_p50s)) / median_p50;
    const double per_descriptor = Descriptors != 0 ? median_p50 / static_cast<double>(Descriptors) : median_p50;
    bench::print("{} p50={}  p99={}  p99.9={}  max={}  per-descriptor={}  spread={}%  {}\n",
                 ::fixy::Padded<44, ::fixy::Align::Left>{label}, ::fixy::Fixed<2, 7>{median_p50},
                 ::fixy::Fixed<2, 7>{last.p99}, ::fixy::Fixed<2, 7>{last.p999}, ::fixy::Fixed<2, 8>{last.max},
                 ::fixy::Fixed<2, 6>{per_descriptor}, ::fixy::Fixed<1, 5>{spread * 100.0},
                 spread <= 0.05 ? ::fixy::TextView{""} : ::fixy::TextView{"[VOID: spread over 5%]"});
}

}  // namespace

int main() {
    bench::print("descriptor zeroing, sizeof(TensorMeta)={}, {} runs of {} batch-averaged samples\n\n",
                 sizeof(crucible::TensorMeta), kRuns, kSamples);
    report<1>("A: default-construct 1 descriptor", construct_batch_ns<1>);
    report<3>("A: default-construct 3 descriptors", construct_batch_ns<3>);
    report<8>("A: default-construct 8 descriptors", construct_batch_ns<8>);
    report<32>("A: default-construct 32 descriptors", construct_batch_ns<32>);
    report<1>("A raw: C array of 1 descriptor", construct_c_array_batch_ns<1>);
    report<3>("A raw: C array of 3 descriptors", construct_c_array_batch_ns<3>);
    report<8>("A raw: C array of 8 descriptors", construct_c_array_batch_ns<8>);
    report<32>("A raw: C array of 32 descriptors", construct_c_array_batch_ns<32>);
    report<1>("B: redundant second zero, 1 descriptor", reassign_batch_ns);
    return 0;
}
