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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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
#include <fixy/os/Fs.h>
#include <fixy/os/Process.h>
#include <fixy/os/Sched.h>
#include <fixy/os/Time.h>
#include <foundation/Quarantine.h>
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
//
// The two reads and clobber_memory() below are the raw instructions that
// bracket each sample, so they stay in a region of the class MEASURE.  The fence
// before the start read is not in the TSC reader of fixy/os/Time.h, and the
// timed loop must keep each instruction that it had.

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the fenced reads of the TSC and the compiler barrier bracket each sample")

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

// A compiler barrier: no load or store moves across it, and it emits no
// instruction.  After it, the optimizer reads each object in memory again.
// A row calls it only where it must see the effects of its body on memory
// (the sink).
[[gnu::always_inline]] inline void clobber_memory() noexcept { asm volatile("" : : : "memory"); }

// A compiler barrier that reads and writes value in memory, so the optimizer
// keeps each store of value before it, also of a local that no other code
// reads.  It emits no instruction and makes no call, so a timed loop keeps
// only its own instructions.
template <typename T>
[[gnu::always_inline]] inline void keep_in_memory(T& value) noexcept {
    asm volatile("" : "+m"(value) : : "memory");
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// ── RdtscPinned witness + pinned-overload rdtsc primitives ───────────
//
// Modern x86 has invariant_tsc, so cross-core TSC reads are comparable
// in absolute value — but a thread that migrates BETWEEN the rdtsc_start
// and rdtsc_end of a short-window measurement still sees pipeline-state
// artifacts (warm-vs-cold L1, branch-predictor reset, store-buffer
// drain on context switch).  Short-window measurements must be pinned
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

// ── Sink ───────────────────────────────────────────────────────
//
// do_not_optimize(value) gives value to an empty asm statement.  The
// optimizer must compute value for the statement, and it cannot remove the
// statement.  The statement makes no call and has no memory clobber.  So a
// timed loop keeps each other value in its register, as the loop of a real
// caller does, and the loop holds only its own instructions.
//
// The operand of the statement holds value in the place of its type
// (SinkOperand):
//   - An integer, an enumeration or a pointer of at most 8 bytes is in a
//     general register.
//   - A float or a double is in a vector register.
//   - A class of 1, 2, 4 or 8 bytes whose copy is a copy of its bytes, such
//     as an id, a hash or a step, is in a general register when GCC can put
//     it in one, and else in memory.
//   - A value of each other type is in memory, and the statement reads each
//     byte of the object.
// A const value is an input of the statement.  A value that is not const is
// an input and an output.  After the statement, the optimizer then does not
// know the value, so a timed loop cannot move a computation that reads the
// value out of the loop.  Give each input of a timed operation that does not
// change to the sink in this form, so that each call of the body computes
// the operation again.  In this form a class is in memory, as the bytes of
// the object.
//
// A pointer is an address, and the sink holds the address only, not the
// object.  To make an object opaque, give the object itself.
// clobber_memory() makes each object in memory opaque.  A row calls it only
// where it must see the effects of its body on memory, and it states the
// reason beside the call.
//
// mark_changed(object) is the input sink of an object in memory.  The
// statement reads and writes each byte of the object, so a read of the
// object after it loads the object again, and a write before it stays.  It
// emits no instruction and holds no other object.  A body that reads an
// object that does not change, such as a cell or a table, gives the object
// to mark_changed() first.  Else the optimizer reads the object one time
// before the timed loop, and the loop holds no instruction of the body.  The
// form that is not const of do_not_optimize() holds an integer in a
// register, so for an integer in memory it adds a load and a store to the
// loop, and the load can still move out of the loop with the store.
// make_opaque(value) gives a copy of a const value, such as a view, through
// that form, so a body that takes the copy computes its operation again.
//
// Each operand has one alternative.  The forms with two alternatives,
// "+m,r" and "r,m", lose the value or its earlier stores when GCC picks the
// register (GCC PR124958, PR109057 and PR105519).

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: an empty asm statement holds each value of a timed body, with no call")

// The place of the operand of the sink for a type (the sink).
enum class SinkOperand : std::uint8_t {
    GeneralRegister,
    VectorRegister,
    RegisterOrMemory,
    Memory,
};

template <typename T>
inline constexpr SinkOperand sink_operand_of =
    (std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>) && sizeof(T) <= sizeof(std::uint64_t)
        ? SinkOperand::GeneralRegister
    : (std::is_same_v<T, float> || std::is_same_v<T, double>) ? SinkOperand::VectorRegister
    : (std::is_class_v<T> && std::is_trivially_copyable_v<T>
       && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8))
        ? SinkOperand::RegisterOrMemory
        : SinkOperand::Memory;

template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& value) noexcept {
    constexpr SinkOperand operand = sink_operand_of<std::remove_cv_t<T>>;
    if constexpr (operand == SinkOperand::GeneralRegister) {
        asm volatile("" : : "r"(value));
    } else if constexpr (operand == SinkOperand::VectorRegister) {
#if defined(__aarch64__)
        asm volatile("" : : "w"(value));
#else
        asm volatile("" : : "x"(value));
#endif
    } else if constexpr (operand == SinkOperand::RegisterOrMemory) {
        asm volatile("" : : "rm"(value));
    } else {
        asm volatile("" : : "m"(value));
    }
}

template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T& value) noexcept {
    constexpr SinkOperand operand = sink_operand_of<std::remove_cv_t<T>>;
    if constexpr (operand == SinkOperand::GeneralRegister) {
        asm volatile("" : "+r"(value));
    } else if constexpr (operand == SinkOperand::VectorRegister) {
#if defined(__aarch64__)
        asm volatile("" : "+w"(value));
#else
        asm volatile("" : "+x"(value));
#endif
    } else {
        // The bytes of the object.  GCC refuses an output of a class with a
        // const member, such as Vigil.  A byte aliases each object, so after
        // the statement the optimizer reads each member of the object again.
        using ObjectBytes = unsigned char[sizeof(T)];
        asm volatile("" : "+m"(*static_cast<ObjectBytes*>(static_cast<void*>(__builtin_addressof(value)))));
    }
}

// The input sink of an object in memory (the sink).  The operand is the bytes
// of the object, as for a class in the form of do_not_optimize() that is not
// const.
template <typename T>
[[gnu::always_inline]] inline void mark_changed(T& object) noexcept {
    using ObjectBytes = unsigned char[sizeof(T)];
    asm volatile("" : "+m"(*static_cast<ObjectBytes*>(static_cast<void*>(__builtin_addressof(object)))));
}

// A copy of value that the optimizer does not know: the input sink of a
// value that does not change, such as a const view of a table (the sink).
// The copy goes through the form of do_not_optimize() that is not const, so
// a copy of a class goes through memory.
template <typename T>
[[nodiscard, gnu::always_inline]] inline T make_opaque(T value) noexcept {
    do_not_optimize(value);
    return value;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// ── clobber_array(View) — array-side DCE kill ─────────────────────
//
// Companion to `do_not_optimize()` (single-value DCE kill) and
// `clobber_memory()` (global memory barrier).  Use when the bench body
// fills or scans an array and the array's memory is otherwise dead
// at the timed region's exit — without this, the optimizer is free
// to elide the fill / scan because nothing observably consumes the
// result.
//
// Implementation: an empty `[[gnu::noipa]]` function.  The View (its
// first element and its count) goes in the registers of the ABI, and
// noipa stops the analysis across the call.  So for the optimizer the
// function can read or write each byte that the arguments reach, and the
// writes and the reads of the elements stay.  An asm statement has no
// operand for a run whose count only the run gives, so the call stays.
//
// One template takes a View of each element type and extent, so a View of
// const elements serves a read-only scan.  A FixedArray, a HeapArray and
// each other owner of the base gives its View through view().
//
// Cost: one real call instruction per use.  No bench row calls it.
//
// Usage:
//
//   ::fixy::FixedArray<int, N> data{};
//   for (int& v : data) v = compute(...);
//   bench::clobber_array(data.view());  // forces materialization
//
//   ::fixy::HeapArray<float> readings = ::fixy::mint_heap_array(alloc, N, 0.0f);
//   bench::Run("scan").measure([&]() noexcept {
//       float sum = 0;
//       for (float x : readings) sum += x;
//       bench::clobber_array(::fixy::View<const float>{readings.view()});
//       bench::do_not_optimize(sum);
//   });

template <typename T, std::size_t Extent>
[[gnu::noipa]] void clobber_array(::fixy::View<T, Extent> elements) noexcept {
    (void)elements;
}

// The same sink for the run of a frame.  The call takes the ViewMut through
// a const reference, so the body keeps the one writer of its run.  As for a
// View, the optimizer keeps each write and each read of the elements.
template <typename T, typename Brand, std::size_t Extent>
[[gnu::noipa]] void clobber_array(::fixy::ViewMut<T, Brand, Extent> const& elements) noexcept {
    (void)elements;
}

namespace detail {

// The time of the monotonic clock in nanoseconds, through the clock door of
// fixy/os/Time.h.  Each caller times a loop with it, so a clock that does not
// read ends the process.  The read is not inline, so a loop that a template
// instantiates holds only the call.
[[nodiscard, gnu::noinline]] inline std::uint64_t read_monotonic_ns() noexcept {
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    const ::foundation::effects::cap::Nondet nondet = ::foundation::effects::testing::test().nondet;
    const auto wall = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
    return wall.read(nondet).expect("the monotonic clock of the bench harness reads").peek();
}

}  // namespace detail

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

    // Correlate TSC vs the monotonic clock over 200 ms. Longer window dampens
    // transient frequency wobble at process start.
    [[nodiscard]] static double calibrate_() noexcept {
        constexpr std::uint64_t kWindowNs = 200000000;

        (void)detail::read_monotonic_ns();
        (void)rdtsc_start();

        const std::uint64_t wall0_ns = detail::read_monotonic_ns();
        const uint64_t tsc0 = rdtsc_start();
        const std::uint64_t deadline_ns = wall0_ns + kWindowNs;
        while (detail::read_monotonic_ns() < deadline_ns) { /* spin */
        }
        const uint64_t tsc1 = rdtsc_end();
        const std::uint64_t wall1_ns = detail::read_monotonic_ns();

        const double ns = static_cast<double>(wall1_ns - wall0_ns);
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

// ── Environment ────────────────────────────────────────────────
//
// The harness reads its variables, such as CRUCIBLE_BENCH_SAMPLES and
// CRUCIBLE_BENCH_CORE_ORDER, from /proc/self/environ: the environment of the
// process at its start, with a zero after each variable.  loop bench and
// ctest set the variables of a bench in that environment, and no bench
// changes its environment.  The process reads the file one time, at the first
// read of a variable.  A file that does not read gives no variable.  An
// environment larger than the buffer ends the process, because a cut can drop
// a variable.

namespace detail {

// The bytes of the environment that the harness reads, at most.
inline constexpr std::size_t kEnvironBytes = 65536;

// The text of /proc/self/environ, and whether the file read.
struct StartEnvironment {
    ::fixy::FixedText<kEnvironBytes> text{};
    bool is_read = false;
};

// The environment of the process at its start.  Complexity: one read of
// procfs at the first call, and none after it.
[[nodiscard]] inline StartEnvironment const& start_environment() noexcept {
    static StartEnvironment const environment = []() noexcept {
        StartEnvironment read{};
        read.is_read = ::foundation::os::read_file("/proc/self/environ", read.text).is_ok();
        if (read.is_read && read.text.size() == kEnvironBytes) {
            ::fixy::fatal("the environment of the process holds {} bytes or more, so the harness cannot read its "
                          "variables in it",
                          kEnvironBytes);
        }
        return read;
    }();
    return environment;
}

// The value of the variable name in an environment text, with a zero after
// each variable, which can be empty, or none when the text has no such
// variable.  The view borrows the text.  Complexity: linear in the text.
[[nodiscard]] inline ::fixy::Option<::fixy::TextView> value_in(::fixy::TextView environment,
                                                               ::fixy::TextView name) noexcept {
    namespace topology = crucible::warden::detail;
    ::fixy::TextView rest = environment;
    while (rest.size() != 0) {
        std::size_t const end = ::fixy::find(rest, '\0').value_or(rest.size());
        ::fixy::TextView const variable = topology::text_prefix(rest, end);
        rest = topology::text_suffix(rest, end + 1);
        // The variable is the name, an equals sign, and the value.
        bool const is_named = ::fixy::starts_with(variable, name)
                           && ::fixy::starts_with(topology::text_suffix(variable, name.size()), ::fixy::TextView{"="});
        if (is_named) return ::fixy::some(topology::text_suffix(variable, name.size() + 1));
    }
    return ::fixy::none;
}

// The value of the variable name in the environment at the start, as
// value_in gives it.  The view lives as long as the process.
[[nodiscard]] inline ::fixy::Option<::fixy::TextView> start_value(::fixy::TextView name) noexcept {
    return value_in(start_environment().text.view(), name);
}

}  // namespace detail

// The value of the variable name in the environment at the start, or none
// when the variable is not set or empty, as the benches read an empty value
// as no value (the environment).
[[nodiscard]] inline ::fixy::Option<::fixy::TextView> env_value(::fixy::TextView name) noexcept {
    ::fixy::Option<::fixy::TextView> const value = detail::start_value(name);
    for (::fixy::TextView const text : value) {
        if (text.size() == 0) return ::fixy::none;
    }
    return value;
}

// ── Cores of a run ─────────────────────────────────────────────
//
// loop bench gives a bench the cores of its claim as the affinity of its
// process: one isolated core for each thread that the bench states with
// CORES in bench/CMakeLists.txt (the cores of a run of
// utils/assay/loop/bench.py).  The cores of a run are the isolated CPUs of
// that affinity.
//
// An affinity mask has no order, so loop bench also gives the cores in the
// order of its claim, in the variable CRUCIBLE_BENCH_CORE_ORDER, as
// "240,242,224".  A core with fewer lines in the record of the steal time
// comes first in that order (the steal time of utils/assay/loop/bench.py).
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
// the thread door can name.  A spread holds as many (the spread of a run).
inline constexpr std::size_t kSpreadCpus = ::fixy::AffinityMask::kBits;

// The cores of a claim in the order of loop bench (the cores of a run).
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
// empty order when the process has no such variable (the cores of a run).
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

// ── Spread of a run ────────────────────────────────────────────
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
    return ::fixy::erase_brand(::fixy::to(::fixy::topology::Topology::instance()));
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

// The core of the measuring thread of a fan-out (Run::fan_out): the core of
// slot 0 of the claim when each allowed CPU is isolated, a claim of loop
// bench, and none when an allowed CPU is not isolated, where the load
// balancer spreads the threads of the body.  None for an empty affinity.
// Complexity: linear in the CPUs of allowed, times the CPUs of order.
[[nodiscard]] inline CpuId fan_out_core(::fixy::Side<side::AllowedCpus, crucible::warden::CpuSet> const& allowed,
                                        ::fixy::Side<side::IsolatedCpus, crucible::warden::CpuSet> const& isolated,
                                        CoreOrder const& order) noexcept {
    for (int const cpu : allowed.value()) {
        if (!isolated.value().contains(cpu)) return CpuId::none();
    }
    return Cores::of(allowed, isolated, order).of_slot(kMeasuringSlot).value_or(CpuId::none());
}

}  // namespace detail

// Pins the calling thread to one core for the rest of its life, through the
// gated door of fixy/os/Sched.h (the cores of a run).  A worker that a body
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
//
// The constructors and the destructor are not inline.  They are setup work
// outside the timed loops.  Inline in the hot code of a caller, GCC 16 at -O3
// reported a read of the storage of an empty prior_ (-Wmaybe-uninitialized),
// although the flag of the Option keeps each read on the full arm.
class CoreHold {
public:
    [[gnu::noinline]] explicit CoreHold(CpuId core) noexcept {
        if (!core.is_valid()) return;
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        prior_ = ::fixy::some(::fixy::sched::apply_affinity_to_cpu(startup, core.raw())
                                  .expect("the pin of a thread to a core of the claim of its run"));
    }

    [[gnu::noinline]] explicit CoreHold(crucible::warden::CpuSet const& cores) noexcept {
        if (cores.is_empty()) return;
        const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
        prior_ = ::fixy::some(::fixy::sched::apply_affinity_to_mask(startup, detail::mask_of(cores))
                                  .expect("the pin of a thread to the cores of the claim of its run"));
    }

    [[gnu::noinline]] ~CoreHold() {
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
// its affinity (the cores of a run).  A thread that fn starts takes that core
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

// ── Pause check ────────────────────────────────────────────────
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
// The readings make no open and no listing between the clocks of the first
// reading and the clocks of the second (PauseFiles).  The measuring thread
// opens its status and its schedstat one time, and each reading reads the two
// files again from their start.  The first reading lists the other threads
// before its clocks, and the second reading lists them after its clocks.  The
// read-set tracer of `loop test-step` stops the caller of each open and of
// each listing until the launcher answers.  An open inside the window can
// give the measuring thread a voluntary switch that the body did not make,
// and an arrival for each thread of the process.  The check then judges the
// thread by its run delay, and the mean wait of its arrivals can fall below
// kWaitBoundNs.
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
// the pause and not valid, and its JSON gives paused 1.

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

// The pause of one Run (the pause check).
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
// loop, in nanoseconds (the windows).  is_read is false when a clock did not
// read.
struct WindowClock {
    std::uint64_t wall_ns = 0;
    std::uint64_t cpu_ns = 0;
    bool is_read = false;
    ::fixy::FixedArray<std::uint8_t, 7> pad_after_flag{};
};

namespace detail {

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

// The pause between two readings of one thread (the pause check).  The
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

// The run delay and the arrivals of the text of a schedstat file of procfs,
// which holds the run time, the run delay in nanoseconds and the arrivals on a
// CPU, or none when the text holds no such numbers.  The thread id of the
// result is zero.
[[nodiscard]] inline ::fixy::Option<ThreadDelay> schedstat_of_text(::fixy::TextView all) noexcept {
    namespace topology = crucible::warden::detail;
    ::fixy::TextView const delay_text = topology::text_suffix(all, ::fixy::find(all, ' ').value_or(all.size()) + 1);
    ::fixy::TextView const arrivals_text =
        topology::text_suffix(delay_text, ::fixy::find(delay_text, ' ').value_or(delay_text.size()) + 1);
    ::fixy::Option<std::uint64_t> delay = leading_number(delay_text);
    ::fixy::Option<std::uint64_t> arrivals = leading_number(arrivals_text);
    if (delay.is_none() || arrivals.is_none()) return ::fixy::none;
    return ::fixy::some(
        ThreadDelay{.run_delay_ns = std::move(delay).value_or(0), .arrivals = std::move(arrivals).value_or(0)});
}

// The run delay and the arrivals of the schedstat file of procfs at path, or
// none when the file does not read.  The thread id of the result is zero.
[[nodiscard]] inline ::fixy::Option<ThreadDelay> read_schedstat(::fixy::CText path) noexcept {
    ::fixy::FixedText<128> text{};
    if (::foundation::os::read_file(path, text).is_err()) return ::fixy::none;
    return schedstat_of_text(text.view());
}

// The frequency in Hz of the text of a scaling_cur_freq file of sysfs, which
// holds kHz: the first decimal number after the blanks that lead it, times
// 1000, or 0 for a text that starts with no digit.
[[nodiscard]] inline std::uint64_t freq_hz_of_text(::fixy::TextView text) noexcept {
    constexpr std::uint64_t kHzPerKhz = 1000;
    return ::fixy::saturating_mul(leading_number(text).value_or(0), kHzPerKhz);
}

// The frequency of a CPU in Hz: scaling_cur_freq of its cpufreq directory in
// sysfs, or 0 when the CPU is none or the file does not read.  Complexity: one
// read of sysfs.
[[nodiscard]] inline std::uint64_t read_cpu_freq_hz(CpuId cpu) noexcept {
    if (!cpu.is_valid()) return 0;
    ::foundation::os::PathText<96> path{};
    const bool is_built = path.append(::fixy::TextView{"/sys/devices/system/cpu/cpu"}).is_ok()
                       && path.append_decimal(static_cast<std::uint64_t>(cpu.raw())).is_ok()
                       && path.append(::fixy::TextView{"/cpufreq/scaling_cur_freq"}).is_ok();
    ::fixy::FixedText<32> text{};
    if (!is_built || ::foundation::os::read_file(path.c_text(), text).is_err()) return 0;
    return freq_hz_of_text(text.view());
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

// Reads the two clocks of the calling thread for the check of a window (the
// windows): two reads of a clock and no read of procfs, so the check makes no
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

// A file of procfs that the measuring thread holds open for the readings of
// the pause check.
using ProcFile = ::fixy::fs::FileOf<::fixy::fs::read_only>;

// The two files of procfs of the measuring thread that each reading of the
// pause check reads (the pause check): its status, with the count of its
// voluntary switches, and its schedstat, with its run delay and its arrivals.
// The thread opens them one time before the first reading, and each reading
// reads them again from their start.  A file that did not open is an empty
// handle, and a reading of it is not measured.
struct PauseFiles {
    ProcFile status{};
    ProcFile schedstat{};
};

// Opens the file of procfs at path for a read, or gives an empty handle when
// the path or the open fails.
[[nodiscard]] inline ProcFile open_proc_file(::fixy::TextView path) noexcept {
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    ::fixy::Result<::fixy::Path<::fixy::tags::source::Sanitized>, ::fixy::PathTraversalError> sanitized =
        ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(path));
    if (sanitized.is_err()) return ProcFile{};
    return ::fixy::fs::mint_file<::fixy::fs::read_only>(startup,
                                                        std::move(sanitized).expect("the test above found the path"))
        .match([](::fixy::Linear<ProcFile> opened) noexcept -> ProcFile { return std::move(opened).consume(); },
               [](::foundation::os::Errno) noexcept -> ProcFile { return ProcFile{}; });
}

// Opens the status and the schedstat of the calling thread for the readings
// of the pause check.  The calling thread is the measuring thread, because
// /proc/thread-self names the thread that opens it.
[[nodiscard, gnu::noinline]] inline PauseFiles open_pause_files() noexcept {
    return PauseFiles{.status = open_proc_file("/proc/thread-self/status"),
                      .schedstat = open_proc_file("/proc/thread-self/schedstat")};
}

// Reads a file of procfs that the caller holds open again, from its start,
// into text.  procfs makes the text at each read, so the text holds the
// values of the time of the read.  The read is no open, so the read-set
// tracer of `loop test-step` does not stop it.  A failed read leaves no
// character in text.  Complexity: linear in the size of the file.
template <std::size_t Capacity>
[[nodiscard]] ::fixy::Result<::fixy::Unit, ::foundation::os::Errno>
read_proc_file_again(ProcFile const& file, ::fixy::FixedText<Capacity>& text) noexcept {
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    text = ::fixy::FixedText<Capacity>{};
    auto [bytes_read, bytes] = ::fixy::build_fixed_array<std::byte, Capacity>([&startup, &file](auto run) noexcept {
        return ::fixy::fs::read_full_at(startup, file.ref(), std::move(run), ::fixy::fs::FileOffset{0});
    });
    CRUCIBLE_TRY(count, std::move(bytes_read));
    for (std::byte const byte : bytes.view().window(0, count).expect("a read gives at most the size of its buffer")) {
        if (text.append(::fixy::bit_cast<char>(byte)).is_err()) return ::fixy::err(::foundation::os::Errno::efbig);
    }
    return ::fixy::Unit{};
}

// The texts of the two files of the measuring thread at one reading.
struct OwnTexts {
    ::fixy::FixedText<4096> status{};
    ::fixy::FixedText<128> schedstat{};
};

// Reads the two files of the measuring thread again into texts, and gives the
// error of the first read that failed.
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> read_own_files(PauseFiles const& files,
                                                                                          OwnTexts& texts) noexcept {
    CRUCIBLE_TRY(_, read_proc_file_again(files.status, texts.status));
    return read_proc_file_again(files.schedstat, texts.schedstat);
}

// The facts of the measuring thread at one reading, from the texts of its two
// files and from its two clocks, into reading.  A text without its numbers or
// a clock that did not read leaves the basis NotMeasured.
inline void fill_own_pause(OwnTexts const& texts, WindowClock const& clock, PauseReading& reading) noexcept {
    ::fixy::Option<std::uint64_t> switches =
        leading_number(value_of_line(texts.status.view(), "voluntary_ctxt_switches"));
    ::fixy::Option<ThreadDelay> own = schedstat_of_text(texts.schedstat.view());
    if (switches.is_none() || own.is_none() || !clock.is_read) return;
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    reading.wall_ns = clock.wall_ns;
    reading.cpu_ns = clock.cpu_ns;
    reading.voluntary_switches = std::move(switches).value_or(0);
    reading.own = std::move(own).value_or(ThreadDelay{});
    reading.own.tid = ::fixy::process::current_thread(startup);
    reading.basis = PauseBasis::LostTime;
}

// The first reading of the pause check (the pause check): the run delay of
// each thread of the process, then the files and the clocks of the measuring
// thread.  The listing and its opens then come before the window.  The reads
// cost a listing of /proc/self/task and some reads of procfs, outside the
// timed regions.
[[nodiscard, gnu::noinline]] inline ::fixy::Side<side::ReadingBefore, PauseReading>
read_pause_before(PauseFiles const& files) noexcept {
    PauseReading reading{};
    read_thread_delays(reading.others);
    OwnTexts texts{};
    const bool is_read = read_own_files(files, texts).is_ok();
    WindowClock const clock = read_window_clock();
    if (is_read) fill_own_pause(texts, clock, reading);
    return ::fixy::side_as<side::ReadingBefore>(std::move(reading));
}

// The second reading of the pause check (the pause check): the clocks and the
// files of the measuring thread, then the run delay of each thread of the
// process.  The listing and its opens then come after the window.
[[nodiscard, gnu::noinline]] inline ::fixy::Side<side::ReadingAfter, PauseReading>
read_pause_after(PauseFiles const& files) noexcept {
    PauseReading reading{};
    WindowClock const clock = read_window_clock();
    OwnTexts texts{};
    const bool is_read = read_own_files(files, texts).is_ok();
    read_thread_delays(reading.others);
    if (is_read) fill_own_pause(texts, clock, reading);
    return ::fixy::side_as<side::ReadingAfter>(std::move(reading));
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
// each other basis (the windows).  A thread that made no voluntary switch in
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
// that no two of them share a core (the cores of a run).  A thread that is
// alone on its core makes an involuntary switch only for a kernel thread of
// that core.  It is noipa, as each door that prints (the print doors of
// print()).  Complexity: one listing of /proc/self/task and two reads for
// each thread.
[[gnu::noipa]] inline void print_thread_cpus(::fixy::TextView label) noexcept {
    using ::fixy::Sink;
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

namespace detail {

// The CPU on which the calling thread runs, or none when the kernel gives no
// CPU: the one read of the current CPU in the harness, through the door of
// fixy/os/Sched.h.
[[nodiscard]] inline CpuId calling_cpu() noexcept {
    const ::fixy::InitLoadCtx startup{::foundation::effects::testing::init()};
    return ::fixy::sched::read_current_cpu(startup).match(
        [](::fixy::sched::CpuIndex cpu) noexcept { return CpuId{::fixy::narrow<int>(cpu.raw()).value_or(-1)}; },
        []() noexcept { return CpuId::none(); });
}

}  // namespace detail

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
        const CpuId cpu = detail::calling_cpu();
        const std::size_t position = !cpu.is_valid() || static_cast<std::size_t>(cpu.raw()) >= kSpreadCpus
                                       ? kSpreadCpus - 1
                                       : static_cast<std::size_t>(cpu.raw());
        // Each count is an atomic counter, which each thread of the bench
        // steps at the same time.
        counts_.with_element(::fixy::Index<kSpreadCpus>::check(position).expect("the test above bounds the CPU"),
                             [](Counter& counter) noexcept { static_cast<void>(counter.fetch_add_relaxed(1)); });
    }

    // Prints each CPU with a note, and its count of notes, on one line after
    // the label.  Call it after the threads that noted joined.  It is noipa,
    // as each door that prints (the print doors of print()).
    [[gnu::noipa]] void print(::fixy::TextView label) const noexcept {
        using ::fixy::Sink;
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
// senses_instance() gives a Ref, which is never null (Senses is a value).
// A build with no BPF has no senses, and no code of its arm reads them.

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
namespace detail {

[[nodiscard]] inline ::fixy::Ref<::crucible::perf::Senses const> senses_instance() noexcept {
    // Senses::load_subset(startup load context, mask) loads only the
    // masked facades, which are sense_hub and sched_switch.  The bench
    // takes the startup load context from the test door.  Hot-path code
    // holds no init context, so it cannot make the argument, and the
    // call chain of Senses::load_subset() never reaches a hot frame.
    // Senses is move-only, and the static is built from the returned
    // value.
    static ::crucible::perf::Senses const slot = ::crucible::perf::Senses::load_subset(
        ::fixy::InitLoadCtx{::foundation::effects::testing::init()}, ::crucible::perf::SensesMask{
                                                                         .sense_hub = true,
                                                                         .sched_switch = true,
                                                                     });
    return ::fixy::erase_brand(::fixy::to(slot));
}

// The longest off-CPU spikes of the window of a run (Report::sched_offcpu).
inline constexpr std::size_t kOffCpuTopK = 5;

// One off-CPU spike of the timeline of SchedSwitch: the time off the CPU in
// nanoseconds, and the CPU on which the thread came back.
struct OffCpuSpike {
    std::uint64_t off_ns = 0;
    std::uint32_t cpu = 0;
    std::uint32_t pad_after_cpu = 0;
};

// The kOffCpuTopK longest spikes of a window, longest first, and the count of
// the events of the window.  The spikes from 0 to kept are in use.
struct OffCpuTop {
    ::fixy::FixedArray<OffCpuSpike, kOffCpuTopK> longest{};
    std::uint32_t kept = 0;
    std::uint32_t count = 0;

    // Counts the event, and keeps its spike when it is among the
    // kOffCpuTopK longest of the window so far.  Complexity: at most
    // kOffCpuTopK moves.
    void keep(OffCpuSpike spike) noexcept {
        ++count;
        if (kept == kOffCpuTopK && spike.off_ns <= at_(kOffCpuTopK - 1).off_ns) return;
        std::size_t place = kept < kOffCpuTopK ? kept : kOffCpuTopK - 1;
        if (kept < kOffCpuTopK) ++kept;
        while (place > 0 && at_(place - 1).off_ns < spike.off_ns) {
            put_(place, at_(place - 1));
            --place;
        }
        put_(place, spike);
    }

    // The spike at a place below kOffCpuTopK.
    [[nodiscard]] OffCpuSpike const& at_(std::size_t place) const noexcept {
        return longest.at(::fixy::Index<kOffCpuTopK>::check(place).expect("a place of the top set is below its size"));
    }

    // Writes the spike at a place below kOffCpuTopK.
    void put_(std::size_t place, OffCpuSpike spike) noexcept {
        longest.set(::fixy::Index<kOffCpuTopK>::check(place).expect("a place of the top set is below its size"), spike);
    }
};

}  // namespace detail

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the BPF program of SchedSwitch writes the timeline that the harness reads")
namespace detail {

// The longest off-CPU spikes of the events from pre to post of the timeline
// of SchedSwitch, at most its capacity, newest first, so a window that the
// ring wrapped reads the newest events.  The view is a SharedView, and each
// event is the copy of the record of one ticket (the ring ticket of
// fixy/os/OwnedMmap.h), so each field is an atomic load.  An event that is not
// whole, whose write dropped, or that the ring wrote over does not count.  The
// window of the fixed extent of the ring checks the count of the view one time,
// so a read of a ticket takes no check.  A view of fewer slots than the ring
// gives no spike.  Complexity: linear in the events of the window.
[[nodiscard]] inline OffCpuTop top_offcpu_of(::fixy::mmap::SharedView<const ::crucible::perf::TimelineSchedEvent> view,
                                             ::fixy::Side<side::ReadingBefore, std::uint64_t> pre,
                                             ::fixy::Side<side::ReadingAfter, std::uint64_t> post) noexcept {
    using Event = ::crucible::perf::TimelineSchedEvent;
    OffCpuTop top{};
    if (post.value() <= pre.value()) return top;
    const uint64_t window_size =
        ::fixy::min(post.value() - pre.value(), static_cast<uint64_t>(::crucible::perf::TIMELINE_CAPACITY));
    for (auto const ring : view.window<::crucible::perf::TIMELINE_CAPACITY>(0)) {
        for (uint64_t i = 0; i < window_size; ++i) {
            const ::fixy::mmap::RingTicket ticket = ::fixy::mmap::RingTicket::mask(post.value() - 1 - i);
            for (Event const event : ring.template read_ticket<&Event::commit>(ticket)) {
                top.keep(OffCpuSpike{.off_ns = event.off_cpu_ns, .cpu = event.on_cpu});
            }
        }
    }
    return top;
}

}  // namespace detail
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
#endif

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
            p.stddev = ::fixy::fp::sqrt(::fixy::max(0.0, var));
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
    // The mint builds the estimates in the order of the resamples, so the draws keep their order.
    ::fixy::HeapArray<double> estimates =
        ::fixy::mint_heap_array_with<double>(alloc, B, [&alloc, &pick, n, frac](std::size_t) noexcept {
            const ::fixy::HeapArray<double> resample =
                sorted_samples(::fixy::mint_heap_array_with<double>(alloc, n, pick));
            return percentile_interp(::fixy::View<const double>{resample.view()}, frac);
        });
    estimates = sorted_samples(std::move(estimates));
    const ::fixy::View<const double> sorted{estimates.view()};
    return {
        percentile_interp(sorted, alpha / 2.0),
        percentile_interp(sorted, 1.0 - alpha / 2.0),
    };
}

// ── Windows ────────────────────────────────────────────────────
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
// (the pause check).  paused_windows counts the windows over the bound.  The
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

// The values of the windows of one run, summed up (the windows).  The values
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

// The median and the spread of the medians of the windows of one run (the
// windows).  The median is the R type 7 median of percentile_interp: the
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

// The longest name of a Run, of a Report and of a side of a Compare, in
// bytes.  The longest name of a bench of the tree holds 55 bytes.
inline constexpr std::size_t kNameBytes = 128;

// The name of a Run, of a Report and of a side of a Compare.  It owns its
// bytes, so a report keeps its name after the text that a bench formatted it
// from ends.
using RunName = ::fixy::FixedText<kNameBytes>;

// The name of a text.  A text longer than kNameBytes ends the process, because
// a cut name would name a different row.  Complexity: one copy of the text.
[[nodiscard]] inline RunName name_of(::fixy::TextView text) noexcept {
    RunName name{};
    if (name.append(text).is_err()) {
        ::fixy::fatal("the name of a bench row holds {} bytes, and a name holds at most {}: '{}'", text.size(),
                      kNameBytes, text);
    }
    return name;
}

// The name of a row that a bench formats from its parts, such as a count of
// threads, with the text and the arguments of the Report family.  A text
// longer than kNameBytes ends the process, as for name_of.
template <class... Args>
    requires(::fixy::Formattable<Args> && ...)
[[nodiscard]] RunName format_name(::fixy::Fmt<std::type_identity_t<Args>...> const& fmt, Args... args) noexcept {
    RunName name{};
    if (::fixy::format(name, fmt, args...).is_err()) {
        ::fixy::fatal("the name of a bench row holds more than {} bytes: '{}'", kNameBytes, name.view());
    }
    return name;
}

// Writes a line of a bench, such as its header, to standard output, with the
// text and the arguments of the Report family.  A bench writes each of its
// lines through print(), and an error line through report() to Sink::Err.
//
// Print doors
//   report() is a cold function.  GCC takes each block after a call of a cold
//   function in main for code that runs rarely, so a report() before the rows
//   moves the rows to cold code: GCC optimizes them for size, puts them in
//   .text.unlikely, and calls the body of each row in place of an inline copy.
//   A function whose body is only a call of report() is cold for GCC too: its
//   blocks all call a cold function, so GCC marks it as unlikely executed, and
//   a call of it is a cold call in its caller.  So print() and each other door
//   of the harness that prints is noipa.  Its caller then knows nothing of its
//   body, as of a function of a different unit, and a call of it can end its
//   block, so GCC does not take the blocks after it for cold code.  The rows
//   after such a call stay in the hot code of main.  The scan of the bench
//   binaries refuses a timed body in cold code (test/bench_windows/
//   check_windows.py, the placement), and its probe holds a door of the cold
//   form as its control.
template <class... Args>
    requires(::fixy::Formattable<Args> && ...)
[[gnu::noipa]] void print(::fixy::Fmt<std::type_identity_t<Args>...> const& fmt, Args... args) noexcept {
    ::fixy::report(::fixy::Sink::Out, fmt, args...);
}

namespace detail {

// The text of a time in nanoseconds for the columns of a report: the value
// with two decimals in ns, µs, ms or s, the largest unit in which the value is
// at least 1, and the unit after it, as "12.35ns" or "1.00µs".  A value whose
// digits do not fit 32 bytes, at least 10^29 s, writes nan before the unit.
// µs is two bytes of UTF-8, so a column of 9 bytes holds "999.99µs".
using NsText = ::fixy::FixedText<40>;

[[nodiscard]] inline NsText ns_text(double ns) noexcept {
    ::fixy::TextView unit{"ns"};
    double scaled = ns;
    if (ns >= 1e9) {
        unit = ::fixy::TextView{"s"};
        scaled = ns / 1e9;
    } else if (ns >= 1e6) {
        unit = ::fixy::TextView{"ms"};
        scaled = ns / 1e6;
    } else if (ns >= 1e3) {
        unit = ::fixy::TextView{"µs"};
        scaled = ns / 1e3;
    }
    ::fixy::FixedText<32> digits{};
    const bool is_cut = ::fixy::format(digits, "{}", ::fixy::Fixed<2>{scaled}).is_err();
    NsText text{};
    static_cast<void>(::fixy::format(text, "{}{}", is_cut ? ::fixy::TextView{"nan"} : digits.view(), unit));
    return text;
}

// The JSON text of a name: the characters between two quotes, with each
// character that a JSON parser refuses in a string written as its escape of
// RFC 8259.  A name holds at most kNameBytes characters, and an escape at
// most 6, so the text holds each name.
using JsonName = ::fixy::FixedText<2 + 6 * kNameBytes>;

[[nodiscard]] inline JsonName json_name_of(::fixy::TextView name) noexcept {
    JsonName text{};
    const auto put = [&text](::fixy::TextView part) noexcept {
        if (text.append(part).is_err()) ::fixy::fatal("the JSON text of a name holds each escaped name");
    };
    put(::fixy::TextView{"\""});
    for (char const character : name) {
        const auto code = static_cast<unsigned char>(character);
        switch (code) {
            case '"':
                put(::fixy::TextView{"\\\""});
                break;
            case '\\':
                put(::fixy::TextView{"\\\\"});
                break;
            case '\b':
                put(::fixy::TextView{"\\b"});
                break;
            case '\f':
                put(::fixy::TextView{"\\f"});
                break;
            case '\n':
                put(::fixy::TextView{"\\n"});
                break;
            case '\r':
                put(::fixy::TextView{"\\r"});
                break;
            case '\t':
                put(::fixy::TextView{"\\t"});
                break;
            default:
                if (code < 0x20) {
                    ::fixy::FixedText<8> escape{};
                    static_cast<void>(::fixy::format(escape, "\\u{}", ::fixy::Hex<4>{std::uint16_t{code}}));
                    put(escape.view());
                } else if (text.append(character).is_err()) {
                    ::fixy::fatal("the JSON text of a name holds each escaped name");
                }
        }
    }
    put(::fixy::TextView{"\""});
    return text;
}

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
// The unit of a counter of the sense hub in the BPF line of a report.
enum class BpfUnit : std::uint8_t {
    Count,
    Ns,
    Bytes
};

// One counter of the BPF line and of the BPF part of the JSON of a report:
// its label, its slot of the snapshot, and its unit.
struct BpfField {
    ::fixy::TextView label{};
    ::crucible::perf::Idx idx{};
    BpfUnit unit = BpfUnit::Count;
};

// Every counter whose (post - pre) is meaningful. Order = the story
// the reader walks: first what interrupted us, then what the bench
// touched (memory → sync → I/O → thread mgmt), finally reliability.
inline constexpr ::fixy::FixedArray<BpfField, 54> kBpfFields{
    // Scheduling interference.
    BpfField{"preempt", ::crucible::perf::SCHED_CTX_INVOL, BpfUnit::Count},
    BpfField{"yield", ::crucible::perf::SCHED_CTX_VOL, BpfUnit::Count},
    BpfField{"migrate", ::crucible::perf::SCHED_MIGRATIONS, BpfUnit::Count},
    BpfField{"runtime", ::crucible::perf::SCHED_RUNTIME_NS, BpfUnit::Ns},
    BpfField{"wait", ::crucible::perf::SCHED_WAIT_NS, BpfUnit::Ns},
    BpfField{"sleep", ::crucible::perf::SCHED_SLEEP_NS, BpfUnit::Ns},
    BpfField{"iowait", ::crucible::perf::SCHED_IOWAIT_NS, BpfUnit::Ns},
    BpfField{"blocked", ::crucible::perf::SCHED_BLOCKED_NS, BpfUnit::Ns},
    BpfField{"softirq", ::crucible::perf::SOFTIRQ_STOLEN_NS, BpfUnit::Ns},
    BpfField{"wake_rx", ::crucible::perf::WAKEUPS_RECEIVED, BpfUnit::Count},
    BpfField{"wake_tx", ::crucible::perf::WAKEUPS_SENT, BpfUnit::Count},
    BpfField{"freq_chg", ::crucible::perf::CPU_FREQ_CHANGES, BpfUnit::Count},
    BpfField{"tid_new", ::crucible::perf::THREADS_CREATED, BpfUnit::Count},
    BpfField{"tid_end", ::crucible::perf::THREADS_EXITED, BpfUnit::Count},

    // Memory.
    BpfField{"pgfault", ::crucible::perf::MEM_PAGE_FAULTS_MIN, BpfUnit::Count},
    BpfField{"majfault", ::crucible::perf::MEM_PAGE_FAULTS_MAJ, BpfUnit::Count},
    BpfField{"mmap", ::crucible::perf::MEM_MMAP_COUNT, BpfUnit::Count},
    BpfField{"munmap", ::crucible::perf::MEM_MUNMAP_COUNT, BpfUnit::Count},
    BpfField{"brk", ::crucible::perf::MEM_BRK_CALLS, BpfUnit::Count},
    BpfField{"reclaim_n", ::crucible::perf::DIRECT_RECLAIM_COUNT, BpfUnit::Count},
    BpfField{"reclaim_t", ::crucible::perf::DIRECT_RECLAIM_NS, BpfUnit::Ns},
    BpfField{"swap_out", ::crucible::perf::SWAP_OUT_PAGES, BpfUnit::Count},
    BpfField{"thp_ok", ::crucible::perf::THP_COLLAPSE_OK, BpfUnit::Count},
    BpfField{"thp_fail", ::crucible::perf::THP_COLLAPSE_FAIL, BpfUnit::Count},
    BpfField{"numa", ::crucible::perf::NUMA_MIGRATE_PAGES, BpfUnit::Count},
    BpfField{"compact", ::crucible::perf::COMPACTION_STALLS, BpfUnit::Count},
    BpfField{"extfrag", ::crucible::perf::EXTFRAG_EVENTS, BpfUnit::Count},

    // Sync contention.
    BpfField{"futex", ::crucible::perf::FUTEX_WAIT_COUNT, BpfUnit::Count},
    BpfField{"futex_t", ::crucible::perf::FUTEX_WAIT_NS, BpfUnit::Ns},
    BpfField{"klock", ::crucible::perf::KERNEL_LOCK_COUNT, BpfUnit::Count},
    BpfField{"klock_t", ::crucible::perf::KERNEL_LOCK_NS, BpfUnit::Ns},

    // I/O (syscall-level).
    BpfField{"read", ::crucible::perf::IO_READ_BYTES, BpfUnit::Bytes},
    BpfField{"write", ::crucible::perf::IO_WRITE_BYTES, BpfUnit::Bytes},
    BpfField{"r_ops", ::crucible::perf::IO_READ_OPS, BpfUnit::Count},
    BpfField{"w_ops", ::crucible::perf::IO_WRITE_OPS, BpfUnit::Count},
    BpfField{"fd_open", ::crucible::perf::FD_OPEN_OPS, BpfUnit::Count},

    // Block I/O (device-level).
    BpfField{"disk_r", ::crucible::perf::DISK_READ_BYTES, BpfUnit::Bytes},
    BpfField{"disk_w", ::crucible::perf::DISK_WRITE_BYTES, BpfUnit::Bytes},
    BpfField{"disk_t", ::crucible::perf::DISK_IO_LATENCY_NS, BpfUnit::Ns},
    BpfField{"disk_n", ::crucible::perf::DISK_IO_COUNT, BpfUnit::Count},
    BpfField{"pg_miss", ::crucible::perf::PAGE_CACHE_MISSES, BpfUnit::Count},
    BpfField{"readahead", ::crucible::perf::READAHEAD_PAGES, BpfUnit::Count},
    BpfField{"unplug", ::crucible::perf::IO_UNPLUG_COUNT, BpfUnit::Count},
    BpfField{"throttle", ::crucible::perf::WRITE_THROTTLE_JIFFIES, BpfUnit::Count},

    // Network.
    BpfField{"tx", ::crucible::perf::NET_TX_BYTES, BpfUnit::Bytes},
    BpfField{"rx", ::crucible::perf::NET_RX_BYTES, BpfUnit::Bytes},
    BpfField{"retrans", ::crucible::perf::TCP_RETRANSMIT_COUNT, BpfUnit::Count},
    BpfField{"rst", ::crucible::perf::TCP_RST_SENT, BpfUnit::Count},
    BpfField{"sk_err", ::crucible::perf::TCP_ERROR_COUNT, BpfUnit::Count},
    BpfField{"skb_drop", ::crucible::perf::SKB_DROP_COUNT, BpfUnit::Count},
    BpfField{"cng_loss", ::crucible::perf::TCP_CONG_LOSS, BpfUnit::Count},

    // Reliability (delta-meaningful signals only).
    BpfField{"sig_fatal", ::crucible::perf::SIGNAL_FATAL_COUNT, BpfUnit::Count},
    BpfField{"oom_kills", ::crucible::perf::OOM_KILLS_SYSTEM, BpfUnit::Count},
    BpfField{"mce", ::crucible::perf::MCE_COUNT, BpfUnit::Count},
};
#endif

}  // namespace detail

// Cycle floor
//
// A row whose median call takes less than kCycleFloor cycles of the TSC is
// Not valid.  A body that does one load in each call takes one cycle a call,
// so a floor of one cycle marks correct rows.  A loop that lost the work of
// its body holds only its counter, and it takes one cycle a call too, so the
// time cannot tell the two apart.  Only an unrolled loop of the counter goes
// below one cycle, such as two calls in each cycle.  So the floor catches
// that case, and it keeps a margin for a core that runs a little faster than
// its TSC.  The scan of the timed windows (test/bench_windows) catches the
// loop that is not unrolled.
inline constexpr double kCycleFloor = 0.75;

// A report owns each of its parts: the name and the samples own their bytes,
// and each other part is a plain value.  So a report moves to another
// thread, and it states sendable (foundation/Transfer.h), which the pointer
// of the samples hides.
struct[[= ::foundation::transfer::sendable{}]] Report {
    RunName name{};
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

    // The cycles of the TSC of the median call, or 0 when the TSC has no
    // calibration (the cycle floor).
    double p50_cycles = 0;

    // The time that the threads of the run lost over the sampled loop (the
    // pause check).  A pause over kPauseBoundNs makes the run not valid.
    Pause pause{};

    // The medians of the windows of the sampled loop, how the windows were
    // judged, and the count of the windows whose own pause is over
    // kPauseBoundNs (the windows).  window_basis is LostTime when the windows
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
    uint64_t sched_pre_idx = 0;
    uint64_t sched_post_idx = 0;
    detail::OffCpuTop sched_offcpu{};
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
    // surfaces the "not enough samples" case as no value.
    //
    // Marked noexcept: bootstrap_ci allocates std::vectors, but OOM in
    // this project aborts() per the MemSafe axiom, so throwing here is
    // structurally impossible and std::terminate via the noexcept barrier
    // is the desired outcome if an allocator ever did throw.
    [[nodiscard]] CI ci(double frac, size_t B = 1000) const noexcept {
        return bootstrap_ci(::fixy::View<const double>{samples.view()}, frac, B);
    }

    // Same as ci() but distinguishes "insufficient samples" (none) from a
    // computed interval.  A CI with lo == hi == 0 from 30 or more samples is
    // still a value.
    [[nodiscard]] ::fixy::Option<CI> ci_opt(double frac, size_t B = 1000) const {
        if (samples.size() < 30) return ::fixy::none;
        return ::fixy::some(bootstrap_ci(::fixy::View<const double>{samples.view()}, frac, B));
    }

    [[nodiscard]] bool noisy(double cv_threshold = 0.05) const noexcept { return pct.cv > cv_threshold; }

    // True when the median call takes less than kCycleFloor cycles of the
    // TSC, so the row is not valid (the cycle floor).
    [[nodiscard]] bool is_below_cycle_floor() const noexcept { return p50_cycles > 0.0 && p50_cycles < kCycleFloor; }

    // Writes the summary line of the report to standard output, then the
    // pause line when the pause is over its bound, then the BPF line.  The
    // columns of the times are 9 bytes wide and the column of max 10 bytes,
    // and µs is two bytes of UTF-8, so a column of µs holds one character
    // less.  It is noipa, so its report() calls stay out of a caller that
    // measures more rows after it (the print doors of print()).
    [[gnu::noipa]] void print_text() const noexcept {
        using ::fixy::Align;
        using ::fixy::Fixed;
        using ::fixy::Padded;
        using ::fixy::Sink;
        const detail::NsText p50_text = detail::ns_text(pct.p50);
        const detail::NsText p90_text = detail::ns_text(pct.p90);
        const detail::NsText p99_text = detail::ns_text(pct.p99);
        const detail::NsText p999_text = detail::ns_text(pct.p99_9);
        const detail::NsText max_text = detail::ns_text(pct.max);
        const detail::NsText mean_text = detail::ns_text(pct.mean);
        const detail::NsText sigma_text = detail::ns_text(pct.stddev);
        ::fixy::report(Sink::Out, "  {}  p50={}  p90={}  p99={}  p99.9={}  max={}  μ={}  σ={}  cv={}%",
                       Padded<38, Align::Left>{name.view()}, Padded<9>{p50_text.view()}, Padded<9>{p90_text.view()},
                       Padded<9>{p99_text.view()}, Padded<9>{p999_text.view()}, Padded<10>{max_text.view()},
                       Padded<9>{mean_text.view()}, Padded<9>{sigma_text.view()}, Fixed<1, 4>{pct.cv * 100.0});
        if (windows.windows > 0) print_windows_();

        if (cycles_per_op > 0.0) {
            ::fixy::report(Sink::Out, "  cyc={}", Fixed<1, 7>{cycles_per_op});
        }
        if (freq_start_hz > 0) {
            ::fixy::report(Sink::Out, "  @{}GHz", Fixed<2>{static_cast<double>(freq_start_hz) / 1e9});
        }
        ::fixy::report(Sink::Out, "  n={}", pct.n);
        if (pinned_cpu_count > 1) {
            ::fixy::report(Sink::Out, "  cpus={}", pinned_cpu_count);
        } else if (pinned_cpu.is_valid()) {
            ::fixy::report(Sink::Out, "  cpu{}", pinned_cpu.raw());
        }
        if (batch > 1) ::fixy::report(Sink::Out, "  [batch-avg]");
        if (noisy()) ::fixy::report(Sink::Out, "  [noisy]");
        if (drift_flag) ::fixy::report(Sink::Out, "  [drift]");
        if (freq_drift_flag) ::fixy::report(Sink::Out, "  [freq-drift]");
        if (is_below_cycle_floor()) {
            ::fixy::report(Sink::Out, "  [NOT VALID: {} cycle a call, below {}]", Fixed<2>{p50_cycles},
                           Fixed<2>{kCycleFloor});
        }
        ::fixy::report(Sink::Out, "\n");
        if (pause.is_over_bound()) print_pause_();

#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        // One-line sensory summary. When BPF is attached and every
        // interesting counter is zero we still emit "└─ sched clean"
        // as confirmation that senses are attached (so the reader can
        // trust the silence). When any counter fires, only the fields
        // that actually have a delta are printed.
        print_bpf_summary_();
#endif
    }

    // Writes the JSON object of the report to standard output: the name as
    // a JSON string (detail::json_name_of), each percentile and time with
    // three decimals, the variation and the spread with five, the wall time
    // with none, each count and each flag as an integer, and the BPF counters
    // when BPF is attached.  It is noipa, as print_text() is.
    [[gnu::noipa]] void print_json() const noexcept {
        using ::fixy::Fixed;
        using ::fixy::Sink;
        const detail::JsonName json_name = detail::json_name_of(name.view());
        ::fixy::report(Sink::Out, "{{\"name\":{},\"batch\":{},\"n\":{},\"cpu\":{},\"cpu_count\":{},", json_name.view(),
                       batch, pct.n, pinned_cpu.raw(), pinned_cpu_count);
        ::fixy::report(Sink::Out,
                       "\"p50\":{},\"p75\":{},\"p90\":{},\"p95\":{},\"p99\":{},\"p99_9\":{},\"p99_99\":{},"
                       "\"min\":{},\"max\":{},\"mean\":{},\"stddev\":{},",
                       Fixed<3>{pct.p50}, Fixed<3>{pct.p75}, Fixed<3>{pct.p90}, Fixed<3>{pct.p95}, Fixed<3>{pct.p99},
                       Fixed<3>{pct.p99_9}, Fixed<3>{pct.p99_99}, Fixed<3>{pct.min}, Fixed<3>{pct.max},
                       Fixed<3>{pct.mean}, Fixed<3>{pct.stddev});
        ::fixy::report(Sink::Out,
                       "\"cv\":{},\"wall_ns\":{},\"drift_pct\":{},\"cycles_per_op\":{},\"freq_start_hz\":{},"
                       "\"freq_end_hz\":{},",
                       Fixed<5>{pct.cv}, Fixed<0>{wall_ns}, Fixed<3>{drift_pct}, Fixed<3>{cycles_per_op}, freq_start_hz,
                       freq_end_hz);
        ::fixy::report(Sink::Out,
                       "\"pause_ns\":{},\"thread_delay_ns\":{},\"thread_arrivals\":{},\"paused\":{},\"windows\":{},"
                       "\"window_p50_median\":{},\"window_spread\":{},\"windows_judged\":{},\"paused_windows\":{},"
                       "\"p50_cycles\":{},\"below_cycle_floor\":{}",
                       pause.own_ns(), pause.other.run_delay_ns, pause.other.arrivals, pause.is_over_bound() ? 1 : 0,
                       windows.windows, Fixed<3>{windows.median}, Fixed<5>{windows.spread},
                       window_basis == PauseBasis::LostTime ? 1 : 0, paused_windows, Fixed<3>{p50_cycles},
                       is_below_cycle_floor() ? 1 : 0);
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        print_bpf_json_();
#endif
        ::fixy::report(Sink::Out, "}}");
    }

private:
    // Prints the windows of the run inside its line, to standard output, which
    // each caller of print_text gives (the windows): their count, the median of
    // their p50 with two decimals in the unit of the other columns, their
    // spread in percent with one decimal, and the count of the windows over the
    // bound of the pause check.
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
        ::fixy::report(Sink::Out, "  win={} w50={}.{}{}{} spread={}.{}%", windows.windows, hundredths / 100,
                       hundredths % 100 / 10, hundredths % 10, unit, tenths / 10, tenths % 10);
        if (paused_windows > 0) ::fixy::report(Sink::Out, "  [paused windows {}]", paused_windows);
    }

    // Prints the pause of a run over kPauseBoundNs on a line of its own after
    // the row, to standard output, which each caller of print_text gives (the
    // pause check).  The times are in microseconds.
    void print_pause_() const noexcept {
        using ::fixy::Sink;
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
    //
    // The labels and the units of the counters are detail::kBpfFields.

    // Writes a counter in the unit of its field, with one decimal above the
    // first step of its scale, as "1.2G", "4.3ms" or "12KB".
    static void print_scaled_(uint64_t v, detail::BpfUnit u) noexcept {
        using ::fixy::Fixed;
        using ::fixy::Sink;
        const auto value = static_cast<double>(v);
        switch (u) {
            case detail::BpfUnit::Count:
                if (v >= 1000000000)
                    ::fixy::report(Sink::Out, "{}G", Fixed<1>{value / 1e9});
                else if (v >= 1000000)
                    ::fixy::report(Sink::Out, "{}M", Fixed<1>{value / 1e6});
                else if (v >= 10000)
                    ::fixy::report(Sink::Out, "{}k", Fixed<1>{value / 1e3});
                else
                    ::fixy::report(Sink::Out, "{}", v);
                break;
            case detail::BpfUnit::Ns:
                if (v >= 1000000000)
                    ::fixy::report(Sink::Out, "{}s", Fixed<1>{value / 1e9});
                else if (v >= 1000000)
                    ::fixy::report(Sink::Out, "{}ms", Fixed<1>{value / 1e6});
                else if (v >= 1000)
                    ::fixy::report(Sink::Out, "{}µs", Fixed<1>{value / 1e3});
                else
                    ::fixy::report(Sink::Out, "{}ns", v);
                break;
            case detail::BpfUnit::Bytes:
                if (v >= (1ULL << 30))
                    ::fixy::report(Sink::Out, "{}GB", Fixed<1>{value / static_cast<double>(1ULL << 30)});
                else if (v >= (1ULL << 20))
                    ::fixy::report(Sink::Out, "{}MB", Fixed<1>{value / static_cast<double>(1ULL << 20)});
                else if (v >= (1ULL << 10))
                    ::fixy::report(Sink::Out, "{}KB", Fixed<1>{value / 1024.0});
                else
                    ::fixy::report(Sink::Out, "{}B", v);
                break;
            default:
                ::fixy::report(Sink::Out, "{}", v);
                break;
        }
    }

    void print_bpf_summary_() const noexcept {
        using ::fixy::Sink;
        if (bpf_attached == 0) return;
        const auto& d = bpf_delta;

        ::fixy::report(Sink::Out, "     └─ ");
        bool first = true;
        for (const detail::BpfField& f : detail::kBpfFields) {
            const uint64_t v = d[f.idx];
            if (v == 0) continue;
            if (!first) ::fixy::report(Sink::Out, " · ");
            first = false;
            ::fixy::report(Sink::Out, "{}=", f.label);
            print_scaled_(v, f.unit);
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
        if (sched_offcpu.kept > 0) {
            if (!first) ::fixy::report(Sink::Out, " · ");
            first = false;
            ::fixy::report(Sink::Out, "offcpu=");
            std::uint32_t written = 0;
            for (detail::OffCpuSpike const& spike : sched_offcpu.longest) {
                if (written == sched_offcpu.kept) break;
                if (written > 0) ::fixy::report(Sink::Out, "/");
                print_scaled_(spike.off_ns, detail::BpfUnit::Ns);
                ::fixy::report(Sink::Out, "@{}", spike.cpu);
                ++written;
            }
            if (sched_offcpu.count > sched_offcpu.kept) {
                ::fixy::report(Sink::Out, " (×{})", sched_offcpu.count);
            }
        }
        if (first) ::fixy::report(Sink::Out, "clean");
        ::fixy::report(Sink::Out, "\n");
    }

    // JSON: emit every counter in the kBpfFields table as a stable schema
    // (all present whether zero or not) so downstream consumers can
    // rely on field presence.
    void print_bpf_json_() const noexcept {
        using ::fixy::Sink;
        if (bpf_attached == 0) return;
        const auto& d = bpf_delta;
        ::fixy::report(Sink::Out, ",\"bpf\":{{\"attached\":{}", bpf_attached);
        for (const detail::BpfField& f : detail::kBpfFields) {
            ::fixy::report(Sink::Out, ",\"{}\":{}", f.label, d[f.idx]);
        }
        ::fixy::report(Sink::Out, "}}");
    }
#endif
};

// ── Mann-Whitney U (Mann & Whitney 1947) for A/B ──────────────────

struct Compare {
    RunName a_name{};
    RunName b_name{};
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

    // Writes the one-line summary to standard output.  A pair that the test
    // tells apart but whose p99 moves less than the threshold says so, and
    // does not claim to be indistinguishable.  It is noipa, as
    // Report::print_text() is.
    [[gnu::noipa]] void print_text() const noexcept {
        using ::fixy::Fixed;
        using ::fixy::SignMark;
        using ::fixy::Sink;
        using Delta = Fixed<2, 6, ::fixy::Align::Right, SignMark::Always>;
        ::fixy::report(Sink::Out, "  Δ {} → {}:  Δp50={}%  Δp99={}%  Δμ={}%  z={}", a_name.view(), b_name.view(),
                       Delta{delta_p50_pct}, Delta{delta_p99_pct}, Delta{delta_mean_pct},
                       Fixed<2, 5, ::fixy::Align::Right, SignMark::Always>{z});
        if (is_regression()) {
            ::fixy::report(Sink::Out, "  [REGRESS]\n");
        } else if (is_improvement()) {
            ::fixy::report(Sink::Out, "  [IMPROVE]\n");
        } else if (distinguishable) {
            ::fixy::report(Sink::Out, "  [distinguishable, |Δp99| <= {}%]\n", Fixed<0>{kFlagDeltaP99Pct});
        } else {
            ::fixy::report(Sink::Out, "  [indistinguishable]\n");
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
    auto const samples = ::fixy::strict_add(n1, n2);
    ::fixy::HeapArray<Tagged> tagged = ::fixy::mint_heap_array_with<Tagged>(
        alloc, samples, [&a_samples, &b_samples, n1](std::size_t position) noexcept {
            if (position < n1) return Tagged{.v = *a_samples.get(position).expect("a position of A"), .src = 0};
            return Tagged{.v = *b_samples.get(position - n1).expect("a position of B"), .src = 1};
        });
    ::fixy::HeapArray<Tagged> scratch = ::fixy::mint_heap_array(alloc, samples, Tagged{});
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
    const double u_min = ::fixy::min(u1, u2);
    const double mu = dn1 * dn2 / 2.0;

    // Tie-corrected variance (Mann & Whitney 1947 §5; Siegel 1956):
    //   sigma² = (n1·n2 / 12) · (N + 1 − T / (N·(N − 1)))
    // For T = 0 (no ties) this reduces to the classical form.
    const double N = static_cast<double>(n1) + static_cast<double>(n2);
    const double n1n2_12 = (static_cast<double>(n1) * dn2) / 12.0;
    const double Ncorr = (N > 1.0) ? (N + 1.0 - tie_sum / (N * (N - 1.0))) : (N + 1.0);
    const double sigma = ::fixy::fp::sqrt(::fixy::max(0.0, n1n2_12 * Ncorr));

    // Continuity correction: subtract 0.5 from |u_min − mu| before
    // dividing by sigma (normal approximation of a discrete statistic).
    // Sign of z preserves direction (is A tending below or above B?).
    const double diff = u_min - mu;
    const double adj = (diff >= 0.0 ? +1.0 : -1.0) * ::fixy::max(0.0, ::fixy::fp::abs(diff) - 0.5);

    c.u = u_min;
    c.z = (sigma > 0) ? (adj / sigma) : 0.0;
    c.distinguishable = ::fixy::fp::abs(c.z) > 2.576;
    return c;
}

// ── Run: fluent builder + measurement loop ─────────────────────────

class Run {
public:
    // A run whose name is a literal or a view of a text of the base.  The run
    // copies the name, so the text can end before the run.  A name of more
    // than kNameBytes bytes ends the process (name_of).
    explicit Run(::fixy::TextView name) noexcept : name_{name_of(name)} {}

    // A run whose name is a text of the base that a bench formats, such as
    // the name of a row with its count of threads.  Complexity: one copy of
    // the text.
    template <std::size_t Capacity>
    explicit Run(::fixy::FixedText<Capacity> const& name) noexcept : name_{name_of(name.view())} {}

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
    // of the slots from 1 (the cores of a run).
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
    //
    // Each instantiation is a function of its own, and the body is inline in
    // its loops.  The code of the caller then cannot change the code of the
    // timed loop.  A measure that GCC inlines into a small main gets the
    // registers and the alignment of main, and a call of a cold function
    // before it moves the loops into main.cold.
    template <typename Body>
    [[nodiscard, gnu::noinline]] Report measure(Body&& body) const {
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
        const std::uint64_t start_all_ns = detail::read_monotonic_ns();
        // Both budgets are in nanoseconds, the unit of the clock.  A
        // compare of a count of milliseconds with a count of nanoseconds once
        // ended the warmup after 2.5 us in place of 2.5 s.
        const std::uint64_t wall_budget_ns = ::fixy::saturating_mul(std::uint64_t{wall_cap_ms}, kNsPerMs);
        const std::uint64_t warmup_budget_ns = wall_budget_ns / 4;

        for (size_t i = 0; i < warmup_; ++i) {
            body();
            // Budget check every 64 iterations — a read of the clock is
            // ~20 ns, amortizing it keeps overhead under 1% for ≥2 µs
            // bodies and invisible for anything heavier.
            if (wall_cap_ms > 0 && (i & 63) == 63) {
                if (detail::read_monotonic_ns() - start_all_ns > warmup_budget_ns) {
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
        // The windows share the samples and the time that the cap leaves
        // after the warmup (the windows).
        const std::uint64_t used_ns = window.start_ns - start_all_ns;
        const WindowPlan plan = plan_windows_(WindowRequest{
            .samples = S,
            .cap = wall_cap_ms > 0 ? WallCap::On : WallCap::Off,
            .remaining_ns = wall_budget_ns > used_ns ? wall_budget_ns - used_ns : 0,
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
                // The element door takes the value alone, so no closure holds
                // the body of the bench.
                const double sample_ns = (static_cast<double>(d) * nspc) / static_cast<double>(batch);
                ns_samples.with_element(
                    filled, [sample_ns](double& sample) noexcept { sample = sample_ns; },
                    [] noexcept { ::fixy::fatal("the windows take at most S samples"); });
                ++filled;
                // The reading of the end of the sample is the clock of the
                // part of the time of the window, so the check costs no read.
                if (taken + 1 >= kMinWindowSamples && t1 > bound.deadline_tsc) break;
            }
            static_cast<void>(window.ends.append(filled));
            close_window_(window);
        }
        return close_(std::move(opening), window, ns_samples, LoopCounts{.filled = filled, .batch = batch});
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
    // the cap leaves after the warmup, which can be zero.
    struct WindowRequest {
        std::size_t samples = 0;
        WallCap cap = WallCap::Off;
        std::uint64_t remaining_ns = 0;
    };

    // What the sampled loop gives close_: the count of the samples that it
    // filled, and the count of the calls of the body in each sample.
    struct LoopCounts {
        std::size_t filled = 0;
        std::size_t batch = 1;
    };

    // One window of the sampled loop: its part of the samples, and the
    // reading of the TSC after which it takes no more sample (the windows).
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
        // The monotonic clock at the start of the sampled loop, in
        // nanoseconds.
        std::uint64_t start_ns = 0;
        // The files of the measuring thread that the two readings of the
        // pause check read, and the first reading (the pause check).
        detail::PauseFiles pause_files{};
        ::fixy::Side<side::ReadingBefore, PauseReading> pause_before{PauseReading{}};
        // The clocks at the end of the last window, and the count of the
        // samples at the end of each window with the time that the measuring
        // thread lost in it (the windows).
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
        ::fixy::Option<crucible::warden::Policy> policy = have_hardening_ ? ::fixy::some(hardening_) : env_hardening_();
        std::move(policy).match(
            [&opening, &startup](crucible::warden::Policy chosen) noexcept {
                opening.hardening_guard = crucible::warden::mint_hardening(startup, detail::harness_topology(), chosen);
                opening.pin = PinResult::one(CpuId{opening.hardening_guard.pinned_cpu()});
            },
            [this, &opening]() noexcept { opening.pin = pin_(); });
        return opening;
    }

    [[nodiscard, gnu::noinline]] static SampleWindow start_window_(CpuId pinned_cpu) {
        SampleWindow window{};
        window.freq_start = detail::read_cpu_freq_hz(pinned_cpu);
#if defined(CRUCIBLE_HAVE_BPF) && CRUCIBLE_HAVE_BPF
        const ::fixy::Ref<::crucible::perf::Senses const> senses = detail::senses_instance();
        window.hub = senses->sense_hub();
        window.sw = senses->sched_switch();
        for (auto const hub : window.hub)
            window.bpf_pre = hub->read(::foundation::effects::testing::test().nondet);
        for (auto const sw : window.sw)
            window.sched_pre_idx = sw->timeline_write_index();
#endif
        window.pause_files = detail::open_pause_files();
        window.pause_before = detail::read_pause_before(window.pause_files);
        window.clock_last = detail::read_window_clock();
        window.start_ns = detail::read_monotonic_ns();
        return window;
    }

    // The windows of the sampled loop (the windows): min(samples, kWindows)
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
        auto const remaining_ns = static_cast<double>(request.remaining_ns);
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
    // keeps the time that the measuring thread lost in that window (the
    // windows).  The two reads come between two samples, outside the timed
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
                                               ::fixy::HeapArray<double> const& taken, LoopCounts counts) const {
        const std::size_t filled = counts.filled;
        const std::size_t batch = counts.batch;
        const ::fixy::Side<side::ReadingAfter, PauseReading> pause_after = detail::read_pause_after(window.pause_files);
        // The samples that the loop filled, the first filled of taken.
        ::fixy::HeapArray<double> ns_samples =
            part_of_samples(::fixy::View<const double>{taken.view()}, ::fixy::side_as<side::PartFirst>(std::size_t{0}),
                            ::fixy::side_as<side::PartEnd>(filled));
        const CpuId pinned_cpu = opening.pin.cpu;
        const uint64_t freq_start = window.freq_start;

        const std::uint64_t end_ns = detail::read_monotonic_ns();
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
        const uint64_t freq_end = detail::read_cpu_freq_hz(pinned_cpu);
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
            drift = (m > 0) ? ::fixy::fp::abs(m1 - m2) / m * 100.0 : 0.0;
            drift_flag = drift > 10.0;
        }

        // The median of each window, from its samples in the order of the
        // loop, before the sort of all the samples (the windows).
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
        r.wall_ns = static_cast<double>(end_ns - window.start_ns);
        r.pct = Percentiles::compute(ns_samples);
        r.samples = std::move(ns_samples);
        r.pause = detail::pause_between(window.pause_before, pause_after);
        r.window_basis = r.pause.basis == PauseBasis::LostTime ? PauseBasis::LostTime : PauseBasis::NotMeasured;
        r.paused_windows = detail::paused_windows_of(window.lost_ns.view(), r.pause.basis);

        // Sysfs-based freq drift check.
        r.freq_start_hz = freq_start;
        r.freq_end_hz = freq_end;
        if (freq_start > 0 && freq_end > 0) {
            const auto hi = ::fixy::max(freq_start, freq_end);
            const auto lo = ::fixy::min(freq_start, freq_end);
            const double ratio = static_cast<double>(hi - lo) / static_cast<double>(hi);
            r.freq_drift_flag = ratio > 0.05;
        }

        // Cycles/op derived from rdtsc mean ns and TSC frequency.
        // Consistent with how "cyc" was reported before, independent of
        // PMU availability.
        if (const double tsc_hz = Timer::tsc_freq_hz(); tsc_hz > 0.0) {
            r.cycles_per_op = r.pct.mean * tsc_hz / 1e9;
            r.p50_cycles = r.pct.p50 * tsc_hz / 1e9;
        }
        // A bench that prints its rows with no report of the harness still
        // shows the mark of the floor, on standard error (the cycle floor).
        if (r.is_below_cycle_floor()) {
            ::fixy::report(::fixy::Sink::Err,
                           "bench: the row {} takes {} cycle a call, below {}, so its timed loop lost the work of its "
                           "body, and the row is NOT VALID.  Give each input of the body an input sink, such as "
                           "bench::mark_changed(object).\n",
                           r.name.view(), ::fixy::Fixed<2>{r.p50_cycles}, ::fixy::Fixed<2>{kCycleFloor});
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
        ::fixy::mmap::SharedView<const ::crucible::perf::TimelineSchedEvent> view{};
        for (auto const sw : window.sw)
            view = sw->timeline_view();
        r.sched_offcpu = detail::top_offcpu_of(view, ::fixy::side_as<side::ReadingBefore>(sched_pre_idx),
                                               ::fixy::side_as<side::ReadingAfter>(sched_post_idx));
#endif

        return r;
    }

    enum class Pin : uint8_t {
        Auto,
        Explicit,
        None,
        FanOut
    };

    RunName name_{};
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
    static constexpr std::uint64_t kNsPerMs = 1000000;
    size_t max_wall_ms_ = kDefaultMaxWallMs;
    bool have_max_wall_ = false;  // true iff .max_wall_ms() was called

    // CRUCIBLE_BENCH_WALL_MS=<n>.  Empty, unset, or a text with no leading
    // decimal number, such as a negative value → default applies.
    [[nodiscard]] static ::fixy::Option<std::size_t> env_wall_ms_() noexcept {
        return env_value("CRUCIBLE_BENCH_WALL_MS")
            .match([](::fixy::TextView text) noexcept { return detail::leading_number(text); },
                   []() noexcept -> ::fixy::Option<std::uint64_t> { return ::fixy::none; });
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
    [[nodiscard]] static ::fixy::Option<crucible::warden::Policy> env_hardening_() noexcept {
        using Named = ::fixy::Option<crucible::warden::Policy>;
        return env_value("CRUCIBLE_BENCH_HARDENING")
            .match(
                [](::fixy::TextView name) noexcept -> Named {
                    using ::fixy::TextView;
                    if (name == TextView{"production"}) return ::fixy::some(crucible::warden::Policy::production());
                    if (name == TextView{"cloud_vm"} || name == TextView{"vm"} || name == TextView{"cloud"})
                        return ::fixy::some(crucible::warden::Policy::cloud_vm());
                    if (name == TextView{"dev_quiet"} || name == TextView{"dev"})
                        return ::fixy::some(crucible::warden::Policy::dev_quiet());
                    if (name == TextView{"none"}) return ::fixy::some(crucible::warden::Policy::none());
                    return ::fixy::none;
                },
                []() noexcept -> Named { return ::fixy::none; });
    }

    // CRUCIBLE_BENCH_SAMPLES=<n> with n above zero, or 100000.
    [[nodiscard]] static size_t env_samples_() noexcept {
        constexpr std::size_t kDefaultSamples = 100000;
        const std::uint64_t asked =
            env_value("CRUCIBLE_BENCH_SAMPLES")
                .match([](::fixy::TextView text) noexcept { return detail::leading_number(text).value_or(0); },
                       []() noexcept { return std::uint64_t{0}; });
        return asked > 0 ? asked : kDefaultSamples;
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
        if (pin_mode_ == Pin::None) return PinResult{.cpu = detail::calling_cpu(), .cpu_count = 0};
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
            ::fixy::report(::fixy::Sink::Err,
                           "WARN bench: cpu{} is outside the affinity of the process, so the run measures on cpu{}\n",
                           asked.raw(), target);
        }
        if (target < 0) target = detail::calling_cpu().raw();
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
        const int actual = detail::calling_cpu().raw();
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
    // An affinity of isolated CPUs only is a claim of loop bench (the cores
    // of a run).  No load balancer spreads the threads of the body there, so
    // each thread of the body pins itself to a core of its slot: a worker
    // through pin_calling_thread_to(), and the threads of a door through the
    // placement OneCpuEach over Cores::first_slots_apart of slot 0.  The
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

        int start_cpu = detail::calling_cpu().raw();
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
        const CoreOrder order = read_core_order();
        const Cores cores = Cores::of(::fixy::side_as<side::AllowedCpus>(allowed),
                                      ::fixy::side_as<side::IsolatedCpus>(isolated), order);
        const CpuId measuring = detail::fan_out_core(::fixy::side_as<side::AllowedCpus>(allowed),
                                                     ::fixy::side_as<side::IsolatedCpus>(isolated), order);
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
// Call it first in main, before a Run pins the thread.  It is noipa, so its
// report() calls stay out of main, and the rows after it stay in the hot code
// of main (the print doors of print()).
[[gnu::noipa]] inline void print_system_info() {
    using ::fixy::Sink;
    using ::fixy::TextView;
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
    const ::fixy::Ref<::crucible::perf::Senses const> senses = detail::senses_instance();
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

    for (TextView const samples : detail::start_value("CRUCIBLE_BENCH_SAMPLES")) {
        ::fixy::report(Sink::Out, "  samples:   {} (CRUCIBLE_BENCH_SAMPLES)\n", samples);
    }
    ::fixy::report(Sink::Out, "\n");
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
// CRUCIBLE_BENCH_HARDENING is not parsed here — it's consumed by
// Run::measure() directly, so no bench-side work is required.

// The text of CRUCIBLE_BENCH_CORE_ORDER comes from the environment of the
// process at its start (the environment).  loop bench sets the variable in
// that environment (the cores of a run).  A file that does not read gives a
// warning and the empty order.  A text that parse_core_order() refuses ends
// the process, because loop bench writes each such text.  Complexity: linear
// in the environment.
inline CoreOrder read_core_order() noexcept {
    if (!detail::start_environment().is_read) {
        ::fixy::report(::fixy::Sink::Err, "WARN bench: /proc/self/environ does not read, so the slots of the run take "
                                          "the cores in ascending order\n");
        return CoreOrder{};
    }
    return detail::start_value("CRUCIBLE_BENCH_CORE_ORDER")
        .match(
            [](::fixy::TextView text) noexcept {
                return parse_core_order(text).match(
                    [](CoreOrder order) noexcept { return order; },
                    [&text]() noexcept -> CoreOrder {
                        ::fixy::fatal("CRUCIBLE_BENCH_CORE_ORDER is '{}', and it must hold CPU numbers with a comma "
                                      "between two, each CPU one time, such as 240,242,224",
                                      text);
                    });
            },
            []() noexcept { return CoreOrder{}; });
}

// CRUCIBLE_BENCH_CORE as a CPU, or -1 when it is not set, is not a decimal
// number, or is past the int range.  -1 is the "no explicit pin" signal that
// Run::pin_() falls back to Auto on.  A negative value is no CPU, so it gives
// -1 too.
[[nodiscard]] inline int env_core() noexcept {
    return env_value("CRUCIBLE_BENCH_CORE")
        .match(
            [](::fixy::TextView text) noexcept {
                return detail::leading_number(text).match(
                    [](std::uint64_t cpu) noexcept { return ::fixy::narrow<int>(cpu).value_or(-1); },
                    []() noexcept { return -1; });
            },
            []() noexcept { return -1; });
}

// Whether a bench prints the JSON tail of its reports after their text.
enum class JsonTail : std::uint8_t {
    Off,
    On,
};

// CRUCIBLE_BENCH_JSON set to a value other than "0" asks for the JSON tail.
[[nodiscard]] inline JsonTail env_json() noexcept {
    return env_value("CRUCIBLE_BENCH_JSON")
        .match(
            [](::fixy::TextView text) noexcept { return text == ::fixy::TextView{"0"} ? JsonTail::Off : JsonTail::On; },
            []() noexcept { return JsonTail::Off; });
}

// One-shot: build a Run with env-driven core pinning and measure body.
// For benches that need .samples()/.warmup() overrides, construct Run
// directly and apply env_core() manually.
template <typename Body>
[[nodiscard]] inline Report run(::fixy::TextView name, Body&& body) {
    Run r{name};
    if (const int c = env_core(); c >= 0) (void)r.core(c);
    return r.measure(std::forward<Body>(body));
}

// The same, for a name that a bench formats into a text of the base.
template <std::size_t Capacity, typename Body>
[[nodiscard]] inline Report run(::fixy::FixedText<Capacity> const& name, Body&& body) {
    return run(name.view(), std::forward<Body>(body));
}

// State of a row
//   run_with(name, state, body) times body(state) as run(name, body) times
//   body().  The harness holds the state by value for the whole row, the
//   body takes it as an lvalue in each call, and the call gives the state
//   back beside the Report in one Record.  So a row whose state is linear,
//   such as the Seat of a session, needs no lambda that captures the state by
//   reference: the row takes the state back and ends it at its own door.  The
//   result of the body holds no borrow, so no reference to the state leaves
//   a call.  measure_with(run, state, body) is the same for a Run that a row
//   sets up itself.
template <typename State, typename Body>
struct StateBody {
    State state;
    Body body;

    [[gnu::always_inline]] void operator()() noexcept(noexcept(body(state))) { body(state); }

    // The state, out of a body that the row gives up.
    [[nodiscard]] State take_state() && noexcept { return static_cast<State&&>(state); }
};

// A body of a row with a state: a call on the state as an lvalue, whose
// result holds no borrow.
template <typename Body, typename State>
concept StateRowBody = !std::is_reference_v<State> && std::is_invocable_v<Body&, State&>
                    && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&, State&>>;

template <typename State, typename Body>
    requires StateRowBody<Body, State>
[[nodiscard]] inline ::fixy::Record<Report, State> measure_with(Run const& run, State state, Body body) {
    StateBody<State, Body> held{static_cast<State&&>(state), static_cast<Body&&>(body)};
    Report report = run.measure(held);
    return {static_cast<Report&&>(report), static_cast<StateBody<State, Body>&&>(held).take_state()};
}

template <typename State, typename Body>
    requires StateRowBody<Body, State>
[[nodiscard]] inline ::fixy::Record<Report, State> run_with(::fixy::TextView name, State state, Body body) {
    Run r{name};
    if (const int c = env_core(); c >= 0) (void)r.core(c);
    return measure_with(r, static_cast<State&&>(state), static_cast<Body&&>(body));
}

// Writes the text line of each report to standard output, in order.  A bench
// holds its reports in an owner of the base and passes its view().  Each emit
// function is noipa, so its report() calls stay out of a caller that measures
// more rows after it (the print doors of print()).
[[gnu::noipa]] inline void emit_reports_text(::fixy::View<const Report> reports) noexcept {
    for (Report const& report : reports)
        report.print_text();
}

// Writes the one-line summary of each Compare to standard output, in the
// order of the view.
[[gnu::noipa]] inline void emit_compares(::fixy::View<const Compare> compares) noexcept {
    for (Compare const& compare : compares)
        compare.print_text();
}

// Writes the line "=== json ===" and the JSON array of the reports to
// standard output when tail is On, and nothing when it is Off.  The reader of
// utils/assay/readers/bench.py reads the text after the last such line, so a
// bench calls this last.  A bench that prints more between the text and the
// JSON, such as a bench::compare block, calls emit_reports_text, then its
// prints, then this.
[[gnu::noipa]] inline void emit_reports_json(::fixy::View<const Report> reports, JsonTail tail) noexcept {
    using ::fixy::Sink;
    if (tail == JsonTail::Off) return;
    ::fixy::report(Sink::Out, "\n=== json ===\n[\n");
    std::size_t written = 0;
    for (Report const& report : reports) {
        ++written;
        ::fixy::report(Sink::Out, "  ");
        report.print_json();
        ::fixy::report(Sink::Out, "{}\n", written < reports.size() ? ::fixy::TextView{","} : ::fixy::TextView{});
    }
    ::fixy::report(Sink::Out, "]\n");
}

// The usual end of main: the text of the reports, then their JSON tail.
[[gnu::noipa]] inline void emit_reports(::fixy::View<const Report> reports, JsonTail tail) noexcept {
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
// CRUCIBLE_BENCH_CHECK checks in each build.  A failed check ends the
// process through fatal() of the Report family, which writes the file, the
// line and the condition to stderr, with the site of the check.  fatal() is
// a cold call that is not inline, so the check leaves one branch in the code.
//
// Put each check outside the timed body.  The body can count the results
// that are not the expected ones, and a check after the measurement reads
// the count.

}  // namespace bench

#define CRUCIBLE_BENCH_CHECK(cond)                                                                              \
    do {                                                                                                        \
        if (!(cond)) [[unlikely]]                                                                               \
            ::fixy::fatal("{}:{}: bench check failed: {}", ::fixy::TextView{__FILE__}, std::uint32_t{__LINE__}, \
                          ::fixy::TextView{#cond});                                                             \
    } while (0)
