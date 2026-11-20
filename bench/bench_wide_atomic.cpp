// Microbench of the cell of 128 bits of foundation/os/WideAtomic.h, with
// libatomic of GCC 16 as the reference.  GCC calls libatomic for each
// __atomic operation of 16 bytes, so libatomic is the speed of each program
// that holds a std::atomic of 16 bytes.
//
// THE ROWS
//   On one thread: a load, a store, a compare-and-swap that stores and one
//   that refuses, of the instruction form, of the lock form and of libatomic.
//   Each row of a form gives the ratio of its median to the median of the
//   same operation of libatomic, in thousandths.  A ratio above 1000 is
//   slower than the reference.
//
//   On two threads: the load of one thread while a second thread loads the
//   same cell in a loop, of the instruction form and of libatomic.  A load
//   through a compare-and-swap writes the line, so two readers take turns to
//   own it, and a vector move keeps the line shared.
//
//   The first line tells the load form of the host (THE LOAD AND THE STORE ON
//   X86-64 of the header).
//
// THE CORES
//   The bench states CORES 2 in bench/CMakeLists.txt.  loop bench gives it two
//   isolated cores, and each thread pins itself to the core of its slot.  A run
//   with no claim, such as a run under ctest, pins no thread.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/WideAtomic.h>

#include <cstdint>

#include "bench_harness.h"

namespace {

namespace eff = ::foundation::effects;
namespace os = ::foundation::os;

// An address and the count of its generation, the value of a tagged pointer.
struct Generation {
    std::uint64_t address = 0;
    std::uint64_t count = 0;
};

// A time in nanoseconds, as the nearest count of picoseconds.
[[nodiscard]] std::uint64_t picoseconds(double nanoseconds) noexcept {
    return static_cast<std::uint64_t>(nanoseconds * 1000.0 + 0.5);
}

// The median of an operation in thousandths of the median of its reference.
[[nodiscard]] std::uint64_t permille_of(bench::Report const& measured, bench::Report const& reference) noexcept {
    return reference.pct.p50 > 0.0 ? static_cast<std::uint64_t>(measured.pct.p50 * 1000.0 / reference.pct.p50 + 0.5)
                                   : 0;
}

// The form of a row and its operation.
struct RowName final {
    fixy::TextView form;
    fixy::TextView operation;
};

// Writes one line: the name of the row, the percentiles of its samples, and
// the ratio to the reference.
void print_row(RowName const& name, bench::Report const& measured, bench::Report const& reference) noexcept {
    bench::print("{} {}  p50={} ps  p99={} ps  max={} ps  cv={} permille  ratio to libatomic={} permille\n", name.form,
                 name.operation, picoseconds(measured.pct.p50), picoseconds(measured.pct.p99),
                 picoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5),
                 permille_of(measured, reference));
}

// The four rows of one form on one thread.
struct FormRows final {
    bench::Report load{};
    bench::Report store{};
    bench::Report swap_stores{};
    bench::Report swap_refuses{};
};

// THE LAYOUT
//   Each measured operation is one call to a function of its own, aligned to
//   64 bytes and never inlined, in each form, so a change of other code does
//   not move one form against the other.  With the operation inlined in the
//   loop, two rows of one build with the same instructions differed by 12%.
//   A call through a CallRef in one shared loop cost more than the operation.
//   The value that a row carries from one sample to the next is a member of
//   its lambda, so it can stay in a register, as in a loop of a caller.
template <class Cell>
[[gnu::noinline, gnu::aligned(64)]] Generation load_once(Cell const& cell) noexcept {
    return cell.load_acquire();
}

template <class Cell>
[[gnu::noinline, gnu::aligned(64)]] void store_once(Cell& cell, Generation value) noexcept {
    cell.store_release(value);
}

template <class Cell>
[[gnu::noinline, gnu::aligned(64)]] bool swap_once(Cell& cell, Generation expected, Generation desired) noexcept {
    return cell.cas_acq_rel(expected, desired).is_ok();
}

// Only this thread touches the cell, so each compare-and-swap that expects
// the last value stores, and each one that expects a different count refuses.
// The cell is alone on its cache line, so no write of a local of the loop
// shares the line of a locked instruction.
template <class Cell>
[[nodiscard]] FormRows measure_form_of() noexcept {
    ::fixy::CacheLine<Cell> line{Generation{1, 1}};
    Cell& cell = *line;
    FormRows rows{};
    rows.load = bench::run("wide load", [&cell] {
        Generation const value = load_once(cell);
        bench::do_not_optimize(value);
    });
    rows.store = bench::run("wide store", [&cell, step = std::uint64_t{1}]() mutable {
        step += 1;
        store_once(cell, Generation{step, step});
    });
    rows.swap_stores = bench::run("wide swap stores", [&cell, expected = cell.load_acquire()]() mutable {
        Generation const desired{expected.address + 1, expected.count + 1};
        bool const is_stored = swap_once(cell, expected, desired);
        expected = is_stored ? desired : expected;
        bench::do_not_optimize(is_stored);
    });
    rows.swap_refuses = bench::run("wide swap refuses", [&cell] {
        bool const is_stored = swap_once(cell, Generation{0, 0}, Generation{2, 2});
        bench::do_not_optimize(is_stored);
    });
    return rows;
}

void print_form(fixy::TextView form, FormRows const& rows, FormRows const& reference) noexcept {
    print_row(RowName{.form = form, .operation = "load_acquire"}, rows.load, reference.load);
    print_row(RowName{.form = form, .operation = "store_release"}, rows.store, reference.store);
    print_row(RowName{.form = form, .operation = "cas_acq_rel stores"}, rows.swap_stores, reference.swap_stores);
    print_row(RowName{.form = form, .operation = "cas_acq_rel refuses"}, rows.swap_refuses, reference.swap_refuses);
}

// Pins the calling thread to the core of its slot, when the run has one.
void pin_to(::fixy::Option<bench::CpuId> const& core) noexcept {
    core.match(
        [](bench::CpuId cpu) noexcept {
            bench::pin_calling_thread_to(cpu).expect("the pin of a thread of the bench to a core of its claim");
        },
        []() noexcept {});
}

// The cores of the two threads of a row, or none in a run with no claim.
struct RowCores final {
    ::fixy::Option<bench::CpuId> measured{};
    ::fixy::Option<bench::CpuId> second{};
};

// The thread of a shared load row that measures.  It keeps its report, which
// the caller reads after the join.
template <class Cell>
struct MeasureSharedLoad {
    Cell& cell;
    RowCores const& cores;
    ::fixy::Atomic<std::uint32_t> const& is_second_ready;
    ::fixy::Atomic<std::uint32_t>& is_done;
    bench::Report measured{};

    void operator()() noexcept {
        pin_to(cores.measured);
        while (is_second_ready.load_acquire() == 0) {
            CRUCIBLE_SPIN_PAUSE;
        }
        measured = bench::run("wide shared load", [this] {
            Generation const value = load_once(cell);
            bench::do_not_optimize(value);
        });
        is_done.store_release(1);
    }
};

// The load of one thread while a second thread loads the same cell.
template <class Cell>
[[nodiscard]] bench::Report measure_shared_load(::fixy::BgLoadCtx const& ctx, RowCores const& cores) noexcept {
    ::fixy::CacheLine<Cell> line{Generation{1, 1}};
    Cell& cell = *line;
    ::fixy::CacheLine<::fixy::Atomic<std::uint32_t>> is_second_ready{std::uint32_t{0}};
    ::fixy::CacheLine<::fixy::Atomic<std::uint32_t>> is_done{std::uint32_t{0}};
    MeasureSharedLoad<Cell> measuring{
        .cell = cell, .cores = cores, .is_second_ready = *is_second_ready, .is_done = *is_done};
    auto second = [&cell, &cores, &is_second_ready, &is_done]() noexcept {
        pin_to(cores.second);
        is_second_ready->store_release(1);
        while (is_done->load_acquire() == 0) {
            Generation const value = cell.load_acquire();
            bench::do_not_optimize(value);
        }
    };
    ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, measuring, second)
        .expect("the two threads of a shared load row start and join");
    return std::move(measuring.measured);
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: libatomic is the reference of each atomic operation of 16 bytes")
namespace {

// The cell of the reference: a value of 16 bytes that each operation reaches
// through the __atomic builtins, which GCC sends to libatomic.  Each access is
// atomic, so the cell states shareable: two threads can read and write it at
// the same time.
class[[= ::foundation::transfer::shareable{}]] LibatomicCell final {
    __extension__ using Word = unsigned __int128;

    alignas(16) Word word_ = 0;

    [[nodiscard]] static Word to_word_(Generation value) noexcept { return __builtin_bit_cast(Word, value); }
    [[nodiscard]] static Generation from_word_(Word word) noexcept { return __builtin_bit_cast(Generation, word); }

public:
    explicit LibatomicCell(Generation initial) noexcept : word_{to_word_(initial)} {}

    [[nodiscard]] Generation load_acquire() const noexcept {
        return from_word_(__atomic_load_n(&word_, __ATOMIC_ACQUIRE));
    }

    void store_release(Generation value) noexcept { __atomic_store_n(&word_, to_word_(value), __ATOMIC_RELEASE); }

    // The Result of the base, so the rows of the two cells run one body.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, ::fixy::CasRefusal<Generation>>
    cas_acq_rel(Generation expected, Generation desired) noexcept {
        Word observed = to_word_(expected);
        if (__atomic_compare_exchange_n(&word_, &observed, to_word_(desired), false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return ::fixy::Unit{};
        }
        return ::fixy::err(::fixy::CasRefusal<Generation>{from_word_(observed)});
    }
};

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    bench::elevate_priority();
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    bench::Cores const cores = bench::Cores::of_calling_thread();
    RowCores const row_cores{.measured = cores.of_slot(bench::ThreadSlot{0}),
                             .second = cores.of_slot(bench::ThreadSlot{1})};
    pin_to(row_cores.measured);

#if defined(__x86_64__)
    bench::print("load form of the host: {}\n", os::has_atomic_vector_moves() ? fixy::TextView{"one vector move"}
                                                                              : fixy::TextView{"a compare-and-swap"});
#endif

    FormRows const reference = measure_form_of<LibatomicCell>();
    print_form("libatomic", reference, reference);
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
    print_form("WideAtomicByInstruction", measure_form_of<os::WideAtomicByInstruction<Generation>>(), reference);
#endif
    print_form("WideAtomicByLock", measure_form_of<os::WideAtomicByLock<Generation>>(), reference);

    bench::Report const shared_reference = measure_shared_load<LibatomicCell>(ctx, row_cores);
    print_row(RowName{.form = "libatomic", .operation = "load_acquire with a second reader"}, shared_reference,
              shared_reference);
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
    print_row(RowName{.form = "WideAtomicByInstruction", .operation = "load_acquire with a second reader"},
              measure_shared_load<os::WideAtomicByInstruction<Generation>>(ctx, row_cores), shared_reference);
#endif
    return 0;
}
