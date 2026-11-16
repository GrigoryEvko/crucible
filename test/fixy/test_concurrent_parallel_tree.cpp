// The parallel tree forms of fixy/concurrent/ParallelTree.h, run for real.
//
// Each form runs with 1, 2, 4, 8 and 64 shards.  The coupling WaitOnEachOther
// gives each shard its own thread, so those runs use 1, 2, 4, 8 and 64
// threads.  The coupling Independent takes the threads of the parallelism
// rule.  Each result must have the bits of the form of one thread of
// foundation/core.  A combine that records the id of each thread that calls
// it proves that the runs used the stated count of threads.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelTree.h>
#include <fixy/os/Process.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>
#include <foundation/effects/Effect.h>

#include "../foundation/philox_stream.h"
#include "../test_assert.h"

#include <cstddef>
#include <cstdint>

namespace {

namespace eff = ::foundation::effects;
namespace spawn = ::fixy::spawn;

using ::foundation::core::HeapArray;
using ::foundation::core::View;

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

[[nodiscard]] ::foundation::core::Alloc test_alloc() noexcept { return eff::testing::test().alloc; }

[[nodiscard]] float draw_float(::crucible::test::PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    auto const mantissa = static_cast<std::uint32_t>(bits & 0x7FFFFFu);
    auto const exponent = static_cast<std::uint32_t>(127 + (bits >> 23) % 24 - 12);
    auto const sign = static_cast<std::uint32_t>((bits >> 40) & 1u);
    return ::fixy::bit_cast<float>((sign << 31) | (exponent << 23) | mantissa);
}

[[nodiscard]] HeapArray<float> draw_floats(std::size_t count, std::uint64_t seed) noexcept {
    HeapArray<float> run = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
    ::crucible::test::PhiloxStream stream{seed};
    for (float& value : run) {
        value = draw_float(stream);
    }
    return run;
}

[[nodiscard]] bool same_bits(float left, float right) noexcept {
    return ::fixy::bit_cast<std::uint32_t>(left) == ::fixy::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool same_runs(View<float const> left, View<float const> right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_bits(::crucible::test::element_at(left, index), ::crucible::test::element_at(right, index))) {
            return false;
        }
    }
    return true;
}

// The counts of the runs: no element, one, less than one grain, the counts
// around one grain, and 65 grains with a short last grain.
constexpr std::size_t kLongest = (std::size_t{1} << 20) + 7;
constexpr ::fixy::FixedArray<std::size_t, 7> kCounts{0, 1, 1000, 16383, 16384, 16385, kLongest};

// The counts of a run of 64 threads: each run starts 64 threads, and a start
// costs about a millisecond under AddressSanitizer.
constexpr ::fixy::FixedArray<std::size_t, 3> kWideCounts{0, 16385, kLongest};

// The parallel tree sum and the parallel sum scans give the bits of the
// forms of one thread for each count.
template <spawn::BodyCoupling Coupling, std::size_t Shards, std::size_t Counts>
void check_sums_and_scans(View<float const> all, ::fixy::FixedArray<std::size_t, Counts> const& counts) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    for (std::size_t const count : counts) {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        float const parallel = ::fixy::concurrent::parallel_tree_sum<Coupling, Shards>(ctx, test_alloc(), part);
        assert(same_bits(parallel, ::foundation::core::tree_sum(part)));

        HeapArray<float> parallel_scan = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
        HeapArray<float> single_scan = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
        ::fixy::concurrent::parallel_sum_scan<Coupling, Shards>(
            ctx, test_alloc(), ::foundation::core::ScanKind::Inclusive, part, parallel_scan.view());
        assert(::foundation::core::inclusive_sum_scan(part, single_scan.view()).is_ok());
        assert(same_runs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()}));
        ::fixy::concurrent::parallel_sum_scan<Coupling, Shards>(
            ctx, test_alloc(), ::foundation::core::ScanKind::Exclusive, part, parallel_scan.view());
        assert(::foundation::core::exclusive_sum_scan(part, single_scan.view()).is_ok());
        assert(same_runs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()}));
    }
}

// The parallel segmented sum gives the bits of segmented_tree_sum, for
// segments of drawn lengths, some of them empty.
template <spawn::BodyCoupling Coupling, std::size_t Shards>
void check_segments(View<float const> all, ::crucible::test::PhiloxStream& stream) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    constexpr std::size_t kSegments = 300;
    HeapArray<std::size_t> offsets = ::foundation::core::mint_heap_array(test_alloc(), kSegments + 1, std::size_t{0});
    View<std::size_t> const offset_view = offsets.view();
    std::size_t position = 0;
    for (std::size_t segment = 0; segment <= kSegments; ++segment) {
        ::crucible::test::element_at(offset_view, segment) = position;
        position += stream.below(4) == 0 ? 0 : stream.below(9000);
    }
    View<float const> const part = all.window(0, ::crucible::test::element_at(offset_view, kSegments)).expect("fits");
    HeapArray<float> parallel = ::foundation::core::mint_heap_array(test_alloc(), kSegments, 0.0f);
    HeapArray<float> single = ::foundation::core::mint_heap_array(test_alloc(), kSegments, 0.0f);
    View<std::size_t const> const checked{offset_view};
    ::fixy::concurrent::parallel_segmented_tree_sum<Coupling, Shards>(ctx, part, checked, parallel.view());
    assert(::foundation::core::segmented_tree_sum(part, checked, single.view()).is_ok());
    assert(same_runs(View<float const>{parallel.view()}, View<float const>{single.view()}));
}

// The run of the recording combine that the calling thread last recorded.
// A thread records its id on its first call in each run, so a run makes one
// system call for each thread and not one for each combine.
constinit thread_local std::uint64_t recorded_generation = 0;

// The number of the next run of the recording combine.  Only the thread of
// main writes it.
constinit std::uint64_t next_generation = 0;

// A combine that is neither associative nor commutative, and that records
// the id of each thread that calls it in a table of slots.  A slot holds 0 or
// one id, and each id takes one slot.
struct RecordingMix {
    View<::fixy::Atomic<std::uint32_t>> slots;
    std::uint64_t generation = 0;

    [[nodiscard]] std::uint64_t operator()(std::uint64_t const& left, std::uint64_t const& right) const noexcept {
        record_thread();
        return ::fixy::rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    }

    void record_thread() const noexcept {
        if (recorded_generation == generation) return;
        recorded_generation = generation;
        ::fixy::TestRunnerCtx const runner{eff::testing::test()};
        auto const id = static_cast<std::uint32_t>(::fixy::process::current_thread(runner));
        for (::fixy::Atomic<std::uint32_t>& slot : slots) {
            std::uint32_t const held = slot.load_acquire();
            if (held == id) return;
            if (held != 0) continue;
            if (slot.cas_acq_rel(0, id).is_ok()) return;
        }
        ::fixy::fatal("the table of the threads of a combine is full");
    }
};

// The count of the slots that hold an id.
[[nodiscard]] std::size_t threads_in(View<::fixy::Atomic<std::uint32_t>> slots) noexcept {
    std::size_t count = 0;
    for (::fixy::Atomic<std::uint32_t>& slot : slots) {
        if (slot.load_acquire() != 0) ++count;
    }
    return count;
}

// The parallel tree reduce with the recording combine gives the value of
// tree_reduce, and the combine ran on Shards threads and on the calling
// thread.  One shard runs on the calling thread alone.
template <std::size_t Shards>
void check_reduce_on_stated_threads(View<std::uint64_t const> all) noexcept {
    BgCtx const ctx{eff::testing::bg()};
    // One grain for each shard and a short last grain, so each shard holds at
    // least one grain of combines.
    View<std::uint64_t const> const run =
        all.window(0, Shards * ::fixy::concurrent::parallel_tree_grain + 7).expect("the run fits");
    HeapArray<::fixy::Atomic<std::uint32_t>> table =
        ::foundation::core::mint_heap_array_with<::fixy::Atomic<std::uint32_t>>(
            test_alloc(), 256, [](std::size_t) noexcept { return ::fixy::Atomic<std::uint32_t>{0}; });
    RecordingMix const mix{table.view(), ++next_generation};
    auto const parallel = ::fixy::concurrent::parallel_tree_reduce<spawn::BodyCoupling::WaitOnEachOther, Shards>(
        ctx, test_alloc(), run, mix);
    std::size_t const threads = threads_in(table.view());
    std::uint64_t const single = ::foundation::core::tree_reduce(run, mix).expect("the run has elements");
    assert(parallel.is_some());
    for (std::uint64_t const value : parallel) {
        assert(value == single);
    }
    assert(threads == (Shards == 1 ? 1 : Shards + 1));
}

void test_sums_and_scans_on_each_thread_count() {
    HeapArray<float> const run = draw_floats(kLongest, 0x9A4A0001u);
    View<float const> const all{run.view()};
    check_sums_and_scans<spawn::BodyCoupling::WaitOnEachOther, 1>(all, kCounts);
    check_sums_and_scans<spawn::BodyCoupling::WaitOnEachOther, 2>(all, kCounts);
    check_sums_and_scans<spawn::BodyCoupling::WaitOnEachOther, 4>(all, kCounts);
    check_sums_and_scans<spawn::BodyCoupling::WaitOnEachOther, 8>(all, kCounts);
    check_sums_and_scans<spawn::BodyCoupling::WaitOnEachOther, 64>(all, kWideCounts);
    check_sums_and_scans<spawn::BodyCoupling::Independent, 1>(all, kCounts);
    check_sums_and_scans<spawn::BodyCoupling::Independent, 8>(all, kCounts);
    check_sums_and_scans<spawn::BodyCoupling::Independent, 64>(all, kCounts);
}

void test_segments_on_each_thread_count() {
    HeapArray<float> const run = draw_floats(300 * 9000, 0x9A4A0002u);
    View<float const> const all{run.view()};
    ::crucible::test::PhiloxStream stream{0x9A4A0003u};
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 1>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 2>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 4>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 8>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 64>(all, stream);
    check_segments<spawn::BodyCoupling::Independent, 64>(all, stream);
}

// Each count of threads gives the value of the reduce of one thread, and
// two runs of 64 threads give two schedules of the operating system.
void test_reduce_on_stated_threads() {
    ::crucible::test::PhiloxStream stream{0x9A4A0004u};
    HeapArray<std::uint64_t> run = ::foundation::core::mint_heap_array(test_alloc(), kLongest, std::uint64_t{0});
    for (std::uint64_t& value : run) {
        value = stream.next_wide();
    }
    View<std::uint64_t const> const all{run.view()};
    check_reduce_on_stated_threads<1>(all);
    check_reduce_on_stated_threads<2>(all);
    check_reduce_on_stated_threads<4>(all);
    check_reduce_on_stated_threads<8>(all);
    for (int repeat = 0; repeat < 2; ++repeat) {
        check_reduce_on_stated_threads<64>(all);
    }
}

}  // namespace

int main() {
    test_sums_and_scans_on_each_thread_count();
    test_segments_on_each_thread_count();
    test_reduce_on_stated_threads();
    crucible::test::pass("test_concurrent_parallel_tree: all tests passed\n");
    return 0;
}
