// The differential test of the Choice family against the standard library.
// Random sequences of steps run on Option, Result and OneOf: build, assign,
// move, compare, match and the roads to a payload.  The same steps run on
// std::optional, std::expected and std::variant with the same payloads, and
// the std types are the oracle.  After each step, the test compares the
// state of each pair: the index of the active alternative and the payload.
// A fixed Philox seed gives each sequence, so each run makes the same steps.
//
// The payloads are plain, so a move of a sum copies its bytes and the source
// keeps its value, as the move of the std type does.  The documented
// differences are tested as differences: value_or, expect and ok_or of a sum
// empty their source, and those of the std types do not.  The code that
// names a std type is in one opt-out region of the class ORACLE.

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/OneOf.h>

#include "philox_stream.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <variant>

namespace {

using ::foundation::core::err;
using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::none;
using ::foundation::core::OneOf;
using ::foundation::core::Option;
using ::foundation::core::Result;

enum class Refusal : std::uint8_t {
    full,
    closed,
    gone,
};

// The number of sums of each kind, and the number of steps of a sequence.
inline constexpr std::size_t kSlots = 4;
inline constexpr int kSteps = 20000;

using Maybe = Option<std::uint64_t>;
using Outcome = Result<std::uint32_t, Refusal>;
using Reading = OneOf<std::uint32_t, Refusal, std::uint16_t>;
using Slot = Index<kSlots>;

// The state of one sum: the index of its active alternative, and its payload
// as a word.  An empty Option has the index 0 and the payload 0.  A Result has
// the error at index 0 and the value at index 1, as std::expected tells with
// has_value().
struct State {
    std::size_t which = 0;
    std::uint64_t payload = 0;

    [[nodiscard]] friend constexpr bool operator==(State, State) noexcept = default;
};

[[nodiscard]] State state_of(Maybe const& maybe) noexcept {
    return maybe.match([](std::uint64_t const& value) noexcept { return State{1, value}; },
                       [] noexcept { return State{0, 0}; });
}

[[nodiscard]] State state_of(Outcome const& outcome) noexcept {
    return outcome.match([](std::uint32_t const& value) noexcept { return State{1, value}; },
                         [](Refusal const& refusal) noexcept { return State{0, static_cast<std::uint64_t>(refusal)}; });
}

[[nodiscard]] State state_of(Reading const& reading) noexcept {
    State const state =
        reading.match([](std::uint32_t const& word) noexcept { return State{0, word}; },
                      [](Refusal const& refusal) noexcept { return State{1, static_cast<std::uint64_t>(refusal)}; },
                      [](std::uint16_t const& half) noexcept { return State{2, half}; });
    CRUCIBLE_FATAL_INVARIANT(reading.which().raw() == state.which);
    return state;
}

[[nodiscard]] Maybe make_maybe(bool holds, std::uint64_t value) noexcept {
    if (!holds) return none;
    return Maybe::some(value);
}

[[nodiscard]] Outcome make_outcome(bool is_value, std::uint32_t value, Refusal refusal) noexcept {
    if (!is_value) return err(refusal);
    return value;
}

[[nodiscard]] Reading make_reading(std::size_t which, std::uint32_t word) noexcept {
    if (which == 0) return word;
    if (which == 1) return static_cast<Refusal>(word % 3u);
    return static_cast<std::uint16_t>(word);
}

// The draws of one step: the operation, two slots and the values.  The values
// come from a small range, so equal payloads occur often.
struct Draw {
    std::uint64_t operation = 0;
    std::size_t target = 0;
    std::size_t source = 0;
    bool flag = false;
    std::uint32_t value = 0;
    std::size_t which = 0;
};

[[nodiscard]] Draw draw_step(::crucible::test::PhiloxStream& stream, std::uint64_t operations) noexcept {
    Draw draw{};
    draw.operation = stream.below(operations);
    draw.target = stream.below(kSlots);
    draw.source = stream.below(kSlots);
    draw.flag = (stream.next_word() & 1u) != 0;
    draw.value = static_cast<std::uint32_t>(stream.below(4));
    draw.which = stream.below(3);
    return draw;
}

[[nodiscard]] Slot slot_at(std::size_t raw) noexcept { return Slot::check(raw).expect("the draw is below the count"); }

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the std types that the Choice family replaces give the state of each step")

using MaybeOracle = std::optional<std::uint64_t>;
using OutcomeOracle = std::expected<std::uint32_t, Refusal>;
using ReadingOracle = std::variant<std::uint32_t, Refusal, std::uint16_t>;

[[nodiscard]] State oracle_state(MaybeOracle const& maybe) noexcept {
    return maybe.has_value() ? State{1, *maybe} : State{0, 0};
}

[[nodiscard]] State oracle_state(OutcomeOracle const& outcome) noexcept {
    return outcome.has_value() ? State{1, *outcome} : State{0, static_cast<std::uint64_t>(outcome.error())};
}

[[nodiscard]] State oracle_state(ReadingOracle const& reading) noexcept {
    switch (reading.index()) {
        case 0:
            return State{0, std::get<0>(reading)};
        case 1:
            return State{1, static_cast<std::uint64_t>(std::get<1>(reading))};
        default:
            return State{2, std::get<2>(reading)};
    }
}

// Each sum and its oracle, in the same slots.
template <class Sum, class Oracle>
struct Lane {
    FixedArray<Sum, kSlots> sums;
    std::array<Oracle, kSlots> oracles;

    [[nodiscard]] Sum& sum(std::size_t raw) noexcept { return sums.at(slot_at(raw)); }

    // The state of each pair agrees after each step.
    void expect_agree() noexcept {
        for (std::size_t raw = 0; raw < kSlots; ++raw) {
            CRUCIBLE_FATAL_INVARIANT(state_of(sum(raw)) == oracle_state(oracles.at(raw)));
        }
    }
};

void run_option_sequence(std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    Lane<Maybe, MaybeOracle> lane{};
    for (int step = 0; step < kSteps; ++step) {
        Draw const draw = draw_step(stream, 9);
        Maybe& target = lane.sum(draw.target);
        MaybeOracle& oracle = lane.oracles.at(draw.target);
        std::uint64_t const fallback = 100 + draw.value;
        switch (draw.operation) {
            case 0:
                target = make_maybe(draw.flag, draw.value);
                oracle = draw.flag ? MaybeOracle{draw.value} : MaybeOracle{};
                break;
            case 1:
                target = lane.sum(draw.source);
                oracle = lane.oracles.at(draw.source);
                break;
            case 2:
                target = std::move(lane.sum(draw.source));
                oracle = std::move(lane.oracles.at(draw.source));
                break;
            case 3:
                target = none;
                oracle = std::nullopt;
                break;
            case 4:
                CRUCIBLE_FATAL_INVARIANT((target == lane.sum(draw.source)) == (oracle == lane.oracles.at(draw.source)));
                CRUCIBLE_FATAL_INVARIANT((target != lane.sum(draw.source)) == (oracle != lane.oracles.at(draw.source)));
                break;
            case 5: {
                Maybe taken = target;
                CRUCIBLE_FATAL_INVARIANT(std::move(taken).value_or(fallback) == oracle.value_or(fallback));
                CRUCIBLE_FATAL_INVARIANT(taken.is_none());
                break;
            }
            case 6: {
                int turns = 0;
                for (std::uint64_t const& value : target) {
                    CRUCIBLE_FATAL_INVARIANT(oracle.has_value() && value == *oracle);
                    ++turns;
                }
                CRUCIBLE_FATAL_INVARIANT(turns == (oracle.has_value() ? 1 : 0));
                break;
            }
            case 7: {
                Maybe taken = target;
                Outcome const converted =
                    std::move(taken)
                        .ok_or(Refusal::gone)
                        .match(
                            [](std::uint64_t value) noexcept -> Outcome { return static_cast<std::uint32_t>(value); },
                            [](Refusal refusal) noexcept -> Outcome { return err(refusal); });
                OutcomeOracle const expected_outcome = oracle.has_value()
                                                         ? OutcomeOracle{static_cast<std::uint32_t>(*oracle)}
                                                         : OutcomeOracle{std::unexpect, Refusal::gone};
                CRUCIBLE_FATAL_INVARIANT(state_of(converted) == oracle_state(expected_outcome));
                CRUCIBLE_FATAL_INVARIANT(taken.is_none());
                break;
            }
            default:
                if (oracle.has_value()) {
                    Maybe taken = target;
                    CRUCIBLE_FATAL_INVARIANT(std::move(taken).expect("the oracle holds a value") == *oracle);
                }
                break;
        }
        lane.expect_agree();
    }
}

void run_result_sequence(std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    Lane<Outcome, OutcomeOracle> lane{
        FixedArray<Outcome, kSlots>{Outcome{0u}, Outcome{0u}, Outcome{0u}, Outcome{0u}},
        std::array<OutcomeOracle, kSlots>{},
    };
    for (int step = 0; step < kSteps; ++step) {
        Draw const draw = draw_step(stream, 7);
        Outcome& target = lane.sum(draw.target);
        OutcomeOracle& oracle = lane.oracles.at(draw.target);
        Refusal const refusal = static_cast<Refusal>(draw.which);
        std::uint32_t const fallback = 100 + draw.value;
        switch (draw.operation) {
            case 0:
                target = make_outcome(draw.flag, draw.value, refusal);
                oracle = draw.flag ? OutcomeOracle{draw.value} : OutcomeOracle{std::unexpect, refusal};
                break;
            case 1:
                target = lane.sum(draw.source);
                oracle = lane.oracles.at(draw.source);
                break;
            case 2:
                target = std::move(lane.sum(draw.source));
                oracle = std::move(lane.oracles.at(draw.source));
                break;
            case 3:
                CRUCIBLE_FATAL_INVARIANT((target == lane.sum(draw.source)) == (oracle == lane.oracles.at(draw.source)));
                break;
            case 4: {
                Outcome taken = target;
                MaybeOracle const value_oracle = oracle.has_value() ? MaybeOracle{*oracle} : MaybeOracle{};
                Option<std::uint32_t> const value = std::move(taken).ok();
                CRUCIBLE_FATAL_INVARIANT(value.is_some() == value_oracle.has_value());
                CRUCIBLE_FATAL_INVARIANT(std::move(Option<std::uint32_t>{value}).value_or(fallback)
                                         == value_oracle.value_or(fallback));
                break;
            }
            case 5: {
                Option<Refusal> const error = target.err();
                CRUCIBLE_FATAL_INVARIANT(error.is_some() == !oracle.has_value());
                for (Refusal const& seen : error) {
                    CRUCIBLE_FATAL_INVARIANT(seen == oracle.error());
                }
                break;
            }
            default: {
                Outcome taken = target;
                CRUCIBLE_FATAL_INVARIANT(std::move(taken).value_or(fallback) == oracle.value_or(fallback));
                break;
            }
        }
        lane.expect_agree();
    }
}

void run_one_of_sequence(std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    Lane<Reading, ReadingOracle> lane{
        FixedArray<Reading, kSlots>{Reading{0u}, Reading{0u}, Reading{0u}, Reading{0u}},
        std::array<ReadingOracle, kSlots>{},
    };
    for (int step = 0; step < kSteps; ++step) {
        Draw const draw = draw_step(stream, 6);
        Reading& target = lane.sum(draw.target);
        ReadingOracle& oracle = lane.oracles.at(draw.target);
        switch (draw.operation) {
            case 0:
                target = make_reading(draw.which, draw.value);
                if (draw.which == 0) {
                    oracle = draw.value;
                } else if (draw.which == 1) {
                    oracle = static_cast<Refusal>(draw.value % 3u);
                } else {
                    oracle = static_cast<std::uint16_t>(draw.value);
                }
                break;
            case 1:
                target = lane.sum(draw.source);
                oracle = lane.oracles.at(draw.source);
                break;
            case 2:
                target = std::move(lane.sum(draw.source));
                oracle = std::move(lane.oracles.at(draw.source));
                break;
            case 3:
                CRUCIBLE_FATAL_INVARIANT((target == lane.sum(draw.source)) == (oracle == lane.oracles.at(draw.source)));
                break;
            case 4:
                CRUCIBLE_FATAL_INVARIANT(target.is<std::uint32_t>() == std::holds_alternative<std::uint32_t>(oracle));
                CRUCIBLE_FATAL_INVARIANT(target.is<Refusal>() == std::holds_alternative<Refusal>(oracle));
                CRUCIBLE_FATAL_INVARIANT(target.is<std::uint16_t>() == std::holds_alternative<std::uint16_t>(oracle));
                break;
            default: {
                std::uint32_t const* const oracle_word = std::get_if<std::uint32_t>(&oracle);
                std::uint32_t const word = target.get<std::uint32_t>().match(
                    [](auto held) noexcept { return *held; }, [] noexcept { return std::uint32_t{0xFFFFFFFFu}; });
                CRUCIBLE_FATAL_INVARIANT(word == (oracle_word != nullptr ? *oracle_word : 0xFFFFFFFFu));
                break;
            }
        }
        lane.expect_agree();
    }
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

int main() {
    run_option_sequence(0x5EEDC401CE0A0001u);
    run_result_sequence(0x5EEDC401CE0A0002u);
    run_one_of_sequence(0x5EEDC401CE0A0003u);
    return 0;
}
