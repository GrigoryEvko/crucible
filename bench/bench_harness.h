#pragma once

// Benchmark harness.
//
// Methodology — each primitive has a citation in its doc comment:
//   • RDTSC bracketing via LFENCE / RDTSCP + LFENCE — Intel, "How to
//     Benchmark Code Execution Times on Intel IA-32 and IA-64" (2010).
//   • Per-sample collection + sorted-percentile extraction with linear
//     interpolation — Hyndman & Fan (1996) type 7, the R default.
//   • Bootstrap confidence intervals — Efron (1979).
//   • Mann-Whitney U rank-sum for A/B comparison — Mann & Whitney (1947).
//   • Kernel-side context via eBPF — 96 counters in a BPF_F_MMAPABLE
//     array map (bench/bpf/sense_hub.bpf.c, ported from symbiotic).
//     Tracepoint handlers run in kernel context on the event; the bench
//     side just does volatile mmap loads on entry and exit of the run,
//     so zero overhead lands inside the timed region.
//
// What the harness gives you:
//   bench::Run("name").samples(N).warmup(W).core(C).measure(body) → Report
//   Report → text + JSON; bootstrap CI on any percentile on demand.
//   bench::compare(a, b) → Mann-Whitney U; distinguishable at p<0.01 or not.
//
// Pin-by-default: picks a core of the affinity of the process, an isolated
// CPU first (so an isolcpu reserved via the kernel cmdline wins), falling back
// to sched_getcpu() only if the affinity is empty.  The pin never leaves the
// affinity, so the one core that loop bench gives a bench is the core of each
// of its runs. A body that starts threads
// asks for Run::fan_out instead, because its threads take the one-CPU
// pin of the measuring thread. Each pin goes through the gated door of
// fixy/os/Sched.h, which records a pin event, and a Run gives the thread
// back its affinity when the measurement ends. Each Run also reads
// cpufreq at the start and end of the run, and — when BPF is available
// — reads the 96-counter sense hub on both sides of the measurement.
// The reported deltas cover context switches, page faults, migrations,
// futex contention, I/O bytes, and the rest of the kernel surface the
// bench inadvertently touches.
//
// If BPF fails to load (missing CAP_BPF, kernel.unprivileged_bpf_disabled,
// kernel too old), the harness degrades gracefully: ns/cycle numbers are
// unchanged, sensory fields read zero, and the text output omits the
// sensory line.
//
// Auto-batching averages over 2^k calls until the timed region exceeds
// 1000 cycles — batched percentiles are over batch means, labeled
// "[batch-avg]" in the output.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>
#endif

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
// Bench BPF sensing uses the production observability substrate directly.
#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SchedSwitch.h>
// Senses aggregator — single entry point for senses_instance() below.
// Every bench shares one Senses singleton instead of separate SenseHub
// and SchedSwitch loaders.
#include <crucible/perf/Senses.h>
#endif

#include <crucible/warden/CpuTopology.h>
#include <crucible/warden/Hardening.h>
#include <crucible/warden/Policy.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/Process.h>
#include <fixy/os/Sched.h>
#include <fixy/os/Time.h>
#include <foundation/Transfer.h>
#include <foundation/core/Philox.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Directory.h>
#include <foundation/os/File.h>
#include <foundation/os/PathText.h>

namespace bench {

// ── CpuId strong type (TypeSafe axiom) ─────────────────────────────
//
// Wraps a signed CPU index so that sched_getcpu()/sched_setaffinity
// arguments aren't interchangeable with raw `int`s flowing through the
// builder API. Mirrors CRUCIBLE_STRONG_ID in include/crucible/Types.h
// but uses a signed underlying type because POSIX CPU indices can be
// -1 ("unknown / no pin"). No arithmetic operators — callers must
// .raw() to compute, then rewrap.
//
//   • Default construct → CpuId::none() (raw() == -1, is_valid() == false)
//   • CpuId{sched_getcpu()} — explicit ctor, no implicit conversions
//   • is_valid() — true iff raw() >= 0 (a real CPU index)
//   • printed with %d after .raw() — matches existing format strings
struct CpuId {
    int raw_ = -1;

    constexpr CpuId() noexcept = default;
    constexpr explicit CpuId(int v) noexcept : raw_{v} {}

    [[nodiscard]] static constexpr CpuId none() noexcept { return CpuId{-1}; }

    [[nodiscard]] constexpr int raw() const noexcept { return raw_; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return raw_ >= 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return is_valid(); }

    constexpr auto operator<=>(const CpuId&) const noexcept = default;
};
static_assert(sizeof(CpuId) == sizeof(int));

// ── Fenced RDTSC primitives ────────────────────────────────────────
//
// Intel's "How to Benchmark..." (2010) §3.2.1: LFENCE before RDTSC to
// serialize preceding loads/stores; RDTSCP + LFENCE at the end so that
// subsequent loads cannot be sampled into the timed region. Bare RDTSC
// is out-of-order-able with both adjacent instructions and is incorrect
// for short-region timing. Overhead ~15-30 cycles; Timer measures the
// exact value and we subtract it.

[[nodiscard, gnu::always_inline]] inline uint64_t rdtsc_start() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_lfence();
    return __builtin_ia32_rdtsc();
#else
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

[[nodiscard, gnu::always_inline]] inline uint64_t rdtsc_end() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int aux;
    const uint64_t t = __builtin_ia32_rdtscp(&aux);
    __builtin_ia32_lfence();
    return t;
#else
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// ── RdtscPinned witness + pinned-overload rdtsc primitives ───────────
//
// Modern x86 has invariant_tsc, so cross-core TSC reads ARE comparable
// in absolute value — but a thread that migrates BETWEEN the rdtsc_start
// and rdtsc_end of a short-window measurement still sees pipeline-state
// artifacts (warm-vs-cold L1, branch-predictor reset, store-buffer
// drain on context switch).  Short-window measurements MUST be pinned
// for the duration of the pair.
//
// `RdtscPinned` is a move-only phantom witness — its existence at a
// call site is a static proof that `Run::pin_()` succeeded
// just prior, and the thread has not migrated since.  The construction
// is mint-private to Run, so the only legitimate way to
// produce a witness is the bench-harness affinity-set path.
//
// The pinned-overload `rdtsc_start(RdtscPinned const&) / rdtsc_end(...)`
// share the body of the bare forms — the overload is pure type-system
// discipline, zero runtime cost.  The bare forms remain available for
// startup-time Timer calibration / overhead measurement (pre-pin) and
// for ad-hoc external benches that don't run the harness affinity path.
//
// fixy::CpuPinned<Mask, Posture, Unit> requires Mask as an
// NTTP — fits production code where the target core is compile-time
// chosen, but bench's `warden::select_hot_cpu` picks the target at
// runtime.  RdtscPinned is the dynamic-mask analogue specifically for
// bench measurement loops.
class RdtscPinned {
    CpuId core_ = CpuId::none();
    struct mint_tag {};

    constexpr RdtscPinned(CpuId core, mint_tag) noexcept : core_{core} {}

    // Run is the sole mint authority; the witness is produced
    // immediately after sched_setaffinity success and travels through
    // the measurement loop by const-ref.
    friend class Run;

public:
    RdtscPinned(const RdtscPinned&) = delete;
    RdtscPinned& operator=(const RdtscPinned&) = delete;
    constexpr RdtscPinned(RdtscPinned&&) noexcept = default;
    constexpr RdtscPinned& operator=(RdtscPinned&&) noexcept = default;

    [[nodiscard]] constexpr CpuId core() const noexcept { return core_; }
};

// Pinned-overload rdtsc_start / rdtsc_end — gate per-iteration TSC
// reads on the RdtscPinned proof.  Body is identical to the bare form;
// the witness parameter is the type-level discipline marker.
[[nodiscard, gnu::always_inline]] inline uint64_t rdtsc_start(RdtscPinned const&) noexcept { return rdtsc_start(); }

[[nodiscard, gnu::always_inline]] inline uint64_t rdtsc_end(RdtscPinned const&) noexcept { return rdtsc_end(); }

// Defeat dead-code elimination of values we want the optimizer to treat
// as consumed.
//
// We do NOT use the canonical Google-Benchmark / folly / nanobench form
//     asm volatile("" : "+m,r"(v) : : "memory")
//     asm volatile("" : :  "r,m"(v) : "memory")
// because both are subtly broken under modern GCC inlining + IPA:
//
//   * "+m,r"(v) is shorthand for "=m,r" output paired with "m,0" matching
//     input. When GCC selects the `r` alternative the matching `0` input
//     ties the input register to the output register (a temp); the asm
//     operates on the temp; nothing writes the temp back to v's storage.
//     Under inlining this leaves v's slot whatever it was at function
//     entry (uninitialized stack), and downstream lambda value-expr
//     substitutions that look up v by stack offset read the wrong bytes.
//     This form caused a SIGSEGV in the via-pointer workload of
//     bench_pool_allocator. GCC PR124958 reports it, and Andrew Pinski
//     closed the report as INVALID on 2026-04-21: the fault is in the
//     inline asm, not in GCC. The do_not_optimize functions that follow
//     are empty [[gnu::noipa]] functions with no inline asm.
//
//   * "r,m"(v) on a value: when GCC picks `r`, the asm sees a register
//     copy and the original storage is invisible. Earlier writes to v's
//     storage become DSE-eligible (PR109057, Jakub Jelinek).
//
//   * "+m,r" on objects > ~8 KB additionally forces a memcpy into a
//     temporary that is then never read back (PR105519).
//
// Pinski's recommended replacement, PR105519 c#3:
//     [[gnu::noipa]] void DoNotOptimize(T&) {}
// Empty function, no inline asm. `noipa` blocks inter-procedural
// analysis so the call cannot be deleted as dead, the argument is
// passed through real ABI registers / stack, and from the optimizer's
// point of view anything could happen inside. Cost is one real call
// instruction per use. Acceptable for benchmark scaffolding — the
// alternative is wrong-code at -O3.
//
// References:
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=124958  (our filing)
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109057  (r,m DSE)
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105519  (+m,r temp)

template <typename T>
[[gnu::noipa]] void do_not_optimize(T const& v) noexcept {
    (void)v;
}

template <typename T>
[[gnu::noipa]] void do_not_optimize(T& v) noexcept {
    (void)v;
}

[[gnu::always_inline]] inline void clobber() noexcept { asm volatile("" : : : "memory"); }

// ── clobber_array<T>(std::span<T>) — array-side DCE kill ──────────
//
// Companion to `do_not_optimize()` (single-value DCE kill) and
// `clobber()` (global memory barrier).  Use when the bench body
// fills or scans an array and the array's memory is otherwise dead
// at the timed region's exit — without this, the optimizer is free
// to elide the fill / scan because nothing observably consumes the
// result.
//
// Implementation: `[[gnu::noipa]]` empty function, same discipline
// as `do_not_optimize`.  The span (data pointer + length) is passed
// through real ABI registers; noipa blocks IPA so from the
// optimizer's point of view the function could read or write any
// memory reachable from those arguments — therefore the writes/
// reads to the array's elements MUST be materialized.
//
// Why not the asm `"+m,r"` form: per the project memory
// `feedback/project_gcc16_miscompile`, that idiom miscompiles on
// some types under GCC 16.  noipa is the recommended replacement.
//
// Two overloads — mutable and const — so callers don't need to
// const_cast a read-only scan.  std::span deduces T from any
// contiguous range (vector, array, C array, raw pointer + size).
//
// Cost: one real call instruction per use (same as do_not_optimize).
// Acceptable for benchmark scaffolding.
//
// Usage:
//
//   std::array<int, N> data;
//   for (auto& v : data) v = compute(...);
//   bench::clobber_array(std::span{data});  // forces materialization
//
//   std::vector<float> readings(N);
//   bench::Run("scan").measure([&]() noexcept {
//       float sum = 0;
//       for (float x : readings) sum += x;
//       bench::clobber_array(std::span<const float>{readings});
//       bench::do_not_optimize(sum);
//   });

template <typename T>
[[gnu::noipa]] void clobber_array(std::span<T> s) noexcept {
    (void)s;
}

template <typename T>
[[gnu::noipa]] void clobber_array(std::span<const T> s) noexcept {
    (void)s;
}

// ── Timer: ns/cycle calibration + rdtsc overhead (Meyer singleton) ──

class Timer {
public:
    [[nodiscard, gnu::pure]] static double ns_per_cycle() noexcept { return instance().ns_per_cycle_; }
    [[nodiscard, gnu::pure]] static uint64_t overhead_cycles() noexcept { return instance().overhead_cycles_; }
    [[nodiscard, gnu::pure]] static double overhead_ns() noexcept {
        return static_cast<double>(overhead_cycles()) * ns_per_cycle();
    }
    [[nodiscard, gnu::pure]] static double tsc_freq_hz() noexcept {
        return (ns_per_cycle() > 0) ? 1e9 / ns_per_cycle() : 0.0;
    }

    [[nodiscard, gnu::pure]] static double to_ns(uint64_t cycles) noexcept {
        return static_cast<double>(cycles) * ns_per_cycle();
    }

private:
    Timer() noexcept : ns_per_cycle_{calibrate_()}, overhead_cycles_{measure_overhead_()} {}

    [[nodiscard]] static Timer& instance() noexcept {
        static Timer t;
        return t;
    }

    // Correlate TSC vs steady_clock over 200 ms. Longer window dampens
    // transient frequency wobble at process start.
    [[nodiscard]] static double calibrate_() noexcept {
        using namespace std::chrono;
        constexpr auto window = milliseconds(200);

        (void)steady_clock::now();
        (void)rdtsc_start();

        const auto wall0 = steady_clock::now();
        const uint64_t tsc0 = rdtsc_start();
        const auto deadline = wall0 + window;
        while (steady_clock::now() < deadline) { /* spin */
        }
        const uint64_t tsc1 = rdtsc_end();
        const auto wall1 = steady_clock::now();

        const double ns = static_cast<double>(duration_cast<nanoseconds>(wall1 - wall0).count());
        const double cycles = static_cast<double>(tsc1 - tsc0);
        return (cycles > 0) ? (ns / cycles) : 0.0;
    }

    [[nodiscard]] static uint64_t measure_overhead_() noexcept {
        uint64_t best = UINT64_MAX;
        for (int i = 0; i < 10000; ++i) {
            const uint64_t t0 = rdtsc_start();
            const uint64_t t1 = rdtsc_end();
            const uint64_t d = t1 - t0;
            if (d < best) best = d;
        }
        return best;
    }

    double ns_per_cycle_;
    uint64_t overhead_cycles_;
};

// ── THE CORES OF A RUN ─────────────────────────────────────────────
//
// loop bench gives a bench the cores of its claim as the affinity of its
// process: one isolated core for each thread that the bench states with
// CORES in bench/CMakeLists.txt (THE CORES OF A RUN of
// utils/assay/loop/bench.py).  The cores of a run are the isolated CPUs of
// that affinity.
//
// An affinity mask has no order, so loop bench also gives the cores in the
// order of its claim, in the variable CRUCIBLE_BENCH_CORE_ORDER, as
// "240,242,224".  A core with fewer lines in the record of the steal time
// comes first in that order (THE STEAL TIME of utils/assay/loop/bench.py).
// The slots of a run take the cores of the run in that order, and then each
// core that the variable does not name, in ascending order.  With no such
// variable, as under ctest, the slots take the cores in ascending order.
//
// Each thread of a run has a slot.  Slot 0 is the measuring thread of each
// Run, so it takes the quietest core of the claim.  A body gives the threads
// that it starts the slots 1, 2 and so on.  The thread of slot k runs on the
// core k of the run, so no two threads of a run share a core.  A thread that
// starts on an isolated CPU with a mask of
// several CPUs stays on the CPU of the thread that started it, because no load
// balancer reaches an isolated CPU.  So each thread takes a pin of its own:
// a worker calls pin_calling_thread_to() with the core of its slot, and a
// thread that a library starts, such as the drain thread of a Vigil, starts
// inside run_on_core() with that core.
//
// Read the cores with Cores::of_calling_thread() on the main thread before
// the first Run.  A Run pins its thread to one core for the measurement, and
// a thread that its body starts takes that one core as its affinity.  A run
// with no isolated CPU in its affinity, such as a run under ctest, has no
// cores.  No slot then has a core, and each thread keeps the affinity that it
// inherits.

namespace side {

// The roles of the two sets of CPUs of Cores::of.
struct AllowedCpus final {};
struct IsolatedCpus final {};

}  // namespace side

struct ThreadSlotTag final {};

// The slot of one thread of a run.
using ThreadSlot = ::fixy::Id<ThreadSlotTag, std::uint32_t>;

// The slot of the measuring thread of each Run.
inline constexpr ThreadSlot kMeasuringSlot{0};

// The CPUs of the slots of a run, at most: the CPUs that an affinity mask of
// the thread door can name.  A spread holds as many (THE SPREAD OF A RUN).
inline constexpr std::size_t kSpreadCpus = ::fixy::AffinityMask::kBits;

// The cores of a claim in the order of loop bench (THE CORES OF A RUN).
using CoreOrder = ::fixy::FixedList<CpuId, kSpreadCpus>;

// Where the order of the slots of a run comes from: the order of the claim of
// loop bench, or the ascending order of the CPUs.
enum class SlotOrder : std::uint8_t {
    Ascending,
    Claim,
};

// The order of a text of the variable CRUCIBLE_BENCH_CORE_ORDER: CPU numbers
// with one comma between two, such as "240,242,224".  An empty text gives an
// empty order.  None for a text with a number that parse() refuses, a CPU that
// an affinity mask cannot name, a CPU twice, or more than kSpreadCpus CPUs.
// Complexity: linear in the text, times the CPUs of the order for the check of
// a CPU twice.
[[nodiscard]] inline ::fixy::Option<CoreOrder> parse_core_order(::fixy::TextView text) noexcept {
    namespace topology = crucible::warden::detail;
    CoreOrder order{};
    ::fixy::TextView rest = text;
    bool has_number = text.size() != 0;
    while (has_number) {
        std::size_t const comma = ::fixy::find(rest, ',').value_or(rest.size());
        has_number = comma < rest.size();
        ::fixy::Option<CpuId> cpu =
            ::fixy::parse<std::uint16_t>(topology::text_prefix(rest, comma))
                .match(
                    [](std::uint16_t parsed) noexcept -> ::fixy::Option<CpuId> {
                        if (parsed > ::fixy::AffinityMask::kMaxCore) return ::fixy::none;
                        return ::fixy::some(CpuId{parsed});
                    },
                    [](::fixy::ParseError) noexcept -> ::fixy::Option<CpuId> { return ::fixy::none; });
        rest = topology::text_suffix(rest, comma + 1);
        CpuId const parsed = std::move(cpu).value_or(CpuId::none());
        if (!parsed.is_valid()) return ::fixy::none;
        for (CpuId const held : order) {
            if (held == parsed) return ::fixy::none;
        }
        if (order.append(parsed).is_err()) return ::fixy::none;
    }
    return ::fixy::some(order);
}

// The order of the variable CRUCIBLE_BENCH_CORE_ORDER of the process, or an
// empty order when the process has no such variable (THE CORES OF A RUN).
[[nodiscard]] inline CoreOrder read_core_order() noexcept;

// The isolated cores of a run, in the order of its slots.  A Cores holds a
// CpuSet of 1 KiB and the CPUs of the slots, 4 KiB, and it is a plain value.
class Cores {
public:
    // The isolated CPUs of the allowed CPUs.  The slots take them in the
    // order of order, which can name other CPUs, and then each core that order
    // does not name, in ascending order.  Complexity: linear in the CPUs of
    // the set, times the CPUs of order.
    [[nodiscard]] static Cores of(::fixy::Side<side::AllowedCpus, crucible::warden::CpuSet> const& allowed,
                                  ::fixy::Side<side::IsolatedCpus, crucible::warden::CpuSet> const& isolated,
                                  CoreOrder const& order) noexcept {
        Cores cores{};
        cores.set_ = allowed.value().intersection(isolated.value());
        for (CpuId const cpu : order) {
            bool is_held = false;
            for (CpuId const held : cores.slots_) {
                is_held = is_held || held == cpu;
            }
            if (is_held || !cores.set_.contains(cpu.raw())) continue;
            static_cast<void>(cores.slots_.append(cpu));
            cores.source_ = SlotOrder::Claim;
        }
        for (int const cpu : cores.set_) {
            bool is_named = false;
            for (CpuId const named : order) {
                is_named = is_named || named.raw() == cpu;
            }
            if (is_named) continue;
            if (cores.slots_.append(CpuId{cpu}).is_err()) break;
        }
        return cores;
    }

    // The cores of the affinity of the calling thread, in the order of
    // CRUCIBLE_BENCH_CORE_ORDER.  Complexity: one read of procfs for the
    // affinity, one for the order, and one of sysfs.
    [[nodiscard]] static Cores of_calling_thread() noexcept {
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        return of(::fixy::side_as<side::AllowedCpus>(crucible::warden::allowed_cpus(startup)),
                  ::fixy::side_as<side::IsolatedCpus>(crucible::warden::isolated_cpus(startup)), read_core_order());
    }

    // The count of the cores: one for each slot.
    [[nodiscard]] std::size_t count() const noexcept { return slots_.size(); }

    // Where the order of the slots comes from.
    [[nodiscard]] SlotOrder source() const noexcept { return source_; }

    // The core of a slot: the CPU at the position of the slot in the order of
    // the slots, or none when the run has too few cores.  Two slots never get
    // one core.  Complexity: linear in the slots up to the slot.
    [[nodiscard]] ::fixy::Option<CpuId> of_slot(ThreadSlot slot) const noexcept {
        std::uint32_t position = 0;
        for (CpuId const cpu : slots_) {
            if (position == slot.raw()) return ::fixy::some(cpu);
            ++position;
        }
        return ::fixy::none;
    }

    // The core of each slot, in the order of the slots.
    [[nodiscard]] ::fixy::View<CpuId const> slots() const& noexcept { return slots_.view(); }
    void slots() const&& = delete("a view into a temporary Cores dangles at the end of the full expression");

    // The cores as a set, for the affinity mask of a thread door.
    [[nodiscard]] crucible::warden::CpuSet const& set() const& noexcept { return set_; }
    void set() const&& = delete("a reference into a temporary Cores dangles at the end of the full expression");

    // The cores of the slots from first to the last slot of the run, as a set,
    // or the empty set when the run has too few cores.  A library that starts
    // several threads and pins each one inside the mask of its caller, such as
    // the pipeline of a Vigil, takes these cores through run_on_cores().
    // Complexity: linear in the slots.
    [[nodiscard]] crucible::warden::CpuSet from_slot(ThreadSlot first) const noexcept {
        crucible::warden::CpuSet later{};
        std::uint32_t position = 0;
        for (CpuId const cpu : slots_) {
            if (position >= first.raw()) later.insert(static_cast<std::size_t>(cpu.raw()));
            ++position;
        }
        return later;
    }

    // The first cores of the order of the slots apart from one core, as a set
    // of at most count cores.  A row of count threads that a mask places, such
    // as the shards of a parallel walk, takes them, so the row takes the
    // quietest cores of the claim and not the lowest.  Complexity: linear in
    // the slots.
    [[nodiscard]] crucible::warden::CpuSet
    first_slots_apart(CpuId apart, ::fixy::Quantity<::fixy::unit::Element, std::size_t> count) const noexcept {
        crucible::warden::CpuSet first{};
        std::size_t taken = 0;
        for (CpuId const cpu : slots_) {
            if (taken == count.raw()) break;
            if (cpu == apart) continue;
            first.insert(static_cast<std::size_t>(cpu.raw()));
            ++taken;
        }
        return first;
    }

private:
    crucible::warden::CpuSet set_{};
    ::fixy::FixedList<CpuId, kSpreadCpus> slots_{};
    SlotOrder source_ = SlotOrder::Ascending;
};

// The CPUs that share the L3 cache of a CPU: the shared_cpu_list of the cache
// of level 3 of the CPU in sysfs, or the empty set when sysfs gives no such
// cache.  Complexity: at most two reads of sysfs for each of the first eight
// caches of the CPU.
[[nodiscard]] inline crucible::warden::CpuSet read_l3_group(CpuId cpu) noexcept {
    namespace topology = crucible::warden::detail;
    constexpr std::uint64_t kCacheIndexes = 8;
    if (!cpu.is_valid()) return crucible::warden::CpuSet{};
    for (std::uint64_t index = 0; index < kCacheIndexes; ++index) {
        const auto read = [&cpu, index](::fixy::TextView leaf, auto& text) noexcept {
            ::foundation::os::PathText<96> path{};
            const bool is_built = path.append(::fixy::TextView{"/sys/devices/system/cpu/cpu"}).is_ok()
                               && path.append_decimal(static_cast<std::uint64_t>(cpu.raw())).is_ok()
                               && path.append(::fixy::TextView{"/cache/index"}).is_ok()
                               && path.append_decimal(index).is_ok() && path.append(leaf).is_ok();
            return is_built && ::foundation::os::read_file(path.c_text(), text).is_ok();
        };
        ::fixy::FixedText<16> level{};
        if (!read("/level", level)) return crucible::warden::CpuSet{};
        if (!::fixy::starts_with(level.view(), ::fixy::TextView{"3"})) continue;
        ::fixy::FixedText<4096> listed{};
        if (!read("/shared_cpu_list", listed)) return crucible::warden::CpuSet{};
        const std::size_t line_end = ::fixy::find(listed.view(), '\n').value_or(listed.size());
        return topology::parse_cpulist(topology::text_prefix(listed.view(), line_end));
    }
    return crucible::warden::CpuSet{};
}

// ── THE SPREAD OF A RUN ────────────────────────────────────────────
//
// A run of many threads gives slot k the CPU k of a list.  With a claim of
// loop bench, the list is the cores of the claim in the order of their slots,
// as Cores::of_slot gives them.  With no claim, such as a row of more threads
// than the reserved cores, the list is the CPUs of the affinity of the process
// in the order of the topology: the first allowed CPU of each core in
// ascending order, then the second allowed CPU of each core, and so on.  A run
// of as many threads as the host has cores then puts no two threads on one
// core, and the SMT siblings come last.  Linux gives adjacent numbers to the
// cores of one L3 group and of one socket, so the first slots fill one group
// and one socket before the next.  The CPUs of the core of cpu0 come last in
// their rank, because cpu0 takes the timer interrupts on most hosts, and slot
// 0 is the measuring thread.
//
// A spread holds at most kSpreadCpus CPUs: the CPUs that an affinity mask of
// the thread door can name.

// Where the CPUs of a spread come from.
enum class SpreadSource : std::uint8_t {
    Claim,
    Topology,
};

class Spread {
public:
    // The cores of a claim, in the order of their slots.  Complexity: linear
    // in the cores.
    [[nodiscard]] static Spread of_claim(Cores const& cores) noexcept {
        Spread spread{};
        spread.source_ = SpreadSource::Claim;
        for (CpuId const cpu : cores.slots()) {
            if (spread.order_.append(cpu).is_err()) break;
        }
        return spread;
    }

    // The CPUs of allowed in the order of the topology.  siblings_of gives
    // the SMT group of a CpuId: the CPU and its siblings.  A CPU that an
    // affinity mask cannot name is left out.  Complexity: linear in the CPUs
    // of allowed times the CPUs of a group, plus one pass of the CPUs for each
    // rank in a group.
    template <class SiblingsOf>
    [[nodiscard]] static Spread of_topology(crucible::warden::CpuSet const& allowed,
                                            SiblingsOf const& siblings_of) noexcept {
        // Where a CPU goes in its rank: the CPUs of the core of cpu0 go last.
        enum class Turn : std::uint8_t {
            First,
            Last,
        };
        // The rank of a CPU in its core: the count of the allowed CPUs of its
        // group with a lower number.
        struct RankedCpu {
            CpuId cpu{};
            std::size_t rank = 0;
            Turn turn = Turn::First;
        };
        ::fixy::FixedList<RankedCpu, kSpreadCpus> ranked{};
        std::size_t deepest = 0;
        for (int const cpu : allowed) {
            if (cpu > ::fixy::AffinityMask::kMaxCore) break;
            crucible::warden::CpuSet const group = siblings_of(CpuId{cpu});
            std::size_t rank = 0;
            for (int const sibling : group) {
                if (sibling < cpu && allowed.contains(sibling)) ++rank;
            }
            deepest = rank > deepest ? rank : deepest;
            Turn const turn = cpu == 0 || group.contains(0) ? Turn::Last : Turn::First;
            if (ranked.append(RankedCpu{.cpu = CpuId{cpu}, .rank = rank, .turn = turn}).is_err()) break;
        }
        Spread spread{};
        spread.source_ = SpreadSource::Topology;
        for (std::size_t rank = 0; rank <= deepest; ++rank) {
            for (RankedCpu const& each : ranked) {
                if (each.rank == rank && each.turn == Turn::First) static_cast<void>(spread.order_.append(each.cpu));
            }
            for (RankedCpu const& each : ranked) {
                if (each.rank == rank && each.turn == Turn::Last) static_cast<void>(spread.order_.append(each.cpu));
            }
        }
        return spread;
    }

    // The spread of the calling thread: the claim when its affinity holds an
    // isolated CPU, else the topology of its affinity from sysfs.
    // Complexity: one read of procfs, and one read of sysfs for each CPU.
    [[nodiscard]] static Spread of_calling_thread() noexcept {
        Cores const cores = Cores::of_calling_thread();
        if (cores.count() > 0) return of_claim(cores);
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        return of_topology(crucible::warden::allowed_cpus(startup), [&startup](CpuId cpu) noexcept {
            return crucible::warden::smt_siblings(startup, cpu.raw());
        });
    }

    // The count of the slots.
    [[nodiscard]] std::size_t count() const noexcept { return order_.size(); }

    [[nodiscard]] SpreadSource source() const noexcept { return source_; }

    // The CPU of a slot, or none past the last slot.  Complexity: linear in
    // the slots up to the slot.
    [[nodiscard]] ::fixy::Option<CpuId> of_slot(ThreadSlot slot) const noexcept {
        std::uint32_t position = 0;
        for (CpuId const cpu : order_) {
            if (position == slot.raw()) return ::fixy::some(cpu);
            ++position;
        }
        return ::fixy::none;
    }

private:
    ::fixy::FixedList<CpuId, kSpreadCpus> order_{};
    SpreadSource source_ = SpreadSource::Topology;
};

namespace detail {

// The topology of the host that the harness lends to each choice of a CPU of
// the warden.  A bench process probes it one time, at its first use.
[[nodiscard]] inline crucible::warden::TopologyRef harness_topology() noexcept {
    return ::fixy::to(::fixy::topology::Topology::instance());
}

// The core of a run in the affinity allowed of the process: the asked core
// when allowed holds it, else the core of the measuring slot, else the core
// that warden::select_hot_cpu_of picks in allowed.  A run never leaves its
// affinity.  loop bench gives each bench cores of its own, so a pin to a core
// outside them would measure on the core of another bench.  None for an empty
// affinity.  Complexity: linear in the CPUs of allowed, times one sysfs read
// each.
[[nodiscard]] inline CpuId core_of_run(Cores const& cores, crucible::warden::CpuSet const& allowed,
                                       CpuId asked) noexcept {
    if (asked.is_valid() && allowed.contains(asked.raw())) return asked;
    ::fixy::Option<CpuId> const measuring = cores.of_slot(kMeasuringSlot);
    for (CpuId const core : measuring) {
        if (allowed.contains(core.raw())) return core;
    }
    return CpuId{crucible::warden::detail::select_hot_cpu_of(
        ::fixy::side_as<crucible::warden::side::AllowedCpus>(allowed), harness_topology(),
        crucible::warden::CoreSelector{},
        ::fixy::side_as<crucible::warden::side::ExcludedCpus>(crucible::warden::CpuSet{}))};
}

// Read /sys/.../cpu<N>/cpufreq/scaling_cur_freq (kHz) → Hz, 0 on failure.
[[nodiscard]] inline uint64_t read_cpu_freq_hz(int cpu) noexcept {
#ifdef __linux__
    if (cpu < 0) return 0;
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    FILE* f = std::fopen(path, "r");
    if (!f) return 0;
    long khz = 0;
    const int got = std::fscanf(f, "%ld", &khz);
    std::fclose(f);
    return (got == 1 && khz > 0) ? static_cast<uint64_t>(khz) * 1000u : 0u;
#else
    (void)cpu;
    return 0;
#endif
}

}  // namespace detail

// Pins the calling thread to one core for the rest of its life, through the
// gated door of fixy/os/Sched.h (THE CORES OF A RUN).  A worker that a body
// starts calls it first, with the core of its slot.  The result is the error
// of the kernel, or einval for a core that names no CPU.
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> pin_calling_thread_to(CpuId core) noexcept {
    if (!core.is_valid()) return ::fixy::err(::foundation::os::Errno::einval);
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    // A drop of the prior mask keeps the pin.
    return ::fixy::sched::apply_affinity_to_cpu(startup, core.raw())
        .match(
            [](::fixy::sched::PriorAffinity) noexcept -> ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> {
                return ::fixy::Unit{};
            },
            [](::foundation::os::Errno error) noexcept -> ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> {
                return ::fixy::err(error);
            });
}

namespace detail {

// The affinity mask of a set of CPUs.  A CPU past the last CPU of a mask is
// left out.  Complexity: linear in the CPUs of the set.
[[nodiscard]] inline ::fixy::AffinityMask mask_of(crucible::warden::CpuSet const& cpus) noexcept {
    ::fixy::AffinityMask mask{};
    for (int const cpu : cpus) {
        if (cpu > ::fixy::AffinityMask::kMaxCore) continue;
        mask = ::fixy::AffinityLattice::join(mask, ::fixy::AffinityMask::single(static_cast<std::uint16_t>(cpu)));
    }
    return mask;
}

}  // namespace detail

// The pin of the calling thread to some cores for a scope, and the restore of
// its affinity at the end of the scope.  A hold of no core changes nothing.
// The process ends when the pin or the restore fails, because the thread would
// then run on a CPU that the claim does not give it.
class CoreHold {
public:
    explicit CoreHold(CpuId core) noexcept {
        if (!core.is_valid()) return;
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        prior_ = ::fixy::some(::fixy::sched::apply_affinity_to_cpu(startup, core.raw())
                                  .expect("the pin of a thread to a core of the claim of its run"));
    }

    explicit CoreHold(crucible::warden::CpuSet const& cores) noexcept {
        if (cores.is_empty()) return;
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        prior_ = ::fixy::some(::fixy::sched::apply_affinity_to_mask(startup, detail::mask_of(cores))
                                  .expect("the pin of a thread to the cores of the claim of its run"));
    }

    ~CoreHold() {
        std::move(prior_).match(
            [](::fixy::sched::PriorAffinity held) noexcept {
                std::move(held).restore().expect("the restore of the affinity of a thread after a hold of one core");
            },
            []() noexcept {});
    }

    CoreHold(CoreHold const&) = delete("a copy would restore one affinity two times");
    CoreHold& operator=(CoreHold const&) = delete("a copy would restore one affinity two times");
    CoreHold(CoreHold&&) = delete("a hold ends in the scope of the thread that pinned");
    CoreHold& operator=(CoreHold&&) = delete("a hold ends in the scope of the thread that pinned");

private:
    ::fixy::Option<::fixy::sched::PriorAffinity> prior_{};
};

// Calls fn with the calling thread on one core, and then gives the thread back
// its affinity (THE CORES OF A RUN).  A thread that fn starts takes that core
// as its affinity and keeps it, so a thread that a library starts, such as the
// drain thread of a Vigil, runs on the core of its slot.  The result of fn is a
// prvalue, so an object that cannot move, such as a Vigil, is built in the
// place of the result:
//
//   Vigil vigil = bench::run_on_core(drain_core, [] { return Vigil{}; });
//
// A core that is not valid calls fn with no pin.
template <class Fn>
[[nodiscard]] decltype(auto) run_on_core(CpuId core, Fn&& fn) {
    CoreHold const hold{core};
    return static_cast<Fn&&>(fn)();
}

// Calls fn with the calling thread on a set of cores, and then gives the
// thread back its affinity.  A thread that fn starts takes that set as its
// mask.  It stays on the core on which it starts, but a library that pins its
// threads inside the mask of their caller gives each thread its own core of
// the set.  The pipeline of a Vigil does so for its four stages (fixy/
// concurrent/StageCpus.h):
//
//   Vigil vigil = bench::run_on_cores(cores.from_slot(bench::ThreadSlot{1}), [] { return Vigil{}; });
//
// An empty set calls fn with no pin.
template <class Fn>
[[nodiscard]] decltype(auto) run_on_cores(crucible::warden::CpuSet const& cores, Fn&& fn) {
    CoreHold const hold{cores};
    return static_cast<Fn&&>(fn)();
}

// ── THE PAUSE CHECK ────────────────────────────────────────────────
//
// loop bench reads the steal column of /proc/stat, which counts in ticks of
// 10 ms, so a pause of a few milliseconds reads as no steal.  The harness
// measures the time that the threads of a run lost, in nanoseconds, over the
// sampled loop of each Run.
//
// The thread CPU time of the kernel (CLOCK_THREAD_CPUTIME_ID) counts only the
// time in which the thread ran.  Under a hypervisor with a steal clock, the
// kernel leaves the stolen time out of it.  On the bench VM a busy loop of 2 s
// on cpu224 lost 0.25 to 3.3 ms between the wall time and the CPU time, while
// /proc/stat gave 0 ms of steal in 7 of 8 such loops.  So for the measuring
// thread, the wall time of the sampled loop minus its CPU time is its lost
// time: the steal of the hypervisor, the interrupts, and each wait on the run
// queue behind another thread of its core.
//
// A thread that sleeps, such as a thread that waits in a join, also loses the
// time of its sleep.  The harness reads the count of the voluntary switches of
// the measuring thread.  When it switched, its pause is its run delay alone:
// the time in which it waited on a run queue, from
// /proc/thread-self/schedstat.  The steal of its core is then not measured,
// and the report says so.
//
// Each other thread of the process that lives at the start and at the end of
// the sampled loop gives its run delay from /proc/self/task/TID/schedstat.  A
// run delay shows two threads on one core.  A thread that starts and ends in
// the loop gives none.
//
// A run delay is a pause only when its mean wait for each arrival on a CPU is
// more than kWaitBoundNs.  A thread that shares its core with a thread that
// runs waits a slice of the scheduler at each arrival, hundreds of
// microseconds or more.  A thread that parks and wakes many times, such as a
// stage of a pipeline, can wait some microseconds at each wake, and the sum of
// many such waits says nothing about a lost core.  The arrivals are the third
// number of schedstat.  The check marked the start of the pipeline of a Vigil
// in a sampled loop: a stage waited 1.6 ms in 5 arrivals on the core of the
// pipeline thread, before it moved to its own core.
//
// A Run whose pause is more than kPauseBoundNs is not valid: its row prints
// the pause and NOT VALID, and its JSON gives paused 1.

// The bound of the pause check: a Run in which a thread lost more time than
// this is not valid.  A clean isolated core of the bench VM loses less than
// 0.1 ms in 2 s, and a core whose host core runs other work loses 0.25 to
// 3.3 ms.
inline constexpr std::uint64_t kPauseBoundNs = 1000000;

// The bound of the mean wait for each arrival of a run delay that is a pause:
// a wait longer than the wake of an idle vCPU, and shorter than a slice of the
// scheduler.
inline constexpr std::uint64_t kWaitBoundNs = 100000;

// The threads whose run delay one reading holds, at most.  The other threads
// of a larger process are not read.
inline constexpr std::size_t kPauseThreads = 64;

// The run delay of one thread at one reading, in nanoseconds, and the count of
// its arrivals on a CPU.
struct ThreadDelay {
    ::fixy::process::ThreadId tid{};
    std::uint64_t run_delay_ns = 0;
    std::uint64_t arrivals = 0;
};

// True when a run delay over a window is a pause: more than kPauseBoundNs in
// all, with a mean wait for each arrival of more than kWaitBoundNs.  A delay
// with no arrival waited in one piece.
[[nodiscard]] constexpr bool is_run_delay_paused(ThreadDelay const& window) noexcept {
    return window.run_delay_ns > kPauseBoundNs && window.run_delay_ns > window.arrivals * kWaitBoundNs;
}

// How the pause of the measuring thread is judged: by its lost time, or, when
// it slept, by its run delay alone.
enum class PauseBasis : std::uint8_t {
    NotMeasured,
    LostTime,
    RunDelay,
};

// What the pause check reads at the start and at the end of a sampled loop:
// the clocks and the switches of the measuring thread, its run delay, and the
// run delay of each thread of the process.  basis is NotMeasured when a read
// failed.
struct PauseReading {
    PauseBasis basis = PauseBasis::NotMeasured;
    std::uint64_t wall_ns = 0;
    std::uint64_t cpu_ns = 0;
    std::uint64_t voluntary_switches = 0;
    ThreadDelay own{};
    ::fixy::FixedList<ThreadDelay, kPauseThreads> others{};
};

// The pause of one Run (THE PAUSE CHECK).
struct Pause {
    PauseBasis basis = PauseBasis::NotMeasured;
    // The wall time of the window, and the time that the measuring thread
    // did not run in it.
    std::uint64_t wall_ns = 0;
    std::uint64_t lost_ns = 0;
    // The run delay of the measuring thread in the window, and its arrivals.
    ThreadDelay own{};
    // The run delay of the other thread that the check names: the longest
    // delay that is a pause, or else the longest delay.
    ThreadDelay other{};

    // The pause of the measuring thread by its basis: its lost time, its run
    // delay when it slept, or zero when the check did not read.
    [[nodiscard]] constexpr std::uint64_t own_ns() const noexcept {
        switch (basis) {
            case PauseBasis::LostTime:
                return lost_ns;
            case PauseBasis::RunDelay:
                return own.run_delay_ns;
            case PauseBasis::NotMeasured:
                return 0;
            default:
                return 0;
        }
    }

    // True when the measuring thread lost more than kPauseBoundNs, or when a
    // run delay of a thread of the Run is a pause.
    [[nodiscard]] constexpr bool is_over_bound() const noexcept {
        const bool is_own_paused = basis == PauseBasis::LostTime ? lost_ns > kPauseBoundNs
                                 : basis == PauseBasis::RunDelay ? is_run_delay_paused(own)
                                                                 : false;
        return is_own_paused || is_run_delay_paused(other);
    }
};

namespace side {

// The roles of the two readings of a pause.
struct ReadingBefore final {};
struct ReadingAfter final {};

}  // namespace side

// The clocks of the measuring thread at one end of a window of the sampled
// loop, in nanoseconds (THE WINDOWS).  is_read is false when a clock did not
// read.
struct WindowClock {
    std::uint64_t wall_ns = 0;
    std::uint64_t cpu_ns = 0;
    bool is_read = false;
    ::fixy::FixedArray<std::uint8_t, 7> pad_after_flag{};
};

namespace detail {

// Writes the text that stdio holds for standard output, so a report of the
// base, which writes at once, comes after it.
inline void flush_standard_output() noexcept { static_cast<void>(std::fflush(stdout)); }

// The value of the first line of text that starts with key: the characters
// after the colon of the line, with no blank at either end, or no character.
// Complexity: linear in the text.
[[nodiscard]] inline ::fixy::TextView value_of_line(::fixy::TextView text, ::fixy::TextView key) noexcept {
    namespace topology = crucible::warden::detail;
    ::fixy::TextView rest = text;
    while (rest.size() != 0) {
        std::size_t const end = ::fixy::find(rest, '\n').value_or(rest.size());
        ::fixy::TextView const line = topology::text_prefix(rest, end);
        rest = topology::text_suffix(rest, end + 1);
        if (!::fixy::starts_with(line, key)) continue;
        std::size_t const colon = ::fixy::find(line, ':').value_or(line.size());
        return topology::trim_blanks(topology::text_suffix(line, colon + 1));
    }
    return ::fixy::TextView{};
}

// The first line of a small file of procfs or sysfs, or no character when the
// file does not read.  The text lives in the FixedText of the caller.
template <std::size_t Capacity>
[[nodiscard]] ::fixy::TextView read_first_line(::fixy::CText path, ::fixy::FixedText<Capacity>& text) noexcept {
    if (::foundation::os::read_file(path, text).is_err()) return ::fixy::TextView{};
    ::fixy::TextView const all = text.view();
    return crucible::warden::detail::text_prefix(all, ::fixy::find(all, '\n').value_or(all.size()));
}

// The difference of two counters that never go down, or zero when the later
// one is lower, as for a counter of a thread that the kernel reset.
[[nodiscard]] constexpr std::uint64_t grown(::fixy::Side<side::ReadingBefore, std::uint64_t> before,
                                            ::fixy::Side<side::ReadingAfter, std::uint64_t> after) noexcept {
    return after.value() > before.value() ? after.value() - before.value() : 0;
}

// The run delay of one thread over a window: the growth of its delay and of
// its arrivals between two readings.
[[nodiscard]] constexpr ThreadDelay delay_between(::fixy::Side<side::ReadingBefore, ThreadDelay> const& before,
                                                  ::fixy::Side<side::ReadingAfter, ThreadDelay> const& after) noexcept {
    const auto delta = [](std::uint64_t first, std::uint64_t last) noexcept {
        return grown(::fixy::side_as<side::ReadingBefore>(first), ::fixy::side_as<side::ReadingAfter>(last));
    };
    return ThreadDelay{.tid = after.value().tid,
                       .run_delay_ns = delta(before.value().run_delay_ns, after.value().run_delay_ns),
                       .arrivals = delta(before.value().arrivals, after.value().arrivals)};
}

// The pause between two readings of one thread (THE PAUSE CHECK).  The
// measuring thread is judged by its run delay when it made a voluntary switch,
// and else by its lost time.  The other threads that both readings hold give
// the longest run delay that is a pause, or else the longest run delay.
// Complexity: O(n * m) in the threads of the two readings, at most
// kPauseThreads each.
[[nodiscard]] constexpr Pause pause_between(::fixy::Side<side::ReadingBefore, PauseReading> const& before_side,
                                            ::fixy::Side<side::ReadingAfter, PauseReading> const& after_side) noexcept {
    PauseReading const& before = before_side.value();
    PauseReading const& after = after_side.value();
    Pause pause{};
    if (before.basis == PauseBasis::NotMeasured || after.basis == PauseBasis::NotMeasured) return pause;
    pause.wall_ns =
        grown(::fixy::side_as<side::ReadingBefore>(before.wall_ns), ::fixy::side_as<side::ReadingAfter>(after.wall_ns));
    const std::uint64_t cpu_ns =
        grown(::fixy::side_as<side::ReadingBefore>(before.cpu_ns), ::fixy::side_as<side::ReadingAfter>(after.cpu_ns));
    pause.lost_ns = pause.wall_ns > cpu_ns ? pause.wall_ns - cpu_ns : 0;
    pause.own =
        delay_between(::fixy::side_as<side::ReadingBefore>(before.own), ::fixy::side_as<side::ReadingAfter>(after.own));
    pause.basis = after.voluntary_switches > before.voluntary_switches ? PauseBasis::RunDelay : PauseBasis::LostTime;
    for (ThreadDelay const& late : after.others) {
        if (late.tid == after.own.tid) continue;
        for (ThreadDelay const& early : before.others) {
            if (early.tid != late.tid) continue;
            const ThreadDelay window =
                delay_between(::fixy::side_as<side::ReadingBefore>(early), ::fixy::side_as<side::ReadingAfter>(late));
            const bool is_paused = is_run_delay_paused(window);
            const bool was_paused = is_run_delay_paused(pause.other);
            const bool is_longer = window.run_delay_ns > pause.other.run_delay_ns;
            if ((is_paused && (!was_paused || is_longer)) || (!was_paused && is_longer)) pause.other = window;
        }
    }
    return pause;
}

// The first decimal number of a text, after the blanks that lead it, or none.
[[nodiscard]] inline ::fixy::Option<std::uint64_t> leading_number(::fixy::TextView text) noexcept {
    namespace topology = crucible::warden::detail;
    ::fixy::TextView const trimmed = topology::trim_blanks(text);
    std::size_t digits = 0;
    for (char const character : trimmed) {
        if (character < '0' || character > '9') break;
        ++digits;
    }
    if (digits == 0) return ::fixy::none;
    return ::fixy::parse<std::uint64_t>(topology::text_prefix(trimmed, digits))
        .match([](std::uint64_t value) noexcept -> ::fixy::Option<std::uint64_t> { return ::fixy::some(value); },
               [](::fixy::ParseError) noexcept -> ::fixy::Option<std::uint64_t> { return ::fixy::none; });
}

// The run delay and the arrivals of a schedstat file of procfs, which holds
// the run time, the run delay in nanoseconds and the arrivals on a CPU, or
// none when the file does not read.  The thread id of the result is zero.
[[nodiscard]] inline ::fixy::Option<ThreadDelay> read_schedstat(::fixy::CText path) noexcept {
    namespace topology = crucible::warden::detail;
    ::fixy::FixedText<128> text{};
    if (::foundation::os::read_file(path, text).is_err()) return ::fixy::none;
    ::fixy::TextView const all = text.view();
    ::fixy::TextView const delay_text = topology::text_suffix(all, ::fixy::find(all, ' ').value_or(all.size()) + 1);
    ::fixy::TextView const arrivals_text =
        topology::text_suffix(delay_text, ::fixy::find(delay_text, ' ').value_or(delay_text.size()) + 1);
    ::fixy::Option<std::uint64_t> delay = leading_number(delay_text);
    ::fixy::Option<std::uint64_t> arrivals = leading_number(arrivals_text);
    if (delay.is_none() || arrivals.is_none()) return ::fixy::none;
    return ::fixy::some(
        ThreadDelay{.run_delay_ns = std::move(delay).value_or(0), .arrivals = std::move(arrivals).value_or(0)});
}

// The run delay of each thread of the process, at most kPauseThreads of them.
// Complexity: one listing of /proc/self/task and one read for each thread.
inline void read_thread_delays(::fixy::FixedList<ThreadDelay, kPauseThreads>& delays) noexcept {
    static_cast<void>(::foundation::os::for_each_entry("/proc/self/task", [&delays](::fixy::CText name) noexcept {
        ::fixy::Option<std::uint64_t> tid = leading_number(name.view());
        ::foundation::os::PathText<64> path{};
        const bool is_built = path.append(::fixy::CText{"/proc/self/task/"}).is_ok() && path.append(name).is_ok()
                           && path.append(::fixy::CText{"/schedstat"}).is_ok();
        if (tid.is_none() || !is_built) return ::foundation::os::Visit::proceed;
        ::fixy::Option<ThreadDelay> found = read_schedstat(path.c_text());
        if (found.is_none()) return ::foundation::os::Visit::proceed;
        ThreadDelay delay = std::move(found).value_or(ThreadDelay{});
        delay.tid = ::fixy::process::ThreadId{::fixy::narrow<std::uint32_t>(std::move(tid).value_or(0)).value_or(0)};
        return delays.append(delay).is_ok() ? ::foundation::os::Visit::proceed : ::foundation::os::Visit::stop;
    }));
}

// Reads the facts of the pause check for the calling thread and for each
// thread of the process (THE PAUSE CHECK).  A reading whose clocks or files
// do not read has the basis NotMeasured.  The reads cost a listing of
// /proc/self/task and some reads of procfs, outside the timed regions.
[[nodiscard, gnu::noinline]] inline PauseReading read_pause() noexcept {
    PauseReading reading{};
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    const ::foundation::effects::cap::Nondet nondet = ::foundation::effects::testing::test().nondet;
    const auto wall = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
    const auto cpu = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::ThreadCpu>(startup);
    ::fixy::FixedText<4096> status{};
    const bool is_status_read = ::foundation::os::read_file("/proc/thread-self/status", status).is_ok();
    ::fixy::Option<std::uint64_t> switches = is_status_read
                                               ? leading_number(value_of_line(status.view(), "voluntary_ctxt_switches"))
                                               : ::fixy::Option<std::uint64_t>{};
    ::fixy::Option<ThreadDelay> own = read_schedstat("/proc/thread-self/schedstat");
    auto cpu_read = cpu.read(nondet);
    auto wall_read = wall.read(nondet);
    if (switches.is_none() || own.is_none() || cpu_read.is_err() || wall_read.is_err()) return reading;
    reading.cpu_ns = std::move(cpu_read).expect("the test above found the reading").peek();
    reading.wall_ns = std::move(wall_read).expect("the test above found the reading").peek();
    reading.voluntary_switches = std::move(switches).value_or(0);
    reading.own = std::move(own).value_or(ThreadDelay{});
    reading.own.tid = ::fixy::process::current_thread(startup);
    read_thread_delays(reading.others);
    reading.basis = PauseBasis::LostTime;
    return reading;
}

// Reads the two clocks of the calling thread for the check of a window (THE
// WINDOWS): two reads of a clock and no read of procfs, so the check makes no
// call that can sleep inside the sampled loop.  A clock that does not read
// gives a reading that is not read.
[[nodiscard, gnu::noinline]] inline WindowClock read_window_clock() noexcept {
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    const ::foundation::effects::cap::Nondet nondet = ::foundation::effects::testing::test().nondet;
    const auto wall = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
    const auto cpu = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::ThreadCpu>(startup);
    auto cpu_read = cpu.read(nondet);
    auto wall_read = wall.read(nondet);
    if (cpu_read.is_err() || wall_read.is_err()) return WindowClock{};
    return WindowClock{.wall_ns = std::move(wall_read).expect("the test above found the reading").peek(),
                       .cpu_ns = std::move(cpu_read).expect("the test above found the reading").peek(),
                       .is_read = true};
}

// The time that the measuring thread did not run between two readings of its
// clocks: the growth of the wall time minus the growth of the CPU time, or
// zero when a reading is not read.
[[nodiscard]] constexpr std::uint64_t
lost_between(::fixy::Side<side::ReadingBefore, WindowClock> const& before,
             ::fixy::Side<side::ReadingAfter, WindowClock> const& after) noexcept {
    if (!before.value().is_read || !after.value().is_read) return 0;
    const std::uint64_t wall_ns = grown(::fixy::side_as<side::ReadingBefore>(before.value().wall_ns),
                                        ::fixy::side_as<side::ReadingAfter>(after.value().wall_ns));
    const std::uint64_t cpu_ns = grown(::fixy::side_as<side::ReadingBefore>(before.value().cpu_ns),
                                       ::fixy::side_as<side::ReadingAfter>(after.value().cpu_ns));
    return wall_ns > cpu_ns ? wall_ns - cpu_ns : 0;
}

// The windows whose lost time is over kPauseBoundNs, when the check of the
// whole loop judged the measuring thread by its lost time, or no window for
// each other basis (THE WINDOWS).  A thread that made no voluntary switch in
// the loop slept in no window, so the lost time of each window is a pause.  A
// thread that slept lost the time of its sleep too, and the clocks of a window
// cannot tell the two apart.  Complexity: linear in the windows.
[[nodiscard]] constexpr std::size_t paused_windows_of(::fixy::View<std::uint64_t const> lost_ns,
                                                      PauseBasis whole_loop) noexcept {
    if (whole_loop != PauseBasis::LostTime) return 0;
    std::size_t paused = 0;
    for (std::uint64_t const window_lost_ns : lost_ns) {
        paused += window_lost_ns > kPauseBoundNs ? std::size_t{1} : std::size_t{0};
    }
    return paused;
}

// The field processor of a stat file of procfs: the CPU on which the thread
// ran last, or none.  The name of a thread can hold a parenthesis and blanks,
// so the fields start after the last closing parenthesis.  Field 3, the
// state, comes first there, and processor is field 39.  Complexity: linear in
// the text.
[[nodiscard]] inline ::fixy::Option<std::uint64_t> processor_of_stat(::fixy::TextView stat) noexcept {
    namespace topology = crucible::warden::detail;
    constexpr std::size_t kFieldsBeforeProcessor = 36;
    std::size_t after_name = 0;
    std::size_t position = 0;
    for (char const character : stat) {
        ++position;
        if (character == ')') after_name = position;
    }
    if (after_name == 0) return ::fixy::none;
    ::fixy::TextView rest = topology::trim_blanks(topology::text_suffix(stat, after_name));
    for (std::size_t field = 0; field < kFieldsBeforeProcessor; ++field) {
        std::size_t const blank = ::fixy::find(rest, ' ').value_or(rest.size());
        rest = topology::trim_blanks(topology::text_suffix(rest, blank + 1));
    }
    return leading_number(rest);
}

}  // namespace detail

// Prints the id of each thread of the process, the CPU on which it ran last,
// and the count of its involuntary switches since it started, from
// /proc/self/task/TID/stat and status, on one line after the label.  A bench
// of several threads prints it after a row, while its threads live, to show
// that no two of them share a core (THE CORES OF A RUN).  A thread that is
// alone on its core makes an involuntary switch only for a kernel thread of
// that core.  Complexity: one listing of /proc/self/task and two reads for
// each thread.
inline void print_thread_cpus(::fixy::TextView label) noexcept {
    using ::fixy::Sink;
    detail::flush_standard_output();
    ::fixy::report(Sink::Out, "     └─ threads of {} (cpu, involuntary switches):", label);
    static_cast<void>(::foundation::os::for_each_entry("/proc/self/task", [](::fixy::CText name) noexcept {
        const auto read = [&name](::fixy::CText leaf, auto& text) noexcept {
            ::foundation::os::PathText<64> path{};
            const bool is_built = path.append(::fixy::CText{"/proc/self/task/"}).is_ok() && path.append(name).is_ok()
                               && path.append(leaf).is_ok();
            return is_built && ::foundation::os::read_file(path.c_text(), text).is_ok();
        };
        ::fixy::FixedText<1024> stat{};
        ::fixy::FixedText<4096> status{};
        if (!read("/stat", stat) || !read("/status", status)) return ::foundation::os::Visit::proceed;
        ::fixy::Option<std::uint64_t> cpu = detail::processor_of_stat(stat.view());
        ::fixy::Option<std::uint64_t> switches =
            detail::leading_number(detail::value_of_line(status.view(), "nonvoluntary_ctxt_switches"));
        if (cpu.is_some()) {
            ::fixy::report(Sink::Out, " {} cpu{} {},", name.view(), std::move(cpu).value_or(0),
                           std::move(switches).value_or(0));
        }
        return ::foundation::os::Visit::proceed;
    }));
    ::fixy::report(Sink::Out, "\n");
}

// The CPUs on which the threads of a body ran.  Each thread that the body
// starts notes the CPU on which it runs, and the main thread prints the set
// after the measurement, with the count of the notes of each CPU.  A body
// whose threads share one CPU shows one CPU.  The tally holds one counter for
// each CPU that an affinity mask can name, and a note of a CPU past them
// counts in the last counter.  Complexity: one atomic add for each note, and
// one pass of the counters for each print.
class CpuTally {
public:
    CpuTally() = default;
    CpuTally(CpuTally const&) = delete("the threads of a body note into one tally");
    CpuTally& operator=(CpuTally const&) = delete("the threads of a body note into one tally");

    // Notes the CPU of the calling thread.
    void note_calling_thread() noexcept {
        const int cpu = sched_getcpu();
        const std::size_t position =
            cpu < 0 || static_cast<std::size_t>(cpu) >= kSpreadCpus ? kSpreadCpus - 1 : static_cast<std::size_t>(cpu);
        static_cast<void>(
            counts_.at(::fixy::Index<kSpreadCpus>::check(position).expect("the test above bounds the CPU"))
                .fetch_add_relaxed(1));
    }

    // Prints each CPU with a note, and its count of notes, on one line after
    // the label.  Call it after the threads that noted joined.
    void print(::fixy::TextView label) const noexcept {
        using ::fixy::Sink;
        detail::flush_standard_output();
        ::fixy::report(Sink::Out, "     └─ cpus of the threads of {} (cpu: notes):", label);
        std::size_t cpu = 0;
        for (Counter const& count : counts_) {
            const std::uint64_t notes = count.load_acquire();
            if (notes != 0) ::fixy::report(Sink::Out, " cpu{}: {}", cpu, notes);
            ++cpu;
        }
        ::fixy::report(Sink::Out, "\n");
    }

private:
    using Counter = ::fixy::Atomic<std::uint64_t, ::fixy::Counter>;
    ::fixy::FixedArray<Counter, kSpreadCpus> counts_{};
};

// ── BPF perf::Senses accessor (shared aggregator singleton) ───────
//
// Single entry point for every BPF program the harness consumes.
//
// All Runs in the same process share one loaded set of BPF programs
// via crucible::perf::Senses — the kernel tracepoints stay attached
// for the lifetime of the bench binary.  First call loads + attaches;
// subsequent calls are a simple pointer return.  load_subset() is
// used so the harness pays the same per-event cost as before (only
// the two facades the harness actually consumes: SenseHub + SchedSwitch);
// callers that want richer telemetry (PmuSample / LockContention /
// SyscallLatency) can construct their own Senses with a wider mask.
//
// Per-facade availability is queried via Senses accessors:
//   senses_instance()->sense_hub()    — 96-counter mmap snapshot
//   senses_instance()->sched_switch() — per-tenant off-CPU timeline
// Either may return none if that facade failed to attach (missing
// CAP_BPF, kernel.unprivileged_bpf_disabled=2, kernel too old);
// senses_instance() itself is never nullptr (Senses is a value).

namespace detail {
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
[[nodiscard]] inline const ::crucible::perf::Senses* senses_instance() noexcept {
    // Senses::load_subset(startup load context, mask) loads only the
    // masked facades, which are sense_hub and sched_switch.  The bench
    // takes the startup load context from the test door.  Hot-path code
    // holds no init context, so it cannot make the argument, and the
    // call chain of Senses::load_subset() never reaches a hot frame.
    // Senses is move-only, and the static is built from the returned
    // value.
    static ::crucible::perf::Senses slot = ::crucible::perf::Senses::load_subset(
        ::fixy::InitLoadCtx{::foundation::effects::testing::init()}, ::crucible::perf::SensesMask{
                                                                         .sense_hub = true,
                                                                         .sched_switch = true,
                                                                     });
    return &slot;
}
#else
[[nodiscard]] inline const void* senses_instance() noexcept { return nullptr; }
#endif
}  // namespace detail

// ── Percentile with linear interpolation (R type 7) ───────────────
//
// Hyndman & Fan (1996) enumerate nine sample-quantile definitions.
// Type 7 (R default) interpolates between the two samples straddling
// frac * (n-1). Removes the integer-floor bias that otherwise appears
// in commit-to-commit diffs.

// A frac outside [0, 1] names no sample, and the read of it ends the process.
// Complexity: two reads.
[[nodiscard]] inline double percentile_interp(::fixy::View<const double> sorted, double frac) noexcept {
    const size_t n = sorted.size();
    if (n == 0) return 0.0;
    if (n == 1) return *sorted.get(0).expect("a run of one sample holds its first sample");
    const double q = frac * static_cast<double>(n - 1);
    const size_t lo = static_cast<size_t>(q);
    const size_t hi = (lo + 1 < n) ? (lo + 1) : lo;
    const double f = q - static_cast<double>(lo);
    return *sorted.get(lo).expect("a fraction in [0, 1] names a sample") * (1.0 - f)
         + *sorted.get(hi).expect("the sample after the low sample is in the run") * f;
}

// The samples in ascending order, sorted in their own storage by sort_stably
// and Ascending, with a scratch from the heap.  A sample is a time of at
// least zero with no NaN, so Ascending gives the order of <.  Complexity: one
// radix sort of the samples, and one allocation of the scratch.
[[nodiscard]] inline ::fixy::HeapArray<double> sorted_samples(::fixy::HeapArray<double> samples) noexcept {
    std::size_t const count = samples.size();
    if (count == 0) return samples;
    auto const alloc = ::foundation::effects::testing::test().alloc;
    ::fixy::HeapArray<double> scratch = ::fixy::mint_heap_array(alloc, count, 0.0);
    bool is_sorted = false;
    samples = ::fixy::with_mut(std::move(samples), [&scratch, &is_sorted](auto elements) noexcept {
        scratch = ::fixy::with_mut(std::move(scratch), [&elements, &is_sorted](auto room) noexcept {
            is_sorted =
                ::foundation::core::sort_stably(std::move(elements), std::move(room), ::foundation::core::Ascending{})
                    .is_ok();
        });
    });
    if (!is_sorted) ::fixy::fatal("the scratch of the sort of the samples of a run has the count of the samples");
    return samples;
}

// The median of the samples of one part of a run, such as a window or a half,
// with the interpolation of percentile_interp at 0.5, from sorted_samples.  An
// empty part gives 0.  Complexity: one sort of the part, and one pass up to
// its middle.
[[nodiscard]] inline double median_of_part(::fixy::HeapArray<double> unsorted) noexcept {
    std::size_t const count = unsorted.size();
    if (count == 0) return 0.0;
    ::fixy::HeapArray<double> const part = sorted_samples(std::move(unsorted));
    double const middle = 0.5 * static_cast<double>(count - 1);
    auto const low_position = static_cast<std::size_t>(middle);
    std::size_t const high_position = low_position + 1 < count ? low_position + 1 : low_position;
    double const fraction = middle - static_cast<double>(low_position);
    double low = 0.0;
    double high = 0.0;
    std::size_t position = 0;
    for (double const value : part) {
        if (position == low_position) low = value;
        if (position == high_position) {
            high = value;
            break;
        }
        ++position;
    }
    return low * (1.0 - fraction) + high * fraction;
}

namespace side {

// The roles of the two positions of a part of the samples.
struct PartFirst final {};
struct PartEnd final {};

}  // namespace side

// A copy of the samples from first up to end, the part of median_of_part.
// Complexity: one allocation and one copy of the part.
[[nodiscard]] inline ::fixy::HeapArray<double> part_of_samples(::fixy::View<const double> samples,
                                                               ::fixy::Side<side::PartFirst, std::size_t> first,
                                                               ::fixy::Side<side::PartEnd, std::size_t> end) noexcept {
    std::size_t const begin = first.value();
    ::fixy::View<const double> const part =
        samples.window(begin, end.value() - begin).expect("a part of the samples lies inside the samples");
    return ::fixy::mint_heap_array_with<double>(
        ::foundation::effects::testing::test().alloc, part.size(),
        [&part](std::size_t position) noexcept { return *part.get(position).expect("a position below the count"); });
}

struct Percentiles {
    double p50 = 0, p75 = 0, p90 = 0, p95 = 0;
    double p99 = 0, p99_9 = 0, p99_99 = 0;
    double min = 0, max = 0, mean = 0, stddev = 0, cv = 0;
    size_t n = 0;

    // Sorts the samples in place (sorted_samples) and reads the percentiles,
    // the mean and the deviation of the sorted samples.  Complexity: one sort
    // and one pass of the samples.
    [[nodiscard]] static Percentiles compute(::fixy::HeapArray<double>& ns_samples) noexcept {
        Percentiles p{};
        p.n = ns_samples.size();
        if (p.n == 0) return p;

        ns_samples = sorted_samples(std::move(ns_samples));
        ::fixy::View<const double> const sorted{ns_samples.view()};
        p.min = *sorted.get(0).expect("a run of samples holds its first sample");
        p.max = *sorted.get(p.n - 1).expect("a run of samples holds its last sample");
        p.p50 = percentile_interp(sorted, 0.50);
        p.p75 = percentile_interp(sorted, 0.75);
        p.p90 = percentile_interp(sorted, 0.90);
        p.p95 = percentile_interp(sorted, 0.95);
        p.p99 = percentile_interp(sorted, 0.99);
        p.p99_9 = percentile_interp(sorted, 0.999);
        p.p99_99 = percentile_interp(sorted, 0.9999);

        // Welford's one-pass online algorithm with IEEE 754 binary64
        // (double) accumulators, for two reasons:
        //
        // (1) Numerical stability.  The two-pass formula
        //     var = (Σx² − (Σx)²/n) / (n−1) catastrophically cancels
        //     when σ² ≪ μ² — the common case for bench timing where
        //     mean=50ns σ=5ns gives Σx²≈n·2500 minus (Σx)²/n≈n·2500
        //     and the small difference is the variance.  Welford
        //     accumulates Σ(x−mean)² directly, no cancellation.
        //     Welford 1962, "Note on a Method for Calculating
        //     Corrected Sums of Squares and Products".
        //
        // (2) Cross-platform bit-equality.  `long double` is 80-bit
        //     on x86-64 (x87 extended precision) but 64-bit on
        //     AArch64/Apple-Silicon — same input would give
        //     bit-different stddev across the fleet.  `double` is
        //     IEEE 754 binary64 on every conforming platform.  The
        //     project's crucible_fp_strict floor (-ffp-contract=off,
        //     -fno-fast-math, -fno-associative-math) blocks compiler
        //     FMA fusion that would otherwise perturb the
        //     accumulator update order.
        double mean = 0.0;
        double m2 = 0.0;
        size_t k = 0;
        for (double v : sorted) {
            ++k;
            const double delta = v - mean;
            mean += delta / static_cast<double>(k);
            const double delta2 = v - mean;
            m2 += delta * delta2;
        }
        p.mean = mean;
        if (p.n > 1) {
            const double var = m2 / static_cast<double>(p.n - 1);
            p.stddev = std::sqrt(std::max(0.0, var));
        }
        p.cv = (p.mean > 0) ? (p.stddev / p.mean) : 0.0;
        return p;
    }
};

// ── Bootstrap CI (Efron 1979) ─────────────────────────────────────

struct CI {
    double lo = 0;
    double hi = 0;
};

// The interval of the percentile frac of the samples from B resamples, each a
// draw of n samples with replacement.  Draw d of the run takes the word d mod
// 4 of the Philox4x32-10 block of the counter d / 4 under the key seed, and
// the high 32 bits of its product with n are the position of the drawn
// sample.  The samples of one run are fewer than 2^32, so the product fits 64
// bits.  One seed gives the same interval on each run and each host.  Fewer
// than 30 samples or no resample gives the interval {0, 0}.  Complexity: B
// resamples, each with n draws and one radix sort of n samples.
[[nodiscard]] inline CI bootstrap_ci(::fixy::View<const double> ns_samples, double frac, size_t B = 1000,
                                     double alpha = 0.05, uint64_t seed = 0xBEEFCAFEDEADF00Dull) {
    const size_t n = ns_samples.size();
    if (n < 30 || B == 0) return CI{};

    auto const alloc = ::foundation::effects::testing::test().alloc;
    uint64_t draw = 0;
    ::foundation::core::Philox4x32::Counter block{};
    const auto pick = [&ns_samples, &draw, &block, n, seed](std::size_t) noexcept {
        const uint64_t lane = draw % 4;
        if (lane == 0) block = ::foundation::core::Philox4x32::generate(draw / 4, seed);
        ++draw;
        const uint64_t word = block.at(::fixy::Index<4>::check(lane).expect("a lane of a block is below 4"));
        const std::size_t position = (word * uint64_t{n}) >> 32;
        return *ns_samples.get(position).expect("the high half of a product with n is below n");
    };
    ::fixy::HeapArray<double> estimates = ::fixy::mint_heap_array(alloc, B, 0.0);
    for (size_t b = 0; b < B; ++b) {
        const ::fixy::HeapArray<double> resample = sorted_samples(::fixy::mint_heap_array_with<double>(alloc, n, pick));
        *estimates.get(b).expect("b is below the count of the resamples") =
            percentile_interp(::fixy::View<const double>{resample.view()}, frac);
    }
    estimates = sorted_samples(std::move(estimates));
    const ::fixy::View<const double> sorted{estimates.view()};
    return {
        percentile_interp(sorted, alpha / 2.0),
        percentile_interp(sorted, 1.0 - alpha / 2.0),
    };
}

// ── THE WINDOWS ────────────────────────────────────────────────────
//
// Run::measure splits its sampled loop into kWindows consecutive windows.
// Each window takes its part of the samples and its part of the time that
// the wall cap leaves after the warmup, so the samples and the time of a run
// do not grow.  A window ends after its part of the samples, or at the first
// sample past the end of its part of the time after its first
// kMinWindowSamples samples.  So a run under the cap takes at least
// kWindows * kMinWindowSamples samples, and a run that asks for at most that
// count takes each sample that it asks for.  A heavy body, such as a walk of
// threads, keeps the samples of its run, and a body of nanoseconds never
// meets the cap.  A run of fewer than kWindows samples has one window for
// each sample.
//
// Each window has its own pause check: the time that the measuring thread
// lost in the window, its wall time minus its CPU time, against kPauseBoundNs
// (THE PAUSE CHECK).  paused_windows counts the windows over the bound.  The
// check reads two clocks at each end of a window and no file of procfs.  A
// read of procfs inside the loop can sleep, and a sleep makes the check of the
// whole loop judge the measuring thread by its run delay, which can miss a
// pause.  A measuring thread that slept in the loop, such as a thread that
// joins the threads of its body, lost the time of its sleep too, and two clocks
// cannot tell a sleep from a pause.  So the windows judge only a loop that the
// check of the whole loop judges by its lost time, and window_basis says so.
// The pause of a report is the check of the whole loop, with the run delays of
// the threads.
//
// The fields of a report before the windows keep their meaning: each
// percentile is over all the samples of the run.  The report adds the count
// of the windows, the median of the p50 of the windows, and their spread: the
// largest p50 minus the smallest, as a part of that median.  A row whose time
// moves between the windows of one run shows a large spread, as a row that
// moves between two runs does.

// The windows of a sampled loop, and the samples that a window takes before
// the end of its time can end it: five windows take at least 65 samples.
inline constexpr std::size_t kWindows = 5;
inline constexpr std::size_t kMinWindowSamples = 13;

// The values of the windows of one run, summed up (THE WINDOWS).  The values
// of a Report are the p50 of its windows in nanoseconds.  A bench can sum up
// other values of its windows, such as a throughput.
struct WindowSpread {
    // The windows with a value.
    std::size_t windows = 0;
    // The median of the values, by R type 7, in the unit of the values.
    double median = 0.0;
    // The largest value minus the smallest, as a part of the median.  Zero
    // for fewer than two windows, and for a median of zero.
    double spread = 0.0;
};

// The median and the spread of the medians of the windows of one run (THE
// WINDOWS).  The median is the R type 7 median of percentile_interp: the
// middle value of an odd count, and the mean of the two middle values of an
// even count.  A value takes its rank from the values below it and the equal
// values before it, so no sort and no compare of two floats for equality is
// necessary.  Complexity: quadratic in the count of the medians.
[[nodiscard]] inline WindowSpread spread_of_windows(::fixy::View<double const> medians) noexcept {
    std::size_t const count = medians.size();
    if (count == 0) return WindowSpread{};
    std::size_t const low_rank = (count - 1) / 2;
    std::size_t const high_rank = count / 2;
    double low = 0.0;
    double high = 0.0;
    double smallest = 0.0;
    double largest = 0.0;
    std::size_t position = 0;
    for (double const value : medians) {
        std::size_t rank = 0;
        std::size_t other = 0;
        for (double const compared : medians) {
            bool const is_before = compared < value || (other < position && !(value < compared));
            rank += is_before ? std::size_t{1} : std::size_t{0};
            ++other;
        }
        low = rank == low_rank ? value : low;
        high = rank == high_rank ? value : high;
        smallest = rank == 0 ? value : smallest;
        largest = rank + 1 == count ? value : largest;
        ++position;
    }
    double const median = low * 0.5 + high * 0.5;
    double const spread = count >= 2 && median > 0.0 ? (largest - smallest) / median : 0.0;
    return WindowSpread{.windows = count, .median = median, .spread = spread};
}

// ── Report ────────────────────────────────────────────────────────

// A report owns each of its parts: the name and the samples own their bytes,
// and each other part is a plain value.  So a report moves to another
// thread, and it states sendable (foundation/Transfer.h), which the pointers
// of the name and of the samples hide.
struct[[= ::foundation::transfer::sendable{}]] Report {
    std::string name;
    size_t batch = 1;  // > 1 → pct over batch means  // TODO: strong type
    CpuId pinned_cpu{};  // CpuId::none() = -1 / no pin, or a Run::fan_out measurement
    // The CPUs that the measuring thread and the threads of its body could
    // use: 1 for a pin to one CPU, the CPUs that isolcpus did not isolate
    // for Run::fan_out, 0 when the harness set no affinity.
    size_t pinned_cpu_count = 0;
    double wall_ns = 0;  // total measurement wall time
    double drift_pct = 0;  // |first-half - second-half p50| / p50
    bool drift_flag = false;  // drift_pct > 10 %
    Percentiles pct{};

    // sysfs-derived frequency bracketing (not PMU).
    uint64_t freq_start_hz = 0;  // scaling_cur_freq at run start
    uint64_t freq_end_hz = 0;  // scaling_cur_freq at run end
    bool freq_drift_flag = false;

    // Cycles/op computed from rdtsc-derived ns and the TSC frequency
    // (Timer::tsc_freq_hz). This is wall-time cycles, not PMU retired
    // cycles — but on a pinned, non-throttled core the two are equal
    // to within a few percent and suffice for cross-commit comparison.
    double cycles_per_op = 0;

    // The time that the threads of the run lost over the sampled loop (THE
    // PAUSE CHECK).  A pause over kPauseBoundNs makes the run not valid.
    Pause pause{};

    // The medians of the windows of the sampled loop, how the windows were
    // judged, and the count of the windows whose own pause is over
    // kPauseBoundNs (THE WINDOWS).  window_basis is LostTime when the windows
    // were judged, and NotMeasured when the measuring thread slept in the loop.
    WindowSpread windows{};
    PauseBasis window_basis = PauseBasis::NotMeasured;
    std::size_t paused_windows = 0;

    // Kernel-side context via eBPF sense hub. All zero when BPF is
    // unavailable. Deltas over the measured run (monotonic counters).
    //
    // For the gauge-valued Idx slots listed in bpf_senses.h's
    // Snapshot::operator- comment (FD_CURRENT, TCP_{MIN,MAX}_SRTT_US,
    // TCP_LAST_CWND, THERMAL_MAX_TRIP, SIGNAL_LAST_SIGNO, OOM_KILL_US,
    // RECLAIM_STALL_LOOPS, NET_TCP_*, NET_UDP_ACTIVE, NET_UNIX_ACTIVE,
    // RSS_{ANON,FILE,SHMEM}_BYTES, RSS_SWAP_ENTRIES) the post - pre
    // difference is meaningless; callers wanting those instantaneous
    // values should pull them from the post snapshot directly.
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    ::crucible::perf::Snapshot bpf_delta{};
    size_t bpf_attached = 0;  // # programs the kernel accepted

    // SchedSwitch off-CPU drill-down for the bench window.  Pre/post
    // are timeline_write_index() captures (~1 ns each, no syscall);
    // events in [pre, post) are the off-CPU records that completed
    // during this run.  At print time we walk that range and keep a
    // sorted top-K of the longest off-CPU spikes — explains *which*
    // preempts cost the most when SenseHub's `preempt`/`wait` deltas
    // are non-zero.  K=5 covers the common diagnosis scenario (a few
    // big spikes + many tiny ones); the `(×N)` suffix on print
    // discloses the total event count when N > K.  All zero on
    // systems without SchedSwitch loaded.
    static constexpr uint32_t kSchedOffCpuTopK = 5;
    uint64_t sched_pre_idx = 0;
    uint64_t sched_post_idx = 0;
    uint64_t sched_offcpu_top_ns[kSchedOffCpuTopK] = {};  // sorted desc
    uint32_t sched_offcpu_top_cpu[kSchedOffCpuTopK] = {};  // parallel
    uint32_t sched_offcpu_top_n = 0;  // # used
    uint32_t sched_offcpu_count = 0;  // total events
#endif

    ::fixy::HeapArray<double> samples{};  // kept for bootstrap_ci, compare

    // Report owns a potentially-large sample vector (~100k doubles by
    // default = 800 KB). Copying is almost always an accidental pessim-
    // ization — accepted callers either take `const Report&` (compare,
    // print_*) or move from a return value. Delete copy, keep move.
    Report() = default;
    ~Report() = default;
    Report(const Report&) =
        delete("copy of Report is almost always unintentional; use move semantics or pass by const&");
    Report&
    operator=(const Report&) = delete("copy-assignment of Report is almost always unintentional; use move or const&");
    Report(Report&&) noexcept = default;
    Report& operator=(Report&&) noexcept = default;

    // Bootstrap CI on a sample percentile. Returns CI{0,0} on insufficient
    // samples (n < 30) — callers that need to distinguish "no data" from
    // a genuine interval centred at zero should use `ci_opt()`, which
    // surfaces the "not enough samples" case as std::nullopt.
    //
    // Marked noexcept: bootstrap_ci allocates std::vectors, but OOM in
    // this project aborts() per the MemSafe axiom, so throwing here is
    // structurally impossible and std::terminate via the noexcept barrier
    // is the desired outcome if an allocator ever did throw.
    [[nodiscard]] CI ci(double frac, size_t B = 1000) const noexcept {
        return bootstrap_ci(::fixy::View<const double>{samples.view()}, frac, B);
    }

    // Same as ci() but distinguishes "insufficient samples" (nullopt)
    // from a computed interval. Implicitly wraps the CI-returning
    // bootstrap_ci — a CI with lo == hi == 0 still reports has_value()
    // == true; callers that need the n < 30 signal should gate on
    // samples.size() >= 30 before consulting the return.
    [[nodiscard]] std::optional<CI> ci_opt(double frac, size_t B = 1000) const {
        if (samples.size() < 30) return std::nullopt;
        return bootstrap_ci(::fixy::View<const double>{samples.view()}, frac, B);
    }

    [[nodiscard]] bool noisy(double cv_threshold = 0.05) const noexcept { return pct.cv > cv_threshold; }

    // Auto-scaled ns formatter for the main summary columns: renders a
    // fractional-ns double as "NN.NNns" / "NN.NNµs" / "NN.NNms" /
    // "NN.NNs" with 2 decimals, right-padded to 9 bytes for column
    // alignment. µs is 2-byte UTF-8 so alignment is slightly off on
    // µs/ms/s columns — 9 bytes fits up to "9999.99ns" and "999.99µs".
    static void fmt_ns_(char* buf, size_t len, double ns) noexcept {
        const char* suffix = "ns";
        double scaled = ns;
        if (ns >= 1e9) {
            suffix = "s";
            scaled = ns / 1e9;
        } else if (ns >= 1e6) {
            suffix = "ms";
            scaled = ns / 1e6;
        } else if (ns >= 1e3) {
            suffix = "µs";
            scaled = ns / 1e3;
        }

        char value[32];
        auto [ptr, ec] = std::to_chars(value, value + sizeof(value), scaled, std::chars_format::fixed, 2);
        if (ec != std::errc{}) {
            const char fallback[] = "nan";
            const size_t fallback_len = sizeof(fallback) - 1;
            const size_t suffix_len = std::strlen(suffix);
            const size_t max_out = (len == 0) ? 0 : len - 1;
            const size_t first = std::min(fallback_len, max_out);
            std::memcpy(buf, fallback, first);
            const size_t second = std::min(suffix_len, max_out > first ? max_out - first : 0);
            std::memcpy(buf + first, suffix, second);
            if (len != 0) buf[first + second] = '\0';
            return;
        }
        const size_t value_len = static_cast<size_t>(ptr - value);
        const size_t suffix_len = std::strlen(suffix);
        const size_t max_out = (len == 0) ? 0 : len - 1;
        const size_t first = std::min(value_len, max_out);
        std::memcpy(buf, value, first);
        const size_t second = std::min(suffix_len, max_out > first ? max_out - first : 0);
        std::memcpy(buf + first, suffix, second);
        if (len != 0) buf[first + second] = '\0';
    }

    // Writes the summary line to standard output.  The one reference to the
    // stream is here, so a call of a bench names no C stream.
    void print_text() const noexcept { print_text(stdout); }

    // Writes a single summary line (optionally followed by the BPF
    // sensory line) to `out`. Every underlying call is C stdio and
    // std::string::c_str() — no throwing paths, so noexcept.
    void print_text(FILE* out) const noexcept {
        char p50_b[32], p90_b[32], p99_b[32], p999_b[32], max_b[32], mean_b[32], sigma_b[32];
        fmt_ns_(p50_b, sizeof(p50_b), pct.p50);
        fmt_ns_(p90_b, sizeof(p90_b), pct.p90);
        fmt_ns_(p99_b, sizeof(p99_b), pct.p99);
        fmt_ns_(p999_b, sizeof(p999_b), pct.p99_9);
        fmt_ns_(max_b, sizeof(max_b), pct.max);
        fmt_ns_(mean_b, sizeof(mean_b), pct.mean);
        fmt_ns_(sigma_b, sizeof(sigma_b), pct.stddev);
        std::fprintf(out,
                     "  %-38s  p50=%9s  p90=%9s  p99=%9s  p99.9=%9s  "
                     "max=%10s  μ=%9s  σ=%9s  cv=%4.1f%%",
                     name.c_str(), p50_b, p90_b, p99_b, p999_b, max_b, mean_b, sigma_b, pct.cv * 100.0);
        if (windows.windows > 0) print_windows_();

        if (cycles_per_op > 0.0) {
            std::fprintf(out, "  cyc=%7.1f", cycles_per_op);
        }
        if (freq_start_hz > 0) {
            std::fprintf(out, "  @%.2fGHz", static_cast<double>(freq_start_hz) / 1e9);
        }
        std::fprintf(out, "  n=%zu", pct.n);
        if (pinned_cpu_count > 1) {
            std::fprintf(out, "  cpus=%zu", pinned_cpu_count);
        } else if (pinned_cpu.is_valid()) {
            std::fprintf(out, "  cpu%d", pinned_cpu.raw());
        }
        if (batch > 1) std::fprintf(out, "  [batch-avg]");
        if (noisy()) std::fprintf(out, "  [noisy]");
        if (drift_flag) std::fprintf(out, "  [drift]");
        if (freq_drift_flag) std::fprintf(out, "  [freq-drift]");
        std::fprintf(out, "\n");
        if (pause.is_over_bound()) print_pause_();

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        // One-line sensory summary. When BPF is attached and every
        // interesting counter is zero we still emit "└─ sched clean"
        // as confirmation that senses ARE attached (so the reader can
        // trust the silence). When any counter fires, only the fields
        // that actually have a delta are printed.
        print_bpf_summary_(out);
#endif
    }

    // Minimal RFC-8259 string escape — covers every char that would
    // make a JSON parser unhappy inside a "..."-quoted value. Names in
    // benches can contain `"` (e.g. arena.copy_string(\"relu\")), and
    // are user-controlled by the Run constructor. Escape these before
    // emission to produce valid JSON unconditionally.
    static void fprint_json_string(FILE* out, std::string_view s) noexcept {
        std::fputc('"', out);
        for (char ch : s) {
            const auto c = static_cast<unsigned char>(ch);
            switch (c) {
                case '"':
                    std::fputs("\\\"", out);
                    break;
                case '\\':
                    std::fputs("\\\\", out);
                    break;
                case '\b':
                    std::fputs("\\b", out);
                    break;
                case '\f':
                    std::fputs("\\f", out);
                    break;
                case '\n':
                    std::fputs("\\n", out);
                    break;
                case '\r':
                    std::fputs("\\r", out);
                    break;
                case '\t':
                    std::fputs("\\t", out);
                    break;
                default:
                    if (c < 0x20) {
                        std::fprintf(out, "\\u%04x", static_cast<unsigned>(c));
                    } else {
                        std::fputc(static_cast<char>(c), out);
                    }
            }
        }
        std::fputc('"', out);
    }

    // Writes the JSON object to standard output.  The one reference to the
    // stream is here, so a call of emit_reports_json names no C stream.
    void print_json() const noexcept { print_json(stdout); }

    // JSON emitter — same argument as print_text(): C stdio + noexcept
    // accessors only, no throwing paths.
    void print_json(FILE* out) const noexcept {
        std::fputc('{', out);
        std::fputs("\"name\":", out);
        fprint_json_string(out, name);
        std::fprintf(out,
                     ",\"batch\":%zu,\"n\":%zu,\"cpu\":%d,\"cpu_count\":%zu,"
                     "\"p50\":%.3f,\"p75\":%.3f,\"p90\":%.3f,\"p95\":%.3f,"
                     "\"p99\":%.3f,\"p99_9\":%.3f,\"p99_99\":%.3f,"
                     "\"min\":%.3f,\"max\":%.3f,\"mean\":%.3f,\"stddev\":%.3f,"
                     "\"cv\":%.5f,\"wall_ns\":%.0f,\"drift_pct\":%.3f,"
                     "\"cycles_per_op\":%.3f,"
                     "\"freq_start_hz\":%lu,\"freq_end_hz\":%lu,"
                     "\"pause_ns\":%lu,\"thread_delay_ns\":%lu,\"thread_arrivals\":%lu,\"paused\":%d,"
                     "\"windows\":%zu,\"window_p50_median\":%.3f,\"window_spread\":%.5f,\"windows_judged\":%d,"
                     "\"paused_windows\":%zu",
                     batch, pct.n, pinned_cpu.raw(), pinned_cpu_count, pct.p50, pct.p75, pct.p90, pct.p95, pct.p99,
                     pct.p99_9, pct.p99_99, pct.min, pct.max, pct.mean, pct.stddev, pct.cv, wall_ns, drift_pct,
                     cycles_per_op, static_cast<unsigned long>(freq_start_hz), static_cast<unsigned long>(freq_end_hz),
                     pause.own_ns(), pause.other.run_delay_ns, pause.other.arrivals, pause.is_over_bound() ? 1 : 0,
                     windows.windows, windows.median, windows.spread, window_basis == PauseBasis::LostTime ? 1 : 0,
                     paused_windows);
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        print_bpf_json_(out);
#endif
        std::fputc('}', out);
    }

private:
    // Prints the windows of the run inside its line, to standard output, which
    // each caller of print_text gives (THE WINDOWS): their count, the median of
    // their p50 with two decimals in the unit of the other columns, their
    // spread in percent with one decimal, and the count of the windows over the
    // bound of the pause check.  The text before it goes through stdio, so a
    // flush comes first.
    void print_windows_() const noexcept {
        using ::fixy::Sink;
        double scaled = windows.median;
        ::fixy::TextView unit{"ns"};
        if (scaled >= 1e9) {
            scaled /= 1e9;
            unit = ::fixy::TextView{"s"};
        } else if (scaled >= 1e6) {
            scaled /= 1e6;
            unit = ::fixy::TextView{"ms"};
        } else if (scaled >= 1e3) {
            scaled /= 1e3;
            unit = ::fixy::TextView{"µs"};
        }
        auto const hundredths = static_cast<std::uint64_t>(scaled * 100.0 + 0.5);
        auto const tenths = static_cast<std::uint64_t>(windows.spread * 1000.0 + 0.5);
        detail::flush_standard_output();
        ::fixy::report(Sink::Out, "  win={} w50={}.{}{}{} spread={}.{}%", windows.windows, hundredths / 100,
                       hundredths % 100 / 10, hundredths % 10, unit, tenths / 10, tenths % 10);
        if (paused_windows > 0) ::fixy::report(Sink::Out, "  [paused windows {}]", paused_windows);
    }

    // Prints the pause of a run over kPauseBoundNs on a line of its own after
    // the row, to standard output, which each caller of print_text gives (THE
    // PAUSE CHECK).  The times are in microseconds.
    void print_pause_() const noexcept {
        using ::fixy::Sink;
        detail::flush_standard_output();
        ::fixy::report(Sink::Out, "     └─ [pause] in {} ms of the sampled loop:", pause.wall_ns / 1000000);
        if (pause.basis == PauseBasis::LostTime) {
            ::fixy::report(Sink::Out,
                           " the measuring thread did not run for {} us (steal, interrupts or other threads).",
                           pause.lost_ns / 1000);
        } else {
            ::fixy::report(Sink::Out,
                           " the measuring thread slept, and it waited {} us on its run queue in {} arrivals.  The "
                           "steal of its core is not measured.",
                           pause.own.run_delay_ns / 1000, pause.own.arrivals);
        }
        if (pause.other.run_delay_ns > 0) {
            ::fixy::report(Sink::Out, "  Thread {} waited {} us on its run queue in {} arrivals.",
                           static_cast<std::uint32_t>(pause.other.tid), pause.other.run_delay_ns / 1000,
                           pause.other.arrivals);
        }
        ::fixy::report(Sink::Out,
                       "  The bounds are {} us in all and {} us for each arrival, so the row is NOT VALID.\n",
                       kPauseBoundNs / 1000, kWaitBoundNs / 1000);
    }

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    // Print every monotonic counter that actually incremented during the
    // run, in unit-scaled form. The gauge-valued Idx slots listed in
    // bpf_senses.h's Snapshot::operator- comment (FD_CURRENT, TCP_
    // {MIN,MAX}_SRTT_US, TCP_LAST_CWND, THERMAL_MAX_TRIP, SIGNAL_LAST_
    // SIGNO, OOM_KILL_US, RECLAIM_STALL_LOOPS, NET_TCP_*, NET_UDP_ACTIVE,
    // NET_UNIX_ACTIVE, RSS_{ANON,FILE,SHMEM}_BYTES, RSS_SWAP_ENTRIES)
    // are deliberately excluded from kAll — their (post - pre) delta
    // saturates to zero in Snapshot::operator-. Callers wanting raw
    // gauge values should pull them from the post snapshot directly.
    //
    // Output:
    //   └─ clean                                   (no monotonic counter moved)
    //   └─ 6 preempt · 243 pgfault · 4.3ms softirq · 48 mmap
    //
    // "N label" reads as English. Times auto-scale ns → µs → ms → s,
    // bytes auto-scale to KB/MB/GB, counts to k/M/G above 10k.

    enum class Unit : uint8_t {
        Count,
        Ns,
        Bytes
    };

    struct Field {
        const char* label;
        ::crucible::perf::Idx idx;
        Unit unit;
    };

    // Every counter whose (post - pre) is meaningful. Order = the story
    // the reader walks: first what interrupted us, then what the bench
    // touched (memory → sync → I/O → thread mgmt), finally reliability.
    static constexpr Field kAll[] = {
        // Scheduling interference.
        {"preempt", ::crucible::perf::SCHED_CTX_INVOL, Unit::Count},
        {"yield", ::crucible::perf::SCHED_CTX_VOL, Unit::Count},
        {"migrate", ::crucible::perf::SCHED_MIGRATIONS, Unit::Count},
        {"runtime", ::crucible::perf::SCHED_RUNTIME_NS, Unit::Ns},
        {"wait", ::crucible::perf::SCHED_WAIT_NS, Unit::Ns},
        {"sleep", ::crucible::perf::SCHED_SLEEP_NS, Unit::Ns},
        {"iowait", ::crucible::perf::SCHED_IOWAIT_NS, Unit::Ns},
        {"blocked", ::crucible::perf::SCHED_BLOCKED_NS, Unit::Ns},
        {"softirq", ::crucible::perf::SOFTIRQ_STOLEN_NS, Unit::Ns},
        {"wake_rx", ::crucible::perf::WAKEUPS_RECEIVED, Unit::Count},
        {"wake_tx", ::crucible::perf::WAKEUPS_SENT, Unit::Count},
        {"freq_chg", ::crucible::perf::CPU_FREQ_CHANGES, Unit::Count},
        {"tid_new", ::crucible::perf::THREADS_CREATED, Unit::Count},
        {"tid_end", ::crucible::perf::THREADS_EXITED, Unit::Count},

        // Memory.
        {"pgfault", ::crucible::perf::MEM_PAGE_FAULTS_MIN, Unit::Count},
        {"majfault", ::crucible::perf::MEM_PAGE_FAULTS_MAJ, Unit::Count},
        {"mmap", ::crucible::perf::MEM_MMAP_COUNT, Unit::Count},
        {"munmap", ::crucible::perf::MEM_MUNMAP_COUNT, Unit::Count},
        {"brk", ::crucible::perf::MEM_BRK_CALLS, Unit::Count},
        {"reclaim_n", ::crucible::perf::DIRECT_RECLAIM_COUNT, Unit::Count},
        {"reclaim_t", ::crucible::perf::DIRECT_RECLAIM_NS, Unit::Ns},
        {"swap_out", ::crucible::perf::SWAP_OUT_PAGES, Unit::Count},
        {"thp_ok", ::crucible::perf::THP_COLLAPSE_OK, Unit::Count},
        {"thp_fail", ::crucible::perf::THP_COLLAPSE_FAIL, Unit::Count},
        {"numa", ::crucible::perf::NUMA_MIGRATE_PAGES, Unit::Count},
        {"compact", ::crucible::perf::COMPACTION_STALLS, Unit::Count},
        {"extfrag", ::crucible::perf::EXTFRAG_EVENTS, Unit::Count},

        // Sync contention.
        {"futex", ::crucible::perf::FUTEX_WAIT_COUNT, Unit::Count},
        {"futex_t", ::crucible::perf::FUTEX_WAIT_NS, Unit::Ns},
        {"klock", ::crucible::perf::KERNEL_LOCK_COUNT, Unit::Count},
        {"klock_t", ::crucible::perf::KERNEL_LOCK_NS, Unit::Ns},

        // I/O (syscall-level).
        {"read", ::crucible::perf::IO_READ_BYTES, Unit::Bytes},
        {"write", ::crucible::perf::IO_WRITE_BYTES, Unit::Bytes},
        {"r_ops", ::crucible::perf::IO_READ_OPS, Unit::Count},
        {"w_ops", ::crucible::perf::IO_WRITE_OPS, Unit::Count},
        {"fd_open", ::crucible::perf::FD_OPEN_OPS, Unit::Count},

        // Block I/O (device-level).
        {"disk_r", ::crucible::perf::DISK_READ_BYTES, Unit::Bytes},
        {"disk_w", ::crucible::perf::DISK_WRITE_BYTES, Unit::Bytes},
        {"disk_t", ::crucible::perf::DISK_IO_LATENCY_NS, Unit::Ns},
        {"disk_n", ::crucible::perf::DISK_IO_COUNT, Unit::Count},
        {"pg_miss", ::crucible::perf::PAGE_CACHE_MISSES, Unit::Count},
        {"readahead", ::crucible::perf::READAHEAD_PAGES, Unit::Count},
        {"unplug", ::crucible::perf::IO_UNPLUG_COUNT, Unit::Count},
        {"throttle", ::crucible::perf::WRITE_THROTTLE_JIFFIES, Unit::Count},

        // Network.
        {"tx", ::crucible::perf::NET_TX_BYTES, Unit::Bytes},
        {"rx", ::crucible::perf::NET_RX_BYTES, Unit::Bytes},
        {"retrans", ::crucible::perf::TCP_RETRANSMIT_COUNT, Unit::Count},
        {"rst", ::crucible::perf::TCP_RST_SENT, Unit::Count},
        {"sk_err", ::crucible::perf::TCP_ERROR_COUNT, Unit::Count},
        {"skb_drop", ::crucible::perf::SKB_DROP_COUNT, Unit::Count},
        {"cng_loss", ::crucible::perf::TCP_CONG_LOSS, Unit::Count},

        // Reliability (delta-meaningful signals only).
        {"sig_fatal", ::crucible::perf::SIGNAL_FATAL_COUNT, Unit::Count},
        {"oom_kills", ::crucible::perf::OOM_KILLS_SYSTEM, Unit::Count},
        {"mce", ::crucible::perf::MCE_COUNT, Unit::Count},
    };

    static void print_scaled_(FILE* out, uint64_t v, Unit u) noexcept {
        switch (u) {
            case Unit::Count:
                if (v >= 1000000000)
                    std::fprintf(out, "%.1fG", static_cast<double>(v) / 1e9);
                else if (v >= 1000000)
                    std::fprintf(out, "%.1fM", static_cast<double>(v) / 1e6);
                else if (v >= 10000)
                    std::fprintf(out, "%.1fk", static_cast<double>(v) / 1e3);
                else
                    std::fprintf(out, "%lu", v);
                break;
            case Unit::Ns:
                if (v >= 1000000000)
                    std::fprintf(out, "%.1fs", static_cast<double>(v) / 1e9);
                else if (v >= 1000000)
                    std::fprintf(out, "%.1fms", static_cast<double>(v) / 1e6);
                else if (v >= 1000)
                    std::fprintf(out, "%.1fµs", static_cast<double>(v) / 1e3);
                else
                    std::fprintf(out, "%luns", v);
                break;
            case Unit::Bytes:
                if (v >= (1ULL << 30))
                    std::fprintf(out, "%.1fGB", static_cast<double>(v) / static_cast<double>(1ULL << 30));
                else if (v >= (1ULL << 20))
                    std::fprintf(out, "%.1fMB", static_cast<double>(v) / static_cast<double>(1ULL << 20));
                else if (v >= (1ULL << 10))
                    std::fprintf(out, "%.1fKB", static_cast<double>(v) / 1024.0);
                else
                    std::fprintf(out, "%luB", v);
                break;
            default:
                std::fprintf(out, "%lu", v);
                break;
        }
    }

    void print_bpf_summary_(FILE* out) const noexcept {
        if (bpf_attached == 0) return;
        const auto& d = bpf_delta;

        std::fprintf(out, "     └─ ");
        bool first = true;
        for (const Field& f : kAll) {
            const uint64_t v = d[f.idx];
            if (v == 0) continue;
            if (!first) std::fprintf(out, " · ");
            first = false;
            std::fprintf(out, "%s=", f.label);
            print_scaled_(out, v, f.unit);
        }
        // SchedSwitch off-CPU top-K — appended inline (same line) as
        // the SenseHub deltas.  Dense
        // answer to "WHICH preempts cost the most during this bench
        // window".  SenseHub already shows aggregate `preempt`/`yield`
        // counts and total `wait`/`sleep` ns; the top-K shows the
        // longest individual off-CPU spikes + which CPUs we ended up
        // on.  Format:
        //   offcpu=19µs@1/17µs@1/15µs@5            (3 events, all shown)
        //   offcpu=19µs@1/17µs@1/15µs@5 (×8)       (top 3 of 8 total)
        // `/` separates events; `@N` is the CPU we landed on; `(×N)`
        // appears only when the timeline saw more events than K, so
        // the reader knows the top-K is a truncation rather than the
        // exhaustive list.
        if (sched_offcpu_top_n > 0) {
            if (!first) std::fprintf(out, " · ");
            first = false;
            std::fprintf(out, "offcpu=");
            for (uint32_t i = 0; i < sched_offcpu_top_n; ++i) {
                if (i > 0) std::fputc('/', out);
                print_scaled_(out, sched_offcpu_top_ns[i], Unit::Ns);
                std::fprintf(out, "@%u", sched_offcpu_top_cpu[i]);
            }
            if (sched_offcpu_count > sched_offcpu_top_n) {
                std::fprintf(out, " (×%u)", sched_offcpu_count);
            }
        }
        if (first) std::fprintf(out, "clean");
        std::fputc('\n', out);
    }

    // JSON: emit every counter in the kAll table as a stable schema
    // (all present whether zero or not) so downstream consumers can
    // rely on field presence.
    void print_bpf_json_(FILE* out) const noexcept {
        if (bpf_attached == 0) return;
        const auto& d = bpf_delta;
        std::fprintf(out, ",\"bpf\":{\"attached\":%zu", bpf_attached);
        for (const Field& f : kAll) {
            std::fprintf(out, ",\"%s\":%lu", f.label, d[f.idx]);
        }
        std::fputc('}', out);
    }
#endif
};

// ── Mann-Whitney U (Mann & Whitney 1947) for A/B ──────────────────

struct Compare {
    std::string a_name, b_name;
    double delta_p50_pct = 0;
    double delta_p99_pct = 0;
    double delta_mean_pct = 0;
    double u = 0;
    double z = 0;
    bool distinguishable = false;  // |z| > 2.576 → p < 0.01

    // The p99 change, in percent, past which a distinguishable pair counts
    // as a regression or an improvement.
    static constexpr double kFlagDeltaP99Pct = 5.0;

    [[nodiscard]] bool is_regression() const noexcept { return distinguishable && delta_p99_pct > kFlagDeltaP99Pct; }
    [[nodiscard]] bool is_improvement() const noexcept { return distinguishable && delta_p99_pct < -kFlagDeltaP99Pct; }

    // Writes the summary line to standard output.  The one reference to the
    // stream is here, so a call of a bench names no C stream.
    void print_text() const noexcept { print_text(stdout); }

    // One-line summary — C stdio + std::string::c_str() only, noexcept
    // for the same reasons as Report::print_text.
    // A pair that the test tells apart but whose p99 moves less than the
    // threshold says so, and does not claim to be indistinguishable.
    void print_text(FILE* out) const noexcept {
        std::fprintf(out, "  Δ %s → %s:  Δp50=%+6.2f%%  Δp99=%+6.2f%%  Δμ=%+6.2f%%  z=%+5.2f", a_name.c_str(),
                     b_name.c_str(), delta_p50_pct, delta_p99_pct, delta_mean_pct, z);
        if (is_regression()) {
            std::fputs("  [REGRESS]\n", out);
        } else if (is_improvement()) {
            std::fputs("  [IMPROVE]\n", out);
        } else if (distinguishable) {
            std::fprintf(out, "  [distinguishable, |Δp99| <= %.0f%%]\n", kFlagDeltaP99Pct);
        } else {
            std::fputs("  [indistinguishable]\n", out);
        }
    }
};

[[nodiscard]] inline Compare compare(const Report& a, const Report& b) {
    Compare c{.a_name = a.name, .b_name = b.name};

    const auto pct = [](double x, double y) { return (x > 0) ? 100.0 * (y - x) / x : 0.0; };
    c.delta_p50_pct = pct(a.pct.p50, b.pct.p50);
    c.delta_p99_pct = pct(a.pct.p99, b.pct.p99);
    c.delta_mean_pct = pct(a.pct.mean, b.pct.mean);

    const size_t n1 = a.samples.size();
    const size_t n2 = b.samples.size();
    if (n1 < 30 || n2 < 30) {
        c.distinguishable = false;
        return c;
    }

    // The union of the samples of A and B, each with its source, in the order
    // of the samples: sort_stably by the key v, a radix sort.  A sample is a
    // time of at least zero with no NaN, so the order of the key is the order
    // of <.  The ranks below read each tie group whole, so the order inside a
    // group does not change them.
    struct Tagged {
        double v = 0.0;
        uint8_t src = 0;
    };
    const ::fixy::View<const double> a_samples{a.samples.view()};
    const ::fixy::View<const double> b_samples{b.samples.view()};
    auto const alloc = ::foundation::effects::testing::test().alloc;
    ::fixy::HeapArray<Tagged> tagged = ::fixy::mint_heap_array_with<Tagged>(
        alloc, n1 + n2, [&a_samples, &b_samples, n1](std::size_t position) noexcept {
            if (position < n1) return Tagged{.v = *a_samples.get(position).expect("a position of A"), .src = 0};
            return Tagged{.v = *b_samples.get(position - n1).expect("a position of B"), .src = 1};
        });
    ::fixy::HeapArray<Tagged> scratch = ::fixy::mint_heap_array(alloc, n1 + n2, Tagged{});
    bool is_sorted = false;
    tagged = ::fixy::with_mut(std::move(tagged), [&scratch, &is_sorted](auto elements) noexcept {
        scratch = ::fixy::with_mut(std::move(scratch), [&elements, &is_sorted](auto room) noexcept {
            is_sorted = ::foundation::core::sort_stably(std::move(elements), std::move(room),
                                                        ::foundation::core::order_by_key(&Tagged::v))
                            .is_ok();
        });
    });
    if (!is_sorted) ::fixy::fatal("the scratch of the sort of the union of two runs has the count of the union");
    const ::fixy::View<const Tagged> all{tagged.view()};
    const auto value_at = [&all](std::size_t position) noexcept {
        return all.get(position).expect("a position of the union is below its count")->v;
    };
    const auto source_at = [&all](std::size_t position) noexcept {
        return all.get(position).expect("a position of the union is below its count")->src;
    };

    // Rank the union of A and B. In a tie group spanning positions
    // [i..j] (inclusive), every element gets the average of the ranks
    // i+1 through j+1. Simultaneously accumulate the tie-correction
    // sum T = Σ(tᵢ³ − tᵢ), where tᵢ is each tie-group's size; used
    // below to adjust sigma for ties.
    // The accumulators are `double` (IEEE 754 binary64), not
    // `long double` — `long double` is 80-bit on x86 but 64-bit on
    // AArch64; same rank-sum input would produce bit-different sigma
    // across the fleet.  `double` precision is sufficient: for the
    // worst-case n=10⁴ the maximum tie_sum ≤ N³ ≈ 8·10¹² fits in
    // double (15-16 decimal digits); maximum r1 ≤ n·N ≤ 2·10⁸ same.
    double r1 = 0.0;
    double tie_sum = 0.0;
    size_t i = 0;
    while (i < all.size()) {
        size_t j = i;
        while (j + 1 < all.size() && !(value_at(j + 1) < value_at(i)) && !(value_at(i) < value_at(j + 1))) {
            ++j;
        }
        const double avg_rank = (static_cast<double>(i + j) + 2.0) / 2.0;
        const double t = static_cast<double>(j - i + 1);
        tie_sum += t * t * t - t;  // 0 for tie groups of size 1
        for (size_t k = i; k <= j; ++k) {
            if (source_at(k) == 0) r1 += avg_rank;
        }
        i = j + 1;
    }

    const double dn1 = static_cast<double>(n1);
    const double dn2 = static_cast<double>(n2);
    const double u1 = r1 - dn1 * (dn1 + 1.0) / 2.0;
    const double u2 = dn1 * dn2 - u1;
    const double u_min = std::min(u1, u2);
    const double mu = dn1 * dn2 / 2.0;

    // Tie-corrected variance (Mann & Whitney 1947 §5; Siegel 1956):
    //   sigma² = (n1·n2 / 12) · (N + 1 − T / (N·(N − 1)))
    // For T = 0 (no ties) this reduces to the classical form.
    const double N = static_cast<double>(n1) + static_cast<double>(n2);
    const double n1n2_12 = (static_cast<double>(n1) * dn2) / 12.0;
    const double Ncorr = (N > 1.0) ? (N + 1.0 - tie_sum / (N * (N - 1.0))) : (N + 1.0);
    const double sigma = std::sqrt(std::max(0.0, n1n2_12 * Ncorr));

    // Continuity correction: subtract 0.5 from |u_min − mu| before
    // dividing by sigma (normal approximation of a discrete statistic).
    // Sign of z preserves direction (is A tending below or above B?).
    const double diff = u_min - mu;
    const double adj = (diff >= 0.0 ? +1.0 : -1.0) * std::max(0.0, std::abs(diff) - 0.5);

    c.u = u_min;
    c.z = (sigma > 0) ? (adj / sigma) : 0.0;
    c.distinguishable = std::abs(c.z) > 2.576;
    return c;
}

// ── Run: fluent builder + measurement loop ─────────────────────────

class Run {
public:
    explicit Run(std::string name) : name_{std::move(name)} {}

    // A run whose name is a text of the base that a bench formats, such as
    // the name of a row with its count of threads.  Complexity: one copy of
    // the text.
    template <std::size_t Capacity>
    explicit Run(::fixy::FixedText<Capacity> const& name) {
        for (char const character : name)
            name_.push_back(character);
    }

    // Pinning policy:
    //   default:       pin to first isolcpu (else sched_getcpu())
    //   .core(N) N≥0:  pin to CPU N
    //   .core(-1):     treat as .default; still applies affinity to
    //                  isolcpu/sched_getcpu() (previously silently
    //                  returned without pinning, so env CRUCIBLE_BENCH_CORE
    //                  unset → no affinity ever applied)
    //   .no_pin():     do not touch affinity
    //   .fan_out():    start on a CPU that isolcpus did not isolate, and
    //                  keep the affinity of the thread, for a body that
    //                  starts threads
    //
    // Each pin ends with the measurement: the thread gets back the affinity
    // that it had before the Run.
    //
    // [[nodiscard]] on every setter — the fluent builder is worthless if
    // the returned Run& is thrown away before .measure().
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& samples(size_t n) noexcept {
        samples_ = n;
        return *this;
    }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& warmup(size_t n) noexcept {
        warmup_ = n;
        return *this;
    }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& batch(size_t n) noexcept {
        batch_ = n;
        return *this;
    }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& core(int c) noexcept {
        core_ = CpuId{c};
        pin_mode_ = Pin::Explicit;
        return *this;
    }
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& no_pin() noexcept {
        pin_mode_ = Pin::None;
        return *this;
    }

    // For a body that starts threads.  A new thread takes the affinity set
    // of the thread that starts it.  So under a pin to one CPU, every
    // thread of the body runs on that CPU, and a parallel body measures no
    // parallel work.  A thread that starts on an isolated CPU gives the
    // same result: the load balancer does not reach an isolated CPU, so
    // the thread stays there.  This mode moves the measuring thread to one
    // allowed CPU that the kernel command line did not isolate, and then
    // gives the thread back its affinity.  The threads of the body start
    // on that CPU, and the scheduler spreads them over the CPUs that are
    // not isolated.  The measuring thread can move too.  The two readings
    // of a sample still agree on a host with an invariant TSC, and a body
    // that starts threads runs far longer than a move costs.  On a claim of
    // loop bench, whose cores are all isolated, the measuring thread takes
    // the core of slot 0, and each thread of the body pins itself to a core
    // of the slots from 1 (THE CORES OF A RUN).
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& fan_out() noexcept {
        pin_mode_ = Pin::FanOut;
        return *this;
    }

    // Apply a `crucible::warden::Policy` to the measuring thread for the
    // duration of the Run. When set, the policy's `hot_core` selector
    // drives pinning and the direct pin_() path is skipped entirely —
    // so `.hardening(p)` unconditionally wins over any prior or
    // subsequent `.core(N)` / `.no_pin()` in the same builder chain.
    // The RAII guard returned by crucible::warden::mint_hardening()
    // reverts sched class, affinity, mlock'd regions, and THP flag when
    // measure() returns.
    //
    // Default: no hardening (have_hardening_ == false -> direct pin_()).
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& hardening(const crucible::warden::Policy& p) noexcept {
        hardening_ = p;
        have_hardening_ = true;
        return *this;
    }

    // Cap the Run's total wall time. `ms=0` disables the cap (fall
    // back to the raw samples_ × batch iteration count). Default
    // budget is 10 seconds per Run — enough for stable p99.9 on any
    // sensible body, short enough that heavy bodies (~1 ms) don't
    // iterate so many times that glibc's heap pool stops returning
    // pages to the OS (observed: bench_graph at N=4096 built 1 MB
    // per body, 100k samples → 20 GB RSS before the cap).
    //
    // Behavior when the cap hits:
    //   - Warmup stops as soon as wall exceeds max_wall_ms_/4.
    //   - The sample loop stops at the first sample boundary past
    //     max_wall_ms_. Collected samples are kept (ns_samples.size()
    //     < samples_), drift calculation and Percentiles::compute
    //     work on whatever landed.
    //
    // Override with env CRUCIBLE_BENCH_WALL_MS=<ms>. Explicit
    // `.max_wall_ms(0)` disables capping even with env set.
    [[nodiscard("builder chain result is discarded — did you forget .measure(...)?")]]
    Run& max_wall_ms(size_t ms) noexcept {
        max_wall_ms_ = ms;
        have_max_wall_ = true;
        return *this;
    }

    // The affinity that a pin gave the calling thread, and the affinity that
    // the pin replaced.  cpu is the CPU of a pin to one CPU, and none for a
    // fan-out or a failed pin.  cpu_count is the number of CPUs that the
    // thread and its workers could use, and 0 when no affinity was set.
    // prior holds the replaced affinity while a pin to one CPU holds.
    // restore() gives it back to the thread, and changes nothing when
    // prior is empty or on a thread that did not pin.
    struct PinResult {
        CpuId cpu{};
        size_t cpu_count = 0;
        ::fixy::Option<::fixy::sched::PriorAffinity> prior{};

        [[nodiscard]] static PinResult one(CpuId pinned) noexcept {
            return PinResult{.cpu = pinned, .cpu_count = pinned.is_valid() ? size_t{1} : size_t{0}};
        }

        // A restore takes the mask from prior, so a second restore changes
        // nothing.
        [[nodiscard]] ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> restore() noexcept {
            return std::move(prior).match(
                [](::fixy::sched::PriorAffinity held) noexcept { return std::move(held).restore(); },
                []() noexcept -> ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> { return ::fixy::Unit{}; });
        }
    };

    // Pins the calling thread as measure() would, with no measurement.  The
    // pin holds until the caller restores it.  A bench that builds its data
    // while the pin holds touches the pages first from that CPU, so the
    // kernel puts them on the NUMA node of the CPU that the default Run
    // measures on.
    [[nodiscard]] PinResult pin_calling_thread() const noexcept { return pin_(); }

    // The policy, the pin and the report of a measurement are cold work
    // outside the timed loops.  Three members that are not templates do that
    // work: open_ before the loops, start_window_ before the sampled loop and
    // close_ after it.  Each instantiation of measure holds only its two
    // loops, so a translation unit compiles the cold work one time and not one
    // time for each body.
    //
    // A body with a member before_sample() gets a call of it before each
    // sample, outside the timed region.  A body that allocates puts its
    // allocator back to the same state there.  Each sample then times the same
    // path, and no sample meets a new block or the first touch of a page.  A
    // body with no such member gives the instructions of a measure with no
    // hook, because the discarded branch makes no code.
    template <typename Body>
    [[nodiscard]] Report measure(Body&& body) const {
        MeasureOpening opening = open_();
        const CpuId pinned_cpu = opening.pin.cpu;

        const double nspc = Timer::ns_per_cycle();
        const uint64_t ovh = Timer::overhead_cycles();
        const size_t S = samples_ ? samples_ : env_samples_();

        // Mint the RdtscPinned witness immediately after
        // sched_setaffinity (or warden hardening apply) — the proof
        // travels by const-ref through the measurement loop and
        // auto_batch_; calls to rdtsc_start/end go through the
        // pinned-overload pair, gated on the witness.  CpuId::none()
        // (pin failure) still mints a witness — the type-system
        // invariant is "we tried to pin"; runtime observation of
        // pin_witness.core().is_valid() distinguishes hard-failed
        // from successful pins on the report.
        const RdtscPinned pin_witness{pinned_cpu, RdtscPinned::mint_tag{}};

        const size_t batch = (batch_ == 0) ? auto_batch_(body, pin_witness) : batch_;

        // Wall-time cap: explicit `.max_wall_ms()` wins; else env var
        // CRUCIBLE_BENCH_WALL_MS; else default `kDefaultMaxWallMs`.
        // Zero disables capping entirely.
        const size_t wall_cap_ms = have_max_wall_ ? max_wall_ms_ : env_wall_ms_().value_or(kDefaultMaxWallMs);
        const auto start_all = std::chrono::steady_clock::now();
        const auto wall_budget = std::chrono::milliseconds(wall_cap_ms);
        const auto warmup_budget = wall_budget / 4;

        for (size_t i = 0; i < warmup_; ++i) {
            body();
            // Budget check every 64 iterations — steady_clock::now() is
            // ~20 ns, amortizing it keeps overhead under 1% for ≥2 µs
            // bodies and invisible for anything heavier.
            if (wall_cap_ms > 0 && (i & 63) == 63) {
                const auto elapsed = std::chrono::steady_clock::now() - start_all;
                // Compare the durations, not their counts. steady_clock's
                // duration is nanoseconds and warmup_budget is milliseconds,
                // so comparing .count() to .count() put 2,500 ns against a
                // budget meant to read 2,500 ms — a factor of a million. The
                // warmup then ended at the first 64-iteration check past
                // 2.5 us, which is 64 or 128 iterations for any body worth
                // measuring, never the 10,000 the default asks for. Letting
                // chrono convert is the whole reason it carries units.
                if (elapsed > warmup_budget) {
                    break;
                }
            }
        }

        // Pre-sized to S; if the wall-cap cutoff fires early, we trim to
        // `filled` below so downstream stats (sort, percentile) only see
        // real samples — never default-zero trailing slots.
        ::fixy::HeapArray<double> ns_samples =
            ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, S, 0.0);
        size_t filled = 0;

        SampleWindow window = start_window_(pinned_cpu);
        const auto wall0 = std::chrono::steady_clock::now();
        // The windows share the samples and the time that the cap leaves
        // after the warmup (THE WINDOWS).
        const WindowPlan plan = plan_windows_(WindowRequest{
            .samples = S,
            .cap = wall_cap_ms > 0 ? WallCap::On : WallCap::Off,
            .remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(wall_budget - (wall0 - start_all)),
        });

        for (WindowBound const& bound : plan.bounds) {
            for (size_t taken = 0; taken < bound.samples; ++taken) {
                if constexpr (requires { body.before_sample(); }) {
                    body.before_sample();
                }
                // The pinned-overload form — the witness's existence
                // proves a sched_setaffinity (or hardening apply) ran above
                // and the thread has not migrated since.
                const uint64_t t0 = rdtsc_start(pin_witness);
                for (size_t j = 0; j < batch; ++j)
                    body();
                const uint64_t t1 = rdtsc_end(pin_witness);
                // RDTSCP+LFENCE guarantees t1 >= t0 (each RDTSC pair is
                // monotone on a single core); modular unsigned subtraction
                // is therefore safe. The older form `(t1 > t0 + ovh)`
                // wraps to false when t0 + ovh overflows UINT64_MAX, which
                // can happen after a long runtime on a 4+ GHz core.
                const uint64_t raw = t1 - t0;
                const uint64_t d = (raw > ovh) ? (raw - ovh) : 0;
                // After the reading of the end, so the check of the position
                // is outside the timed region.  The windows share S samples.
                *ns_samples.get(filled).expect("the windows take at most S samples") =
                    (static_cast<double>(d) * nspc) / static_cast<double>(batch);
                ++filled;
                // The reading of the end of the sample is the clock of the
                // part of the time of the window, so the check costs no read.
                if (taken + 1 >= kMinWindowSamples && t1 > bound.deadline_tsc) break;
            }
            static_cast<void>(window.ends.append(filled));
            close_window_(window);
        }
        return close_(std::move(opening), window, ns_samples, filled, batch, wall0);
    }

private:
    // What open_ holds for the whole measurement: the policy guard, which
    // reverts the policy when the measurement ends, and the pin.
    struct MeasureOpening {
        crucible::warden::AppliedPolicy hardening_guard;
        PinResult pin{};
    };

    // Whether the wall cap bounds the time of the sampled loop.
    enum class WallCap : std::uint8_t {
        Off,
        On,
    };

    // What plan_windows_ splits: the samples of the run, and the time that
    // the cap leaves after the warmup, which can be zero or less.
    struct WindowRequest {
        std::size_t samples = 0;
        WallCap cap = WallCap::Off;
        std::chrono::nanoseconds remaining{0};
    };

    // One window of the sampled loop: its part of the samples, and the
    // reading of the TSC after which it takes no more sample (THE WINDOWS).
    struct WindowBound {
        std::size_t samples = 0;
        std::uint64_t deadline_tsc = 0;
    };

    // The windows of the sampled loop, in order.
    struct WindowPlan {
        ::fixy::FixedList<WindowBound, kWindows> bounds{};
    };

    // What start_window_ reads just before the sampled loop: the clock of
    // the pinned CPU and, with BPF, the counters that close_ subtracts.  The
    // loop adds the end of each window, and close_window_ adds the time that
    // the measuring thread lost in it.
    struct SampleWindow {
        uint64_t freq_start = 0;
        // The first reading of the pause check (THE PAUSE CHECK).
        PauseReading pause_before{};
        // The clocks at the end of the last window, and the count of the
        // samples at the end of each window with the time that the measuring
        // thread lost in it (THE WINDOWS).
        WindowClock clock_last{};
        ::fixy::FixedList<std::size_t, kWindows> ends{};
        ::fixy::FixedList<std::uint64_t, kWindows> lost_ns{};
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        ::fixy::Option<::fixy::Ref<::crucible::perf::SenseHub const>> hub{};
        // SchedSwitch — see detail::senses_instance() docblock for cost
        // analysis.  Pre/post are ~1 ns volatile loads each; the walk
        // happens at print time on bg thread, never per-iteration.
        ::fixy::Option<::fixy::Ref<::crucible::perf::SchedSwitch const>> sw{};
        ::crucible::perf::Snapshot bpf_pre{};
        uint64_t sched_pre_idx = 0;
#endif
    };

    // Resolves the policy and pins the thread.  The policy wins when the
    // builder set one; else the variable CRUCIBLE_BENCH_HARDENING
    // (production, cloud_vm, dev_quiet or none) names one; else pin_()
    // pins the thread directly.
    [[nodiscard, gnu::noinline]] MeasureOpening open_() const noexcept {
        MeasureOpening opening{};
        // The bench applies a policy at the start of a measurement, which is
        // startup work, so it holds the startup load context.
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        if (have_hardening_) {
            opening.hardening_guard = crucible::warden::mint_hardening(startup, detail::harness_topology(), hardening_);
            opening.pin = PinResult::one(CpuId{opening.hardening_guard.pinned_cpu()});
        } else if (auto env = env_hardening_(); env.has_value()) {
            opening.hardening_guard = crucible::warden::mint_hardening(startup, detail::harness_topology(), *env);
            opening.pin = PinResult::one(CpuId{opening.hardening_guard.pinned_cpu()});
        } else {
            opening.pin = pin_();
        }
        return opening;
    }

    [[nodiscard, gnu::noinline]] static SampleWindow start_window_(CpuId pinned_cpu) {
        SampleWindow window{};
        window.freq_start = detail::read_cpu_freq_hz(pinned_cpu.raw());
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        const ::crucible::perf::Senses* senses = detail::senses_instance();
        window.hub = senses->sense_hub();
        window.sw = senses->sched_switch();
        for (auto const hub : window.hub)
            window.bpf_pre = hub->read(::foundation::effects::testing::test().nondet);
        for (auto const sw : window.sw)
            window.sched_pre_idx = sw->timeline_write_index();
#endif
        window.pause_before = detail::read_pause();
        window.clock_last = detail::read_window_clock();
        return window;
    }

    // The windows of the sampled loop (THE WINDOWS): min(samples, kWindows)
    // windows, the first samples % count of them with one more sample, and
    // each window with the end of its part of the time, from the TSC of now.
    // With no cap, or with no calibration of the TSC, a window has no end of
    // its time.
    [[nodiscard, gnu::noinline]] static WindowPlan plan_windows_(WindowRequest const& request) noexcept {
        WindowPlan plan{};
        std::size_t const count = request.samples < kWindows ? request.samples : kWindows;
        if (count == 0) return plan;
        double const nspc = Timer::ns_per_cycle();
        bool const is_timed = request.cap == WallCap::On && nspc > 0.0;
        double const remaining_ns =
            request.remaining.count() > 0 ? static_cast<double>(request.remaining.count()) : 0.0;
        std::uint64_t const span_tsc =
            is_timed ? static_cast<std::uint64_t>(remaining_ns / nspc / static_cast<double>(count)) : 0;
        std::size_t const base = request.samples / count;
        std::size_t const extra = request.samples % count;
        std::uint64_t const start_tsc = rdtsc_start();
        for (std::size_t done = 0; done < count; ++done) {
            std::size_t const share = base + (done < extra ? std::size_t{1} : std::size_t{0});
            std::uint64_t const deadline = is_timed ? start_tsc + span_tsc * (done + 1) : UINT64_MAX;
            static_cast<void>(plan.bounds.append(WindowBound{.samples = share, .deadline_tsc = deadline}));
        }
        return plan;
    }

    // Reads the clocks at the end of the last window that the loop ended, and
    // keeps the time that the measuring thread lost in that window (THE
    // WINDOWS).  The two reads come between two samples, outside the timed
    // regions.
    [[gnu::noinline]] static void close_window_(SampleWindow& window) noexcept {
        WindowClock const clock = detail::read_window_clock();
        static_cast<void>(window.lost_ns.append(detail::lost_between(
            ::fixy::side_as<side::ReadingBefore>(window.clock_last), ::fixy::side_as<side::ReadingAfter>(clock))));
        window.clock_last = clock;
    }

    // Ends the measurement and builds its report from the samples that the
    // sampled loop filled.
    [[nodiscard, gnu::noinline]] Report close_(MeasureOpening opening, SampleWindow const& window,
                                               ::fixy::HeapArray<double> const& taken, size_t filled, size_t batch,
                                               std::chrono::steady_clock::time_point wall0) const {
        const PauseReading pause_after = detail::read_pause();
        // The samples that the loop filled, the first filled of taken.
        ::fixy::HeapArray<double> ns_samples =
            part_of_samples(::fixy::View<const double>{taken.view()}, ::fixy::side_as<side::PartFirst>(std::size_t{0}),
                            ::fixy::side_as<side::PartEnd>(filled));
        const CpuId pinned_cpu = opening.pin.cpu;
        const uint64_t freq_start = window.freq_start;

        const auto wall1 = std::chrono::steady_clock::now();
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        ::crucible::perf::Snapshot const& bpf_pre = window.bpf_pre;
        const uint64_t sched_pre_idx = window.sched_pre_idx;
        ::crucible::perf::Snapshot bpf_post{};
        uint64_t sched_post_idx = 0;
        for (auto const hub : window.hub)
            bpf_post = hub->read(::foundation::effects::testing::test().nondet);
        for (auto const sw : window.sw)
            sched_post_idx = sw->timeline_write_index();
#endif
        const uint64_t freq_end = detail::read_cpu_freq_hz(pinned_cpu.raw());
        // The measurement ends here, and so does the pin.  A failed restore
        // leaves the pin in place, and the next Run pins again in any case.
        PinResult& pin = opening.pin;
        (void)pin.restore();

        // Drift: compare first-half vs second-half p50 as a proxy for
        // frequency or cache-state transitions during the run. Use
        // (N+1)/2 so that on odd N, h1 contains the median element and
        // is never shorter than h2 — makes the comparison symmetric in
        // "which half includes the middle sample?" rather than biased
        // toward the lower half (which N/2 would do).
        //
        // N is ns_samples.size(), not the requested S — when the
        // wall-time cap breaks the sample loop early, ns_samples is
        // shorter than S and using S here would read past the end.
        double drift = 0;
        bool drift_flag = false;
        const size_t N = ns_samples.size();
        const ::fixy::View<const double> loop_order{ns_samples.view()};
        if (N >= 200) {
            std::size_t const half = (N + 1) / 2;
            const double m1 = median_of_part(part_of_samples(
                loop_order, ::fixy::side_as<side::PartFirst>(std::size_t{0}), ::fixy::side_as<side::PartEnd>(half)));
            const double m2 = median_of_part(
                part_of_samples(loop_order, ::fixy::side_as<side::PartFirst>(half), ::fixy::side_as<side::PartEnd>(N)));
            const double m = (m1 + m2) / 2.0;
            drift = (m > 0) ? std::abs(m1 - m2) / m * 100.0 : 0.0;
            drift_flag = drift > 10.0;
        }

        // The median of each window, from its samples in the order of the
        // loop, before the sort of all the samples (THE WINDOWS).
        ::fixy::FixedList<double, kWindows> window_medians{};
        std::size_t window_begin = 0;
        for (std::size_t const window_end : window.ends) {
            if (window_end > window_begin) {
                static_cast<void>(window_medians.append(
                    median_of_part(part_of_samples(loop_order, ::fixy::side_as<side::PartFirst>(window_begin),
                                                   ::fixy::side_as<side::PartEnd>(window_end)))));
            }
            window_begin = window_end;
        }
        Report r;
        r.windows = spread_of_windows(window_medians.view());
        r.name = name_;
        r.batch = batch;
        r.pinned_cpu = pinned_cpu;
        r.pinned_cpu_count = pin.cpu_count;
        r.drift_pct = drift;
        r.drift_flag = drift_flag;
        r.wall_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(wall1 - wall0).count());
        r.pct = Percentiles::compute(ns_samples);
        r.samples = std::move(ns_samples);
        r.pause = detail::pause_between(::fixy::side_as<side::ReadingBefore>(window.pause_before),
                                        ::fixy::side_as<side::ReadingAfter>(pause_after));
        r.window_basis = r.pause.basis == PauseBasis::LostTime ? PauseBasis::LostTime : PauseBasis::NotMeasured;
        r.paused_windows = detail::paused_windows_of(window.lost_ns.view(), r.pause.basis);

        // Sysfs-based freq drift check.
        r.freq_start_hz = freq_start;
        r.freq_end_hz = freq_end;
        if (freq_start > 0 && freq_end > 0) {
            const auto hi = std::max(freq_start, freq_end);
            const auto lo = std::min(freq_start, freq_end);
            const double ratio = static_cast<double>(hi - lo) / static_cast<double>(hi);
            r.freq_drift_flag = ratio > 0.05;
        }

        // Cycles/op derived from rdtsc mean ns and TSC frequency.
        // Consistent with how "cyc" was reported before, independent of
        // PMU availability.
        if (const double tsc_hz = Timer::tsc_freq_hz(); tsc_hz > 0.0) {
            r.cycles_per_op = r.pct.mean * tsc_hz / 1e9;
        }

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        for (auto const hub : window.hub) {
            r.bpf_delta = bpf_post - bpf_pre;
            // attached_programs() returns Refined<bounded_above<64>, size_t>
            // — the type carries a structural ≤64 bound that
            // matches inplace_vector<bpf_link*, 64>'s capacity.  Unwrap with
            // .value() for the raw count expected by the bench Report
            // struct (kept as plain size_t for printf-friendly emission).
            r.bpf_attached = hub->attached_programs().value();
        }
        // SchedSwitch off-CPU top-K — walk the timeline events that
        // landed during this bench window and maintain a sorted
        // top-K (K=5) of the longest spikes.  All work is on the bg
        // thread; per-iteration cost is the 2 × ~1 ns
        // timeline_write_index() reads done pre/post the loop.
        // Per-event walk cost is ≤ K comparisons + at most K element
        // shifts — for K=5 that's <50 ns per event, totalled across
        // ≤TIMELINE_CAPACITY events per bench.
        r.sched_pre_idx = sched_pre_idx;
        r.sched_post_idx = sched_post_idx;
        // With no scheduler counter, the view stays empty and both indexes
        // stay zero, so the walk below does nothing.
        ::fixy::Borrowed<const ::crucible::perf::TimelineSchedEvent, ::crucible::perf::SchedSwitch> view{};
        for (auto const sw : window.sw)
            view = sw->timeline_view();
        if (sched_post_idx > sched_pre_idx) {
            const uint64_t window_size =
                std::min(sched_post_idx - sched_pre_idx, static_cast<uint64_t>(::crucible::perf::TIMELINE_CAPACITY));
            const auto* events = view.data();
            if (events != nullptr) {
                constexpr uint32_t K = Report::kSchedOffCpuTopK;
                for (uint64_t i = 0; i < window_size; ++i) {
                    // Walk newest-to-oldest so that on a wrap-overflow
                    // (window_size hit cap), we read the most-recent
                    // events rather than the wrapped-over stale ones.
                    const uint64_t global_idx = sched_post_idx - 1 - i;
                    const uint32_t slot = static_cast<uint32_t>(global_idx & ::crucible::perf::TIMELINE_MASK);
                    // Acquire load on ts_ns pairs with the BPF program's
                    // compiler barrier before the ts_ns store.  ts_ns == 0
                    // → the producer has not committed this slot; skip.
                    const uint64_t ts = __atomic_load_n(&events[slot].ts_ns, __ATOMIC_ACQUIRE);
                    if (ts == 0) continue;
                    ++r.sched_offcpu_count;
                    const uint64_t off = events[slot].off_cpu_ns;
                    const uint32_t cpu = events[slot].on_cpu;
                    // Insertion-sort into top-K (descending by `off`).
                    // Skip cheap if (a) top is full AND (b) new
                    // event is smaller than the smallest kept.
                    if (r.sched_offcpu_top_n == K && off <= r.sched_offcpu_top_ns[K - 1]) {
                        continue;
                    }
                    // Slot to start shifting from: either past-end
                    // (when top isn't full) or the last kept slot
                    // (which we'll displace).
                    uint32_t pos = (r.sched_offcpu_top_n < K) ? r.sched_offcpu_top_n : K - 1;
                    if (r.sched_offcpu_top_n < K) ++r.sched_offcpu_top_n;
                    while (pos > 0 && r.sched_offcpu_top_ns[pos - 1] < off) {
                        r.sched_offcpu_top_ns[pos] = r.sched_offcpu_top_ns[pos - 1];
                        r.sched_offcpu_top_cpu[pos] = r.sched_offcpu_top_cpu[pos - 1];
                        --pos;
                    }
                    r.sched_offcpu_top_ns[pos] = off;
                    r.sched_offcpu_top_cpu[pos] = cpu;
                }
            }
        }
#endif

        return r;
    }

    enum class Pin : uint8_t {
        Auto,
        Explicit,
        None,
        FanOut
    };

    std::string name_;
    size_t samples_ = 0;  // TODO: strong type (count, not an id)
    size_t warmup_ = 10000;  // TODO: strong type
    size_t batch_ = 0;  // TODO: strong type
    CpuId core_{};  // CpuId::none() → falls through to Auto
    Pin pin_mode_ = Pin::Auto;

    // Default wall-time budget per Run. Caps any bench where the body
    // × 100'000 samples × 10'000 warmup would run for hours or hold
    // gigabytes of short-lived allocations that glibc's heap pool
    // can't promptly return to the OS. Override globally via
    // CRUCIBLE_BENCH_WALL_MS; per-Run via .max_wall_ms(ms); disable
    // entirely via .max_wall_ms(0) or CRUCIBLE_BENCH_WALL_MS=0.
    static constexpr size_t kDefaultMaxWallMs = 10000;
    size_t max_wall_ms_ = kDefaultMaxWallMs;
    bool have_max_wall_ = false;  // true iff .max_wall_ms() was called

    // CRUCIBLE_BENCH_WALL_MS=<n>. Empty/unset → default applies.
    [[nodiscard]] static std::optional<size_t> env_wall_ms_() noexcept {
        const char* s = std::getenv("CRUCIBLE_BENCH_WALL_MS");
        if (s == nullptr || s[0] == '\0') return std::nullopt;
        char* end = nullptr;
        const long v = std::strtol(s, &end, 10);
        if (end == s || v < 0) return std::nullopt;
        return static_cast<size_t>(v);
    }

    // Optional realtime policy applied for the duration of measure().
    // When set, crucible::warden::mint_hardening() is invoked at the top of measure()
    // and the returned AppliedPolicy is held until measure() returns —
    // scheduler / affinity / mlocks revert automatically. `.core()` and
    // `.no_pin()` are overridden by the policy's own CoreSelector when
    // hardening is set (the policy's Topology-based pick wins).
    crucible::warden::Policy hardening_{crucible::warden::Policy::none()};
    bool have_hardening_ = false;

    // CRUCIBLE_BENCH_HARDENING=production|cloud_vm|dev_quiet|none —
    // applies the named profile to every Run that doesn't call
    // .hardening() itself. Unset -> fall through to direct pin_() (no
    // hardening).
    [[nodiscard]] static std::optional<crucible::warden::Policy> env_hardening_() noexcept {
        const char* s = std::getenv("CRUCIBLE_BENCH_HARDENING");
        if (s == nullptr || s[0] == '\0') return std::nullopt;
        if (std::strcmp(s, "production") == 0) return crucible::warden::Policy::production();
        if (std::strcmp(s, "cloud_vm") == 0 || std::strcmp(s, "vm") == 0 || std::strcmp(s, "cloud") == 0)
            return crucible::warden::Policy::cloud_vm();
        if (std::strcmp(s, "dev_quiet") == 0 || std::strcmp(s, "dev") == 0)
            return crucible::warden::Policy::dev_quiet();
        if (std::strcmp(s, "none") == 0) return crucible::warden::Policy::none();
        return std::nullopt;
    }

    [[nodiscard]] static size_t env_samples_() noexcept {
        if (const char* s = std::getenv("CRUCIBLE_BENCH_SAMPLES")) {
            const long n = std::strtol(s, nullptr, 10);
            if (n > 0) return static_cast<size_t>(n);
        }
        return 100000;
    }

    // Apply affinity and return the CPU we actually ended up on.
    //
    //  • Pin::None → no affinity change, just report sched_getcpu().
    //  • Pin::Explicit with core_ >= 0 → target = core_, when the
    //    affinity of the process holds it.  A core outside the affinity
    //    prints a warning and takes the Auto choice (core_of_run).
    //  • Pin::Explicit with core_ < 0 (e.g. `.core(-1)` because the
    //    caller's env var defaulted to -1) → fall through to the Auto
    //    discovery path instead of silently skipping pinning.
    //  • Pin::Auto → hand off to `warden::select_hot_cpu_of` over the
    //    affinity of the process, which honors isolcpus, prefers P-cores,
    //    and (crucially) steers away from cpu0 and its SMT sibling because
    //    cpu0 absorbs timer IRQs / RCU callbacks on most Linux configs.
    //    Falls back to sched_getcpu() only if the selector returns -1
    //    (empty allowed set).  The pin never leaves the affinity.
    //
    // Each pin goes through fixy::sched::apply_affinity_to_cpu, the gated
    // door that records a pin event, so a CpuPinned proof of an earlier pin
    // on this thread stops being in force.  The harness holds the startup
    // load context, which owns Init, and the door admits it.  The result
    // holds the affinity that the pin replaced, and measure() restores it.
    //
    // After a successful pin, re-read sched_getcpu() — the
    // scheduler is not obliged to migrate us synchronously, so the
    // value we return is the one actually executing user code. On
    // a failed pin we return CpuId::none() rather than
    // the pre-call sched_getcpu(), to avoid silently pretending we
    // successfully pinned.
    [[nodiscard]] PinResult pin_() const noexcept {
#ifdef __linux__
        if (pin_mode_ == Pin::None) return PinResult{.cpu = CpuId{sched_getcpu()}, .cpu_count = 0};
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        if (pin_mode_ == Pin::FanOut) return fan_out_(startup);

        // The core of the run stays in the affinity of the process: the
        // explicit core when the affinity holds it, else the selector of
        // warden inside the affinity, with the heuristic of the Keeper's
        // mint_hardening(): isolcpu first, P-core preference, and no cpu0 or
        // its SMT sibling (timer-tick IRQ landing pad).
        const CpuId asked = (pin_mode_ == Pin::Explicit) ? core_ : CpuId::none();
        const crucible::warden::CpuSet allowed = crucible::warden::allowed_cpus(startup);
        const Cores cores =
            Cores::of(::fixy::side_as<side::AllowedCpus>(allowed),
                      ::fixy::side_as<side::IsolatedCpus>(crucible::warden::isolated_cpus(startup)), read_core_order());
        int target = detail::core_of_run(cores, allowed, asked).raw();
        if (asked.is_valid() && target != asked.raw()) {
            std::fprintf(stderr,
                         "WARN bench: cpu%d is outside the affinity of the process, so the run measures on cpu%d\n",
                         asked.raw(), target);
        }
        if (target < 0) target = sched_getcpu();
        if (target < 0) return PinResult{};

        ::fixy::Result<::fixy::sched::PriorAffinity, ::foundation::os::Errno> prior =
            ::fixy::sched::apply_affinity_to_cpu(startup, target);
        if (prior.is_err()) {
            return PinResult{};  // fail loudly, not silently
        }
        // Re-read — scheduler may not have migrated us yet. This is a
        // benign race: `sched_getcpu()` reads the currently-executing
        // CPU, which, having just been restricted to {target} by
        // the pin, must be `target` on the next schedule
        // boundary. On short paths (no preemption between the pin
        // and getcpu) we might still report the previous CPU — accept
        // that inaccuracy rather than giving the kernel scheduler a vote and
        // burning a context switch inside every .measure().
        const int actual = sched_getcpu();
        PinResult pinned = PinResult::one((actual >= 0) ? CpuId{actual} : CpuId{target});
        pinned.prior = std::move(prior).ok();
        return pinned;
#else
        return PinResult{};
#endif
    }

    // Moves the measuring thread to a CPU of its affinity that isolcpus did
    // not isolate, and gives the thread back its affinity at once.  A pin
    // migrates the calling thread before the system call returns, so the
    // thread then runs on that CPU with its whole affinity.  The load
    // balancer never moves a thread to an isolated CPU, so the thread and
    // each thread of the body stay on the CPUs that are not isolated.  The
    // current CPU is the choice when it is not isolated.  Else the choice is
    // the CPU that warden::select_hot_cpu gives with the isolated CPUs left
    // out, and that selector keeps away from cpu0 and its interrupts.
    // Returns the number of CPUs that are not isolated.
    //
    // An affinity of isolated CPUs only is a claim of loop bench (THE CORES
    // OF A RUN).  No load balancer spreads the threads of the body there, so
    // each thread of the body pins itself to a core of its slot: a worker
    // through pin_calling_thread_to(), and the threads of a door through the
    // placement OneCpuEach over Cores::from_slot(ThreadSlot{1}).  The
    // measuring thread then takes the core of slot 0 for the measurement, and
    // the result counts each core of the claim.  An affinity with no CPU gets
    // no pin and a count of zero.  Complexity: linear in the number of
    // allowed CPUs, times the cost of one sysfs read.
    [[nodiscard]] static PinResult fan_out_(const ::fixy::InitLoadCtx& startup) noexcept {
#ifdef __linux__
        const crucible::warden::CpuSet allowed = crucible::warden::allowed_cpus(startup);
        const crucible::warden::CpuSet isolated = crucible::warden::isolated_cpus(startup);
        const auto is_free = [&isolated](int cpu) noexcept { return cpu >= 0 && !isolated.contains(cpu); };
        size_t free_count = 0;
        for (const int cpu : allowed) {
            if (is_free(cpu)) ++free_count;
        }
        if (free_count == 0) return pin_to_measuring_core_(startup, allowed, isolated);

        int start_cpu = sched_getcpu();
        const bool is_current_allowed = allowed.contains(start_cpu);
        if (!is_current_allowed || !is_free(start_cpu)) {
            crucible::warden::CoreSelector selector{};
            selector.prefer_isolcpu = false;
            selector.avoid_smt_sibling = false;
            start_cpu = crucible::warden::select_hot_cpu(startup, detail::harness_topology(), selector, isolated);
            if (!is_free(start_cpu)) return PinResult{};
        }
        ::fixy::Result<::fixy::sched::PriorAffinity, ::foundation::os::Errno> prior =
            ::fixy::sched::apply_affinity_to_cpu(startup, start_cpu);
        if (prior.is_err()) return PinResult{};
        if (std::move(prior).expect("the test above found the pin").restore().is_err()) {
            // The thread keeps the pin to the start CPU, and the body runs
            // on that one CPU.
            return PinResult::one(CpuId{start_cpu});
        }
        return PinResult{.cpu = CpuId::none(), .cpu_count = free_count};
#else
        (void)startup;
        return PinResult{};
#endif
    }

#ifdef __linux__
    // The pin of the measuring thread of a fan-out to the core of slot 0 of a
    // claim, whose CPUs are all isolated.  The count of the result is the
    // count of the cores of the claim, and the result holds the replaced
    // affinity, which measure() gives back.  An empty claim or a failed pin
    // gives no pin and a count of zero.  Complexity: linear in the CPUs of the
    // claim, times the CPUs of the order of loop bench.
    [[nodiscard]] static PinResult pin_to_measuring_core_(const ::fixy::InitLoadCtx& startup,
                                                          crucible::warden::CpuSet const& allowed,
                                                          crucible::warden::CpuSet const& isolated) noexcept {
        const Cores cores = Cores::of(::fixy::side_as<side::AllowedCpus>(allowed),
                                      ::fixy::side_as<side::IsolatedCpus>(isolated), read_core_order());
        const CpuId measuring = cores.of_slot(kMeasuringSlot).value_or(CpuId::none());
        if (!measuring.is_valid()) return PinResult{};
        ::fixy::Result<::fixy::sched::PriorAffinity, ::foundation::os::Errno> prior =
            ::fixy::sched::apply_affinity_to_cpu(startup, measuring.raw());
        if (prior.is_err()) return PinResult{};
        PinResult pinned{.cpu = measuring, .cpu_count = cores.count()};
        pinned.prior = std::move(prior).ok();
        return pinned;
    }
#endif

    // Ramp batch size by 2× until the **body itself** (raw region minus
    // rdtsc overhead) exceeds 1000 cycles — keeps rdtsc overhead below
    // ~3 % of the measured region. Cap at 2^18.
    //
    // Older form compared `best` (includes overhead) to MIN_CYCLES, so
    // the terminator fired ~30 cycles early. Add `ovh` to the threshold
    // so the real body cost crosses 1000, not body+overhead.
    //
    // Takes `RdtscPinned const&` so the auto-tune pilot
    // loops use the pinned-overload TSC reads — same discipline as
    // the main measurement loop.  auto_batch_ runs *after* pin_() so
    // a witness is always available at the call site (measure()).
    template <typename Body>
    [[nodiscard]] size_t auto_batch_(Body&& body, RdtscPinned const& pin) const {
        constexpr uint64_t MIN_CYCLES = 1000;
        constexpr size_t MAX_BATCH = 1u << 18;
        constexpr size_t PILOT_SAMPLES = 100;

        const uint64_t ovh = Timer::overhead_cycles();

        size_t batch = 1;
        while (batch <= MAX_BATCH) {
            uint64_t best = UINT64_MAX;
            for (size_t i = 0; i < PILOT_SAMPLES; ++i) {
                const uint64_t t0 = rdtsc_start(pin);
                for (size_t j = 0; j < batch; ++j)
                    body();
                const uint64_t t1 = rdtsc_end(pin);
                // Same wrap-safety argument as in the main loop.
                const uint64_t d = t1 - t0;
                if (d < best) best = d;
            }
            if (best >= MIN_CYCLES + ovh) return batch;
            batch *= 2;
        }
        return MAX_BATCH;
    }
};

// ── System info ────────────────────────────────────────────────────

[[nodiscard]] inline int env_core() noexcept;

// Prints the facts of the host and of the run: the processor, the governor,
// the profile, the CPUs, the cores of the run, the TSC and the BPF senses.
// Call it first in main, before a Run pins the thread.
inline void print_system_info() {
    using ::fixy::Sink;
    using ::fixy::TextView;
    detail::flush_standard_output();
    ::fixy::report(Sink::Out, "=== system ===\n");

    // The first 4 KiB of /proc/cpuinfo hold the block of the first CPU.
    ::fixy::FixedText<4096> cpuinfo{};
    if (::foundation::os::read_file("/proc/cpuinfo", cpuinfo).is_ok()) {
        ::fixy::report(Sink::Out, "  cpu:       {}\n", detail::value_of_line(cpuinfo.view(), "model name"));
    }
    ::fixy::FixedText<64> governor_text{};
    TextView const governor =
        detail::read_first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", governor_text);
    if (governor.size() != 0) ::fixy::report(Sink::Out, "  governor:  {}\n", governor);

    // The profile this binary was built with, or none.  The root
    // CMakeLists.txt defines CRUCIBLE_PGO_PROFILE under CRUCIBLE_PGO=use
    // as "<tier> <collected> <commit> <tree>".  A number from a profiled
    // build and a number from a plain build are not comparable, and the
    // report says which one it is.
#ifdef CRUCIBLE_PGO_PROFILE
    ::fixy::report(Sink::Out, "  pgo:       {}\n", TextView{CRUCIBLE_PGO_PROFILE});
#else
    ::fixy::report(Sink::Out, "  pgo:       none\n");
#endif

#ifdef __linux__
    // The cores of the run, and the core of each Run with the default pin,
    // from the affinity of the process and CRUCIBLE_BENCH_CORE, as pin_()
    // picks it.
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    ::fixy::report(Sink::Out, "  cpus:      {} online\n", crucible::warden::num_online_cpus(startup));
    const crucible::warden::CpuSet allowed = crucible::warden::allowed_cpus(startup);
    const Cores cores =
        Cores::of(::fixy::side_as<side::AllowedCpus>(allowed),
                  ::fixy::side_as<side::IsolatedCpus>(crucible::warden::isolated_cpus(startup)), read_core_order());
    ::fixy::report(Sink::Out, "  core:      cpu{}, of the {} cpus of the affinity of the process\n",
                   detail::core_of_run(cores, allowed, CpuId{env_core()}).raw(), allowed.size());
    if (cores.count() > 0) {
        ::fixy::report(Sink::Out, "  cores:     {} isolated cores, slot k on core k, in {}:", cores.count(),
                       cores.source() == SlotOrder::Claim
                           ? TextView{"the order of the claim from CRUCIBLE_BENCH_CORE_ORDER, the quietest first"}
                           : TextView{"ascending order, with no order from loop bench"});
        for (CpuId const cpu : cores.slots()) {
            ::fixy::report(Sink::Out, " cpu{}", cpu.raw());
        }
        ::fixy::report(Sink::Out, "\n");
    }
#endif

    const double nspc = Timer::ns_per_cycle();
    const auto tsc_khz = static_cast<std::uint64_t>(Timer::tsc_freq_hz() / 1e3);
    ::fixy::report(Sink::Out, "  TSC:       {} kHz\n", tsc_khz);
    ::fixy::report(Sink::Out, "  RDTSC oh:  {} cycles\n", Timer::overhead_cycles());
    if (nspc <= 0.0) {
        ::fixy::report(Sink::Err, "WARN bench: TSC calibration failed (ns_per_cycle == 0); cycles/op will read 0.\n");
    }

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
    const ::crucible::perf::Senses* senses = detail::senses_instance();
    auto hub = senses->sense_hub();
    const auto cov = senses->coverage();
    if (hub.is_some()) {
        // Show both the SenseHub-internal tracepoint count and the
        // Senses-aggregator coverage, so a 0/2 attached banner is
        // distinguishable from a partial sense_hub-only attach.
        ::fixy::report(Sink::Out,
                       "  BPF senses: harness subset loaded (SenseHub={} tracepoints, {}/2 harness facades; full "
                       "suite is reported by bench_perf_loader)\n",
                       std::move(hub).expect("the test above found the hub")->attached_programs().value(),
                       cov.attached_count());
    } else if (cov.attached_count() > 0) {
        ::fixy::report(Sink::Out,
                       "  BPF senses: harness subset partial, {}/2 facades attached (sense_hub failed; set "
                       "CRUCIBLE_PERF_VERBOSE=1 for libbpf logs)\n",
                       cov.attached_count());
    } else {
        ::fixy::report(Sink::Out, "  BPF senses: UNAVAILABLE. Set CRUCIBLE_PERF_VERBOSE=1 for libbpf logs.\n"
                                  "              typical fix: sudo sysctl kernel.unprivileged_bpf_disabled=0\n"
                                  "              or run it through /usr/local/bin/crucible-caps (loop bench does)\n");
    }
#else
    ::fixy::report(Sink::Out, "  BPF senses: disabled at build time\n");
#endif

    if (const char* s = std::getenv("CRUCIBLE_BENCH_SAMPLES")) {
        std::fprintf(stdout, "  samples:   %s (CRUCIBLE_BENCH_SAMPLES)\n", s);
    }
    std::fprintf(stdout, "\n");
}

// The bench raises its nice value through the scheduling frame
// `mint_priority<-10>(ctx, body)`, not through a raw setpriority(2).  On
// Linux, `setpriority(PRIO_PROCESS, 0, nice)` sets the nice value of the
// calling thread.  The frame lends a `SchedPriority<-10>` proof to its body,
// which shows that the change ran in a context whose row owns Init.  The
// bench needs no proof after the call, so its body gives nothing.  The nice
// value stays in force after the frame ends.
//
// A failed call (EPERM, EACCES or EINVAL) is ignored.  A bench that runs
// without the permission keeps the default nice value.  The measurement
// still runs, with more scheduler noise in the tail.
inline void elevate_priority() noexcept {
#ifdef __linux__
    // ColdInitCtx owns Init, Alloc and IO, and it satisfies
    // CtxFitsPriorityMint.  A context over Init is built only from an Init
    // source, and the bench takes that source from the test witness.
    ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> const raised = ::fixy::sched::mint_priority<-10>(
        ::fixy::ColdInitCtx{::foundation::effects::testing::init()}, [](auto) noexcept {});
    static_cast<void>(raised.is_ok());
#endif
}

// ── Per-bench boilerplate helpers ──────────────────────────────────
//
// Every bench executable ends up duplicating:
//   - getenv-with-empty-check
//   - CRUCIBLE_BENCH_CORE → int parse (-1 if unset/invalid)
//   - CRUCIBLE_BENCH_JSON → bool
//   - a local `run = [&](...)` lambda that applies env-core to a new Run
//   - a JSON emission block after the reports array
//
// Pull those into the harness so every migration is the minimum needed.
// CRUCIBLE_BENCH_HARDENING is NOT parsed here — it's consumed by
// Run::measure() directly, so no bench-side work is required.

// Read an env var; return nullptr if unset OR empty (matches the convention
// of existing benches where an empty value is "unset").
[[nodiscard]] inline const char* env(const char* name) noexcept {
    const char* s = std::getenv(name);
    return (s != nullptr && s[0] != '\0') ? s : nullptr;
}

// The text of CRUCIBLE_BENCH_CORE_ORDER comes from /proc/self/environ, the
// environment of the process at its start, with a zero after each variable.
// loop bench sets the variable in that environment (THE CORES OF A RUN).  A
// file that does not read gives a warning and the empty order.  An
// environment larger than the buffer ends the process, because a cut can drop
// the variable.  A text that parse_core_order() refuses ends the process too,
// because loop bench writes each such text.  Complexity: one read of procfs,
// and linear in the environment.
inline CoreOrder read_core_order() noexcept {
    constexpr std::size_t kEnvironBytes = 65536;
    namespace topology = crucible::warden::detail;
    constexpr ::fixy::TextView kKey{"CRUCIBLE_BENCH_CORE_ORDER="};
    ::fixy::FixedText<kEnvironBytes> environ_text{};
    if (::foundation::os::read_file("/proc/self/environ", environ_text).is_err()) {
        ::fixy::report(::fixy::Sink::Err, "WARN bench: /proc/self/environ does not read, so the slots of the run take "
                                          "the cores in ascending order\n");
        return CoreOrder{};
    }
    if (environ_text.size() == kEnvironBytes) {
        ::fixy::fatal("the environment of the process holds {} bytes or more, so the harness cannot read "
                      "CRUCIBLE_BENCH_CORE_ORDER in it",
                      kEnvironBytes);
    }
    ::fixy::TextView rest = environ_text.view();
    while (rest.size() != 0) {
        std::size_t const end = ::fixy::find(rest, '\0').value_or(rest.size());
        ::fixy::TextView const variable = topology::text_prefix(rest, end);
        rest = topology::text_suffix(rest, end + 1);
        if (!::fixy::starts_with(variable, kKey)) continue;
        ::fixy::TextView const text = topology::text_suffix(variable, kKey.size());
        return parse_core_order(text).match(
            [](CoreOrder order) noexcept { return order; },
            [&text]() noexcept -> CoreOrder {
                ::fixy::fatal("CRUCIBLE_BENCH_CORE_ORDER is '{}', and it must hold CPU numbers with a comma "
                              "between two, each CPU one time, such as 240,242,224",
                              text);
            });
    }
    return CoreOrder{};
}

// Parse CRUCIBLE_BENCH_CORE. Returns -1 when unset, invalid, or outside
// the int range (the -1 sentinel is the "no explicit pin" signal that
// Run::pin_() falls back to Auto on).
[[nodiscard]] inline int env_core() noexcept {
    if (const char* s = env("CRUCIBLE_BENCH_CORE")) {
        char* endp = nullptr;
        const long v = std::strtol(s, &endp, 10);
        if (endp != s && v >= static_cast<long>(INT_MIN) && v <= static_cast<long>(INT_MAX)) {
            return static_cast<int>(v);
        }
    }
    return -1;
}

// Whether a bench prints the JSON tail of its reports after their text.
enum class JsonTail : std::uint8_t {
    Off,
    On,
};

// CRUCIBLE_BENCH_JSON set to a value other than "0" asks for the JSON tail.
[[nodiscard]] inline JsonTail env_json() noexcept {
    const char* s = env("CRUCIBLE_BENCH_JSON");
    return s != nullptr && std::strcmp(s, "0") != 0 ? JsonTail::On : JsonTail::Off;
}

// One-shot: build a Run with env-driven core pinning and measure body.
// For benches that need .samples()/.warmup() overrides, construct Run
// directly and apply env_core() manually.
template <typename Body>
[[nodiscard]] inline Report run(std::string name, Body&& body) {
    Run r{std::move(name)};
    if (const int c = env_core(); c >= 0) (void)r.core(c);
    return r.measure(std::forward<Body>(body));
}

// Writes the text line of each report to standard output, in order.  A bench
// holds its reports in an owner of the base and passes its view().
inline void emit_reports_text(::fixy::View<const Report> reports) noexcept {
    for (Report const& report : reports)
        report.print_text();
}

// Writes the one-line summary of each Compare to standard output, in the
// order of the view.
inline void emit_compares(::fixy::View<const Compare> compares) noexcept {
    for (Compare const& compare : compares)
        compare.print_text(stdout);
}

// Writes the line "=== json ===" and the JSON array of the reports to
// standard output when tail is On, and nothing when it is Off.  The reader of
// utils/assay/readers/bench.py reads the text after the last such line, so a
// bench calls this last.  A bench that prints more between the text and the
// JSON, such as a bench::compare block, calls emit_reports_text, then its
// prints, then this.  The frame of the array goes through fixy::report, which
// writes at once, and each object through stdio, so a flush comes after each
// object.
inline void emit_reports_json(::fixy::View<const Report> reports, JsonTail tail) noexcept {
    using ::fixy::Sink;
    if (tail == JsonTail::Off) return;
    detail::flush_standard_output();
    ::fixy::report(Sink::Out, "\n=== json ===\n[\n");
    std::size_t written = 0;
    for (Report const& report : reports) {
        ++written;
        ::fixy::report(Sink::Out, "  ");
        report.print_json();
        detail::flush_standard_output();
        ::fixy::report(Sink::Out, "{}\n", written < reports.size() ? ::fixy::TextView{","} : ::fixy::TextView{});
    }
    ::fixy::report(Sink::Out, "]\n");
}

// The usual end of main: the text of the reports, then their JSON tail.
inline void emit_reports(::fixy::View<const Report> reports, JsonTail tail) noexcept {
    emit_reports_text(reports);
    emit_reports_json(reports, tail);
}

// Puts a report after the last report of the list of a bench.  The bench
// states the capacity of its list, so a push into a full list ends the
// process.
template <std::size_t Capacity>
void add_report(::fixy::FixedList<Report, Capacity>& reports, Report report) noexcept {
    static_cast<void>(
        reports.append(std::move(report)).expect("the list of the reports of a bench has room for each row"));
}

// ── Setup checks ───────────────────────────────────────────────────
//
// Each preset that builds the benches (bench, pgo-generate and pgo)
// defines NDEBUG, so assert checks nothing in a bench.
// CRUCIBLE_BENCH_CHECK checks in each build.  A failed check prints its
// file, its line and its condition to stderr, and then it aborts.
//
// Put each check outside the timed body.  The body can count the results
// that are not the expected ones, and a check after the measurement reads
// the count.
namespace detail {

[[noreturn, gnu::cold, gnu::noinline]] inline void check_failed(const char* file, int line,
                                                                const char* condition) noexcept {
    std::fprintf(stderr, "%s:%d: bench check failed: %s\n", file, line, condition);
    std::fflush(stderr);
    std::abort();
}

}  // namespace detail

}  // namespace bench

#define CRUCIBLE_BENCH_CHECK(cond)                                    \
    do {                                                              \
        if (!(cond)) [[unlikely]]                                     \
            ::bench::detail::check_failed(__FILE__, __LINE__, #cond); \
    } while (0)
