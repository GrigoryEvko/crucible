// Tests of foundation/os/WideAtomic.h: the two forms of the cell of 128 bits,
// alone, against a plain model, and under contention.  The tsan preset runs
// the same binary, so each order that the names claim is checked by
// ThreadSanitizer too, through the compare-and-swap forms of the load and the
// store.  The test is a fixy test, because foundation starts no thread: the
// fork of fixy/concurrent/PermissionFork.h starts the four threads.  No test
// reads a clock.

#include <foundation/os/WideAtomic.h>

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionFork.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include "../foundation/philox_stream.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace worker_tags {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct First {
    using permission_row = ::foundation::effects::Row<>;
};
struct Second {
    using permission_row = ::foundation::effects::Row<>;
};
struct Third {
    using permission_row = ::foundation::effects::Row<>;
};
struct Fourth {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace worker_tags

}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<worker_tags::Whole, worker_tags::First, worker_tags::Second, worker_tags::Third,
                           worker_tags::Fourth> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<worker_tags::Whole, worker_tags::First, worker_tags::Second, worker_tags::Third,
                                        worker_tags::Fourth> : std::true_type {};
}  // namespace foundation::permissions

namespace {

using ::foundation::core::CacheLine;
using ::foundation::core::Tally;
using ::foundation::os::WideAtomicByLock;

namespace eff = ::foundation::effects;
using WorkerCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

constexpr std::uint64_t steps_per_worker = 20000;

// An address and the count of its generation, the value of a tagged pointer.
struct Generation {
    std::uint64_t address = 0;
    std::uint64_t count = 0;
};

[[nodiscard]] bool is_same(Generation left, Generation right) noexcept {
    return left.address == right.address && left.count == right.count;
}

// The value that a compare-and-swap read: the value of its refusal, or the
// expected value of a swap that stored.
[[nodiscard]] Generation observed_by(
    ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::CasRefusal<Generation>> const& outcome,
    Generation expected) noexcept {
    return outcome.match([expected](::foundation::core::Unit) noexcept { return expected; },
                         [](::foundation::core::CasRefusal<Generation> refusal) noexcept { return refusal.observed; });
}

// Runs the same body on four threads at the same time, and returns after
// each thread joins.
template <class Body>
void run_on_four_threads(Body const& body) {
    auto whole = ::foundation::permissions::mint_permission_root<worker_tags::Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    using ::foundation::permissions::WriteView;
    auto rebuilt = ::fixy::concurrent::mint_permission_fork<worker_tags::First, worker_tags::Second, worker_tags::Third,
                                                            worker_tags::Fourth>(
        WorkerCtx{eff::testing::bg()}, std::move(whole),
        [&body](WriteView<worker_tags::First, WholeBrand> const&, WorkerCtx const&) noexcept { body(); },
        [&body](WriteView<worker_tags::Second, WholeBrand> const&, WorkerCtx const&) noexcept { body(); },
        [&body](WriteView<worker_tags::Third, WholeBrand> const&, WorkerCtx const&) noexcept { body(); },
        [&body](WriteView<worker_tags::Fourth, WholeBrand> const&, WorkerCtx const&) noexcept { body(); });
    ::foundation::linear::drop(std::move(rebuilt));
}

// Runs the same body on four threads at the same time, for a probe of the
// memory orders, and returns after each thread joins.  The body shares a
// plain payload that the orders of the probe publish, so it runs through the
// raw probe of the thread door (THE RAW PROBE of fixy/os/ThreadTasks.h).
template <class Body>
void probe_on_four_threads(Body const& body) {
    auto first = [&body]() noexcept { body(); };
    auto second = first;
    auto third = first;
    auto fourth = first;
    ::fixy::thread::run_joined_raw_probe(
        WorkerCtx{eff::testing::bg()}, ::fixy::TestRunnerCtx{eff::testing::test()},
        "a probe of the memory orders of a wide atomic: the body shares a plain payload that the orders publish",
        ::fixy::thread::ThreadOptions{}, first, second, third, fourth)
        .expect("the probe starts four threads with the default options");
}

template <class Cell>
void test_single_thread() {
    Cell zero{};
    CRUCIBLE_FATAL_INVARIANT(is_same(zero.load_acquire(), Generation{}));

    Cell cell{Generation{0x1000, 1}};
    CRUCIBLE_FATAL_INVARIANT(is_same(cell.load_acquire(), Generation{0x1000, 1}));
    cell.store_release(Generation{0x2000, 2});
    CRUCIBLE_FATAL_INVARIANT(is_same(cell.load_acquire(), Generation{0x2000, 2}));

    auto const swapped = cell.cas_acq_rel(Generation{0x2000, 2}, Generation{0x3000, 3});
    CRUCIBLE_FATAL_INVARIANT(swapped.is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_same(cell.load_acquire(), Generation{0x3000, 3}));

    // A value with the same address and a different count is a different
    // value, so the swap refuses it and reads the value of the cell.
    auto const refused = cell.cas_acq_rel(Generation{0x3000, 2}, Generation{0x4000, 4});
    CRUCIBLE_FATAL_INVARIANT(refused.is_err());
    CRUCIBLE_FATAL_INVARIANT(is_same(observed_by(refused, Generation{0x3000, 2}), Generation{0x3000, 3}));
    CRUCIBLE_FATAL_INVARIANT(is_same(cell.load_acquire(), Generation{0x3000, 3}));

    // Each bit of the two words goes through the cell.
    Generation const high_bits{~std::uint64_t{0}, std::uint64_t{1} << 63};
    cell.store_release(high_bits);
    CRUCIBLE_FATAL_INVARIANT(is_same(cell.load_acquire(), high_bits));

    Cell const shared{Generation{7, 8}};
    CRUCIBLE_FATAL_INVARIANT(is_same(shared.load_acquire(), Generation{7, 8}));
}

// The two forms and a plain value take the same random steps, and each step
// gives the same result in the three.
void test_forms_agree_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDA703000000D7u};
    WideAtomicByLock<Generation> locked{};
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
    ::foundation::os::WideAtomicByInstruction<Generation> instructed{};
#endif
    Generation model{};
    for (int step = 0; step < 20000; ++step) {
        Generation const operand{stream.next_wide(), stream.next_wide() % 4};
        switch (stream.below(3)) {
            case 0: {
                CRUCIBLE_FATAL_INVARIANT(is_same(locked.load_acquire(), model));
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
                CRUCIBLE_FATAL_INVARIANT(is_same(instructed.load_acquire(), model));
#endif
                break;
            }
            case 1: {
                locked.store_release(operand);
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
                instructed.store_release(operand);
#endif
                model = operand;
                break;
            }
            default: {
                // Half of the swaps expect the value of the cell.  Each other
                // swap expects the address of the cell with a random count
                // from 0 to 3, so most of them match only in one word.
                bool const expects_model = (operand.count & 1u) != 0;
                Generation const expected = expects_model ? model : Generation{model.address, operand.count};
                Generation const desired{operand.address ^ 0x5555u, operand.count + 1};
                bool const should_store = is_same(expected, model);
                auto const locked_outcome = locked.cas_acq_rel(expected, desired);
                CRUCIBLE_FATAL_INVARIANT(locked_outcome.is_ok() == should_store);
                CRUCIBLE_FATAL_INVARIANT(is_same(observed_by(locked_outcome, expected), model));
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
                auto const instructed_outcome = instructed.cas_acq_rel(expected, desired);
                CRUCIBLE_FATAL_INVARIANT(instructed_outcome.is_ok() == should_store);
                CRUCIBLE_FATAL_INVARIANT(is_same(observed_by(instructed_outcome, expected), model));
#endif
                if (should_store) model = desired;
                break;
            }
        }
    }
}

// One thread stores the pairs (x, ~x) with a rising x, and three threads
// read the cell.  A read of half of one store and half of a different store
// gives a pair whose count is not ~x, and the readers count such pairs.  A
// read never sees x fall, because the reads of one cell keep the order of
// its stores.
template <class Cell>
void test_no_torn_read() {
    CacheLine<Cell> cell{Generation{0, ~std::uint64_t{0}}};
    CacheLine<::foundation::core::Atomic<std::uint64_t>> role{std::uint64_t{0}};
    CacheLine<Tally> torn_reads{};
    CacheLine<Tally> falling_reads{};
    run_on_four_threads([&] {
        bool const is_writer = role->fetch_add_acq_rel(1) == 0;
        if (is_writer) {
            for (std::uint64_t step = 1; step <= 4 * steps_per_worker; ++step) {
                cell->store_release(Generation{step, ~step});
            }
            return;
        }
        std::uint64_t last_seen = 0;
        for (std::uint64_t step = 0; step < 4 * steps_per_worker; ++step) {
            Generation const seen = cell->load_acquire();
            if (seen.count != ~seen.address) torn_reads->add(1);
            if (seen.address < last_seen) falling_reads->add(1);
            last_seen = seen.address;
        }
    });
    CRUCIBLE_FATAL_INVARIANT(torn_reads->read() == 0);
    CRUCIBLE_FATAL_INVARIANT(falling_reads->read() == 0);
    CRUCIBLE_FATAL_INVARIANT(is_same(cell->load_acquire(), Generation{4 * steps_per_worker, ~(4 * steps_per_worker)}));
}

// Four threads step one pair with compare-and-swap operations: each step adds
// one to the address and two to the count.  No step is lost, so the two words
// end at the total of the steps.
template <class Cell>
void test_cas_under_contention() {
    CacheLine<Cell> cell{};
    run_on_four_threads([&] {
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            Generation seen = cell->load_acquire();
            for (;;) {
                auto const outcome = cell->cas_acq_rel(seen, Generation{seen.address + 1, seen.count + 2});
                if (outcome.is_ok()) break;
                seen = observed_by(outcome, seen);
            }
        }
    });
    CRUCIBLE_FATAL_INVARIANT(is_same(cell->load_acquire(), Generation{4 * steps_per_worker, 8 * steps_per_worker}));
}

// A release store publishes the plain writes before it, and an acquire load
// that reads the stored value sees them.  ThreadSanitizer reports a race on
// the plain payload if either order is missing.
template <class Cell>
void test_release_publishes_to_acquire() {
    CacheLine<Cell> published{};
    std::uint64_t payload_first = 0;
    std::uint64_t payload_second = 0;
    CacheLine<::foundation::core::Atomic<std::uint64_t>> role{std::uint64_t{0}};
    CacheLine<Tally> complete_reads{};
    probe_on_four_threads([&] {
        bool const is_writer = role->fetch_add_acq_rel(1) == 0;
        if (is_writer) {
            payload_first = 0x1111;
            payload_second = 0x2222;
            published->store_release(Generation{1, 2});
            return;
        }
        while (published->load_acquire().address == 0) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (payload_first == 0x1111 && payload_second == 0x2222) complete_reads->add(1);
    });
    CRUCIBLE_FATAL_INVARIANT(complete_reads->read() == 3);
}

template <class Cell>
void run_each_test_of_a_form() {
    test_single_thread<Cell>();
    test_no_torn_read<Cell>();
    test_cas_under_contention<Cell>();
    test_release_publishes_to_acquire<Cell>();
}

}  // namespace

int main() {
    run_each_test_of_a_form<WideAtomicByLock<Generation>>();
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
    run_each_test_of_a_form<::foundation::os::WideAtomicByInstruction<Generation>>();
#endif
    run_each_test_of_a_form<::foundation::os::WideAtomic<Generation>>();
    test_forms_agree_with_plain_model();
    return 0;
}
