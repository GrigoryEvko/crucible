// Tests of foundation/core/Atomic.h: Atomic, CacheLine and Tally, alone
// and under contention.  The tsan preset runs the same binary, so each
// order that the names claim is checked by ThreadSanitizer too.

#include <foundation/core/Atomic.h>

#include <foundation/Platform.h>
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

void test_read_modify_write_operations() {
    Atomic<std::uint32_t> bits{0b0101u};
    CRUCIBLE_FATAL_INVARIANT(bits.exchange_acq_rel(0b1100u) == 0b0101u);
    CRUCIBLE_FATAL_INVARIANT(bits.fetch_or_acq_rel(0b0011u) == 0b1100u);
    CRUCIBLE_FATAL_INVARIANT(bits.load_acquire() == 0b1111u);
    CRUCIBLE_FATAL_INVARIANT(bits.fetch_and_acq_rel(0b1010u) == 0b1111u);
    CRUCIBLE_FATAL_INVARIANT(bits.load_acquire() == 0b1010u);
    CRUCIBLE_FATAL_INVARIANT(bits.fetch_sub_acq_rel(0b1011u) == 0b1010u);
    CRUCIBLE_FATAL_INVARIANT(bits.load_acquire() == 0xFFFFFFFFu);

    Atomic<std::int64_t> signed_bound{-5};
    CRUCIBLE_FATAL_INVARIANT(signed_bound.fetch_max_acq_rel(-9) == -5);
    CRUCIBLE_FATAL_INVARIANT(signed_bound.load_acquire() == -5);
    CRUCIBLE_FATAL_INVARIANT(signed_bound.fetch_max_acq_rel(-2) == -5);
    CRUCIBLE_FATAL_INVARIANT(signed_bound.load_acquire() == -2);
    CRUCIBLE_FATAL_INVARIANT(signed_bound.fetch_min_acq_rel(-1) == -2);
    CRUCIBLE_FATAL_INVARIANT(signed_bound.load_acquire() == -2);
    CRUCIBLE_FATAL_INVARIANT(signed_bound.fetch_min_acq_rel(-40) == -2);
    CRUCIBLE_FATAL_INVARIANT(signed_bound.load_acquire() == -40);

    Atomic<std::uint8_t> unsigned_bound{200};
    CRUCIBLE_FATAL_INVARIANT(unsigned_bound.fetch_max_acq_rel(255) == 200);
    CRUCIBLE_FATAL_INVARIANT(unsigned_bound.fetch_min_acq_rel(0) == 255);
    CRUCIBLE_FATAL_INVARIANT(unsigned_bound.load_acquire() == 0);

    Atomic<std::int32_t> signed_count{std::int32_t{-2147483647 - 1}};
    CRUCIBLE_FATAL_INVARIANT(signed_count.fetch_sub_acq_rel(1) == -2147483647 - 1);
    CRUCIBLE_FATAL_INVARIANT(signed_count.load_acquire() == 2147483647);

    Atomic<Phase> phase{Phase::idle};
    CRUCIBLE_FATAL_INVARIANT(phase.exchange_acq_rel(Phase::done) == Phase::idle);
    CRUCIBLE_FATAL_INVARIANT(phase.load_acquire() == Phase::done);
}

// Two words with no padding, so a cell holds the pair as one word.
struct Pair {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

[[nodiscard]] bool same_pair(Pair left, Pair right) noexcept {
    return left.first == right.first && left.second == right.second;
}

void test_class_value() {
    Atomic<Pair> cell{};
    CRUCIBLE_FATAL_INVARIANT(same_pair(cell.load_acquire(), Pair{0, 0}));
    cell.store_release(Pair{1, 2});
    CRUCIBLE_FATAL_INVARIANT(same_pair(cell.load_acquire(), Pair{1, 2}));
    CRUCIBLE_FATAL_INVARIANT(same_pair(cell.exchange_acq_rel(Pair{3, 4}), Pair{1, 2}));

    auto const refused = cell.cas_acq_rel(Pair{3, 5}, Pair{7, 7});
    CRUCIBLE_FATAL_INVARIANT(!refused.is_swapped && same_pair(refused.observed, Pair{3, 4}));
    auto const swapped = cell.cas_acq_rel(Pair{3, 4}, Pair{8, 9});
    CRUCIBLE_FATAL_INVARIANT(swapped.is_swapped && same_pair(swapped.observed, Pair{3, 4}));

    cell.store_release(Pair{10, 11});
    CRUCIBLE_FATAL_INVARIANT(same_pair(cell.load_acquire(), Pair{10, 11}));
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
        switch (stream.below(9)) {
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
            case 3:
                CRUCIBLE_FATAL_INVARIANT(cell.exchange_acq_rel(operand) == model);
                model = operand;
                break;
            case 4:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_sub_acq_rel(operand) == model);
                model -= operand;
                break;
            case 5:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_or_acq_rel(operand) == model);
                model |= operand;
                break;
            case 6:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_and_acq_rel(operand) == model);
                model &= operand;
                break;
            case 7:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_max_acq_rel(operand) == model);
                model = operand > model ? operand : model;
                break;
            case 8:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_min_acq_rel(operand) == model);
                model = operand < model ? operand : model;
                break;
            default:
                cell.store_release(operand);
                model = operand;
                break;
        }
        CRUCIBLE_FATAL_INVARIANT(cell.load_acquire() == model);
    }
}

// The same property for the signed bounds, where the order of the
// operands differs from the order of their bits.
void test_signed_bounds_agree_with_plain_model() {
    ::foundation::test::PhiloxStream stream{0x5EEDA703000000C4u};
    Atomic<std::int64_t> cell{};
    std::int64_t model = 0;
    for (int step = 0; step < 20000; ++step) {
        std::int64_t const operand = std::bit_cast<std::int64_t>(stream.next_wide());
        switch (stream.below(3)) {
            case 0:
                cell.store_release(operand);
                model = operand;
                break;
            case 1:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_max_acq_rel(operand) == model);
                model = operand > model ? operand : model;
                break;
            default:
                CRUCIBLE_FATAL_INVARIANT(cell.fetch_min_acq_rel(operand) == model);
                model = operand < model ? operand : model;
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

// Each thread takes a number from 0 to 3, and works on its own share.
void test_read_modify_write_under_contention() {
    Atomic<std::uint64_t> next_worker{};
    Atomic<std::uint64_t> remaining{4 * steps_per_worker};
    Atomic<std::uint64_t> bits{};
    Atomic<std::int64_t> highest{std::int64_t{-1}};
    Atomic<std::int64_t> lowest{std::int64_t{1}};
    Atomic<std::uint64_t> token{};
    CacheLine<Tally> token_sum{};
    run_on_four_threads([&] {
        std::uint64_t const worker = next_worker.fetch_add_acq_rel(1);
        std::int64_t const signed_worker = static_cast<std::int64_t>(worker);
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            static_cast<void>(remaining.fetch_sub_acq_rel(1));
            static_cast<void>(bits.fetch_or_acq_rel(std::uint64_t{1} << (worker * 16 + step % 16)));
            std::int64_t const signed_step = static_cast<std::int64_t>(step);
            static_cast<void>(highest.fetch_max_acq_rel(signed_step * 4 + signed_worker));
            static_cast<void>(lowest.fetch_min_acq_rel(-(signed_step * 4 + signed_worker)));
            // Each token goes into the cell one time and comes out one time,
            // so the sum of the tokens that came out, with the last one in
            // the cell, is the sum of all the tokens.
            token_sum.get().add(token.exchange_acq_rel(worker * steps_per_worker + step + 1));
        }
    });
    CRUCIBLE_FATAL_INVARIANT(remaining.load_acquire() == 0);
    CRUCIBLE_FATAL_INVARIANT(bits.load_acquire() == ~std::uint64_t{0});
    std::int64_t const last_signed_step = static_cast<std::int64_t>(steps_per_worker - 1);
    CRUCIBLE_FATAL_INVARIANT(highest.load_acquire() == last_signed_step * 4 + 3);
    CRUCIBLE_FATAL_INVARIANT(lowest.load_acquire() == -(last_signed_step * 4 + 3));
    std::uint64_t const token_count = 4 * steps_per_worker;
    CRUCIBLE_FATAL_INVARIANT(token_sum.get().read() + token.load_acquire() == token_count * (token_count + 1) / 2);
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
            published.get().store_release(1);
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
    test_read_modify_write_operations();
    test_class_value();
    test_cache_line();
    test_agrees_with_plain_model();
    test_signed_bounds_agree_with_plain_model();
    test_fetch_add_under_contention();
    test_cas_under_contention();
    test_read_modify_write_under_contention();
    test_tally_under_contention();
    test_release_publishes_to_acquire();
    return 0;
}
