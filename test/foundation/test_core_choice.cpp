// Tests of foundation/core/Choice.h: Option, none, expect, value_or, match
// and the loop of zero or one turns.

#include <foundation/core/Choice.h>

#include <foundation/Platform.h>

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

void test_some() {
    Option<int> holder = Option<int>::some(42);
    CRUCIBLE_FATAL_INVARIANT(holder.is_some() && !holder.is_none());
    int const taken = static_cast<Option<int>&&>(holder).expect("the holder was built with a value");
    CRUCIBLE_FATAL_INVARIANT(taken == 42);
}

// The probe reads the Option after expect() on purpose: expect() leaves
// the source empty.
void test_expect_empties_the_source() {
    Option<int> holder = Option<int>::some(43);
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<int>&&>(holder).expect("the holder was built with a value") == 43);
    CRUCIBLE_FATAL_INVARIANT(holder.is_none());
}

void test_expect() {
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(7).expect("the test gives a seven") == 7);
    bool const empty_expect_aborts = ::foundation::test::aborts(
        [] { static_cast<void>(Option<int>{}.expect("the test option is empty on purpose")); });
    CRUCIBLE_FATAL_INVARIANT(empty_expect_aborts);
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
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<int>&&>(holder).expect("the loop changed the value") == 2);
    CRUCIBLE_FATAL_INVARIANT(fixed_sum == 5);
    CRUCIBLE_FATAL_INVARIANT(temporary_sum == 3);
}

// A read through the end cursor ends the process: a cursor never reads an
// empty Option.
void test_end_cursor_read_aborts() {
    Option<int> holder = Option<int>::some(9);
    bool const end_read_aborts = ::foundation::test::aborts([&holder] { static_cast<void>(*holder.end()); });
    CRUCIBLE_FATAL_INVARIANT(end_read_aborts);
    Option<int> empty{};
    bool const empty_read_aborts = ::foundation::test::aborts([&empty] { static_cast<void>(*empty.begin()); });
    CRUCIBLE_FATAL_INVARIANT(empty_read_aborts);
}

// The probe reads each owning Option after its move and after expect() on
// purpose: each one leaves the source empty.
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
        CountedOwner const taken = static_cast<Option<CountedOwner>&&>(target).expect("the target took the owner");
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

void test_value_or() {
    CRUCIBLE_FATAL_INVARIANT(Option<int>::some(4).value_or(9) == 4);
    CRUCIBLE_FATAL_INVARIANT(Option<int>{}.value_or(9) == 9);
    {
        Option<CountedOwner> held = Option<CountedOwner>::some(CountedOwner{21});
        CountedOwner const taken = static_cast<Option<CountedOwner>&&>(held).value_or(CountedOwner{22});
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 21);
        Option<CountedOwner> empty{};
        CountedOwner const fallback = static_cast<Option<CountedOwner>&&>(empty).value_or(CountedOwner{23});
        CRUCIBLE_FATAL_INVARIANT(fallback.handle() == 23);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// The probe reads the source after the rvalue match on purpose: the match
// moves the payload out first.
void test_match() {
    Option<int> const held = Option<int>::some(5);
    Option<int> const empty{};
    auto const doubled = [](int const& value) noexcept { return value * 2; };
    auto const missing = [] noexcept { return -1; };
    CRUCIBLE_FATAL_INVARIANT(held.match(doubled, missing) == 10);
    CRUCIBLE_FATAL_INVARIANT(empty.match(doubled, missing) == -1);
    CRUCIBLE_FATAL_INVARIANT(held.is_some());

    int some_runs = 0;
    int none_runs = 0;
    held.match([&some_runs](int const&) noexcept { ++some_runs; }, [&none_runs] noexcept { ++none_runs; });
    empty.match([&some_runs](int const&) noexcept { ++some_runs; }, [&none_runs] noexcept { ++none_runs; });
    CRUCIBLE_FATAL_INVARIANT(some_runs == 1 && none_runs == 1);

    {
        Option<CountedOwner> source = Option<CountedOwner>::some(CountedOwner{31});
        bool source_was_empty_in_arm = false;
        int const handle = static_cast<Option<CountedOwner>&&>(source).match(
            [&source, &source_was_empty_in_arm](CountedOwner&& owner) noexcept {
                source_was_empty_in_arm = source.is_none();
                CountedOwner const kept{static_cast<CountedOwner&&>(owner)};
                return kept.handle();
            },
            [] noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(handle == 31 && source_was_empty_in_arm && source.is_none());
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
    CRUCIBLE_FATAL_INVARIANT(static_cast<Option<SlotNumber>&&>(held_slot).expect("the slot was built").raw == 5);
    for (SlotNumber const& slot : copied_slot) {
        CRUCIBLE_FATAL_INVARIANT(slot.raw == 5);
    }
}

// A payload equal to the empty value of its niche would read as no value,
// so Option::some refuses it in each build.
void test_niche_empty_value_aborts() {
    bool const empty_value_aborts =
        ::foundation::test::aborts([] { static_cast<void>(Option<SlotNumber>::some(SlotNumber{0xFFFFFFFFu})); });
    CRUCIBLE_FATAL_INVARIANT(empty_value_aborts);
}

// The property: for each value and each choice of empty or full, the
// Option agrees with a plain model of one flag and one value.  The loop
// makes one turn exactly when the model holds a value, and expect,
// value_or and match give the value back.
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
        std::uint64_t const fallback = ~model_value;
        std::uint64_t const matched = wide.match([](std::uint64_t const& value) noexcept { return value; },
                                                 [fallback] noexcept { return fallback; });
        CRUCIBLE_FATAL_INVARIANT(matched == (model_holds ? model_value : fallback));
        CRUCIBLE_FATAL_INVARIANT(static_cast<Option<std::uint64_t>&&>(wide).value_or(fallback) == matched);

        std::uint32_t const raw_slot = static_cast<std::uint32_t>(stream.below(0xFFFFFFFFu));
        Option<SlotNumber> slot = model_holds ? Option<SlotNumber>::some(SlotNumber{raw_slot}) : Option<SlotNumber>{};
        CRUCIBLE_FATAL_INVARIANT(slot.is_some() == model_holds);
        if (model_holds) {
            CRUCIBLE_FATAL_INVARIANT(static_cast<Option<SlotNumber>&&>(slot).expect("the model holds a slot").raw
                                     == raw_slot);
        }
    }
}

// The error of the Result tests.
enum class Refusal : std::uint8_t {
    full,
    closed
};

using ::foundation::core::err;
using ::foundation::core::Result;
using ::foundation::core::Unit;

[[nodiscard]] Result<int, Refusal> half_of(int value) noexcept {
    if (value % 2 != 0) return err(Refusal::closed);
    return value / 2;
}

void test_result_roads() {
    Result<int, Refusal> const even = half_of(8);
    Result<int, Refusal> const odd = half_of(7);
    CRUCIBLE_FATAL_INVARIANT(even.is_ok() && !even.is_err());
    CRUCIBLE_FATAL_INVARIANT(odd.is_err() && !odd.is_ok());
    CRUCIBLE_FATAL_INVARIANT(even.err().is_none());
    CRUCIBLE_FATAL_INVARIANT(odd.err().expect("an odd value is refused") == Refusal::closed);
    CRUCIBLE_FATAL_INVARIANT(half_of(8).ok().expect("an even value has a half") == 4);
    CRUCIBLE_FATAL_INVARIANT(half_of(7).ok().is_none());
    CRUCIBLE_FATAL_INVARIANT(half_of(8).value_or(-1) == 4);
    CRUCIBLE_FATAL_INVARIANT(half_of(7).value_or(-1) == -1);
    CRUCIBLE_FATAL_INVARIANT(half_of(8).expect("an even value has a half") == 4);

    auto const doubled = [](int const& value) noexcept { return value * 2; };
    auto const coded = [](Refusal refusal) noexcept { return refusal == Refusal::closed ? -2 : -3; };
    CRUCIBLE_FATAL_INVARIANT(even.match(doubled, coded) == 8);
    CRUCIBLE_FATAL_INVARIANT(odd.match(doubled, coded) == -2);
    CRUCIBLE_FATAL_INVARIANT(half_of(6).match([](int&& value) noexcept { return value; }, coded) == 3);

    bool const error_expect_aborts =
        ::foundation::test::aborts([] { static_cast<void>(half_of(7).expect("the test value is odd on purpose")); });
    CRUCIBLE_FATAL_INVARIANT(error_expect_aborts);
}

// A Result of Unit gives no value, so its expect() gives void.
void test_result_of_unit() {
    auto const close_when = [](bool is_open) noexcept -> Result<Unit, Refusal> {
        if (!is_open) return err(Refusal::closed);
        return Unit{};
    };
    close_when(true).expect("an open door closes");
    CRUCIBLE_FATAL_INVARIANT(close_when(false).is_err());
    bool const unit_expect_aborts =
        ::foundation::test::aborts([&close_when] { close_when(false).expect("the test door is closed on purpose"); });
    CRUCIBLE_FATAL_INVARIANT(unit_expect_aborts);
}

// A Result of a value that owns something moves the value once, and
// destroys each value one time.
void test_result_owning_value() {
    {
        Result<CountedOwner, Refusal> held{CountedOwner{41}};
        CRUCIBLE_FATAL_INVARIANT(live_owners == 1 && held.is_ok());
        Result<CountedOwner, Refusal> moved{static_cast<Result<CountedOwner, Refusal>&&>(held)};
        CRUCIBLE_FATAL_INVARIANT(live_owners == 2);
        CountedOwner const taken =
            static_cast<Result<CountedOwner, Refusal>&&>(moved).expect("the Result was built with a value");
        CRUCIBLE_FATAL_INVARIANT(taken.handle() == 41);
        Result<CountedOwner, Refusal> refused{err(Refusal::full)};
        CRUCIBLE_FATAL_INVARIANT(refused.is_err() && live_owners == 3);
    }
    CRUCIBLE_FATAL_INVARIANT(live_owners == 0);
}

// The property: half_of agrees with a plain model for random values, on
// each road to the value and to the error.
void test_result_agrees_with_plain_model() {
    ::foundation::test::PhiloxStream stream{0x5EEDC401CE000002u};
    for (int trial = 0; trial < 4096; ++trial) {
        int const value = static_cast<int>(stream.below(1u << 20));
        bool const model_is_even = value % 2 == 0;
        Result<int, Refusal> const outcome = half_of(value);
        CRUCIBLE_FATAL_INVARIANT(outcome.is_ok() == model_is_even);
        int const matched =
            outcome.match([](int const& half) noexcept { return half; }, [](Refusal) noexcept { return -1; });
        CRUCIBLE_FATAL_INVARIANT(matched == (model_is_even ? value / 2 : -1));
        CRUCIBLE_FATAL_INVARIANT(half_of(value).value_or(-1) == matched);
        CRUCIBLE_FATAL_INVARIANT(half_of(value).ok().is_some() == model_is_even);
        CRUCIBLE_FATAL_INVARIANT(outcome.err().is_some() == !model_is_even);
    }
}

}  // namespace

int main() {
    test_empty_states();
    test_some();
    test_expect_empties_the_source();
    test_expect();
    test_loop();
    test_end_cursor_read_aborts();
    test_owning_payload();
    test_value_or();
    test_match();
    test_niche_payload();
    test_niche_empty_value_aborts();
    test_agrees_with_plain_model();
    test_result_roads();
    test_result_of_unit();
    test_result_owning_value();
    test_result_agrees_with_plain_model();
    return 0;
}
