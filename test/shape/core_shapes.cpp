// The probes of the shape check of the core families and of the linear
// values.
//
// The rule codegen-shapes (utils/assay/guard/engines/elf/codegen_shapes.py) reads the disassembly of this unit.  Each
// probe is a pair of functions.  The first function does one hot operation of a
// family of include/foundation/core, of fixy::Linear or of a permission token.
// The second function does the same work in the raw form that the family
// replaces: a std::atomic operation, a raw pointer loop, memcpy or memset, a
// std::unique_ptr or std::optional access, std::unreachable, a plain value or
// an empty class.  The check compares the instructions of the two functions
// of each pair, and the probe of the pair in the rule declares the
// differences that the family function can add.
//
// test/shape/CMakeLists.txt compiles this unit with the flags of the
// foundation library.  Each build then compares the pairs with its own flags.
// Each function takes its operands as parameters, and the optimizer cannot
// fold an operand into the code.  Each function is [[gnu::used]] in an
// unnamed namespace.  The compiler then emits it with the signature that it
// declares, and its name stays in this unit.
//
// The raw forms are in one opt-out region of the class ORACLE.  Each raw form
// is the reference for the shape of its family operation, so it must stay raw.

#include <fixy/Core.h>
#include <fixy/Qtt.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <compare>
#include <expected>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <utility>

namespace {

// A value of two words.  The cell of an Atomic holds it as one 64-bit integer.
struct WordPair final {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
};

struct Payload final {
    std::uint32_t value = 0;
};

// The brand of the Ref of a probe.  A brand is an empty class.
struct RefProbeBrand final {};

// A payload of 64 bytes.  An Option of it goes through memory.
struct Block64 final {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;
    std::uint64_t fourth = 0;
    std::uint64_t fifth = 0;
    std::uint64_t sixth = 0;
    std::uint64_t seventh = 0;
    std::uint64_t eighth = 0;
};

enum class Kind : std::uint8_t {
    first,
    second,
    third,
    fourth
};

// An error of four bytes.
enum class Code32 : std::uint32_t {
    gone = 1,
    late = 2
};

// An error of one byte, and the error of a caller that takes it through an
// implicit conversion.
enum class Code8 : std::uint8_t {
    lost = 1
};

struct NetError8 final {
    Code8 code = Code8::lost;
    constexpr NetError8(Code8 from) noexcept : code{from} {}
};

// ── Atomic ──────────────────────────────────────────────────────────────────

[[gnu::used]] std::uint64_t atomic_load(fixy::Atomic<std::uint64_t> const& cell) noexcept {
    return cell.load_acquire();
}

[[gnu::used]] void atomic_store(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    cell.store_release(value);
}

[[gnu::used]] std::uint64_t atomic_exchange(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.exchange_acq_rel(value);
}

[[gnu::used]] bool atomic_cas(fixy::Atomic<std::uint64_t>& cell, std::uint64_t expected,
                              std::uint64_t desired) noexcept {
    return cell.cas_acq_rel(expected, desired).is_ok();
}

[[gnu::used]] bool atomic_cas_pair(fixy::Atomic<WordPair>& cell, WordPair expected, WordPair desired) noexcept {
    return cell.cas_acq_rel(expected, desired).is_ok();
}

[[gnu::used]] std::uint64_t atomic_fetch_add(fixy::Atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_add_acq_rel(delta);
}

[[gnu::used]] std::uint64_t atomic_fetch_sub(fixy::Atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_sub_acq_rel(delta);
}

[[gnu::used]] std::uint64_t atomic_fetch_or(fixy::Atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_or_acq_rel(bits);
}

[[gnu::used]] std::uint64_t atomic_fetch_and(fixy::Atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_and_acq_rel(bits);
}

[[gnu::used]] std::uint64_t atomic_fetch_max(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_max_acq_rel(value);
}

[[gnu::used]] std::uint64_t atomic_fetch_min(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_min_acq_rel(value);
}

[[gnu::used]] void tally_add(fixy::Tally& tally, std::uint64_t amount) noexcept { tally.add(amount); }

[[gnu::used]] std::uint64_t tally_read(fixy::Tally const& tally) noexcept { return tally.read(); }

[[gnu::used]] std::uint64_t cache_line_load(fixy::CacheLine<fixy::Atomic<std::uint64_t>> const& line) noexcept {
    return line->load_acquire();
}

// The cell of one writer.  The proof is a token with no bytes, so its
// reference is one more register argument that no instruction reads.  A
// fresh brand stays in the scope that made it, so no alias names the writer.
// Each probe names its cell in its first parameter, and the second parameter
// takes the writer of that cell.
struct ShapeHeadRole final {};

template <class CellReference>
struct writer_of;

template <class T, class Writer>
struct writer_of<fixy::Atomic<T, Writer> const&> {
    using type = Writer;
};

template <class T, class Writer>
struct writer_of<fixy::Atomic<T, Writer>&> {
    using type = Writer;
};

[[gnu::used]] std::uint64_t
sole_load_owned(fixy::Atomic<std::uint64_t, decltype(fixy::mint_sole_writer<ShapeHeadRole>())> const& cell,
                typename writer_of<decltype(cell)>::type const& writer) noexcept {
    return cell.load_owned(writer);
}

[[gnu::used]] void sole_store(fixy::Atomic<std::uint64_t, decltype(fixy::mint_sole_writer<ShapeHeadRole>())>& cell,
                              typename writer_of<decltype(cell)>::type const& writer, std::uint64_t value) noexcept {
    cell.store_release(writer, value);
}

[[gnu::used]] std::uint64_t
sole_load(fixy::Atomic<std::uint64_t, decltype(fixy::mint_sole_writer<ShapeHeadRole>())> const& cell) noexcept {
    return cell.load_acquire();
}

// A seat number that is never zero, a value that keeps an invariant.  Its
// word is the number, and zero is its empty word and the empty value of its
// niche, so the cell of an Option of a seat holds one 32-bit word.  The
// optimizer then removes the check of the niche on a load and the check of
// the empty word on a store.
class ShapeSeat final {
    std::uint32_t number_ = 1;
    constexpr explicit ShapeSeat(std::uint32_t number) noexcept : number_{number} {}
    friend struct ::foundation::core::atomic_word<ShapeSeat>;

public:
    constexpr ShapeSeat(ShapeSeat const&) noexcept = default;
    constexpr ShapeSeat& operator=(ShapeSeat const& other) noexcept {
        number_ = other.number_;
        return *this;
    }
    [[nodiscard]] static constexpr ShapeSeat niche_empty(fixy::niche_key<ShapeSeat> const&) noexcept {
        return ShapeSeat{0};
    }
    [[nodiscard]] constexpr bool is_niche_empty(fixy::niche_key<ShapeSeat> const&) const noexcept {
        return number_ == 0;
    }
};

}  // namespace

template <>
struct foundation::core::atomic_word<ShapeSeat> {
    using word_type = std::uint32_t;
    static constexpr word_type empty_word = 0;
    [[nodiscard]] static constexpr word_type to_word(ShapeSeat const& seat) noexcept { return seat.number_; }
    [[nodiscard]] static constexpr ShapeSeat from_word(atomic_word_key<ShapeSeat> const&, word_type word) noexcept {
        return ShapeSeat{word};
    }
};

namespace {

[[gnu::used]] fixy::Option<ShapeSeat> word_option_load(fixy::Atomic<fixy::Option<ShapeSeat>> const& cell) noexcept {
    return cell.load_acquire();
}

[[gnu::used]] void word_option_store(fixy::Atomic<fixy::Option<ShapeSeat>>& cell,
                                     fixy::Option<ShapeSeat> value) noexcept {
    cell.store_release(value);
}

// ── Region ──────────────────────────────────────────────────────────────────

[[gnu::used]] std::uint32_t view_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t element : view) {
        sum += element;
    }
    return sum;
}

[[gnu::used]] fixy::Option<fixy::View<std::uint32_t const>>
view_window(fixy::View<std::uint32_t const> view, std::size_t offset, std::size_t count) noexcept {
    return view.window(offset, count);
}

// The append path of a producer: the window of the run in the segment, then
// the copy of the run into the window.
[[gnu::used]] bool view_append(fixy::View<std::uint8_t> segment, std::size_t used,
                               fixy::View<std::uint8_t const> run) noexcept {
    bool is_appended = false;
    for (fixy::View<std::uint8_t> window : segment.window(used, run.size())) {
        is_appended = fixy::copy(window, run).is_ok();
    }
    return is_appended;
}

[[gnu::used]] void view_fill(fixy::View<std::uint8_t> view) noexcept { fixy::fill(view, std::uint8_t{0}); }

[[gnu::used]] std::uint32_t fixed_array_at(::foundation::core::FixedArray<std::uint32_t, 16> const& array,
                                           fixy::Index<16> position) noexcept {
    return array.at(position);
}

[[gnu::used]] fixy::Option<fixy::Index<16>> index_check(std::size_t position) noexcept {
    return fixy::Index<16>::check(position);
}

[[gnu::used]] fixy::Option<fixy::TextView> text_window(fixy::TextView text, std::size_t offset,
                                                       std::size_t count) noexcept {
    return text.window(offset, count);
}

// The value of the digits of a run of a text: a loop over the Option of the
// window of the run, and a loop over the characters of the window.
[[gnu::used]] std::uint32_t text_window_digits(fixy::TextView text, std::size_t offset, std::size_t count) noexcept {
    std::uint32_t value = 0;
    for (fixy::TextView run : text.window(offset, count)) {
        for (char const character : run) {
            value = value * 10u + static_cast<std::uint32_t>(character - '0');
        }
    }
    return value;
}

// The sum of a run of a View: a loop over the Option of the window of the
// run, and a loop over the elements of the window.
[[gnu::used]] std::uint32_t view_window_sum(fixy::View<std::uint32_t const> view, std::size_t offset,
                                            std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (fixy::View<std::uint32_t const> run : view.window(offset, count)) {
        for (std::uint32_t const element : run) {
            sum += element;
        }
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_reverse_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t element : view.reverse()) {
        sum += element;
    }
    return sum;
}

// Each run of two consecutive elements, with a step of one element.
[[gnu::used]] std::uint32_t view_windows_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (fixy::View<std::uint32_t const, 2> run : view.windows<2>()) {
        sum += run.at(fixy::index<0>()) ^ run.at(fixy::index<1>());
    }
    return sum;
}

// Each run of four elements, with a step of four elements.
[[gnu::used]] std::uint32_t view_chunks_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (fixy::View<std::uint32_t const, 4> run : view.chunks<4>()) {
        sum += run.at(fixy::index<0>()) ^ run.at(fixy::index<3>());
    }
    return sum;
}

// Each element of a View of a fixed extent with its proven index.
[[gnu::used]] std::uint32_t view_enumerate_sum(fixy::View<std::uint32_t const, 64> view) noexcept {
    std::uint32_t sum = 0;
    for (auto const [position, element] : view.enumerate()) {
        sum += *element * static_cast<std::uint32_t>(position.raw());
    }
    return sum;
}

// The pairs of two Views of one fixed extent: the type proves the counts
// equal, so the loop has no compare before its first step.
[[gnu::used]] std::uint32_t view_zip_fixed_sum(fixy::View<std::uint32_t const, 64> left,
                                               fixy::View<std::uint32_t const, 64> right) noexcept {
    std::uint32_t sum = 0;
    for (auto const [first, second] : fixy::zip(left, right)) {
        sum += *first * *second;
    }
    return sum;
}

// The pairs of two Views of a dynamic extent: the compare of the counts, and
// the loop over the pairs.
[[gnu::used]] std::uint32_t view_zip_sum(fixy::View<std::uint32_t const> left,
                                         fixy::View<std::uint32_t const> right) noexcept {
    return fixy::zip(left, right)
        .match(
            [](auto pairs) noexcept {
                std::uint32_t sum = 0;
                for (auto const [first, second] : pairs) {
                    sum += *first * *second;
                }
                return sum;
            },
            [](fixy::LengthMismatch) noexcept { return std::uint32_t{0}; });
}

// A push into a FixedList: one compare of the count, the store of the
// element, and the step of the count.
[[gnu::used]] bool fixed_list_push(fixy::FixedList<std::uint32_t, 64>& list, std::uint32_t value) noexcept {
    return list.push(value).is_ok();
}

// The last element of a FixedList, or zero for an empty list.
[[gnu::used]] std::uint32_t fixed_list_take_last(fixy::FixedList<std::uint32_t, 64>& list) noexcept {
    return list.take_last().value_or(0u);
}

// ── Ref, Choice and Report ──────────────────────────────────────────────────

[[gnu::used]] std::uint32_t box_read(fixy::Box<Payload>& box) noexcept { return box->value; }

// A read through a Ref is the load of a raw pointer.  A Ref has no empty
// state, so the read has no test.
[[gnu::used]] std::uint32_t ref_read(fixy::Ref<Payload const, RefProbeBrand> ref) noexcept { return ref->value; }

// An Option of a plain payload, as a function gives it back: in a register.
// expect adds one test and one branch to a cold call.  For a payload of four
// bytes, the payload is the low word of the register and the flag is bit 32,
// as in std::optional, so the test of the flag takes a shift.
[[gnu::used]] std::uint32_t option_expect(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).expect("the probe gets a full Option");
}

// An Option whose payload has a niche, so the Option has no flag.  expect
// adds one compare and one branch to a cold call.
[[gnu::used]] fixy::View<std::uint32_t const>
option_expect_niche(fixy::Option<fixy::View<std::uint32_t const>> option) noexcept {
    return std::move(option).expect("the probe gets a full Option");
}

// The match of an Option is one test of the tag.
[[gnu::used]] std::uint32_t option_match(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).match([](std::uint32_t value) noexcept { return value * 3u; },
                                   []() noexcept { return 0u; });
}

// A loop over an Option makes zero turns or one turn.
[[gnu::used]] std::uint32_t option_loop(fixy::Option<std::uint32_t> option, std::uint32_t total) noexcept {
    for (std::uint32_t const& value : option) {
        total += value;
    }
    return total;
}

[[gnu::used]] std::uint32_t option_value_or(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).value_or(7u);
}

// A clear of an Option of a payload of 64 bytes writes only its tag.  Its sum
// goes through memory, so its empty marker writes no zero word.
[[gnu::used]] void option_clear_block(fixy::Option<Block64>& held) noexcept { held = {}; }

// An empty Option of a word, and an Option that an inlined function gives,
// come back in two registers with no store to the stack.  The tag fills the
// word after the storage, so the cells hold no padding byte that a copy
// carries.
namespace {

[[nodiscard]] constexpr fixy::Option<std::size_t> word_below(std::size_t value, std::size_t limit) noexcept {
    if (value >= limit) return fixy::none;
    return fixy::Option<std::size_t>::some(value);
}

}  // namespace

[[gnu::used]] fixy::Option<std::size_t> option_empty_word() noexcept { return fixy::none; }

[[gnu::used]] fixy::Option<std::size_t> option_word_below(std::size_t value, std::size_t limit) noexcept {
    return word_below(value, limit);
}

// A compare of two Options tests the two states, and compares the payloads
// only when both hold one.
[[gnu::used]] bool option_equal(fixy::Option<std::uint32_t> left, fixy::Option<std::uint32_t> right) noexcept {
    return left == right;
}

[[gnu::used]] bool option_equal_word(fixy::Option<std::uint64_t> left, fixy::Option<std::uint64_t> right) noexcept {
    return left == right;
}

// A full Option of a Ref is the address in one register, with no test of
// the null pointer.
[[gnu::used]] fixy::Option<fixy::Ref<std::uint32_t>> option_ref_some(std::uint32_t& slot) noexcept {
    return fixy::some(fixy::Ref<std::uint32_t>{fixy::to(slot)});
}

// ok_or gives the payload of an Option as a Result, in the same register.
[[gnu::used]] fixy::Result<std::uint32_t, Code32> option_ok_or(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).ok_or(Code32::gone);
}

// CRUCIBLE_TRY is one test of the tag.  The value takes a second test of
// the tag, which the optimizer removes.
[[gnu::used]] fixy::Option<std::uint32_t> option_try(fixy::Option<std::uint32_t> option) noexcept {
    CRUCIBLE_TRY(value, std::move(option));
    return fixy::some(value + 1u);
}

[[gnu::used]] fixy::Result<std::uint32_t, Code32> result_try(fixy::Result<std::uint32_t, Code32> result) noexcept {
    CRUCIBLE_TRY(value, std::move(result));
    return value + 1u;
}

// The error of the callee converts to the error of the caller.
[[gnu::used]] fixy::Result<std::uint32_t, NetError8>
result_try_convert(fixy::Result<std::uint32_t, Code8> result) noexcept {
    CRUCIBLE_TRY(value, std::move(result));
    return value + 1u;
}

// A Result of a value of four bytes and an error of four bytes, as a
// function gives it back: in one register.  The value is the low word and
// the tag is bit 32, as in std::expected.  expect adds one test and one
// branch to a cold call.
[[gnu::used]] std::uint32_t result_expect(fixy::Result<std::uint32_t, Code32> result) noexcept {
    return std::move(result).expect("the probe gets a value");
}

// The match of a Result is one test of the tag.
[[gnu::used]] std::uint32_t result_match(fixy::Result<std::uint32_t, Code32> result) noexcept {
    return std::move(result).match([](std::uint32_t value) noexcept { return value * 3u; },
                                   [](Code32 code) noexcept { return static_cast<std::uint32_t>(code); });
}

// The match of a OneOf of three alternatives compares the tag with the first
// two indices, and the last alternative takes the last branch.
[[gnu::used]] std::uint32_t one_of_match(fixy::OneOf<std::uint32_t, Code32, std::uint16_t> reading) noexcept {
    return std::move(reading).match([](std::uint32_t word) noexcept { return word * 3u; },
                                    [](Code32 code) noexcept { return static_cast<std::uint32_t>(code); },
                                    [](std::uint16_t half) noexcept { return std::uint32_t{half} + 5u; });
}

[[gnu::used]] std::uint32_t switch_unreachable(Kind kind) noexcept {
    switch (kind) {
        case Kind::first:
            return 11;
        case Kind::second:
            return 23;
        case Kind::third:
            return 37;
        case Kind::fourth:
            return 41;
        default:
            fixy::unreachable();
    }
}

// ── Record ──────────────────────────────────────────────────────────────────

// Two empty parts of different types.
struct TokenA final {};
struct TokenB final {};

// A Record of two words, as a function gives it back: in two registers.
[[gnu::used]] fixy::Record<std::uint64_t, std::uint64_t> record_return(std::uint64_t first,
                                                                       std::uint64_t second) noexcept {
    return {first, second};
}

// A binding reads the parts in place.
[[gnu::used]] std::uint64_t record_binding(fixy::Record<std::uint64_t, std::uint64_t> record) noexcept {
    auto const& [low, high] = record;
    return low + high;
}

// == compares each part in order.
[[gnu::used]] bool record_equal(fixy::Record<std::uint32_t, std::uint32_t> const& left,
                                fixy::Record<std::uint32_t, std::uint32_t> const& right) noexcept {
    return left == right;
}

// <=> compares the parts in order, and it stops at the first part that is not
// equivalent.
[[gnu::used]] bool record_order(fixy::Record<std::uint32_t, std::uint32_t> left,
                                fixy::Record<std::uint32_t, std::uint32_t> right) noexcept {
    return std::is_lt(left <=> right);
}

// Two empty parts take one byte, and a pass moves no byte.
[[gnu::used]] fixy::Record<TokenA, TokenB> record_pass_tokens(fixy::Record<TokenA, TokenB> tokens) noexcept {
    return tokens;
}

// ── Scalar ──────────────────────────────────────────────────────────────────

// fixy/Checked.h holds the names of the three arithmetic sets until slice
// 4.10, so the probes call the family in foundation::core.
[[gnu::used]] fixy::Option<std::uint32_t> scalar_checked_add(std::uint32_t left, std::uint32_t right) noexcept {
    return ::foundation::core::checked_add(left, right);
}

// An Option of a word has 16 bytes, and it comes back in two registers.
[[gnu::used]] fixy::Option<std::uint64_t> scalar_checked_add_wide(std::uint64_t left, std::uint64_t right) noexcept {
    return ::foundation::core::checked_add(left, right);
}

[[gnu::used]] fixy::Option<std::int32_t> scalar_checked_div(std::int32_t dividend, std::int32_t divisor) noexcept {
    return ::foundation::core::checked_div(dividend, divisor);
}

[[gnu::used]] std::uint64_t scalar_wrapping_shl(std::uint64_t value, int count) noexcept {
    return ::foundation::core::wrapping_shl(value, count);
}

[[gnu::used]] std::int64_t scalar_saturating_add(std::int64_t left, std::int64_t right) noexcept {
    return ::foundation::core::saturating_add(left, right);
}

[[gnu::used]] ::foundation::core::WideProduct<std::uint64_t> scalar_widening_mul(std::uint64_t left,
                                                                                 std::uint64_t right) noexcept {
    return ::foundation::core::widening_mul(left, right);
}

[[gnu::used]] double scalar_min_float(double left, double right) noexcept { return fixy::min(left, right); }

[[gnu::used]] std::int32_t scalar_clamp_bounds(std::int32_t value, fixy::Bounds<std::int32_t> range) noexcept {
    return fixy::clamp(value, range);
}

[[gnu::used]] bool scalar_compare_mixed(std::int64_t left, std::uint64_t right) noexcept {
    return std::is_lt(fixy::compare(left, right));
}

[[gnu::used]] bool scalar_fits(std::int32_t value) noexcept { return fixy::fits<std::uint8_t>(value); }

[[gnu::used]] std::uint32_t scalar_unsigned_abs(std::int32_t value) noexcept { return fixy::unsigned_abs(value); }

[[gnu::used]] fixy::Option<std::uint32_t> scalar_narrow_checked(std::uint64_t value) noexcept {
    return fixy::narrow<std::uint32_t>(value);
}

[[gnu::used]] fixy::Option<std::int32_t> scalar_narrow_float(double value) noexcept {
    return fixy::narrow<std::int32_t>(value);
}

[[gnu::used]] std::int16_t scalar_narrow_saturating(std::int64_t value) noexcept {
    return fixy::saturating_narrow<std::int16_t>(value);
}

[[gnu::used]] std::uint64_t scalar_widen(std::uint32_t value) noexcept { return fixy::widen<std::uint64_t>(value); }

[[gnu::used]] int scalar_popcount(std::uint64_t value) noexcept { return fixy::popcount(value); }

[[gnu::used]] int scalar_countl_zero(std::uint64_t value) noexcept { return fixy::countl_zero(value); }

[[gnu::used]] std::uint64_t scalar_rotl(std::uint64_t value, int count) noexcept { return fixy::rotl(value, count); }

[[gnu::used]] std::uint32_t scalar_byteswap(std::uint32_t value) noexcept { return fixy::byteswap(value); }

[[gnu::used]] fixy::Option<std::uint32_t> scalar_bit_ceil(std::uint32_t value) noexcept {
    return fixy::bit_ceil(value);
}

[[gnu::used]] std::uint64_t scalar_low_mask(int count) noexcept { return fixy::low_mask<std::uint64_t>(count); }

[[gnu::used]] std::uint64_t scalar_bit_cast(double value) noexcept { return fixy::bit_cast<std::uint64_t>(value); }

[[gnu::used]] fixy::Option<bool> scalar_decode_bool(std::uint8_t byte) noexcept { return fixy::decode<bool>(byte); }

using ProbeSlotId = fixy::Id<struct ProbeSlotTag, std::uint32_t>;

[[gnu::used]] fixy::Option<ProbeSlotId> scalar_id_decode(std::uint32_t word) noexcept {
    return fixy::decode<ProbeSlotId>(word);
}

[[gnu::used]] bool scalar_fp_is_nan(double value) noexcept { return fixy::fp::is_nan(value); }

[[gnu::used]] double scalar_fp_abs(double value) noexcept { return fixy::fp::abs(value); }

[[gnu::used]] double scalar_fp_sqrt(double value) noexcept { return fixy::fp::sqrt(value); }

[[gnu::used]] double scalar_fp_fma(double left, double right, double addend) noexcept {
    return fixy::fp::fma(left, right, addend);
}

[[gnu::used]] std::uint64_t scalar_fp_total_key(double value) noexcept { return fixy::fp::total_key(value); }

[[gnu::used]] std::uint64_t scalar_exchange(std::uint64_t& slot, std::uint64_t value) noexcept {
    return fixy::exchange(slot, value);
}

[[gnu::used]] void scalar_swap(std::uint64_t& left, std::uint64_t& right) noexcept { fixy::swap(left, right); }

using ProbeBytes = fixy::Quantity<fixy::unit::Byte, std::uint32_t>;

[[gnu::used]] fixy::Option<ProbeBytes> scalar_quantity_add(ProbeBytes left, ProbeBytes right) noexcept {
    return ::foundation::core::checked_add(left, right);
}

[[gnu::used]] fixy::Option<fixy::Quantity<fixy::unit::Nanosecond, std::uint32_t>>
scalar_quantity_to_finer(fixy::Quantity<fixy::unit::Microsecond, std::uint32_t> value) noexcept {
    return fixy::convert<fixy::unit::Nanosecond>(value);
}

[[gnu::used]] fixy::Option<fixy::Quantity<fixy::unit::Second, std::uint32_t>>
scalar_quantity_to_coarser(fixy::Quantity<fixy::unit::Millisecond, std::uint32_t> value) noexcept {
    return fixy::convert<fixy::unit::Second>(value);
}

[[gnu::used]] std::uint32_t scalar_philox_generate(std::uint64_t offset, std::uint64_t key) noexcept {
    fixy::Philox4x32::Counter const block = fixy::Philox4x32::generate(offset, key);
    return block.at(fixy::index<0>()) ^ block.at(fixy::index<1>()) ^ block.at(fixy::index<2>())
         ^ block.at(fixy::index<3>());
}

// ── Linear and the permission tokens ────────────────────────────────────────

struct ProbeRegion final {
    using permission_row = ::foundation::effects::Row<>;
};
struct ProbeBrand final {};
using ProbeToken = ::foundation::permissions::Permission<ProbeRegion, ProbeBrand>;

// The move constructor and the destructor of a Linear are trivial, so the
// ABI passes a Linear in a register, as it passes its value.
[[gnu::used]] int linear_consume(fixy::Linear<int> value) noexcept { return std::move(value).consume(); }

// The move assignment of a Linear is user-provided, so the class is not
// trivially copyable.  It stores the value, as the assignment of the value
// does.
[[gnu::used]] void linear_assign(fixy::Linear<int>& target, fixy::Linear<int>& source) noexcept {
    target = std::move(source);
}

// A token is empty, and its move constructor is trivial, so a pass and a
// return move no byte.
[[gnu::used]] ProbeToken token_pass(ProbeToken token) noexcept { return token; }

// The move assignment of a token is user-provided, and it moves no byte.
[[gnu::used]] void token_assign(ProbeToken& target, ProbeToken& source) noexcept { target = std::move(source); }

// ── The raw forms ───────────────────────────────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the raw form that the shape of each family probe is compared with")

struct RawWindow final {
    std::uint32_t const* first;
    std::size_t count;
};

struct RawText final {
    char const* first;
    std::size_t count;
};

struct RawList final {
    std::uint32_t slots[64];
    std::uint32_t count;
};

[[gnu::used]] std::uint64_t atomic_load_raw(std::atomic<std::uint64_t> const& cell) noexcept {
    return cell.load(std::memory_order_acquire);
}

[[gnu::used]] void atomic_store_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    cell.store(value, std::memory_order_release);
}

[[gnu::used]] std::uint64_t atomic_exchange_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.exchange(value, std::memory_order_acq_rel);
}

[[gnu::used]] bool atomic_cas_raw(std::atomic<std::uint64_t>& cell, std::uint64_t expected,
                                  std::uint64_t desired) noexcept {
    return cell.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

[[gnu::used]] bool atomic_cas_pair_raw(std::atomic<WordPair>& cell, WordPair expected, WordPair desired) noexcept {
    return cell.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

[[gnu::used]] std::uint64_t atomic_fetch_add_raw(std::atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_add(delta, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_sub_raw(std::atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_sub(delta, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_or_raw(std::atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_or(bits, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_and_raw(std::atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_and(bits, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_max_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_max(value, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_min_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_min(value, std::memory_order_acq_rel);
}

// A statistics counter that a Tally replaces is a relaxed counter.
[[gnu::used]] void tally_add_raw(std::atomic<std::uint64_t>& counter, std::uint64_t amount) noexcept {
    counter.fetch_add(amount, std::memory_order_relaxed);
}

[[gnu::used]] std::uint64_t tally_read_raw(std::atomic<std::uint64_t> const& counter) noexcept {
    return counter.load(std::memory_order_relaxed);
}

// A CacheLine replaces a cell that the code aligns by hand, so the raw form
// reads the cell with no CacheLine around it.
[[gnu::used]] std::uint64_t cache_line_load_raw(fixy::Atomic<std::uint64_t> const& cell) noexcept {
    return cell.load_acquire();
}

// The read of its own variable by the one thread that writes it is a
// relaxed load.
[[gnu::used]] std::uint64_t sole_load_owned_raw(std::atomic<std::uint64_t> const& cell) noexcept {
    return cell.load(std::memory_order_relaxed);
}

[[gnu::used]] void sole_store_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    cell.store(value, std::memory_order_release);
}

[[gnu::used]] std::uint64_t sole_load_raw(std::atomic<std::uint64_t> const& cell) noexcept {
    return cell.load(std::memory_order_acquire);
}

// The raw form of a cell of an Option of a seat is a cell of the number, with
// zero for no seat.
[[gnu::used]] std::uint32_t word_option_load_raw(std::atomic<std::uint32_t> const& cell) noexcept {
    return cell.load(std::memory_order_acquire);
}

[[gnu::used]] void word_option_store_raw(std::atomic<std::uint32_t>& cell, std::uint32_t value) noexcept {
    cell.store(value, std::memory_order_release);
}

[[gnu::used]] std::uint32_t view_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const* cursor = first; cursor != first + count; ++cursor) {
        sum += *cursor;
    }
    return sum;
}

[[gnu::used]] RawWindow view_window_raw(std::uint32_t const* first, std::size_t size, std::size_t offset,
                                        std::size_t count) noexcept {
    if (offset > size || count > size - offset) [[unlikely]] {
        return RawWindow{nullptr, ~std::size_t{0}};
    }
    return RawWindow{first + offset, count};
}

[[gnu::used]] bool view_append_raw(std::uint8_t* segment, std::size_t size, std::size_t used, std::uint8_t const* run,
                                   std::size_t run_size) noexcept {
    if (used > size || run_size > size - used) [[unlikely]] {
        return false;
    }
    std::memcpy(segment + used, run, run_size);
    return true;
}

[[gnu::used]] void view_fill_raw(std::uint8_t* first, std::size_t count) noexcept { std::memset(first, 0, count); }

[[gnu::used]] std::uint32_t fixed_array_at_raw(std::uint32_t const* first, std::uint32_t position) noexcept {
    return first[position];
}

// An Option of an Index has the niche of the count: 16 is the empty value.
[[gnu::used]] std::uint32_t index_check_raw(std::size_t position) noexcept {
    return position < 16 ? static_cast<std::uint32_t>(position) : 16u;
}

[[gnu::used]] RawText text_window_raw(char const* first, std::size_t size, std::size_t offset,
                                      std::size_t count) noexcept {
    if (offset > size || count > size - offset) [[unlikely]] {
        return RawText{nullptr, ~std::size_t{0}};
    }
    return RawText{first + offset, count};
}

[[gnu::used]] std::uint32_t text_window_digits_raw(char const* first, std::size_t size, std::size_t offset,
                                                   std::size_t count) noexcept {
    std::uint32_t value = 0;
    if (offset > size || count > size - offset) [[unlikely]] {
        return value;
    }
    for (char const* cursor = first + offset; cursor != first + offset + count; ++cursor) {
        value = value * 10u + static_cast<std::uint32_t>(*cursor - '0');
    }
    return value;
}

[[gnu::used]] std::uint32_t view_window_sum_raw(std::uint32_t const* first, std::size_t size, std::size_t offset,
                                                std::size_t count) noexcept {
    std::uint32_t sum = 0;
    if (offset > size || count > size - offset) [[unlikely]] {
        return sum;
    }
    for (std::uint32_t const* cursor = first + offset; cursor != first + offset + count; ++cursor) {
        sum += *cursor;
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_reverse_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const* after = first + count; after != first; --after) {
        sum += after[-1];
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_windows_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t const* const end = count < 2 ? first : first + (count - 1);
    for (std::uint32_t const* cursor = first; cursor != end; ++cursor) {
        sum += cursor[0] ^ cursor[1];
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_chunks_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t const* const end = first + count / 4 * 4;
    for (std::uint32_t const* cursor = first; cursor != end; cursor += 4) {
        sum += cursor[0] ^ cursor[3];
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_enumerate_sum_raw(std::uint32_t const* first) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t position = 0;
    for (std::uint32_t const* cursor = first; cursor != first + 64; ++cursor) {
        sum += *cursor * position;
        ++position;
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_zip_fixed_sum_raw(std::uint32_t const* left, std::uint32_t const* right) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t const* other = right;
    for (std::uint32_t const* cursor = left; cursor != left + 64; ++cursor) {
        sum += *cursor * *other;
        ++other;
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_zip_sum_raw(std::uint32_t const* left, std::size_t left_count,
                                             std::uint32_t const* right, std::size_t right_count) noexcept {
    std::uint32_t sum = 0;
    if (left_count != right_count) {
        return sum;
    }
    std::uint32_t const* other = right;
    for (std::uint32_t const* cursor = left; cursor != left + left_count; ++cursor) {
        sum += *cursor * *other;
        ++other;
    }
    return sum;
}

[[gnu::used]] bool fixed_list_push_raw(RawList& list, std::uint32_t value) noexcept {
    if (list.count == 64) {
        return false;
    }
    list.slots[list.count] = value;
    ++list.count;
    return true;
}

[[gnu::used]] std::uint32_t fixed_list_take_last_raw(RawList& list) noexcept {
    if (list.count == 0) {
        return 0;
    }
    --list.count;
    return list.slots[list.count];
}

[[gnu::used]] std::uint32_t box_read_raw(std::unique_ptr<Payload>& box) noexcept { return box->value; }

[[gnu::used]] std::uint32_t ref_read_raw(Payload const* payload) noexcept { return payload->value; }

[[gnu::used]] std::uint32_t option_expect_raw(std::optional<std::uint32_t> option) noexcept { return *option; }

[[gnu::used]] RawWindow option_expect_niche_raw(RawWindow window) noexcept { return window; }

[[gnu::used]] std::uint32_t option_match_raw(std::optional<std::uint32_t> option) noexcept {
    return option ? *option * 3u : 0u;
}

[[gnu::used]] std::uint32_t option_loop_raw(std::optional<std::uint32_t> option, std::uint32_t total) noexcept {
    if (option) {
        total += *option;
    }
    return total;
}

[[gnu::used]] std::uint32_t option_value_or_raw(std::optional<std::uint32_t> option) noexcept {
    return option.value_or(7u);
}

[[gnu::used]] void option_clear_block_raw(std::optional<Block64>& held) noexcept { held = std::nullopt; }

// The raw form of an Option of a word is a size and a flag word, which come
// back in two registers.
struct RawWord final {
    std::size_t value;
    std::size_t holds;
};

[[gnu::used]] RawWord option_empty_word_raw() noexcept { return RawWord{0, 0}; }

[[gnu::used]] RawWord option_word_below_raw(std::size_t value, std::size_t limit) noexcept {
    if (value >= limit) return RawWord{0, 0};
    return RawWord{value, 1};
}

[[gnu::used]] bool option_equal_raw(std::optional<std::uint32_t> left, std::optional<std::uint32_t> right) noexcept {
    return left == right;
}

[[gnu::used]] bool option_equal_word_raw(std::optional<std::uint64_t> left,
                                         std::optional<std::uint64_t> right) noexcept {
    return left == right;
}

[[gnu::used]] std::uint32_t* option_ref_some_raw(std::uint32_t& slot) noexcept { return &slot; }

[[gnu::used]] std::expected<std::uint32_t, Code32> option_ok_or_raw(std::optional<std::uint32_t> option) noexcept {
    if (option) return *option;
    return std::unexpected(Code32::gone);
}

[[gnu::used]] std::optional<std::uint32_t> option_try_raw(std::optional<std::uint32_t> option) noexcept {
    if (!option) return std::nullopt;
    return *option + 1u;
}

[[gnu::used]] std::expected<std::uint32_t, Code32>
result_try_raw(std::expected<std::uint32_t, Code32> result) noexcept {
    if (!result) return std::unexpected(result.error());
    return *result + 1u;
}

[[gnu::used]] std::expected<std::uint32_t, NetError8>
result_try_convert_raw(std::expected<std::uint32_t, Code8> result) noexcept {
    if (!result) return std::unexpected(NetError8{result.error()});
    return *result + 1u;
}

[[gnu::used]] std::uint32_t result_expect_raw(std::expected<std::uint32_t, Code32> result) noexcept { return *result; }

[[gnu::used]] std::uint32_t result_match_raw(std::expected<std::uint32_t, Code32> result) noexcept {
    return result ? *result * 3u : static_cast<std::uint32_t>(result.error());
}

// The raw form of a OneOf is a storage word that holds each alternative and
// a tag of one byte after it.  A union gives the same layout, but the rule
// proof-routes refuses a union, because it can hold a proof type.
struct RawReading final {
    std::uint32_t storage;
    std::uint8_t tag;
};

[[gnu::used]] std::uint32_t one_of_match_raw(RawReading reading) noexcept {
    if (reading.tag == 0) return reading.storage * 3u;
    if (reading.tag == 1) return reading.storage;
    return std::uint32_t{static_cast<std::uint16_t>(reading.storage)} + 5u;
}

[[gnu::used]] std::uint32_t switch_unreachable_raw(Kind kind) noexcept {
    switch (kind) {
        case Kind::first:
            return 11;
        case Kind::second:
            return 23;
        case Kind::third:
            return 37;
        case Kind::fourth:
            return 41;
        default:
            std::unreachable();
    }
}

// The raw form of a Record is a struct with the same members.
struct RawWords final {
    std::uint64_t first;
    std::uint64_t second;
};

// A hand-written struct.  Its == is the compare of the members by hand, and
// its <=> is the defaulted <=> of the struct.
struct RawPair32 final {
    std::uint32_t first;
    std::uint32_t second;

    friend std::strong_ordering operator<=>(RawPair32 const&, RawPair32 const&) noexcept = default;
};

struct RawTokens final {
    [[no_unique_address]] TokenA first;
    [[no_unique_address]] TokenB second;
};

[[gnu::used]] RawWords record_return_raw(std::uint64_t first, std::uint64_t second) noexcept {
    return RawWords{first, second};
}

[[gnu::used]] std::uint64_t record_binding_raw(RawWords record) noexcept { return record.first + record.second; }

[[gnu::used]] bool record_equal_raw(RawPair32 const& left, RawPair32 const& right) noexcept {
    return left.first == right.first && left.second == right.second;
}

[[gnu::used]] bool record_order_raw(RawPair32 left, RawPair32 right) noexcept { return std::is_lt(left <=> right); }

[[gnu::used]] RawTokens record_pass_tokens_raw(RawTokens tokens) noexcept { return tokens; }

[[gnu::used]] std::optional<std::uint32_t> scalar_checked_add_raw(std::uint32_t left, std::uint32_t right) noexcept {
    std::uint32_t sum = 0;
    if (__builtin_add_overflow(left, right, &sum)) return std::nullopt;
    return sum;
}

[[gnu::used]] std::optional<std::uint64_t> scalar_checked_add_wide_raw(std::uint64_t left,
                                                                       std::uint64_t right) noexcept {
    std::uint64_t sum = 0;
    if (__builtin_add_overflow(left, right, &sum)) return std::nullopt;
    return sum;
}

[[gnu::used]] std::optional<std::int32_t> scalar_checked_div_raw(std::int32_t dividend, std::int32_t divisor) noexcept {
    if (divisor == 0 || (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1)) return std::nullopt;
    return dividend / divisor;
}

[[gnu::used]] std::uint64_t scalar_wrapping_shl_raw(std::uint64_t value, int count) noexcept {
    return value << (static_cast<unsigned>(count) & 63u);
}

[[gnu::used]] std::int64_t scalar_saturating_add_raw(std::int64_t left, std::int64_t right) noexcept {
    return std::saturating_add(left, right);
}

struct RawWideProduct final {
    std::uint64_t low;
    std::uint64_t high;
};

[[gnu::used]] RawWideProduct scalar_widening_mul_raw(std::uint64_t left, std::uint64_t right) noexcept {
    __extension__ using Wide = unsigned __int128;
    Wide const product = static_cast<Wide>(left) * static_cast<Wide>(right);
    return RawWideProduct{static_cast<std::uint64_t>(product), static_cast<std::uint64_t>(product >> 64)};
}

[[gnu::used]] double scalar_min_float_raw(double left, double right) noexcept { return std::min(left, right); }

struct RawBounds final {
    std::int32_t lo;
    std::int32_t hi;
};

[[gnu::used]] std::int32_t scalar_clamp_bounds_raw(std::int32_t value, RawBounds range) noexcept {
    return std::clamp(value, range.lo, range.hi);
}

[[gnu::used]] bool scalar_compare_mixed_raw(std::int64_t left, std::uint64_t right) noexcept {
    return std::cmp_less(left, right);
}

[[gnu::used]] bool scalar_fits_raw(std::int32_t value) noexcept { return std::in_range<std::uint8_t>(value); }

[[gnu::used]] std::uint32_t scalar_unsigned_abs_raw(std::int32_t value) noexcept {
    return value < 0 ? 0u - static_cast<std::uint32_t>(value) : static_cast<std::uint32_t>(value);
}

[[gnu::used]] std::optional<std::uint32_t> scalar_narrow_checked_raw(std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

[[gnu::used]] std::optional<std::int32_t> scalar_narrow_float_raw(double value) noexcept {
    if (!(value > -2147483649.0 && value < 2147483648.0)) return std::nullopt;
    return static_cast<std::int32_t>(value);
}

[[gnu::used]] std::int16_t scalar_narrow_saturating_raw(std::int64_t value) noexcept {
    if (value < std::numeric_limits<std::int16_t>::min()) return std::numeric_limits<std::int16_t>::min();
    if (value > std::numeric_limits<std::int16_t>::max()) return std::numeric_limits<std::int16_t>::max();
    return static_cast<std::int16_t>(value);
}

[[gnu::used]] std::uint64_t scalar_widen_raw(std::uint32_t value) noexcept { return value; }

[[gnu::used]] int scalar_popcount_raw(std::uint64_t value) noexcept { return std::popcount(value); }

[[gnu::used]] int scalar_countl_zero_raw(std::uint64_t value) noexcept { return std::countl_zero(value); }

[[gnu::used]] std::uint64_t scalar_rotl_raw(std::uint64_t value, int count) noexcept { return std::rotl(value, count); }

[[gnu::used]] std::uint32_t scalar_byteswap_raw(std::uint32_t value) noexcept { return std::byteswap(value); }

// std::bit_ceil is undefined past the top bit, so the raw form tests the
// range first.
[[gnu::used]] std::optional<std::uint32_t> scalar_bit_ceil_raw(std::uint32_t value) noexcept {
    if (value > (std::uint32_t{1} << 31)) return std::nullopt;
    return std::bit_ceil(value);
}

[[gnu::used]] std::uint64_t scalar_low_mask_raw(int count) noexcept {
    if (count <= 0) return 0;
    if (count >= 64) return ~std::uint64_t{0};
    return (std::uint64_t{1} << count) - 1;
}

[[gnu::used]] std::uint64_t scalar_bit_cast_raw(double value) noexcept { return std::bit_cast<std::uint64_t>(value); }

[[gnu::used]] std::optional<bool> scalar_decode_bool_raw(std::uint8_t byte) noexcept {
    if (byte > 1) return std::nullopt;
    return byte == 1;
}

[[gnu::used]] std::optional<std::uint32_t> scalar_id_decode_raw(std::uint32_t word) noexcept {
    if (word == std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return word;
}

[[gnu::used]] bool scalar_fp_is_nan_raw(double value) noexcept { return std::isnan(value); }

[[gnu::used]] double scalar_fp_abs_raw(double value) noexcept { return std::fabs(value); }

[[gnu::used]] double scalar_fp_sqrt_raw(double value) noexcept { return std::sqrt(value); }

[[gnu::used]] double scalar_fp_fma_raw(double left, double right, double addend) noexcept {
    return std::fma(left, right, addend);
}

[[gnu::used]] std::uint64_t scalar_fp_total_key_raw(double value) noexcept {
    auto const bits = std::bit_cast<std::uint64_t>(value);
    auto const spread = static_cast<std::uint64_t>(static_cast<std::int64_t>(bits) >> 63);
    return bits ^ (spread | (std::uint64_t{1} << 63));
}

[[gnu::used]] std::uint64_t scalar_exchange_raw(std::uint64_t& slot, std::uint64_t value) noexcept {
    return std::exchange(slot, value);
}

[[gnu::used]] void scalar_swap_raw(std::uint64_t& left, std::uint64_t& right) noexcept { std::swap(left, right); }

[[gnu::used]] std::optional<std::uint32_t> scalar_quantity_add_raw(std::uint32_t left, std::uint32_t right) noexcept {
    std::uint32_t sum = 0;
    if (__builtin_add_overflow(left, right, &sum)) return std::nullopt;
    return sum;
}

[[gnu::used]] std::optional<std::uint32_t> scalar_quantity_to_finer_raw(std::uint32_t value) noexcept {
    std::uint32_t product = 0;
    if (__builtin_mul_overflow(value, 1000u, &product)) return std::nullopt;
    return product;
}

[[gnu::used]] std::optional<std::uint32_t> scalar_quantity_to_coarser_raw(std::uint32_t value) noexcept {
    if (value % 1000u != 0) return std::nullopt;
    return value / 1000u;
}

// The ten rounds of Philox4x32-10 over a std::array counter and key, with
// one 64-bit product for the two halves of each multiplied word.
[[gnu::used]] std::uint32_t scalar_philox_generate_raw(std::uint64_t offset, std::uint64_t key) noexcept {
    std::array<std::uint32_t, 4> counter{static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(offset >> 32),
                                         0u, 0u};
    std::array<std::uint32_t, 2> words{static_cast<std::uint32_t>(key), static_cast<std::uint32_t>(key >> 32)};
    for (int round = 0; round < 10; round++) {
        std::uint64_t const product0 = std::uint64_t{0xD2511F53u} * counter[0];
        std::uint64_t const product1 = std::uint64_t{0xCD9E8D57u} * counter[2];
        counter = {
            static_cast<std::uint32_t>(product1 >> 32) ^ counter[1] ^ words[0], static_cast<std::uint32_t>(product1),
            static_cast<std::uint32_t>(product0 >> 32) ^ counter[3] ^ words[1], static_cast<std::uint32_t>(product0)};
        words[0] += 0x9E3779B9u;
        words[1] += 0xBB67AE85u;
    }
    return counter[0] ^ counter[1] ^ counter[2] ^ counter[3];
}

// The raw form of a token is an empty class that the ABI passes in no
// register.
struct RawToken final {};

[[gnu::used]] int linear_consume_raw(int value) noexcept { return value; }

[[gnu::used]] void linear_assign_raw(int& target, int& source) noexcept { target = source; }

[[gnu::used]] RawToken token_pass_raw(RawToken token) noexcept { return token; }

[[gnu::used]] void token_assign_raw(RawToken& target, RawToken& source) noexcept { target = source; }

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace
