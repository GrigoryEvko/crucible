#pragma once

// The property tests of the Check family (fixy/check/Check.h).
//
//   property(test, seed, count, body)   runs body count times on the
//                                       inputs of Philox, and shrinks a
//                                       failure to a minimal input
//   oracle(test, seed, count, generate, family, model)
//                                       a property: the operation of a
//                                       family and its model give the same
//                                       result on each generated input
//   Draw                                the source of the inputs of one run
//
// Inputs
//   A body takes a Draw and gives true when the property holds.  Each value
//   that it draws comes from one or two words of 64 bits.  Run r of the seed
//   s takes its words from the Philox4x32-10 blocks of the key s and the
//   counters (b, r) for b = 0, 1, 2 and so on (foundation/core/Philox.h).  So
//   the same seed gives the same inputs on each run of the test and on each
//   host.
//
// Shrink
//   The words of a run are its choice sequence.  When a run fails, the
//   family replays the body on smaller sequences and keeps each one that
//   still fails: it removes each word, sets each word to zero, and searches
//   for the least word that still fails.  A replay past the end of the
//   sequence draws the word zero.  The value of a word moves toward the
//   value nearest to zero in the range of the draw as the word moves toward
//   zero, so the shrunk input is near zero.  The shrink stops after
//   kShrinkRuns replays.  The body must be deterministic: the same words
//   give the same result.
//
//   On a failure, the test writes the seed, the run and the values of the
//   shrunk input to standard error, and ends with the site of the call.  A
//   body that ends the process, such as by CRUCIBLE_TEST_ASSERT, gets no
//   shrink: it must give false for a failure.
//
// Oracle
//   oracle compares a family operation with its model, often the std form.
//   The test file holds the model inside one opt-out region of the class
//   ORACLE (foundation/Quarantine.h), so the std names stay in the region.
//
// Complexity: count runs of the body, and at most kShrinkRuns more runs on a
// failure.

#include <fixy/check/Check.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Array.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Philox.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fixy::check {

// The count of the words of one run that the shrink keeps.  A body that
// draws more takes its later words from Philox and does not shrink them.
inline constexpr std::size_t kMaxDraws = 256;

// The most replays of the body that one shrink makes.
inline constexpr std::uint32_t kShrinkRuns = 4096;

template <typename Body>
class PropertyRunner;

// The words of one run: the choice sequence of the shrink.  A record of
// words gives no Draw by itself, because only the runner replays one.
struct ChoiceWords {
    ::foundation::core::FixedArray<std::uint64_t, kMaxDraws> words{};
    std::size_t count = 0;
};

namespace detail {

// One value that a run drew, for the report of a failure: the bits of the
// value, and whether the type of the value is signed.  The padding bytes are
// a member, so the value has no hole.
struct DrawnValue {
    std::uint64_t bits = 0;
    bool is_signed = false;
    ::foundation::core::FixedArray<std::uint8_t, 7> padding{};
};

// Writes the seed, the run and the values of the shrunk input of a failed
// property, and ends the process with the site of the call.
[[noreturn, gnu::cold]] void report_property_failure_(std::uint64_t seed, std::uint32_t run,
                                                      ::foundation::core::View<DrawnValue const> values,
                                                      ::foundation::core::Site site) noexcept;

}  // namespace detail

// The source of the inputs of one run of a property (the inputs).
class Draw {
public:
    // A word of 64 bits.
    [[nodiscard]] std::uint64_t word() noexcept {
        std::uint64_t const value = next_word_();
        show_(value, false);
        return value;
    }

    // A value from low to high, both included (the shrink).  The origin is
    // the value of the range nearest to zero.  When the range holds values
    // on the two sides of the origin, a first word picks the side: a word
    // below 2^63 picks the side above the origin.  The next word gives the
    // distance from the origin, which grows with the word, so the word zero
    // gives the origin.
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    [[nodiscard]] T integer(T low, T high) noexcept {
        if (low > high) [[unlikely]] {
            ::foundation::core::fatal("Draw::integer got a low bound above its high bound");
        }
        std::uint64_t const origin = origin_bits_(low, high);
        std::uint64_t const up = bits_of_(high) - origin;
        std::uint64_t const down = origin - bits_of_(low);
        bool is_down = up == 0 && down != 0;
        if (up != 0 && down != 0) {
            is_down = (next_word_() >> 63) != 0;
        }
        std::uint64_t const distance = scale_(next_word_(), is_down ? down : up);
        T const value = value_of_bits_<T>(is_down ? origin - distance : origin + distance);
        show_(bits_of_(value), std::is_signed_v<T>);
        return value;
    }

    // A bool, false for the word zero.
    [[nodiscard]] bool boolean() noexcept { return integer<std::uint8_t>(0, 1) == 1; }

    // The source of the words of run RUN of SEED (the inputs).  A test can
    // also draw values outside a property with it.
    Draw(std::uint64_t seed, std::uint32_t run) noexcept : seed_{seed}, run_{run} {}

    Draw(Draw const&) = delete("a Draw is the source of one run, and a copy would give its words two times");
    Draw& operator=(Draw const&) = delete("a Draw is the source of one run, and a copy would give its words two times");

private:
    template <typename Body>
    friend class PropertyRunner;

    explicit Draw(ChoiceWords const& replay) noexcept : replay_{&replay}, is_replay_{true} {}

    // The values that the run drew, at most kMaxDraws.
    [[nodiscard]] ::foundation::core::View<detail::DrawnValue const> shown_view_() const noexcept {
        return ::foundation::core::View<detail::DrawnValue const>{shown_.view()}
            .window(0, shown_count_)
            .expect("the count of the shown values is at most kMaxDraws");
    }

    // The bits of an integer of at most 64 bits, which wrap: a negative value
    // gives its bits of two's complement.
    template <typename T>
    [[nodiscard]] static std::uint64_t bits_of_(T value) noexcept {
        if constexpr (std::same_as<T, std::uint64_t>) {
            return value;
        } else {
            return static_cast<std::uint64_t>(value);
        }
    }

    // The integer of the low bits, the inverse of bits_of_ for each value of T.
    template <typename T>
    [[nodiscard]] static T value_of_bits_(std::uint64_t bits) noexcept {
        if constexpr (std::same_as<T, std::uint64_t>) {
            return bits;
        } else {
            return static_cast<T>(bits);
        }
    }

    // The bits of the value of the range nearest to zero: zero when the
    // range holds it, and else the bound nearer to it.
    template <typename T>
    [[nodiscard]] static std::uint64_t origin_bits_(T low, T high) noexcept {
        if (low > T{0}) return bits_of_(low);
        if (high < T{0}) return bits_of_(high);
        return 0;
    }

    // The word scaled onto the distances from zero to longest, both
    // included: the high 64 bits of word * (longest + 1).  The scale keeps
    // the order of the words, so a smaller word gives a distance that is not
    // larger, and each distance has a word.
    [[nodiscard]] static std::uint64_t scale_(std::uint64_t word, std::uint64_t longest) noexcept {
        if (longest == ~std::uint64_t{0}) return word;
        std::uint64_t const factor = longest + 1;
        constexpr std::uint64_t half_mask = 0xFFFFFFFFu;
        std::uint64_t const word_high = word >> 32;
        std::uint64_t const word_low = word & half_mask;
        std::uint64_t const factor_high = factor >> 32;
        std::uint64_t const factor_low = factor & half_mask;
        std::uint64_t const low_low = word_low * factor_low;
        std::uint64_t const low_high = word_low * factor_high;
        std::uint64_t const high_low = word_high * factor_low;
        std::uint64_t const high_high = word_high * factor_high;
        std::uint64_t const middle = (low_low >> 32) + (low_high & half_mask) + (high_low & half_mask);
        return high_high + (low_high >> 32) + (high_low >> 32) + (middle >> 32);
    }

    [[nodiscard]] std::uint64_t next_word_() noexcept {
        std::uint64_t value = 0;
        if (is_replay_) {
            if (position_ < replay_->count) {
                value = *replay_->words.get(position_).expect("a replay position is below the count");
            }
        } else {
            value = philox_word_(position_);
        }
        if (position_ < kMaxDraws) {
            record_.words.with_element(
                ::foundation::core::Index<kMaxDraws>::check(position_).expect("a recorded position is below kMaxDraws"),
                [value](std::uint64_t& word) noexcept { word = value; });
            record_.count = position_ + 1;
        }
        ++position_;
        return value;
    }

    // Word POSITION of the run: half of block POSITION / 2 of the key and
    // the run.
    [[nodiscard]] std::uint64_t philox_word_(std::size_t position) const noexcept {
        using ::foundation::core::index;
        using ::foundation::core::Philox4x32;
        std::uint64_t const block = position / 2;
        Philox4x32::Counter const words = Philox4x32::generate(
            Philox4x32::Counter{static_cast<std::uint32_t>(block), static_cast<std::uint32_t>(block >> 32), run_, 0u},
            Philox4x32::Key{static_cast<std::uint32_t>(seed_), static_cast<std::uint32_t>(seed_ >> 32)});
        bool const is_first_half = position % 2 == 0;
        std::uint64_t const high = is_first_half ? words.at(index<0>()) : words.at(index<2>());
        std::uint64_t const low = is_first_half ? words.at(index<1>()) : words.at(index<3>());
        return (high << 32) | low;
    }

    void show_(std::uint64_t bits, bool is_signed) noexcept {
        if (shown_count_ < kMaxDraws) {
            shown_.with_element(
                ::foundation::core::Index<kMaxDraws>::check(shown_count_).expect("a shown position is below kMaxDraws"),
                [bits, is_signed](detail::DrawnValue& shown) noexcept {
                    shown = detail::DrawnValue{bits, is_signed, {}};
                });
            ++shown_count_;
        }
    }

    std::uint64_t seed_ = 0;
    std::uint32_t run_ = 0;
    ChoiceWords const* replay_ = nullptr;
    bool is_replay_ = false;
    std::size_t position_ = 0;
    ChoiceWords record_{};
    ::foundation::core::FixedArray<detail::DrawnValue, kMaxDraws> shown_{};
    std::size_t shown_count_ = 0;
    // GCC counts a class whose copies are all deleted as trivially copyable,
    // so a copy of bytes could make a Draw in the replay state.  The seal
    // closes that route (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// A body of a property: it takes a Draw, gives true when the property holds,
// and does not throw.
template <typename Body>
concept PropertyBody = std::is_nothrow_invocable_r_v<bool, Body&, Draw&>;

// The runs and the shrink of one property (the shrink).
template <typename Body>
class PropertyRunner {
public:
    explicit PropertyRunner(Body& body) noexcept : body_{&body} {}

    // True when the body holds on run RUN of SEED.  On a failure, failed()
    // gives the words of the run.
    [[nodiscard]] bool holds(std::uint64_t seed, std::uint32_t run) noexcept {
        Draw draw{seed, run};
        bool const is_held = (*body_)(draw);
        failed_ = draw.record_;
        return is_held;
    }

    // Shrinks the words of the last failed run, and gives the values that the
    // body drew from the shrunk words.
    void shrink() noexcept {
        bool has_improved = true;
        while (has_improved && budget_ > 0) {
            has_improved = remove_words_() || lower_words_();
        }
    }

    // Runs the body one more time on the shrunk words, and ends the process
    // with the report of the values that it drew.
    [[noreturn]] void report_shrunk(std::uint64_t seed, std::uint32_t run, ::foundation::core::Site site) noexcept {
        Draw draw{failed_};
        static_cast<void>((*body_)(draw));
        detail::report_property_failure_(seed, run, draw.shown_view_(), site);
    }

private:
    // True when the body fails on the words.
    [[nodiscard]] bool fails_on_(ChoiceWords const& words) noexcept {
        --budget_;
        Draw draw{words};
        return !(*body_)(draw);
    }

    // Removes each word that the failure does not need, from the last word.
    [[nodiscard]] bool remove_words_() noexcept {
        bool has_improved = false;
        for (std::size_t position = failed_.count; position-- > 0 && budget_ > 0;) {
            ChoiceWords candidate = failed_;
            for (std::size_t index = position; index + 1 < candidate.count; ++index) {
                std::uint64_t const next =
                    *failed_.words.get(index + 1).expect("an index below the count is in the array");
                candidate.words.with_element(::foundation::core::Index<kMaxDraws>::check(index).expect(
                                                 "an index below the count is in the array"),
                                             [next](std::uint64_t& word) noexcept { word = next; });
            }
            candidate.count = failed_.count - 1;
            if (fails_on_(candidate)) {
                failed_ = candidate;
                has_improved = true;
            }
        }
        return has_improved;
    }

    // Sets each word to the least value that still fails, by a binary search
    // from zero.  The search reads a failure that is not monotone in the word
    // as the least failure that it finds.
    [[nodiscard]] bool lower_words_() noexcept {
        bool has_improved = false;
        for (std::size_t position = 0; position < failed_.count && budget_ > 0; ++position) {
            std::uint64_t const current = *failed_.words.get(position).expect("a position below the count");
            std::uint64_t low = 0;
            std::uint64_t high = current;
            while (low < high && budget_ > 0) {
                std::uint64_t const middle = low + (high - low) / 2;
                ChoiceWords candidate = failed_;
                candidate.words.with_element(
                    ::foundation::core::Index<kMaxDraws>::check(position).expect("a position below the count"),
                    [middle](std::uint64_t& word) noexcept { word = middle; });
                if (fails_on_(candidate)) {
                    high = middle;
                } else {
                    low = middle + 1;
                }
            }
            if (high < current) {
                failed_.words.with_element(
                    ::foundation::core::Index<kMaxDraws>::check(position).expect("a position below the count"),
                    [high](std::uint64_t& word) noexcept { word = high; });
                has_improved = true;
            }
        }
        return has_improved;
    }

    Body* body_;
    ChoiceWords failed_{};
    std::uint32_t budget_ = kShrinkRuns;
};

// Runs body on the inputs of runs 0 to count - 1 of seed (the inputs).  On
// the first run that fails, the shrink finds a smaller input, and the test
// ends with the report of the shrunk input and the site of the call.
template <TestContext Ctx, typename Body>
    requires PropertyBody<Body>
void property(Ctx const& /*test*/, std::uint64_t seed, std::uint32_t count, Body&& body,
              ::foundation::core::Site site = ::foundation::core::Site{}) noexcept {
    PropertyRunner<std::remove_reference_t<Body>> runner{body};
    for (std::uint32_t run = 0; run < count; ++run) {
        if (runner.holds(seed, run)) [[likely]] {
            continue;
        }
        runner.shrink();
        runner.report_shrunk(seed, run, site);
    }
}

// The property that family and model give the same result for each input
// that generate draws (the oracle).
template <TestContext Ctx, typename Generate, typename Family, typename Model>
    requires std::is_nothrow_invocable_v<Generate&, Draw&>
          && std::is_nothrow_invocable_v<Family&, std::invoke_result_t<Generate&, Draw&> const&>
          && std::is_nothrow_invocable_v<Model&, std::invoke_result_t<Generate&, Draw&> const&>
          && std::equality_comparable_with<std::invoke_result_t<Family&, std::invoke_result_t<Generate&, Draw&> const&>,
                                           std::invoke_result_t<Model&, std::invoke_result_t<Generate&, Draw&> const&>>
void oracle(Ctx const& test, std::uint64_t seed, std::uint32_t count, Generate&& generate, Family&& family,
            Model&& model, ::foundation::core::Site site = ::foundation::core::Site{}) noexcept {
    property(
        test, seed, count,
        [&generate, &family, &model](Draw& draw) noexcept -> bool {
            auto const input = generate(draw);
            return family(input) == model(input);
        },
        site);
}

}  // namespace fixy::check
