// Microbench of the sorts and the ordered search of foundation/core/Sort.h on
// one thread, each row of the base with the reference rows of the same input.
//
// THE INPUT
//   A pool of 2^24 floats with a random sign, a random mantissa and an
//   exponent from -12 to 11, the data of probe E4.  Before each sample, the
//   work buffer of a row takes the next window of the pool, so no two
//   samples in a row sort one input.  A row that sorts one input again and
//   again trains the branch predictor of the core on the branches of that
//   input: on a core of the bench partition, std::sort of the same 1024
//   floats makes about 2 branch misses for each call, and the sort of a new
//   window makes thousands.  The copy into the work buffer is outside the
//   timed region, so the time of a row is the sort alone.  The rows of a
//   sorted input sort one sorted window each time.
//
// THE ROWS OF THE BASE
//   sort_stably by Ascending        the radix sort of the keys from
//                                   stable_sort_radix_count elements
//   sort_stably by an order of the caller
//                                   the comparator form: the order of
//                                   Ascending as a function of the caller
//   sort_stably by order_by_key     records of a float key and a payload of
//                                   four bytes, by the radix sort of the keys
//                                   in each direction, and by the comparator
//                                   form of the same order
//   sort_by_network<N>              the network of N, which is not stable
//   find_lower_bound                one search of a sorted run of 2^20
//
// THE REFERENCES
//   Each reference row runs the raw form of a reference on the windows of the
//   row of the base, in the MEASURE region of this file.  After the rows of
//   one input, a line gives the median of a row of the base over the median
//   of each reference, in thousandths: 1000 is parity, and a number above
//   1000 is slower than the reference.
//     std::stable_sort, std::sort   libstdc++ with the same order: the floor
//     fluxsort                      the stable comparator sort of scandum, a
//                                   pin of utils/pins.txt.  The rows of
//                                   floats sort their bits as signed words by
//                                   the key of the total order, so the order
//                                   is the order of the row of the base
//     x86-simd-sort AVX2            the vector sort of numpy at x86-64-v3, a
//                                   pin of utils/pins.txt: the speed roof of
//                                   a sort by a key.  It is not stable, and
//                                   it sorts a float by <.  The pool holds no
//                                   NaN, and the order differs from the
//                                   total order only for -0 and +0
//     x86-simd-sort host            the same sort at the level of the host.
//                                   On a Zen 4 core its AVX-512 arm stores
//                                   each partition with vpcompressd to
//                                   memory, which the core runs in microcode
//   The rows of a pinned reference need its source in the tools directory
//   (python3 utils/bin/setup install fluxsort x86-simd-sort).  With no
//   source, the configure prints the command, and the bench omits the rows.
//   The Rust sorts of the state of the art, driftsort and ipnsort, have no
//   C or C++ form, so the commit bodies give their medians from a probe on
//   the same core and the same windows.
//
// Each line of a row gives the core of the row (the core that the harness
// reads after its pin, or cpu-1 for a row with no pin), the percentiles of
// one call in nanoseconds, the coefficient of variation in thousandths, and
// the rate in elements for each microsecond at the median.  The Report family
// writes integers only.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "bench_core_sort_references.h"
#include "bench_harness.h"

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::View;
using ::foundation::core::with_mut;

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The wall time of one row, in milliseconds.  A row of 2^20 elements takes
// some tens of milliseconds for each call, so the cap still gives tens of
// samples.
constexpr std::size_t kRowWallMs = 2000;

// The count of the floats of the pool.
constexpr std::size_t kPoolCount = std::size_t{1} << 24;

// The count of the largest rows: the L3 of the core holds the run and its
// scratch.
constexpr std::size_t kShared = std::size_t{1} << 20;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// One measured row: its name and its report, which the caller holds.
struct Row final {
    fixy::TextView name;
    bench::Report const& report;
};

// The row of the base and the row of one of its references.
struct Pair final {
    Row row;
    Row reference;
};

// Writes one line: the name of the row, the core of the row, the percentiles
// of one call, and the rate of the Elements of one call at the median.
template <std::size_t Elements>
void print_row(Row const& row) noexcept {
    bench::Report const& measured = row.report;
    double const median = measured.pct.p50 > 0.0 ? measured.pct.p50 : 1.0;
    fixy::report(fixy::Sink::Out,
                 "{}  core=cpu{}  p50={} ns  p99={} ns  max={} ns  cv={} permille  rate={} elements/us\n", row.name,
                 measured.pinned_cpu.raw(), nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(static_cast<double>(Elements) * 1000.0 / median + 0.5));
}

// Writes the median of the row over the median of its reference, in
// thousandths.
void print_ratio(Pair const& pair) noexcept {
    double const measured = pair.row.report.pct.p50;
    double const base = pair.reference.report.pct.p50;
    std::uint64_t const ratio = base > 0.0 ? static_cast<std::uint64_t>(measured / base * 1000.0 + 0.5) : 0;
    fixy::report(fixy::Sink::Out, "{}  ratio to {}={} permille\n", pair.row.name, pair.reference.name, ratio);
}

// The name of a row of Count elements, in the text that holds it.
template <std::size_t Count, std::size_t Capacity>
[[nodiscard]] fixy::TextView name_with_count(fixy::FixedText<Capacity>& text, fixy::TextView name) noexcept {
    return fixy::format(text, "{}, {}", name, Count).expect("the name of a row fits its text");
}

// Measures one row with the pin of CRUCIBLE_BENCH_CORE, the wall cap of
// kRowWallMs, and one call for each sample, so that the hook of each sample
// gives each call a new input.
template <typename Body>
[[nodiscard]] bench::Report measure_row(bench::Run&& run, Body&& body) {
    if (int const core = bench::env_core(); core >= 0) static_cast<void>(run.core(core));
    static_cast<void>(run.max_wall_ms(kRowWallMs));
    static_cast<void>(run.batch(1));
    return run.measure(static_cast<Body&&>(body));
}

// A float with a random sign, a random mantissa and an exponent from -12 to
// 11, from a linear congruential stream: the data of probe E4.
[[nodiscard]] HeapArray<float> draw_pool() noexcept {
    HeapArray<float> run = ::foundation::core::mint_heap_array(kAlloc, kPoolCount, 0.0f);
    std::uint64_t state = 0x9E3779B97F4A7C15u;
    for (float& value : run) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        auto const bits = static_cast<std::uint32_t>(state >> 32);
        std::uint32_t const exponent = 127u + (bits >> 23) % 24u - 12u;
        value = ::fixy::bit_cast<float>((bits & 0x80000000u) | (exponent << 23) | (bits & 0x7FFFFFu));
    }
    return run;
}

// The words of the float bits of the pool, as keys of an integer sort.
[[nodiscard]] HeapArray<std::uint32_t> words_of(View<float const> pool) noexcept {
    HeapArray<std::uint32_t> words = ::foundation::core::mint_heap_array(kAlloc, pool.size(), std::uint32_t{0});
    std::size_t position = 0;
    for (std::uint32_t& word : words) {
        word = ::fixy::bit_cast<std::uint32_t>(*pool.get(position).expect("the element fits the pool"));
        ++position;
    }
    return words;
}

// The words of 64 bits of the float bits of the pool, two floats for each
// word, as keys of eight bytes.
[[nodiscard]] HeapArray<std::uint64_t> wide_words_of(View<float const> pool) noexcept {
    HeapArray<std::uint64_t> words = ::foundation::core::mint_heap_array(kAlloc, pool.size() / 2, std::uint64_t{0});
    std::size_t position = 0;
    for (std::uint64_t& word : words) {
        std::uint64_t const low = ::fixy::bit_cast<std::uint32_t>(*pool.get(position).expect("the element fits"));
        std::uint64_t const high = ::fixy::bit_cast<std::uint32_t>(*pool.get(position + 1).expect("the element fits"));
        word = (high << 32) | low;
        position += 2;
    }
    return words;
}

// The order of Ascending on a float, as a function of the caller, so that a
// sort takes the path of a comparator of the caller.
struct FloatTotalOrder {
    [[nodiscard]] bool operator()(float const& left, float const& right) const noexcept {
        return ::foundation::core::encode_total_order_key(left) < ::foundation::core::encode_total_order_key(right);
    }
};

// A record of a float key and a payload, for the rows of an order by a key.
struct KeyedRecord final {
    float key = 0.0f;
    std::uint32_t payload = 0;
};

// The count of the records of their pool.
constexpr std::size_t kRecordPoolCount = std::size_t{1} << 22;

// The records of the first floats of the pool: the key of each record is a
// float of the pool, and its payload is its position.
[[nodiscard]] HeapArray<KeyedRecord> records_of(View<float const> pool) noexcept {
    HeapArray<KeyedRecord> records = ::foundation::core::mint_heap_array(kAlloc, kRecordPoolCount, KeyedRecord{});
    std::uint32_t position = 0;
    for (KeyedRecord& record : records) {
        record = KeyedRecord{*pool.get(position).expect("the key fits the pool"), position};
        ++position;
    }
    return records;
}

// The ascending order of the keys of the records as a function of the
// caller, so that a sort takes the comparator form.
struct RecordKeyOrder {
    [[nodiscard]] bool operator()(KeyedRecord const& left, KeyedRecord const& right) const noexcept {
        return ::foundation::core::Ascending{}(left.key, right.key);
    }
};

// The windows of Count elements of a pool, one after the other.  The first
// window comes again after the last one.
template <class T, std::size_t Count>
class WindowCursor final {
public:
    explicit WindowCursor(View<T const> pool) noexcept : pool_{pool} {}

    [[nodiscard]] View<T const> next() noexcept {
        std::size_t const windows = pool_.size() / Count;
        View<T const> const window = pool_.window((sample_ % windows) * Count, Count).expect("the window fits");
        ++sample_;
        return window;
    }

private:
    View<T const> pool_;
    std::size_t sample_ = 0;
};

// The stable sort of work with the scratch of scratch, through a frame of
// each, by an order.  True when the sort gave no error.
template <class T, class Less>
[[nodiscard]] bool sort_through_frames(HeapArray<T>& work, HeapArray<T>& scratch, Less less) noexcept {
    bool is_sorted = false;
    work = with_mut(std::move(work), [&](auto elements) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = ::foundation::core::sort_stably(std::move(elements), std::move(room), less).is_ok();
        });
    });
    return is_sorted;
}

// The body of a row of sort_stably by an order: before each sample the work
// buffer takes the next window of the pool, and a call sorts it.  The order
// Ascending gives the sort that a call with no order gives.
template <class T, std::size_t Count, class Less>
class StableSortBody final {
public:
    StableSortBody(View<T const> pool, Less less) noexcept : windows_{pool}, less_{less} {}

    void before_sample() noexcept { static_cast<void>(::fixy::copy(work_.view(), windows_.next())); }

    void operator()() noexcept { bench::do_not_optimize(sort_through_frames(work_, scratch_, less_)); }

private:
    WindowCursor<T, Count> windows_;
    Less less_;
    HeapArray<T> work_ = ::foundation::core::mint_heap_array(kAlloc, Count, T{});
    HeapArray<T> scratch_ = ::foundation::core::mint_heap_array(kAlloc, Count, T{});
};

template <class T, std::size_t Count, class Less>
[[gnu::noinline]] bench::Report measure_sort_stably(bench::Run&& run, View<T const> pool, Less less) {
    return measure_row(static_cast<bench::Run&&>(run), StableSortBody<T, Count, Less>{pool, less});
}

// The body of a row of sort_by_network<N>: before each sample the array takes
// the next window of the pool, and a call sorts it.
template <std::size_t N>
class NetworkBody final {
public:
    explicit NetworkBody(View<float const> pool) noexcept : windows_{pool} {}

    void before_sample() noexcept {
        View<float const> const window = windows_.next();
        std::size_t position = 0;
        for (float& value : work_) {
            value = *window.get(position).expect("the element fits the window");
            ++position;
        }
    }

    void operator()() noexcept {
        work_ = with_mut(std::move(work_), [](auto out) noexcept {
            ::foundation::core::sort_by_network(std::move(out), FloatTotalOrder{});
        });
        bench::do_not_optimize(work_);
    }

private:
    WindowCursor<float, N> windows_;
    ::fixy::FixedArray<float, N> work_{};
};

template <std::size_t N>
[[gnu::noinline]] bench::Report measure_network(bench::Run&& run, View<float const> pool) {
    return measure_row(static_cast<bench::Run&&>(run), NetworkBody<N>{pool});
}

// The body of the row of find_lower_bound: each call searches a sorted run of
// 2^20 for one of its elements, at a different place for each call.
class SearchBody final {
public:
    explicit SearchBody(View<float const> sorted) noexcept : sorted_{sorted} {}

    void operator()() noexcept {
        probe_ = (probe_ + 7919) & (sorted_.size() - 1);
        float const value = *sorted_.get(probe_).expect("the probe fits the run");
        bench::do_not_optimize(::foundation::core::find_lower_bound(sorted_, value, FloatTotalOrder{}));
    }

private:
    View<float const> sorted_;
    std::size_t probe_ = 0;
};

// ── The references ───────────────────────────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the sorts of libstdc++ are the floor references of the sorts of the base")

// A raw run of the heap, for the reference rows.
template <class T>
struct RawRun final {
    T* first = nullptr;
    std::size_t count = 0;

    explicit RawRun(std::size_t elements) noexcept
        : first{static_cast<T*>(__builtin_malloc(elements * sizeof(T)))}, count{elements} {
        if (first == nullptr) fixy::fatal("the raw run of the bench got its storage");
    }
    RawRun(RawRun&& other) noexcept : first{other.first}, count{other.count} { other.first = nullptr; }
    RawRun(RawRun const&) = delete("the run owns its storage");
    RawRun& operator=(RawRun const&) = delete("the run owns its storage");
    ~RawRun() { __builtin_free(first); }
};

// The raw copy of a pool of the base, for the reference rows.
template <class T>
[[nodiscard]] RawRun<T> raw_copy_of(View<T const> pool) noexcept {
    RawRun<T> raw{pool.size()};
    std::size_t position = 0;
    for (T const value : pool) {
        raw.first[position] = value;
        ++position;
    }
    return raw;
}

// The body of a reference row: before each sample the raw work buffer takes
// the window of the raw pool that the row of the base takes, and a call sorts
// it with the raw form of the reference.
template <class T, std::size_t Count, class Sort>
struct RawSortBody final {
    T const* pool = nullptr;
    std::size_t pool_count = 0;
    T* work = nullptr;
    Sort sort;
    std::size_t sample = 0;

    void before_sample() noexcept {
        std::size_t const windows = pool_count / Count;
        __builtin_memcpy(work, pool + (sample % windows) * Count, Count * sizeof(T));
        ++sample;
    }

    void operator()() noexcept {
        sort(work, work + Count);
        bench::do_not_optimize(work[0]);
    }
};

template <class T, std::size_t Count, class Sort>
[[gnu::noinline]] bench::Report measure_reference(bench::Run&& run, RawRun<T> const& pool, Sort sort) {
    RawRun<T> work{Count};
    return measure_row(static_cast<bench::Run&&>(run),
                       RawSortBody<T, Count, Sort>{pool.first, pool.count, work.first, sort, 0});
}

// The raw form of std::stable_sort and of std::sort by an order.
template <class Less>
struct StdStableSort final {
    Less less;
    template <class T>
    void operator()(T* first, T* last) const noexcept {
        std::stable_sort(first, last, less);
    }
};

template <class Less>
struct StdSort final {
    Less less;
    template <class T>
    void operator()(T* first, T* last) const noexcept {
        std::sort(first, last, less);
    }
};

// The bits of a raw run of floats, as signed words, for the rows of fluxsort.
[[nodiscard]] RawRun<std::int32_t> raw_bits_of(RawRun<float> const& floats) noexcept {
    RawRun<std::int32_t> bits{floats.count};
    __builtin_memcpy(bits.first, floats.first, floats.count * sizeof(float));
    return bits;
}

#if defined(CRUCIBLE_BENCH_FLUXSORT)
// The raw form of fluxsort: the bits of floats by the total order, or words
// by their value.
struct Fluxsort final {
    void operator()(std::int32_t* first, std::int32_t* last) const noexcept {
        bench_sort_references::sort_fluxsort_total_order(first, static_cast<std::size_t>(last - first));
    }
    void operator()(std::uint32_t* first, std::uint32_t* last) const noexcept {
        bench_sort_references::sort_fluxsort_words(first, static_cast<std::size_t>(last - first));
    }
    void operator()(std::uint64_t* first, std::uint64_t* last) const noexcept {
        bench_sort_references::sort_fluxsort_wide_words(first, static_cast<std::size_t>(last - first));
    }
};
#endif

#if defined(CRUCIBLE_BENCH_X86_SIMD_SORT)
// The raw forms of x86-simd-sort: the AVX2 arm, and the arm of the host.
struct SimdAvx2 final {
    template <class T>
    void operator()(T* first, T* last) const noexcept {
        bench_sort_references::sort_simd_avx2(first, static_cast<std::size_t>(last - first));
    }
};

struct SimdHost final {
    template <class T>
    void operator()(T* first, T* last) const noexcept {
        bench_sort_references::sort_simd_host(first, static_cast<std::size_t>(last - first));
    }
};
#endif

// The row of std::lower_bound: the searches of the row of find_lower_bound.
[[gnu::noinline]] bench::Report measure_std_lower_bound(bench::Run&& run, RawRun<float> const& sorted) {
    std::size_t probe = 0;
    return measure_row(static_cast<bench::Run&&>(run), [&] {
        probe = (probe + 7919) & (sorted.count - 1);
        float const value = sorted.first[probe];
        bench::do_not_optimize(std::lower_bound(sorted.first, sorted.first + sorted.count, value, FloatTotalOrder{}));
    });
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The rows of the vector roof for a row of the base of Count elements of T:
// the two arms of x86-simd-sort, and the ratios of the row to each.
template <class T, std::size_t Count>
void bench_simd_roof(Row const& row, RawRun<T> const& raw_pool) {
#if defined(CRUCIBLE_BENCH_X86_SIMD_SORT)
    bench::Report const avx2 = measure_reference<T, Count>(bench::Run{"x86-simd-sort avx2"}, raw_pool, SimdAvx2{});
    bench::Report const host = measure_reference<T, Count>(bench::Run{"x86-simd-sort host"}, raw_pool, SimdHost{});
    fixy::FixedText<96> avx2_text{};
    fixy::FixedText<96> host_text{};
    Row const avx2_row{name_with_count<Count>(avx2_text, "x86-simd-sort AVX2, not stable"), avx2};
    Row const host_row{name_with_count<Count>(host_text, "x86-simd-sort of the host, not stable"), host};
    print_row<Count>(avx2_row);
    print_row<Count>(host_row);
    print_ratio(Pair{.row = row, .reference = avx2_row});
    print_ratio(Pair{.row = row, .reference = host_row});
#else
    static_cast<void>(row);
    static_cast<void>(raw_pool);
#endif
}

// The row of fluxsort, with the name flux_name, for a row of the base of
// Count elements, and the ratio of the row to it.  The raw pool holds signed
// words for floats, and the words of the row for an integer row.
template <class T, std::size_t Count>
void bench_fluxsort(Row const& row, RawRun<T> const& raw_pool, fixy::TextView flux_name) {
#if defined(CRUCIBLE_BENCH_FLUXSORT)
    bench::Report const flux = measure_reference<T, Count>(bench::Run{"fluxsort"}, raw_pool, Fluxsort{});
    Row const flux_row{flux_name, flux};
    print_row<Count>(flux_row);
    print_ratio(Pair{.row = row, .reference = flux_row});
#else
    static_cast<void>(row);
    static_cast<void>(raw_pool);
    static_cast<void>(flux_name);
#endif
}

// The rows of Count floats: sort_stably by Ascending and by an order of the
// caller, the references, and the ratios.  raw_bits holds the bits of the
// floats of raw_pool, for fluxsort.
template <std::size_t Count>
[[gnu::noinline]] void bench_floats(View<float const> pool, RawRun<float> const& raw_pool,
                                    RawRun<std::int32_t> const& raw_bits) {
    bench::Report const ascending =
        measure_sort_stably<float, Count>(bench::Run{"sort_stably"}, pool, ::foundation::core::Ascending{});
    bench::Report const ordered =
        measure_sort_stably<float, Count>(bench::Run{"sort_stably order"}, pool, FloatTotalOrder{});
    bench::Report const std_stable =
        measure_reference<float, Count>(bench::Run{"std::stable_sort"}, raw_pool, StdStableSort<FloatTotalOrder>{});
    bench::Report const std_sort =
        measure_reference<float, Count>(bench::Run{"std::sort"}, raw_pool, StdSort<FloatTotalOrder>{});
    fixy::FixedText<96> ascending_text{};
    fixy::FixedText<96> ordered_text{};
    fixy::FixedText<96> std_stable_text{};
    fixy::FixedText<96> std_sort_text{};
    Row const ascending_row{name_with_count<Count>(ascending_text, "sort_stably float"), ascending};
    Row const ordered_row{name_with_count<Count>(ordered_text, "sort_stably float by an order of the caller"), ordered};
    Row const std_stable_row{name_with_count<Count>(std_stable_text, "std::stable_sort float by the total order"),
                             std_stable};
    Row const std_sort_row{name_with_count<Count>(std_sort_text, "std::sort float by the total order"), std_sort};
    print_row<Count>(ascending_row);
    print_row<Count>(ordered_row);
    print_row<Count>(std_stable_row);
    print_row<Count>(std_sort_row);
    print_ratio(Pair{.row = ascending_row, .reference = std_stable_row});
    print_ratio(Pair{.row = ascending_row, .reference = std_sort_row});
    print_ratio(Pair{.row = ordered_row, .reference = std_stable_row});
    print_ratio(Pair{.row = ordered_row, .reference = std_sort_row});
    fixy::FixedText<96> flux_text{};
    bench_fluxsort<std::int32_t, Count>(ordered_row, raw_bits,
                                        name_with_count<Count>(flux_text, "fluxsort float by the total order"));
    bench_simd_roof<float, Count>(ascending_row, raw_pool);
}

// The rows of a sorted input of 2^20 floats.
[[gnu::noinline]] void bench_sorted(View<float const> pool) {
    HeapArray<float> sorted = ::foundation::core::mint_heap_array_copy<float>(
        kAlloc, pool.window(0, kShared).expect("the part fits the pool"));
    HeapArray<float> scratch = ::foundation::core::mint_heap_array(kAlloc, kShared, 0.0f);
    static_cast<void>(sort_through_frames(sorted, scratch, ::foundation::core::Ascending{}));
    View<float const> const sorted_view{sorted.view()};
    RawRun<float> const raw_sorted = raw_copy_of(sorted_view);
    bench::Report const ordered =
        measure_sort_stably<float, kShared>(bench::Run{"sort_stably order sorted"}, sorted_view, FloatTotalOrder{});
    bench::Report const std_stable = measure_reference<float, kShared>(bench::Run{"std::stable_sort sorted"},
                                                                       raw_sorted, StdStableSort<FloatTotalOrder>{});
    Row const row{"sort_stably float by an order of the caller, 1048576 sorted", ordered};
    Row const reference{"std::stable_sort float by the total order, 1048576 sorted", std_stable};
    print_row<kShared>(row);
    print_row<kShared>(reference);
    print_ratio(Pair{.row = row, .reference = reference});
    bench_fluxsort<std::int32_t, kShared>(row, raw_bits_of(raw_sorted),
                                          "fluxsort float by the total order, 1048576 sorted");
}

// The rows of 2^20 integer keys by Ascending.  type_name names T in the rows.
template <class T>
[[gnu::noinline]] void bench_words(View<T const> pool, fixy::TextView type_name) {
    RawRun<T> const raw_pool = raw_copy_of(pool);
    bench::Report const ascending =
        measure_sort_stably<T, kShared>(bench::Run{"sort_stably words"}, pool, ::foundation::core::Ascending{});
    bench::Report const std_stable = measure_reference<T, kShared>(bench::Run{"std::stable_sort words"}, raw_pool,
                                                                   StdStableSort<::foundation::core::Ascending>{});
    fixy::FixedText<96> row_text{};
    fixy::FixedText<96> reference_text{};
    Row const row{fixy::format(row_text, "sort_stably {}, {}", type_name, kShared).expect("the name fits"), ascending};
    Row const reference{
        fixy::format(reference_text, "std::stable_sort {}, {}", type_name, kShared).expect("the name fits"),
        std_stable};
    print_row<kShared>(row);
    print_row<kShared>(reference);
    print_ratio(Pair{.row = row, .reference = reference});
    fixy::FixedText<96> flux_text{};
    bench_fluxsort<T, kShared>(row, raw_pool,
                               fixy::format(flux_text, "fluxsort {}, {}", type_name, kShared).expect("the name fits"));
    bench_simd_roof<T, kShared>(row, raw_pool);
}

// The rows of Count records by their float keys: the radix sort of
// order_by_key in each direction, the comparator form of the same order, and
// std::stable_sort of that order as the floor.
template <std::size_t Count>
[[gnu::noinline]] void bench_records(View<KeyedRecord const> pool, RawRun<KeyedRecord> const& raw_pool) {
    bench::Report const by_key = measure_sort_stably<KeyedRecord, Count>(
        bench::Run{"order_by_key"}, pool, ::foundation::core::order_by_key(&KeyedRecord::key));
    bench::Report const by_key_descending = measure_sort_stably<KeyedRecord, Count>(
        bench::Run{"order_by_key_descending"}, pool, ::foundation::core::order_by_key_descending(&KeyedRecord::key));
    bench::Report const by_order =
        measure_sort_stably<KeyedRecord, Count>(bench::Run{"record comparator"}, pool, RecordKeyOrder{});
    bench::Report const std_stable = measure_reference<KeyedRecord, Count>(bench::Run{"std::stable_sort records"},
                                                                           raw_pool, StdStableSort<RecordKeyOrder>{});
    fixy::FixedText<96> by_key_text{};
    fixy::FixedText<96> descending_text{};
    fixy::FixedText<96> order_text{};
    fixy::FixedText<96> std_text{};
    Row const by_key_row{name_with_count<Count>(by_key_text, "sort_stably records by order_by_key"), by_key};
    Row const descending_row{name_with_count<Count>(descending_text, "sort_stably records by order_by_key_descending"),
                             by_key_descending};
    Row const order_row{name_with_count<Count>(order_text, "sort_stably records by an order of the caller"), by_order};
    Row const std_row{name_with_count<Count>(std_text, "std::stable_sort records by the key"), std_stable};
    print_row<Count>(by_key_row);
    print_row<Count>(descending_row);
    print_row<Count>(order_row);
    print_row<Count>(std_row);
    print_ratio(Pair{.row = by_key_row, .reference = std_row});
    print_ratio(Pair{.row = descending_row, .reference = std_row});
    print_ratio(Pair{.row = order_row, .reference = std_row});
    print_ratio(Pair{.row = by_key_row, .reference = order_row});
}

// The rows of the network of N, with std::sort of the same N as the floor.
template <std::size_t N>
[[gnu::noinline]] void bench_network(View<float const> pool, RawRun<float> const& raw_pool) {
    bench::Report const network = measure_network<N>(bench::Run{"sort_by_network"}, pool);
    bench::Report const std_sort =
        measure_reference<float, N>(bench::Run{"std::sort network"}, raw_pool, StdSort<FloatTotalOrder>{});
    fixy::FixedText<96> row_text{};
    fixy::FixedText<96> reference_text{};
    Row const row{fixy::format(row_text, "sort_by_network<{}> float", N).expect("the name fits"), network};
    Row const reference{name_with_count<N>(reference_text, "std::sort float by the total order"), std_sort};
    print_row<N>(row);
    print_row<N>(reference);
    print_ratio(Pair{.row = row, .reference = reference});
    bench_simd_roof<float, N>(row, raw_pool);
}

// The rows of find_lower_bound and std::lower_bound on a sorted run of 2^20.
[[gnu::noinline]] void bench_search(View<float const> pool) {
    View<float const> const part = pool.window(0, kShared).expect("the part fits the pool");
    HeapArray<float> sorted = ::foundation::core::mint_heap_array_copy<float>(kAlloc, part);
    HeapArray<float> scratch = ::foundation::core::mint_heap_array(kAlloc, kShared, 0.0f);
    static_cast<void>(sort_through_frames(sorted, scratch, ::foundation::core::Ascending{}));
    View<float const> const sorted_view{sorted.view()};
    RawRun<float> const raw_sorted = raw_copy_of(sorted_view);
    bench::Report const search = measure_row(bench::Run{"find_lower_bound"}, SearchBody{sorted_view});
    bench::Report const std_search = measure_std_lower_bound(bench::Run{"std::lower_bound"}, raw_sorted);
    Row const row{"find_lower_bound float, 1048576 sorted", search};
    Row const reference{"std::lower_bound float, 1048576 sorted", std_search};
    print_row<1>(row);
    print_row<1>(reference);
    print_ratio(Pair{.row = row, .reference = reference});
}

}  // namespace

int main() {
    bench::elevate_priority();
    HeapArray<float> const run = draw_pool();
    View<float const> const pool{run.view()};
    RawRun<float> const raw_pool = raw_copy_of(pool);
    RawRun<std::int32_t> const raw_bits = raw_bits_of(raw_pool);
    bench_network<16>(pool, raw_pool);
    bench_network<64>(pool, raw_pool);
    bench_network<256>(pool, raw_pool);
    bench_floats<256>(pool, raw_pool, raw_bits);
    bench_floats<1024>(pool, raw_pool, raw_bits);
    bench_floats<65536>(pool, raw_pool, raw_bits);
    bench_floats<kShared>(pool, raw_pool, raw_bits);
    bench_sorted(pool);
    HeapArray<std::uint32_t> const words = words_of(pool);
    bench_words(View<std::uint32_t const>{words.view()}, "uint32");
    HeapArray<std::uint64_t> const wide_words = wide_words_of(pool);
    bench_words(View<std::uint64_t const>{wide_words.view()}, "uint64");
    HeapArray<KeyedRecord> const records = records_of(pool);
    View<KeyedRecord const> const record_pool{records.view()};
    RawRun<KeyedRecord> const raw_records = raw_copy_of(record_pool);
    bench_records<65536>(record_pool, raw_records);
    bench_records<kShared>(record_pool, raw_records);
    bench_search(pool);
    return 0;
}
