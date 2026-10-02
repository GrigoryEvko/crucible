// Tests of foundation/core/Atomic.h: Atomic, CacheLine and Tally, alone
// and under contention.  The tsan preset runs the same binary, so each
// order that the names claim is checked by ThreadSanitizer too.

#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include "philox_stream.h"

#include <bit>
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

using ::foundation::core::Atomic;
using ::foundation::core::CacheLine;
using ::foundation::core::Tally;

namespace eff = ::foundation::effects;
using WorkerCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

enum class Phase : std::uint8_t {
    idle,
    busy,
    done
};

constexpr std::uint64_t steps_per_worker = 20000;

// Runs the same body on four threads at the same time, and returns after
// each thread joins.
template <class Body>
void run_on_four_threads(Body const& body) {
    auto whole = ::foundation::permissions::mint_permission_root<worker_tags::Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    using ::foundation::permissions::WriteView;
    auto rebuilt = ::foundation::permissions::mint_permission_fork<worker_tags::First, worker_tags::Second,
                                                                   worker_tags::Third, worker_tags::Fourth>(
        WorkerCtx{eff::testing::bg()}, std::move(whole),
        [&body](WriteView<worker_tags::First, WholeBrand> const&, WorkerCtx const&) noexcept { body(); },
        [&body](WriteView<worker_tags::Second, WholeBrand> const&, WorkerCtx const&) noexcept { body(); },
        [&body](WriteView<worker_tags::Third, WholeBrand> const&, WorkerCtx const&) noexcept { body(); },
        [&body](WriteView<worker_tags::Fourth, WholeBrand> const&, WorkerCtx const&) noexcept { body(); });
    ::foundation::permissions::permission_drop(std::move(rebuilt));
}

void test_single_thread_operations() {
    Atomic<std::uint64_t> counter{5};
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 5);
    counter.store_release(9);
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 9);
    CRUCIBLE_FATAL_INVARIANT(counter.fetch_add_acq_rel(3) == 9);
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 12);

    auto const swapped = counter.cas_acq_rel(12, 20);
    CRUCIBLE_FATAL_INVARIANT(swapped.is_swapped && swapped.observed == 12);
    auto const refused = counter.cas_acq_rel(12, 30);
    CRUCIBLE_FATAL_INVARIANT(!refused.is_swapped && refused.observed == 20);
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 20);

    counter.store_release_sole_writer(20, 21);
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 21);

    Atomic<std::uint8_t> small{255};
    CRUCIBLE_FATAL_INVARIANT(small.fetch_add_acq_rel(1) == 255);
    CRUCIBLE_FATAL_INVARIANT(small.load_acquire() == 0);

    Atomic<std::int32_t> signed_cell{-3};
    CRUCIBLE_FATAL_INVARIANT(signed_cell.fetch_add_acq_rel(5) == -3);
    CRUCIBLE_FATAL_INVARIANT(signed_cell.load_acquire() == 2);

    Atomic<Phase> phase{Phase::idle};
    CRUCIBLE_FATAL_INVARIANT(phase.cas_acq_rel(Phase::idle, Phase::busy).is_swapped);
    CRUCIBLE_FATAL_INVARIANT(phase.load_acquire() == Phase::busy);
    phase.store_release(Phase::done);
    CRUCIBLE_FATAL_INVARIANT(phase.load_acquire() == Phase::done);

    Atomic<bool> flag{};
    CRUCIBLE_FATAL_INVARIANT(!flag.load_acquire());
    CRUCIBLE_FATAL_INVARIANT(flag.cas_acq_rel(false, true).is_swapped);
    CRUCIBLE_FATAL_INVARIANT(!flag.cas_acq_rel(false, true).is_swapped);

    Tally refusals{};
    refusals.add(2);
    refusals.add(5);
    CRUCIBLE_FATAL_INVARIANT(refusals.read() == 7);
}

void test_cache_line() {
    CacheLine<Atomic<std::uint64_t>> head{std::uint64_t{4}};
    CacheLine<Tally> tally{};
    CRUCIBLE_FATAL_INVARIANT(head.get().load_acquire() == 4);
    head.get().store_release(8);
    CRUCIBLE_FATAL_INVARIANT(head.get().load_acquire() == 8);
    tally.get().add(1);
    CRUCIBLE_FATAL_INVARIANT(tally.get().read() == 1);
    CRUCIBLE_FATAL_INVARIANT(std::bit_cast<std::uintptr_t>(&head) % 64 == 0);
    CRUCIBLE_FATAL_INVARIANT(std::bit_cast<std::uintptr_t>(&tally) % 64 == 0);
    CRUCIBLE_FATAL_INVARIANT(std::bit_cast<std::uintptr_t>(&head.get()) == std::bit_cast<std::uintptr_t>(&head));
}

// The property: a random sequence of operations on one cell agrees with
// a plain model that applies the same operations to an integer.
void test_agrees_with_plain_model() {
    ::foundation::test::PhiloxStream stream{0x5EEDA703000000C3u};
    Atomic<std::uint64_t> cell{};
    std::uint64_t model = 0;
    for (int step = 0; step < 20000; ++step) {
        std::uint64_t const operand = stream.next_wide();
        switch (stream.below(4)) {
            case 0:
                cell.store_release(operand);
                model = operand;
                break;
            case 1:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_add_acq_rel(operand) == model);
                model += operand;
                break;
            case 2: {
                std::uint64_t const expected = (operand & 1u) != 0 ? model : operand;
                auto const outcome = cell.cas_acq_rel(expected, operand ^ 0x5555u);
                CRUCIBLE_FATAL_INVARIANT(outcome.observed == model);
                CRUCIBLE_FATAL_INVARIANT(outcome.is_swapped == (expected == model));
                if (outcome.is_swapped) model = operand ^ 0x5555u;
                break;
            }
            default:
                cell.store_release_sole_writer(model, operand);
                model = operand;
                break;
        }
        CRUCIBLE_FATAL_INVARIANT(cell.load_acquire() == model);
    }
}

void test_fetch_add_under_contention() {
    Atomic<std::uint64_t> counter{};
    run_on_four_threads([&counter] {
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            static_cast<void>(counter.fetch_add_acq_rel(1));
        }
    });
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 4 * steps_per_worker);
}

void test_cas_under_contention() {
    Atomic<std::uint64_t> counter{};
    run_on_four_threads([&counter] {
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            std::uint64_t seen = counter.load_acquire();
            for (;;) {
                auto const outcome = counter.cas_acq_rel(seen, seen + 1);
                if (outcome.is_swapped) break;
                seen = outcome.observed;
            }
        }
    });
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 4 * steps_per_worker);
}

void test_tally_under_contention() {
    CacheLine<Tally> tally{};
    run_on_four_threads([&tally] {
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            tally.get().add(1);
        }
    });
    CRUCIBLE_FATAL_INVARIANT(tally.get().read() == 4 * steps_per_worker);
}

// A release store publishes the plain writes before it, and an acquire
// load that reads the stored value sees them.  ThreadSanitizer reports a
// race on the plain payload if either order is missing.  One thread
// publishes; the other three read, and each payload must be complete.
void test_release_publishes_to_acquire() {
    CacheLine<Atomic<std::uint64_t>> published{std::uint64_t{0}};
    std::uint64_t payload_first = 0;
    std::uint64_t payload_second = 0;
    CacheLine<Atomic<std::uint64_t>> role{std::uint64_t{0}};
    CacheLine<Tally> complete_reads{};
    run_on_four_threads([&] {
        bool const is_writer = role.get().fetch_add_acq_rel(1) == 0;
        if (is_writer) {
            payload_first = 0x1111;
            payload_second = 0x2222;
            published.get().store_release_sole_writer(0, 1);
            return;
        }
        while (published.get().load_acquire() == 0) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (payload_first == 0x1111 && payload_second == 0x2222) complete_reads.get().add(1);
    });
    CRUCIBLE_FATAL_INVARIANT(complete_reads.get().read() == 3);
}

}  // namespace

int main() {
    test_single_thread_operations();
    test_cache_line();
    test_agrees_with_plain_model();
    test_fetch_add_under_contention();
    test_cas_under_contention();
    test_tally_under_contention();
    test_release_publishes_to_acquire();
    return 0;
}
