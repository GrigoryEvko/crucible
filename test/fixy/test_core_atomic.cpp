// Tests of foundation/core/Atomic.h: Atomic, the word of a value, the cell of
// one writer, CacheLine and Tally, alone and under contention.  The tsan preset runs
// the same binary, so each order that the names claim is checked by
// ThreadSanitizer too.  The test is a fixy test, because foundation starts
// no thread: the fork of fixy/concurrent/PermissionFork.h starts the four
// threads.  Each test under contention asserts an invariant of the values,
// and no test reads a clock.

#include <foundation/core/Atomic.h>

#include <fixy/concurrent/PermissionFork.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include "../foundation/abort_probe.h"
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

// A seat number that is never zero.  The assignment is user-provided, so the
// seat is not trivially copyable, and no cast makes a seat from bytes.  Its
// type states the word of its cell below: the number, with zero as the empty
// word.  The member niche lets an Option of a seat copy.
class Seat {
    std::uint32_t number_ = 1;
    constexpr explicit Seat(std::uint32_t number) noexcept : number_{number} {}
    friend struct ::foundation::core::atomic_word<Seat>;

public:
    // The seat of a number that is not zero.
    [[nodiscard]] static Seat of(std::uint32_t number) noexcept {
        CRUCIBLE_FATAL_INVARIANT(number != 0);
        return Seat{number};
    }
    constexpr Seat(Seat const&) noexcept = default;
    constexpr Seat& operator=(Seat const& other) noexcept {
        number_ = other.number_;
        return *this;
    }
    [[nodiscard]] constexpr std::uint32_t number() const noexcept { return number_; }
    [[nodiscard]] static constexpr Seat niche_empty(::foundation::core::niche_key<Seat> const&) noexcept {
        return Seat{0};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<Seat> const&) const noexcept {
        return number_ == 0;
    }
};

// A seat of the same shape, whose protocol below has a defect: to_word gives
// the empty word for the seat 1.
class LossySeat {
    std::uint32_t number_ = 1;
    constexpr explicit LossySeat(std::uint32_t number) noexcept : number_{number} {}
    friend struct ::foundation::core::atomic_word<LossySeat>;

public:
    [[nodiscard]] static LossySeat of(std::uint32_t number) noexcept {
        CRUCIBLE_FATAL_INVARIANT(number != 0);
        return LossySeat{number};
    }
    constexpr LossySeat(LossySeat const&) noexcept = default;
    constexpr LossySeat& operator=(LossySeat const& other) noexcept {
        number_ = other.number_;
        return *this;
    }
    [[nodiscard]] static constexpr LossySeat niche_empty(::foundation::core::niche_key<LossySeat> const&) noexcept {
        return LossySeat{0};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<LossySeat> const&) const noexcept {
        return number_ == 0;
    }
};

}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<worker_tags::Whole, worker_tags::First, worker_tags::Second, worker_tags::Third,
                           worker_tags::Fourth> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<worker_tags::Whole, worker_tags::First, worker_tags::Second, worker_tags::Third,
                                        worker_tags::Fourth> : std::true_type {};
}  // namespace foundation::permissions

namespace foundation::core {

template <>
struct atomic_word<Seat> {
    using word_type = std::uint32_t;
    static constexpr word_type empty_word = 0;
    [[nodiscard]] static constexpr word_type to_word(Seat const& seat) noexcept { return seat.number_; }
    [[nodiscard]] static constexpr Seat from_word(atomic_word_key<Seat> const&, word_type word) noexcept {
        return Seat{word};
    }
};

template <>
struct atomic_word<LossySeat> {
    using word_type = std::uint32_t;
    static constexpr word_type empty_word = 0;
    [[nodiscard]] static constexpr word_type to_word(LossySeat const& seat) noexcept { return seat.number_ - 1; }
    [[nodiscard]] static constexpr LossySeat from_word(atomic_word_key<LossySeat> const&, word_type word) noexcept {
        return LossySeat{word + 1};
    }
};

}  // namespace foundation::core

namespace {

using ::foundation::core::address_of;
using ::foundation::core::Atomic;
using ::foundation::core::CacheLine;
using ::foundation::core::mint_sole_writer;
using ::foundation::core::Tally;
using ::foundation::core::to;

// The roles of the writers of the tests.
struct HeadRole {};
struct TailRole {};

// True when the address of the mint with a target type of the brand Held
// gives a function, as deduction from the target type would.  The result
// type holds the brand in a context that deduction cannot set, so a target
// type of a held brand gives no second writer of it.
template <class Held>
concept AddressTakesBrand = requires {
    static_cast<::foundation::core::SoleWriter<HeadRole, Held> (*)() noexcept>(&mint_sole_writer<HeadRole>);
};

namespace eff = ::foundation::effects;
using WorkerCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

enum class Phase : std::uint8_t {
    idle,
    busy,
    done
};

constexpr std::uint64_t steps_per_worker = 20000;

// The value that a compare-and-swap read: the value of its refusal, or the
// expected value of a swap that stored.
template <class T>
[[nodiscard]] T
observed_by(::foundation::core::Result<::foundation::core::Unit, ::foundation::core::CasRefusal<T>> const& outcome,
            T expected) noexcept {
    return outcome.match([expected](::foundation::core::Unit) noexcept { return expected; },
                         [](::foundation::core::CasRefusal<T> refusal) noexcept { return refusal.observed; });
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

void test_single_thread_operations() {
    Atomic<std::uint64_t> counter{5};
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 5);
    counter.store_release(9);
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 9);
    CRUCIBLE_FATAL_INVARIANT(counter.fetch_add_acq_rel(3) == 9);
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 12);

    auto const swapped = counter.cas_acq_rel(12, 20);
    CRUCIBLE_FATAL_INVARIANT(swapped.is_ok() && swapped.err().is_none());
    auto const refused = counter.cas_acq_rel(12, 30);
    CRUCIBLE_FATAL_INVARIANT(refused.is_err() && observed_by(refused, std::uint64_t{12}) == 20);
    CRUCIBLE_FATAL_INVARIANT(counter.load_acquire() == 20);

    Atomic<std::uint8_t> small{255};
    CRUCIBLE_FATAL_INVARIANT(small.fetch_add_acq_rel(1) == 255);
    CRUCIBLE_FATAL_INVARIANT(small.load_acquire() == 0);

    Atomic<std::int32_t> signed_cell{-3};
    CRUCIBLE_FATAL_INVARIANT(signed_cell.fetch_add_acq_rel(5) == -3);
    CRUCIBLE_FATAL_INVARIANT(signed_cell.load_acquire() == 2);

    Atomic<Phase> phase{Phase::idle};
    CRUCIBLE_FATAL_INVARIANT(phase.cas_acq_rel(Phase::idle, Phase::busy).is_ok());
    CRUCIBLE_FATAL_INVARIANT(phase.load_acquire() == Phase::busy);
    phase.store_release(Phase::done);
    CRUCIBLE_FATAL_INVARIANT(phase.load_acquire() == Phase::done);

    Atomic<bool> flag{};
    CRUCIBLE_FATAL_INVARIANT(!flag.load_acquire());
    CRUCIBLE_FATAL_INVARIANT(flag.cas_acq_rel(false, true).is_ok());
    CRUCIBLE_FATAL_INVARIANT(flag.cas_acq_rel(false, true).is_err());

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
    CRUCIBLE_FATAL_INVARIANT(refused.is_err() && same_pair(observed_by(refused, Pair{3, 5}), Pair{3, 4}));
    auto const swapped = cell.cas_acq_rel(Pair{3, 4}, Pair{8, 9});
    CRUCIBLE_FATAL_INVARIANT(swapped.is_ok() && same_pair(cell.load_acquire(), Pair{8, 9}));

    cell.store_release(Pair{10, 11});
    CRUCIBLE_FATAL_INVARIANT(same_pair(cell.load_acquire(), Pair{10, 11}));
}

// A slot number with a niche: the largest value is the empty value, and no
// slot holds it.  An Option of it has the four bytes of the slot and one bit
// pattern for each value, so a cell holds the Option as one word.
struct SlotNumber {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr SlotNumber niche_empty(::foundation::core::niche_key<SlotNumber> const&) noexcept {
        return SlotNumber{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<SlotNumber> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

using SlotOption = ::foundation::core::Option<SlotNumber>;

// True when the Option holds the slot of the number raw.
[[nodiscard]] bool holds_slot(SlotOption option, std::uint32_t raw) noexcept {
    return std::move(option).match([raw](SlotNumber slot) noexcept { return slot.raw == raw; },
                                   []() noexcept { return false; });
}

// A cell of a niche Option: an empty cell, a full cell, and a
// compare-and-swap that compares the Option as one word.
void test_niche_option_cell() {
    Atomic<SlotOption> cell{};
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());
    cell.store_release(SlotOption::some(SlotNumber{7}));
    CRUCIBLE_FATAL_INVARIANT(holds_slot(cell.load_acquire(), 7));
    CRUCIBLE_FATAL_INVARIANT(holds_slot(cell.exchange_acq_rel(SlotOption{}), 7));
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());

    CRUCIBLE_FATAL_INVARIANT(cell.cas_acq_rel(SlotOption{}, SlotOption::some(SlotNumber{3})).is_ok());
    auto const refused = cell.cas_acq_rel(SlotOption{}, SlotOption::some(SlotNumber{4}));
    CRUCIBLE_FATAL_INVARIANT(refused.is_err() && holds_slot(observed_by(refused, SlotOption{}), 3));
    // A full Option of a different slot is a different value.
    CRUCIBLE_FATAL_INVARIANT(cell.cas_acq_rel(SlotOption::some(SlotNumber{4}), SlotOption{}).is_err());
    CRUCIBLE_FATAL_INVARIANT(cell.cas_acq_rel(SlotOption::some(SlotNumber{3}), SlotOption{}).is_ok());
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());

    // The slot 0 has zero bytes, and its Option is full.
    cell.store_release(SlotOption::some(SlotNumber{0}));
    CRUCIBLE_FATAL_INVARIANT(holds_slot(cell.load_acquire(), 0));

    auto writer = mint_sole_writer<HeadRole>();
    Atomic<SlotOption, decltype(writer)> owned{};
    CRUCIBLE_FATAL_INVARIANT(owned.load_owned(writer).is_none());
    owned.store_release(writer, SlotOption::some(SlotNumber{9}));
    CRUCIBLE_FATAL_INVARIANT(holds_slot(owned.load_owned(writer), 9) && holds_slot(owned.load_acquire(), 9));
    ::foundation::linear::drop(std::move(writer));
}

using SeatOption = ::foundation::core::Option<Seat>;

// The number of a full Option, or zero for an empty Option.  No seat has the
// number zero.
[[nodiscard]] std::uint32_t number_or_zero(SeatOption option) noexcept {
    return std::move(option).match([](Seat seat) noexcept { return seat.number(); },
                                   []() noexcept { return std::uint32_t{0}; });
}

// The Option of a number of the model: empty for zero, and full for each
// other number.
[[nodiscard]] SeatOption option_of(std::uint32_t number) noexcept {
    return number == 0 ? SeatOption{} : SeatOption::some(Seat::of(number));
}

// A cell of a value with a word: the cell holds the number of the seat, and a
// compare-and-swap compares the numbers.
void test_word_cell() {
    Atomic<Seat> cell{Seat::of(3)};
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().number() == 3);
    cell.store_release(Seat::of(5));
    CRUCIBLE_FATAL_INVARIANT(cell.exchange_acq_rel(Seat::of(6)).number() == 5);
    CRUCIBLE_FATAL_INVARIANT(cell.cas_acq_rel(Seat::of(6), Seat::of(7)).is_ok());
    auto const refused = cell.cas_acq_rel(Seat::of(6), Seat::of(8));
    CRUCIBLE_FATAL_INVARIANT(refused.is_err() && observed_by(refused, Seat::of(6)).number() == 7);
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().number() == 7);

    auto writer = mint_sole_writer<TailRole>();
    Atomic<Seat, decltype(writer)> owned{Seat::of(2)};
    owned.store_release(writer, Seat::of(owned.load_owned(writer).number() + 1));
    CRUCIBLE_FATAL_INVARIANT(owned.load_owned(writer).number() == 3 && owned.load_acquire().number() == 3);
    ::foundation::linear::drop(std::move(writer));
}

// A cell of an Option of a value with a word: the empty word is the empty
// Option, and the cell makes no seat from it.
void test_word_option_cell() {
    Atomic<SeatOption> cell{};
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());
    CRUCIBLE_FATAL_INVARIANT(cell.cas_acq_rel(SeatOption{}, option_of(4)).is_ok());
    auto const refused = cell.cas_acq_rel(SeatOption{}, option_of(9));
    CRUCIBLE_FATAL_INVARIANT(refused.is_err() && number_or_zero(observed_by(refused, SeatOption{})) == 4);
    CRUCIBLE_FATAL_INVARIANT(number_or_zero(cell.exchange_acq_rel(SeatOption{})) == 4);
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());

    auto writer = mint_sole_writer<TailRole>();
    Atomic<SeatOption, decltype(writer)> owned{};
    CRUCIBLE_FATAL_INVARIANT(owned.load_owned(writer).is_none());
    owned.store_release(writer, option_of(2));
    CRUCIBLE_FATAL_INVARIANT(number_or_zero(owned.load_owned(writer)) == 2);
    CRUCIBLE_FATAL_INVARIANT(number_or_zero(owned.load_acquire()) == 2);
    ::foundation::linear::drop(std::move(writer));
}

// The property: a random sequence of stores, exchanges and compare-and-swap
// operations on a cell of an Option of a seat agrees with a plain model, a
// number with zero for the empty Option.
void test_word_option_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDA703000000C6u};
    Atomic<SeatOption> cell{};
    std::uint32_t model = 0;
    for (int step = 0; step < 20000; ++step) {
        auto const number = static_cast<std::uint32_t>(stream.below(4));
        switch (stream.below(3)) {
            case 0:
                cell.store_release(option_of(number));
                model = number;
                break;
            case 1:
                CRUCIBLE_FATAL_INVARIANT(number_or_zero(cell.exchange_acq_rel(option_of(number))) == model);
                model = number;
                break;
            default: {
                auto const expected = static_cast<std::uint32_t>(stream.below(4));
                auto const outcome = cell.cas_acq_rel(option_of(expected), option_of(number));
                CRUCIBLE_FATAL_INVARIANT(outcome.is_ok() == (expected == model));
                CRUCIBLE_FATAL_INVARIANT(number_or_zero(observed_by(outcome, option_of(expected))) == model);
                if (outcome.is_ok()) model = number;
                break;
            }
        }
        CRUCIBLE_FATAL_INVARIANT(number_or_zero(cell.load_acquire()) == model);
    }
}

// A protocol whose to_word gives the empty word for a value would make a full
// Option read back as empty.  The store of that Option ends the process, and
// the cell keeps its value.
void test_word_option_store_of_empty_word_aborts() {
    using LossyOption = ::foundation::core::Option<LossySeat>;
    Atomic<LossyOption> cell{};
    CRUCIBLE_FATAL_INVARIANT(
        ::crucible::test::aborts([&cell] { cell.store_release(LossyOption::some(LossySeat::of(1))); }));
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());
    cell.store_release(LossyOption::some(LossySeat::of(2)));
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_some());
}

// The brand of the slots of the word test, and a slot that states it.  A Ref
// of a branded object takes the brand of the object, so to() of two slots
// gives one Ref type, and one cell holds the two Refs.
struct SlotBrand {};

struct BrandedSlot {
    using brand_type = SlotBrand;
    int value = 0;
};

using SlotRef = ::foundation::core::Ref<BrandedSlot, SlotBrand>;
using RefOption = ::foundation::core::Option<SlotRef>;
using RefWord = ::foundation::core::atomic_word<SlotRef>;

// True when a key of the type Key opens the from_word of a Ref, and from_word
// then gives a Ref with no throw.
template <class Key>
concept OpensRefWord = requires(Key const& key) {
    { RefWord::from_word(key, RefWord::empty_word) } noexcept -> std::same_as<SlotRef>;
};

// True when the Option holds a Ref of the slot.
[[nodiscard]] bool holds_object(RefOption option, BrandedSlot& slot) noexcept {
    return std::move(option).match(
        [&slot](SlotRef ref) noexcept { return ::foundation::core::same_object(ref, to(slot)); },
        []() noexcept { return false; });
}

// The word of a Ref: a cell of an Option of a Ref holds the pointer of the
// object, each read lends the object again with the brand of the Ref, and the
// empty Option is the null word.  The word protocol takes only the key of its
// cell, and the door of a Ref takes no key of the Atomic family.
void test_ref_word_round_trip() {
    static_assert(::foundation::core::AtomicWordProtocol<SlotRef>);
    static_assert(::foundation::core::AtomicValue<RefOption> && sizeof(Atomic<RefOption>) == sizeof(RefOption));
    static_assert(OpensRefWord<::foundation::core::atomic_word_key<SlotRef>>);
    static_assert(!OpensRefWord<::foundation::core::atomic_word_key<int>>);
    static_assert(!std::is_constructible_v<SlotRef, BrandedSlot&, ::foundation::core::atomic_word_key<SlotRef> const&>);

    BrandedSlot first{.value = 1};
    BrandedSlot second{.value = 2};
    static_assert(std::is_same_v<decltype(to(first)), SlotRef>);
    Atomic<RefOption> cell{};
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());
    cell.store_release(RefOption::some(to(first)));
    CRUCIBLE_FATAL_INVARIANT(holds_object(cell.load_acquire(), first));
    CRUCIBLE_FATAL_INVARIANT(holds_object(cell.exchange_acq_rel(RefOption::some(to(second))), first));
    CRUCIBLE_FATAL_INVARIANT(holds_object(cell.load_acquire(), second));

    auto const refused = cell.cas_acq_rel(RefOption::some(to(first)), RefOption{});
    CRUCIBLE_FATAL_INVARIANT(refused.is_err() && holds_object(observed_by(refused, RefOption{}), second));
    CRUCIBLE_FATAL_INVARIANT(cell.cas_acq_rel(RefOption::some(to(second)), RefOption{}).is_ok());
    CRUCIBLE_FATAL_INVARIANT(cell.load_acquire().is_none());

    // A cell of a Ref gives Refs of its brand, and each one reaches the
    // object of the Ref that the cell holds.
    Atomic<SlotRef> plain{to(first)};
    static_assert(std::is_same_v<decltype(plain.load_acquire()), SlotRef>);
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::same_object(plain.load_acquire(), to(first)));
    plain.load_acquire()->value = 5;
    CRUCIBLE_FATAL_INVARIANT(first.value == 5);
}

void test_cache_line() {
    CacheLine<Atomic<std::uint64_t>> head{std::uint64_t{4}};
    CacheLine<Tally> tally{};
    CRUCIBLE_FATAL_INVARIANT(head->load_acquire() == 4);
    (*head).store_release(8);
    CRUCIBLE_FATAL_INVARIANT((*head).load_acquire() == 8);
    tally->add(1);
    CRUCIBLE_FATAL_INVARIANT(tally->read() == 1);
    CacheLine<Atomic<std::uint64_t>> const& read_only = head;
    CRUCIBLE_FATAL_INVARIANT(read_only->load_acquire() == 8 && (*read_only).load_acquire() == 8);
    // Each line starts at the start of a cache line, and each borrow of the
    // cell starts at the start of its line.
    auto const line_address = address_of(to(head));
    CRUCIBLE_FATAL_INVARIANT(line_address.is_aligned<64>());
    CRUCIBLE_FATAL_INVARIANT(address_of(to(tally)).is_aligned<64>());
    CRUCIBLE_FATAL_INVARIANT(address_of(to(*head)) == line_address);
    CRUCIBLE_FATAL_INVARIANT(address_of(to(*head.operator->())) == line_address);
}

// The property: a random sequence of operations on one cell agrees with
// a plain model that applies the same operations to an integer.
void test_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDA703000000C3u};
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
                CRUCIBLE_FATAL_INVARIANT(observed_by(outcome, expected) == model);
                CRUCIBLE_FATAL_INVARIANT(outcome.is_ok() == (expected == model));
                if (outcome.is_ok()) model = operand ^ 0x5555u;
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
    ::crucible::test::PhiloxStream stream{0x5EEDA703000000C4u};
    Atomic<std::int64_t> cell{};
    std::int64_t model = 0;
    for (int step = 0; step < 20000; ++step) {
        // The conversion keeps the bits of the word, and it is defined for
        // each value.
        std::int64_t const operand = static_cast<std::int64_t>(stream.next_wide());
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
                if (outcome.is_ok()) break;
                seen = observed_by(outcome, seen);
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
            token_sum->add(token.exchange_acq_rel(worker * steps_per_worker + step + 1));
        }
    });
    CRUCIBLE_FATAL_INVARIANT(remaining.load_acquire() == 0);
    CRUCIBLE_FATAL_INVARIANT(bits.load_acquire() == ~std::uint64_t{0});
    std::int64_t const last_signed_step = static_cast<std::int64_t>(steps_per_worker - 1);
    CRUCIBLE_FATAL_INVARIANT(highest.load_acquire() == last_signed_step * 4 + 3);
    CRUCIBLE_FATAL_INVARIANT(lowest.load_acquire() == -(last_signed_step * 4 + 3));
    std::uint64_t const token_count = 4 * steps_per_worker;
    CRUCIBLE_FATAL_INVARIANT(token_sum->read() + token.load_acquire() == token_count * (token_count + 1) / 2);
}

void test_tally_under_contention() {
    CacheLine<Tally> tally{};
    run_on_four_threads([&tally] {
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            tally->add(1);
        }
    });
    CRUCIBLE_FATAL_INVARIANT(tally->read() == 4 * steps_per_worker);
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
        bool const is_writer = role->fetch_add_acq_rel(1) == 0;
        if (is_writer) {
            payload_first = 0x1111;
            payload_second = 0x2222;
            published->store_release(1);
            return;
        }
        while (published->load_acquire() == 0) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (payload_first == 0x1111 && payload_second == 0x2222) complete_reads->add(1);
    });
    CRUCIBLE_FATAL_INVARIANT(complete_reads->read() == 3);
}

// Each worker sets and clears its own bits of one shared word.  Only the
// worker touches its bits, so each fetch_or finds its bit clear and each
// fetch_and finds it set, whatever the other workers do to the word.
void test_fetch_and_under_contention() {
    Atomic<std::uint64_t> bits{};
    Atomic<std::uint64_t> next_worker{};
    CacheLine<Tally> wrong_bits{};
    run_on_four_threads([&] {
        std::uint64_t const worker = next_worker.fetch_add_acq_rel(1);
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            std::uint64_t const own_bit = std::uint64_t{1} << (worker * 16 + step % 16);
            if ((bits.fetch_or_acq_rel(own_bit) & own_bit) != 0) wrong_bits->add(1);
            if ((bits.fetch_and_acq_rel(~own_bit) & own_bit) == 0) wrong_bits->add(1);
        }
    });
    CRUCIBLE_FATAL_INVARIANT(wrong_bits->read() == 0);
    CRUCIBLE_FATAL_INVARIANT(bits.load_acquire() == 0);
}

// Four threads take turns to own a cell of a niche Option.  A thread claims
// the empty cell with a compare-and-swap, adds one to a plain count while it
// holds the claim, and gives the cell back.  A refusal reads the slot of a
// different thread.  ThreadSanitizer reports a race on the plain count if an
// order is missing.
void test_niche_option_claim_under_contention() {
    constexpr std::uint64_t claims_per_worker = 2000;
    CacheLine<Atomic<SlotOption>> owner{};
    CacheLine<Atomic<std::uint64_t>> next_worker{std::uint64_t{0}};
    CacheLine<Tally> wrong_reads{};
    std::uint64_t claims = 0;
    run_on_four_threads([&] {
        auto const worker = static_cast<std::uint32_t>(next_worker->fetch_add_acq_rel(1));
        for (std::uint64_t claim = 0; claim < claims_per_worker; ++claim) {
            for (;;) {
                auto const outcome = owner->cas_acq_rel(SlotOption{}, SlotOption::some(SlotNumber{worker}));
                if (outcome.is_ok()) break;
                SlotOption const seen = observed_by(outcome, SlotOption{});
                if (seen.is_none() || holds_slot(seen, worker)) wrong_reads->add(1);
                CRUCIBLE_SPIN_PAUSE;
            }
            ++claims;
            if (owner->cas_acq_rel(SlotOption::some(SlotNumber{worker}), SlotOption{}).is_err()) wrong_reads->add(1);
        }
    });
    CRUCIBLE_FATAL_INVARIANT(wrong_reads->read() == 0 && claims == 4 * claims_per_worker);
    CRUCIBLE_FATAL_INVARIANT(owner->load_acquire().is_none());
}

// One thread stores a full Option of a seat after it writes a plain payload,
// and the other three wait for it with acquire loads.  ThreadSanitizer reports
// a race on the payload if an order is missing.
void test_word_option_publishes_to_acquire() {
    CacheLine<Atomic<SeatOption>> published{};
    std::uint64_t payload = 0;
    CacheLine<Atomic<std::uint64_t>> next_worker{std::uint64_t{0}};
    CacheLine<Tally> complete_reads{};
    run_on_four_threads([&] {
        if (next_worker->fetch_add_acq_rel(1) == 0) {
            payload = 0x4444;
            published->store_release(option_of(11));
            return;
        }
        while (published->load_acquire().is_none()) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (payload == 0x4444 && number_or_zero(published->load_acquire()) == 11) complete_reads->add(1);
    });
    CRUCIBLE_FATAL_INVARIANT(complete_reads->read() == 3);
}

// The cell of one writer, on one thread: the writer reads its own value
// with load_owned, and each read agrees with load_acquire.
void test_sole_writer_single_thread() {
    auto writer = mint_sole_writer<HeadRole>();
    static_assert(!AddressTakesBrand<decltype(writer)::brand_type>,
                  "the address of the mint with a target type of the held brand gives no second writer of it");
    Atomic<std::uint64_t, decltype(writer)> head{std::uint64_t{3}};
    CRUCIBLE_FATAL_INVARIANT(head.load_owned(writer) == 3 && head.load_acquire() == 3);
    head.store_release(writer, head.load_owned(writer) + 4);
    CRUCIBLE_FATAL_INVARIANT(head.load_owned(writer) == 7 && head.load_acquire() == 7);

    // One writer can own more than one cell, also a cell of a class value.
    Atomic<Pair, decltype(writer)> pair_cell{};
    CRUCIBLE_FATAL_INVARIANT(same_pair(pair_cell.load_owned(writer), Pair{0, 0}));
    pair_cell.store_release(writer, Pair{5, 6});
    CRUCIBLE_FATAL_INVARIANT(same_pair(pair_cell.load_owned(writer), Pair{5, 6}));
    CRUCIBLE_FATAL_INVARIANT(same_pair(pair_cell.load_acquire(), Pair{5, 6}));

    auto tail_writer = mint_sole_writer<TailRole>();
    Atomic<Phase, decltype(tail_writer)> phase{Phase::idle};
    phase.store_release(tail_writer, Phase::done);
    CRUCIBLE_FATAL_INVARIANT(phase.load_owned(tail_writer) == Phase::done);

    ::foundation::linear::drop(std::move(tail_writer));
    ::foundation::linear::drop(std::move(writer));
}

// The property: a random sequence of the writes of the writer agrees with a
// plain model, and each read of the writer and each acquire read give the
// value of the model.
void test_sole_writer_agrees_with_plain_model() {
    ::crucible::test::PhiloxStream stream{0x5EEDA703000000C5u};
    auto writer = mint_sole_writer<HeadRole>();
    Atomic<std::uint64_t, decltype(writer)> cell{};
    std::uint64_t model = 0;
    for (int step = 0; step < 20000; ++step) {
        std::uint64_t const operand = stream.next_wide();
        if (stream.below(2) == 0) {
            cell.store_release(writer, operand);
            model = operand;
        } else {
            cell.store_release(writer, cell.load_owned(writer) + operand);
            model += operand;
        }
        CRUCIBLE_FATAL_INVARIANT(cell.load_owned(writer) == model && cell.load_acquire() == model);
    }
    ::foundation::linear::drop(std::move(writer));
}

// One thread writes the cell, and the other three read it.  Each read of
// the writer gives the value that the writer stored last, and each reader
// sees values that never decrease.  The threads borrow the writer through a
// Ref to a const writer, so no thread can end it, and the test ends it after
// the join.
void test_sole_writer_under_contention() {
    auto writer = mint_sole_writer<HeadRole>();
    decltype(writer) const& shared_writer = writer;
    auto const lent_writer = to(shared_writer);
    CacheLine<Atomic<std::uint64_t, decltype(writer)>> head{std::uint64_t{0}};
    CacheLine<Atomic<std::uint64_t>> next_worker{std::uint64_t{0}};
    CacheLine<Tally> wrong_owned_reads{};
    CacheLine<Tally> backward_reads{};
    run_on_four_threads([&, lent_writer] {
        if (next_worker->fetch_add_acq_rel(1) == 0) {
            for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
                std::uint64_t const own = head->load_owned(*lent_writer);
                if (own != step) wrong_owned_reads->add(1);
                head->store_release(*lent_writer, own + 1);
            }
            return;
        }
        std::uint64_t last = 0;
        while (last < steps_per_worker) {
            std::uint64_t const seen = head->load_acquire();
            if (seen < last) backward_reads->add(1);
            last = seen;
            CRUCIBLE_SPIN_PAUSE;
        }
    });
    CRUCIBLE_FATAL_INVARIANT(wrong_owned_reads->read() == 0 && backward_reads->read() == 0);
    CRUCIBLE_FATAL_INVARIANT(head->load_owned(writer) == steps_per_worker);
    ::foundation::linear::drop(std::move(writer));
}

// The store of the writer publishes the plain writes before it, as the
// release store of a shared cell does.  ThreadSanitizer reports a race on
// the payload if an order is missing.
void test_sole_writer_release_publishes_to_acquire() {
    auto writer = mint_sole_writer<HeadRole>();
    decltype(writer) const& shared_writer = writer;
    auto const lent_writer = to(shared_writer);
    CacheLine<Atomic<std::uint64_t, decltype(writer)>> published{std::uint64_t{0}};
    std::uint64_t payload = 0;
    CacheLine<Atomic<std::uint64_t>> next_worker{std::uint64_t{0}};
    CacheLine<Tally> complete_reads{};
    run_on_four_threads([&, lent_writer] {
        if (next_worker->fetch_add_acq_rel(1) == 0) {
            payload = 0x3333;
            published->store_release(*lent_writer, 1);
            return;
        }
        while (published->load_acquire() == 0) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (payload == 0x3333) complete_reads->add(1);
    });
    CRUCIBLE_FATAL_INVARIANT(complete_reads->read() == 3);
    ::foundation::linear::drop(std::move(writer));
}

// The writer changes thread through the start and the join of a fork.  A
// thread of the first fork writes the cell, the caller reads it after the
// join, and a thread of the second fork reads the value of the caller.
void test_sole_writer_moves_through_a_join() {
    auto writer = mint_sole_writer<HeadRole>();
    decltype(writer) const& shared_writer = writer;
    auto const lent_writer = to(shared_writer);
    CacheLine<Atomic<std::uint64_t, decltype(writer)>> head{std::uint64_t{0}};
    CacheLine<Atomic<std::uint64_t>> next_worker{std::uint64_t{0}};
    run_on_four_threads([&, lent_writer] {
        if (next_worker->fetch_add_acq_rel(1) != 0) return;
        for (std::uint64_t step = 0; step < steps_per_worker; ++step) {
            head->store_release(*lent_writer, head->load_owned(*lent_writer) + 1);
        }
    });
    CRUCIBLE_FATAL_INVARIANT(head->load_owned(writer) == steps_per_worker);
    head->store_release(writer, 5);
    next_worker->store_release(0);
    CacheLine<Tally> wrong_owned_reads{};
    run_on_four_threads([&, lent_writer] {
        if (next_worker->fetch_add_acq_rel(1) != 0) return;
        if (head->load_owned(*lent_writer) != 5) wrong_owned_reads->add(1);
        head->store_release(*lent_writer, 9);
    });
    CRUCIBLE_FATAL_INVARIANT(wrong_owned_reads->read() == 0 && head->load_owned(writer) == 9);
    ::foundation::linear::drop(std::move(writer));
}

}  // namespace

int main() {
    test_single_thread_operations();
    test_read_modify_write_operations();
    test_class_value();
    test_niche_option_cell();
    test_word_cell();
    test_word_option_cell();
    test_word_option_agrees_with_plain_model();
    test_word_option_store_of_empty_word_aborts();
    test_ref_word_round_trip();
    test_cache_line();
    test_agrees_with_plain_model();
    test_signed_bounds_agree_with_plain_model();
    test_fetch_add_under_contention();
    test_cas_under_contention();
    test_read_modify_write_under_contention();
    test_tally_under_contention();
    test_release_publishes_to_acquire();
    test_fetch_and_under_contention();
    test_niche_option_claim_under_contention();
    test_word_option_publishes_to_acquire();
    test_sole_writer_single_thread();
    test_sole_writer_agrees_with_plain_model();
    test_sole_writer_under_contention();
    test_sole_writer_release_publishes_to_acquire();
    test_sole_writer_moves_through_a_join();
    return 0;
}
