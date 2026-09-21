// What the descriptor zeroing costs, and which half of it is removable.
//
// The recording kernel builds a Recording<Capacity> per intercepted
// operation and then fills the descriptors it needs.  Two separate zeroings
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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// Keeps a value the optimizer would otherwise discard.
template <typename T>
[[gnu::always_inline]] inline void keep(T& value) noexcept {
    asm volatile("" : "+m"(value) : : "memory");
}

// One sample is the average over a batch, which amortises the clock read
// without letting a single outlier vanish into a mean over everything.
constexpr int kBatch = 1000;
constexpr int kSamples = 2000;
constexpr int kRuns = 10;

struct Percentiles {
    double p50 = 0, p99 = 0, p999 = 0, max = 0;
};

[[nodiscard]] Percentiles percentiles_of(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double frac) {
        const auto idx = static_cast<std::size_t>(frac * static_cast<double>(samples.size() - 1));
        return samples[idx];
    };
    return {at(0.50), at(0.99), at(0.999), samples.back()};
}

// Scenario A: default-construct Count descriptors, which is the zeroing the
// non-static data-member initialisers perform.
template <std::size_t Count>
[[nodiscard]] double construct_batch_ns() {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kBatch; i++) {
        crucible::TensorMeta metas[Count]{};
        keep(metas);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(stop - start).count() / kBatch;
}

// Scenario B: the redundant second zero on one already-live descriptor.
[[nodiscard]] double reassign_batch_ns() {
    crucible::TensorMeta meta{};
    keep(meta);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kBatch; i++) {
        meta = {};
        keep(meta);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(stop - start).count() / kBatch;
}

void report(const char* label, double (*one_batch)(), std::size_t descriptors) {
    std::vector<double> run_p50s;
    Percentiles last{};
    for (int run = 0; run < kRuns; run++) {
        std::vector<double> samples;
        samples.resize(kSamples);
        for (int s = 0; s < kSamples; s++) samples[static_cast<std::size_t>(s)] = one_batch();
        last = percentiles_of(samples);
        run_p50s.push_back(last.p50);
    }
    std::sort(run_p50s.begin(), run_p50s.end());
    const double median_p50 = run_p50s[run_p50s.size() / 2];
    const double spread = (run_p50s.back() - run_p50s.front()) / median_p50;
    const double per_descriptor = descriptors ? median_p50 / static_cast<double>(descriptors) : median_p50;
    std::printf("%-44s p50=%7.2f  p99=%7.2f  p99.9=%7.2f  max=%8.2f  per-descriptor=%6.2f  spread=%5.1f%%  %s\n", label,
                median_p50, last.p99, last.p999, last.max, per_descriptor, spread * 100.0,
                spread <= 0.05 ? "" : "[VOID: spread over 5%]");
}

}  // namespace

int main() {
    std::printf("descriptor zeroing, sizeof(TensorMeta)=%zu, %d runs of %d batch-averaged samples\n\n",
                sizeof(crucible::TensorMeta), kRuns, kSamples);
    report("A: default-construct 1 descriptor", construct_batch_ns<1>, 1);
    report("A: default-construct 3 descriptors", construct_batch_ns<3>, 3);
    report("A: default-construct 8 descriptors", construct_batch_ns<8>, 8);
    report("A: default-construct 32 descriptors", construct_batch_ns<32>, 32);
    report("B: redundant second zero, 1 descriptor", reassign_batch_ns, 1);
    return 0;
}
