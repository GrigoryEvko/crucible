// Tests of foundation/core/Choice.h: Option, none, consume, expect and the
// loop of zero or one turns.

#include <foundation/core/Choice.h>

#include "abort_probe.h"
#include "philox_stream.h"

#include <cstdint>

namespace {

using ::foundation::core::none;
using ::foundation::core::Option;

// The count of live CountedOwner objects.  The owning form must destroy
// each payload one time and leak none.
int live_owners = 0;

class CountedOwner {
public:
    explicit CountedOwner(int handle) noexcept : handle_{handle} { ++live_owners; }
    CountedOwner(CountedOwner&& other) noexcept : handle_{other.handle_} {
        other.handle_ = 0;
        ++live_owners;
    }
    CountedOwner(CountedOwner const&) = delete;
    CountedOwner& operator=(CountedOwner const&) = delete;
    CountedOwner& operator=(CountedOwner&&) = delete;
    ~CountedOwner() { --live_owners; }

    [[nodiscard]] int handle() const noexcept { return handle_; }

private:
    int handle_ = 0;
};

struct SlotNumber {
    std::uint32_t raw = 0;
};

}  // namespace

template <>
struct foundation::core::niche<SlotNumber> {
    [[nodiscard]] static constexpr SlotNumber empty() noexcept { return SlotNumber{0xFFFFFFFFu}; }
    [[nodiscard]] static constexpr bool is_empty(SlotNumber const& slot) noexcept { return slot.raw == 0xFFFFFFFFu; }
};

namespace {

void test_empty_states() {
    Option<int> defaulted{};
    Option<int> from_marker = none;
    CRUCIBLE_FATAL_INVARIANT(defaulted.is_none() && !defaulted.is_some());
    CRUCIBLE_FATAL_INVARIANT(from_marker.is_none() && !from_marker.is_some());
    int turns = 0;
    for ([[maybe_unused]] int& value : defaulted) {
        ++turns;
    }
    CRUCIBLE_FATAL_INVARIANT(turns == 0);
}

void test_some_and_consume() {
    Option<int> holder = Option<int>::some(42);
    CRUCIBLE_FATAL_INVARIANT(holder.is_some() && !holder.is_none());
    int const taken = static_cast<Option<int>&&>(holder).consume();
    CRUCIBLE_FATAL_INVARIANT(taken == 42);
}

// The probe reads the Option after consume() on purpose: consume() leaves
// the source empty.
void test_consume_empties_the_source() {
    Option<int> holder = Option<int>::some(43);
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<int>&&>(holder).consume() == 43);
    CRUCIBLE_FATAL_INVARIANT(holder.is_none());
}

void test_expect() {
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(7).expect("the test gives a seven") == 7);
    bool const empty_expect_aborts = ::foundation::test::aborts(
        [] { static_cast<void>(Option<int>{}.expect("the test option is empty on purpose")); });
    CRUCIBLE_FATAL_INVARIANT(empty_expect_aborts);
}

void test_consume_of_empty_aborts() {
    bool const empty_consume_aborts = ::foundation::test::aborts([] { static_cast<void>(Option<int>{}.consume()); });
    CRUCIBLE_FATAL_INVARIANT(empty_consume_aborts);
}

void test_loop() {
    Option<int> holder = Option<int>::some(1);
    for (int& value : holder) {
        value += 1;
    }
    Option<int> const fixed_holder = Option<int>::some(5);
    int fixed_sum = 0;
    for (int const& value : fixed_holder) {
        fixed_sum += value;
    }
    int temporary_sum = 0;
    for (int const value : Option<int>::some(3)) {
        temporary_sum += value;
    }
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<int>&&>(holder).consume() == 2);
    CRUCIBLE_FATAL_INVARIANT(fixed_sum == 5);
    CRUCIBLE_FATAL_INVARIANT(temporary_sum == 3);
}

// The probe reads each owning Option after its move and after consume()
// on purpose: each one leaves the source empty.
void test_owning_payload() {
    {
        Option<CountedOwner> source = Option<CountedOwner>::some(CountedOwner{11});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
        Option<CountedOwner> target{static_cast<Option<CountedOwner>&&>(source)};
        CRUCIBLE_FATAL_INVARIANT(source.is_none() && target.is_some());
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
        int seen_handle = 0;
        for (CountedOwner const& owner : target) {
            seen_handle = owner.handle();
        }
        CRUCIBLE_FATAL_INVARIANT(seen_handle == 11);
        CountedOwner const taken = static_cast<Option<CountedOwner>&&>(target).consume();
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 11 && target.is_none());
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
    {
        Option<CountedOwner> dropped = Option<CountedOwner>::some(CountedOwner{12});
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && dropped.is_some());
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

void test_niche_payload() {
    static_assert(sizeof(Option<SlotNumber>) == sizeof(SlotNumber));
    Option<SlotNumber> empty_slot{};
    Option<SlotNumber> held_slot = Option<SlotNumber>::some(SlotNumber{5});
    Option<SlotNumber> const copied_slot = held_slot;
    CRUCIBLE_FATAL_INVARIANT(empty_slot.is_none());
    CRUCIBLE_FATAL_INVARIANT(held_slot.is_some() && copied_slot.is_some());
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<SlotNumber>&&>(held_slot).consume().raw == 5);
    for (SlotNumber const& slot : copied_slot) {
        CRUCIBLE_FATAL_INVARIANT(slot.raw == 5);
    }
}

// The property: for each value and each choice of empty or full, the
// Option agrees with a plain model of one flag and one value.  The loop
// makes one turn exactly when the model holds a value, and consume and
// expect give the value back.
void test_agrees_with_plain_model() {
    ::foundation::test::PhiloxStream stream{0x5EEDC401CE000001u};
    for (int trial = 0; trial < 4096; ++trial) {
        std::uint64_t const model_value = stream.next_wide();
        bool const model_holds = (stream.next_word() & 1u) != 0;
        Option<std::uint64_t> wide = model_holds ? Option<std::uint64_t>::some(model_value) : Option<std::uint64_t>{};
        CRUCIBLE_FATAL_INVARIANT(wide.is_some() == model_holds);
        int turns = 0;
        for (std::uint64_t const& value : wide) {
            CRUCIBLE_FATAL_INVARIANT(value == model_value);
            ++turns;
        }
        CRUCIBLE_FATAL_INVARIANT(turns == (model_holds ? 1 : 0));
        if (model_holds) {
            CRUCIBLE_FATAL_INVARIANT(static_cast<Option<std::uint64_t>&&>(wide).consume() == model_value);
        }

        std::uint32_t const raw_slot = static_cast<std::uint32_t>(stream.below(0xFFFFFFFFu));
        Option<SlotNumber> slot = model_holds ? Option<SlotNumber>::some(SlotNumber{raw_slot}) : Option<SlotNumber>{};
        CRUCIBLE_FATAL_INVARIANT(slot.is_some() == model_holds);
        if (model_holds) {
            CRUCIBLE_FATAL_INVARIANT(static_cast<Option<SlotNumber>&&>(slot).expect("the model holds a slot").raw
                                     == raw_slot);
        }
    }
}

}  // namespace

int main() {
    test_empty_states();
    test_some_and_consume();
    test_consume_empties_the_source();
    test_expect();
    test_consume_of_empty_aborts();
    test_loop();
    test_owning_payload();
    test_niche_payload();
    test_agrees_with_plain_model();
    return 0;
}
