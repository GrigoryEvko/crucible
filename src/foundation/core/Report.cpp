// SPDX-License-Identifier: Apache-2.0

// The cold formatter of the Report family, and the report to a sink.
//
// format_window_ writes one window of the formatted text: the bytes from
// skip to skip + capacity.  So a caller with a small buffer gets each byte
// of a long text, one window after the other, and no byte is lost.  The
// formatter uses no locale, no allocation and no global state.  It reads
// the text up to its terminating zero, which the consteval constructor of
// Fmt checked.
//
// The formatter is total for each input that a caller of the base can
// give.  A placeholder with no argument writes nothing, and a stray brace
// is written as itself.  The constructor of Fmt refuses the two at compile
// time, so they do not occur.

#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core::detail {

namespace {

// The capacity of the stack buffer of report_.  A text of at most this
// many bytes is written with one system call.
constexpr std::size_t report_window_bytes = 512;

// Collects the bytes of one window of the formatted text.  position_
// counts each byte of the text, also a byte outside the window.
class Window {
public:
    Window(char* out, std::size_t capacity, std::size_t skip) noexcept : out_{out}, capacity_{capacity}, skip_{skip} {}

    void put(char byte) noexcept {
        if (position_ >= skip_ && position_ - skip_ < capacity_) out_[position_ - skip_] = byte;
        ++position_;
    }

    void put(char const* bytes, std::size_t count) noexcept {
        for (std::size_t index = 0; index < count; ++index)
            put(bytes[index]);
    }

    [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
    char* out_;
    std::size_t capacity_;
    std::size_t skip_;
    std::size_t position_ = 0;
};

// Writes value in decimal.  A std::uint64_t has at most 20 digits.
void put_unsigned(Window& window, std::uint64_t value) noexcept {
    char digits[20] = {};
    std::size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + value % 10U);
        ++count;
        value /= 10U;
    } while (value != 0U);
    while (count > 0) {
        --count;
        window.put(digits[count]);
    }
}

// Writes the std::int64_t whose bits are bits.  The magnitude is the
// unsigned negation of the bits, which is exact also for the smallest
// value.
void put_signed(Window& window, std::uint64_t bits) noexcept {
    if ((bits >> 63U) != 0U) {
        window.put('-');
        put_unsigned(window, 0U - bits);
    } else {
        put_unsigned(window, bits);
    }
}

// ── The text of a floating value ─────────────────────────────────────
//
// Report.h gives the form of the text (THE TEXT OF A FLOAT).  The digits come
// from the free-format algorithm of Steele and White, in the form of Burger
// and Dybvig ("Printing Floating-Point Numbers Quickly and Accurately", PLDI
// 1996), with exact natural numbers.  The value is r / s, and m_plus / s and
// m_minus / s are the halves of the gaps to the next float above and below.
// Each digit is the quotient of 10 r / s, and the digits stop at the first
// one that reads back to the value.  Each step is integer arithmetic on the
// bits of the value, so the text does not change with the target, the
// rounding mode or the floating environment.  Complexity: O(D * W) word
// operations, for D digits and W words of the largest natural number: at
// most 17 digits and 35 words for a double.

// A natural number of at most natural_words words of 32 bits, the least
// significant word first.  size_ counts the words in use, and the word at
// size_ - 1 is not zero.
class Natural {
public:
    static constexpr std::size_t natural_words = 40;

    Natural() noexcept = default;

    explicit Natural(std::uint64_t value) noexcept {
        while (value != 0U) {
            words_[size_] = static_cast<std::uint32_t>(value);
            ++size_;
            value >>= 32U;
        }
    }

    // this = this * factor.
    void multiply(std::uint32_t factor) noexcept {
        std::uint64_t carry = 0;
        for (std::size_t index = 0; index < size_; ++index) {
            std::uint64_t const product = std::uint64_t{words_[index]} * factor + carry;
            words_[index] = static_cast<std::uint32_t>(product);
            carry = product >> 32U;
        }
        push_carry(carry);
    }

    // this = this * 10^power.
    void multiply_power_of_ten(std::uint32_t power) noexcept {
        for (; power >= 9U; power -= 9U)
            multiply(1000000000U);
        std::uint32_t factor = 1;
        for (; power > 0U; --power)
            factor *= 10U;
        multiply(factor);
    }

    // this = this * 2^bits.
    void shift_left(std::uint32_t bits) noexcept {
        if (size_ == 0) return;
        std::size_t const word_shift = bits / 32U;
        std::uint32_t const bit_shift = bits % 32U;
        std::size_t const grown = size_ + word_shift + 1;
        if (grown > natural_words) overflow();
        for (std::size_t index = grown; index-- > 0;) {
            std::uint64_t value = 0;
            if (index >= word_shift && index - word_shift < size_) {
                value = std::uint64_t{words_[index - word_shift]} << bit_shift;
            }
            if (bit_shift != 0U && index > word_shift && index - word_shift - 1 < size_) {
                value |= std::uint64_t{words_[index - word_shift - 1]} >> (32U - bit_shift);
            }
            words_[index] = static_cast<std::uint32_t>(value);
        }
        size_ = grown;
        trim();
    }

    // this = this + other.
    void add(Natural const& other) noexcept {
        std::size_t const longest = size_ > other.size_ ? size_ : other.size_;
        std::uint64_t carry = 0;
        for (std::size_t index = 0; index < longest; ++index) {
            std::uint64_t const sum = std::uint64_t{word_at(index)} + other.word_at(index) + carry;
            words_[index] = static_cast<std::uint32_t>(sum);
            carry = sum >> 32U;
        }
        size_ = longest;
        push_carry(carry);
    }

    // this = this - other.  The caller makes sure that other is not more than
    // this.
    void subtract(Natural const& other) noexcept {
        std::uint64_t borrow = 0;
        for (std::size_t index = 0; index < size_; ++index) {
            std::uint64_t const taken = std::uint64_t{other.word_at(index)} + borrow;
            std::uint64_t const held = words_[index];
            words_[index] = static_cast<std::uint32_t>(held - taken);
            borrow = held < taken ? 1U : 0U;
        }
        trim();
    }

    // Divides this by divisor in place, and gives the remainder.  The
    // divisor is not zero.
    [[nodiscard]] std::uint32_t divide(std::uint32_t divisor) noexcept {
        std::uint64_t remainder = 0;
        for (std::size_t index = size_; index-- > 0;) {
            std::uint64_t const value = (remainder << 32U) | words_[index];
            words_[index] = static_cast<std::uint32_t>(value / divisor);
            remainder = value % divisor;
        }
        trim();
        return static_cast<std::uint32_t>(remainder);
    }

    [[nodiscard]] bool is_zero() const noexcept { return size_ == 0; }

    // A negative value, zero or a positive value when left is less than,
    // equal to or more than right.
    [[nodiscard]] friend int compare(Natural const& left, Natural const& right) noexcept {
        if (left.size_ != right.size_) return left.size_ < right.size_ ? -1 : 1;
        for (std::size_t index = left.size_; index-- > 0;) {
            if (left.words_[index] != right.words_[index]) return left.words_[index] < right.words_[index] ? -1 : 1;
        }
        return 0;
    }

private:
    [[nodiscard]] std::uint32_t word_at(std::size_t index) const noexcept { return index < size_ ? words_[index] : 0U; }

    void push_carry(std::uint64_t carry) noexcept {
        if (carry == 0U) return;
        if (size_ == natural_words) overflow();
        words_[size_] = static_cast<std::uint32_t>(carry);
        ++size_;
    }

    void trim() noexcept {
        while (size_ > 0 && words_[size_ - 1] == 0U)
            --size_;
    }

    // float_layout_bits() proves that no float needs more words, so a call
    // shows a defect of the formatter.
    [[noreturn]] static void overflow() noexcept {
        fatal("the formatter of a floating value needs more words than its natural number holds");
    }

    std::uint32_t words_[natural_words] = {};
    std::size_t size_ = 0;
};

[[nodiscard]] Natural sum_of(Natural left, Natural const& right) noexcept {
    left.add(right);
    return left;
}

// The layout of one IEEE binary format.
struct FloatLayout {
    std::uint32_t fraction_bits = 0;
    std::uint32_t exponent_bits = 0;
};

constexpr FloatLayout binary64_layout{52, 11};
constexpr FloatLayout binary32_layout{23, 8};

[[nodiscard]] constexpr std::int32_t bias_of(FloatLayout layout) noexcept {
    return (std::int32_t{1} << (layout.exponent_bits - 1U)) - 1;
}

// The exponent of the last bit of a subnormal value: -1074 or -149.
[[nodiscard]] constexpr std::int32_t smallest_exponent_of(FloatLayout layout) noexcept {
    return 1 - bias_of(layout) - static_cast<std::int32_t>(layout.fraction_bits);
}

// The exponent of the last bit of the largest finite value: 971 or 104.
[[nodiscard]] constexpr std::int32_t largest_exponent_of(FloatLayout layout) noexcept {
    return bias_of(layout) - static_cast<std::int32_t>(layout.fraction_bits);
}

// An upper bound of the bits of each natural number of shortest_digits().
// With an exponent that is not negative, the scale is at most 4 * 10^k, and
// k is less than the bits of the largest value times log10(2), plus one.
// With a negative exponent, the scale is at most 4 * 2^-exponent.  The fix of
// the estimate multiplies the scale by 10 at most four times (14 bits), and
// r, r + m_plus, 2 r and 10 r stay below 20 times the scale (5 bits).  So 32
// bits above the larger scale hold each number.  log10(2) is less than
// 78914 / 2^18, and log2(10) is less than 3402 / 1024.
[[nodiscard]] constexpr std::uint32_t float_layout_bits(FloatLayout layout) noexcept {
    std::uint32_t const top = static_cast<std::uint32_t>(largest_exponent_of(layout)) + layout.fraction_bits + 1U;
    std::uint32_t const largest_power = ((top * 78914U) >> 18U) + 1U;
    std::uint32_t const positive_scale = 2U + (largest_power * 3402U + 1023U) / 1024U;
    std::uint32_t const negative_scale = 2U + static_cast<std::uint32_t>(-smallest_exponent_of(layout));
    return 32U + (positive_scale > negative_scale ? positive_scale : negative_scale);
}

static_assert(float_layout_bits(binary64_layout) <= 32U * Natural::natural_words);
static_assert(float_layout_bits(binary32_layout) <= 32U * Natural::natural_words);

// The most digits that a shortest digit string holds: 17 for a double.
constexpr std::size_t shortest_digits_max = 17;

// The shortest digits of a positive finite value and its decimal exponent:
// the value is near 0.D1 D2 ... Dn times 10^exponent.
struct ShortestDigits {
    char digits[shortest_digits_max] = {};
    std::size_t count = 0;
    std::int32_t exponent = 0;
};

// The shortest digits of mantissa * 2^exponent.  A reader that rounds ties
// to even reads each end of the interval as the value when the mantissa is
// even.  narrow_below is true when the gap below the value is half of the
// gap above it: the mantissa is a power of two, and the binade below has a
// smaller exponent.
[[nodiscard]] ShortestDigits shortest_digits(std::uint64_t mantissa, std::int32_t exponent,
                                             bool narrow_below) noexcept {
    bool const ends_read_back = (mantissa & 1U) == 0U;
    std::uint32_t const extra = narrow_below ? 2U : 1U;
    Natural r{mantissa};
    Natural s{1};
    Natural m_plus{1};
    Natural m_minus{1};
    if (exponent >= 0) {
        std::uint32_t const bits = static_cast<std::uint32_t>(exponent);
        r.shift_left(bits + extra);
        s.shift_left(extra);
        m_plus.shift_left(bits + extra - 1U);
        m_minus.shift_left(bits);
    } else {
        r.shift_left(extra);
        s.shift_left(static_cast<std::uint32_t>(-exponent) + extra);
        m_plus.shift_left(extra - 1U);
    }
    // An estimate of the decimal exponent that is never more than the
    // exponent of the shortest digits, and at most four less.  top is the
    // exponent of the highest bit of the value.
    std::int32_t const width = 64 - __builtin_clzg(mantissa);
    std::int32_t const top = exponent + width - 1;
    std::int32_t estimate = 0;
    if (top > 0) {
        estimate = static_cast<std::int32_t>((static_cast<std::uint32_t>(top) * 78913U) >> 18U);
    } else if (top < 0) {
        estimate = -static_cast<std::int32_t>((static_cast<std::uint32_t>(-top) * 78913U) >> 18U) - 1;
    }
    if (estimate >= 0) {
        s.multiply_power_of_ten(static_cast<std::uint32_t>(estimate));
    } else {
        std::uint32_t const power = static_cast<std::uint32_t>(-estimate);
        r.multiply_power_of_ten(power);
        m_plus.multiply_power_of_ten(power);
        m_minus.multiply_power_of_ten(power);
    }
    ShortestDigits result{};
    result.exponent = estimate;
    // The upper end of the interval must be below 10^exponent, or at it when
    // the end does not read back to the value.
    for (;;) {
        int const order = compare(sum_of(r, m_plus), s);
        if (ends_read_back ? order < 0 : order <= 0) break;
        s.multiply(10U);
        ++result.exponent;
    }
    for (;;) {
        r.multiply(10U);
        m_plus.multiply(10U);
        m_minus.multiply(10U);
        std::uint32_t digit = 0;
        while (compare(r, s) >= 0) {
            r.subtract(s);
            ++digit;
        }
        int const below = compare(r, m_minus);
        int const above = compare(sum_of(r, m_plus), s);
        bool const low_reads_back = ends_read_back ? below <= 0 : below < 0;
        bool const high_reads_back = ends_read_back ? above >= 0 : above > 0;
        if (low_reads_back && high_reads_back) {
            int const half = compare(sum_of(r, r), s);
            if (half > 0 || (half == 0 && (digit & 1U) != 0U)) ++digit;
        } else if (high_reads_back) {
            ++digit;
        }
        if (result.count == shortest_digits_max)
            fatal("the shortest digits of a floating value hold more than 17 digits");
        result.digits[result.count] = static_cast<char>('0' + digit);
        ++result.count;
        if (low_reads_back || high_reads_back) return result;
    }
}

// Writes the exact integer value of mantissa * 2^exponent.  The caller knows
// that the value is an integer, so a negative exponent drops only zero bits.
void put_exact_integer(Window& window, std::uint64_t mantissa, std::int32_t exponent) noexcept {
    Natural value{mantissa};
    if (exponent > 0) {
        value.shift_left(static_cast<std::uint32_t>(exponent));
    } else if (exponent < 0) {
        if (exponent < -63) fatal("the exact integer of a floating value has no integer value");
        value = Natural{mantissa >> static_cast<std::uint32_t>(-exponent)};
    }
    char digits[shortest_digits_max + 8] = {};
    std::size_t count = 0;
    do {
        if (count == sizeof digits) fatal("the exact integer of a floating value holds more than 25 digits");
        digits[count] = static_cast<char>('0' + value.divide(10U));
        ++count;
    } while (!value.is_zero());
    while (count > 0) {
        --count;
        window.put(digits[count]);
    }
}

// Writes the text of the float with the bits of `bits` in the layout.
void put_float(Window& window, std::uint64_t bits, FloatLayout layout) noexcept {
    std::uint64_t const fraction_mask = (std::uint64_t{1} << layout.fraction_bits) - 1U;
    std::uint64_t const exponent_mask = (std::uint64_t{1} << layout.exponent_bits) - 1U;
    std::uint64_t const fraction = bits & fraction_mask;
    std::uint64_t const biased = (bits >> layout.fraction_bits) & exponent_mask;
    bool const is_negative = ((bits >> (layout.fraction_bits + layout.exponent_bits)) & 1U) != 0U;
    if (is_negative) window.put('-');
    if (biased == exponent_mask) {
        if (fraction == 0U) {
            window.put("inf", 3);
        } else {
            window.put("nan", 3);
        }
        return;
    }
    if (biased == 0U && fraction == 0U) {
        window.put('0');
        return;
    }
    std::uint64_t mantissa = fraction;
    std::int32_t exponent = smallest_exponent_of(layout);
    if (biased != 0U) {
        mantissa |= std::uint64_t{1} << layout.fraction_bits;
        exponent =
            static_cast<std::int32_t>(biased) - bias_of(layout) - static_cast<std::int32_t>(layout.fraction_bits);
    }
    bool const narrow_below = fraction == 0U && biased > 1U;
    ShortestDigits const shortest = shortest_digits(mantissa, exponent, narrow_below);
    std::size_t const count = shortest.count;
    std::int32_t const point = shortest.exponent;
    std::int32_t const scientific = point - 1;
    std::uint32_t const magnitude = static_cast<std::uint32_t>(scientific < 0 ? -scientific : scientific);
    std::size_t const exponent_length = count + (count > 1 ? 1U : 0U) + 2U + (magnitude >= 100U ? 3U : 2U);
    std::size_t plain_length = 0;
    if (point <= 0) {
        plain_length = 2U + static_cast<std::size_t>(-point) + count;
    } else if (static_cast<std::size_t>(point) < count) {
        plain_length = count + 1U;
    } else {
        plain_length = static_cast<std::size_t>(point);
    }
    if (plain_length <= exponent_length) {
        if (point <= 0) {
            window.put("0.", 2);
            for (std::int32_t zero = point; zero < 0; ++zero)
                window.put('0');
            window.put(shortest.digits, count);
        } else if (static_cast<std::size_t>(point) < count) {
            std::size_t const whole = static_cast<std::size_t>(point);
            window.put(shortest.digits, whole);
            window.put('.');
            window.put(shortest.digits + whole, count - whole);
        } else {
            // Each integer of this length reads back to the value, and the
            // exact integer is the nearest one.
            put_exact_integer(window, mantissa, exponent);
        }
        return;
    }
    window.put(shortest.digits[0]);
    if (count > 1) {
        window.put('.');
        window.put(shortest.digits + 1, count - 1);
    }
    window.put('e');
    window.put(scientific < 0 ? '-' : '+');
    if (magnitude >= 100U) window.put(static_cast<char>('0' + magnitude / 100U));
    window.put(static_cast<char>('0' + magnitude / 10U % 10U));
    window.put(static_cast<char>('0' + magnitude % 10U));
}

void put_argument(Window& window, FmtArg const& argument) noexcept {
    switch (argument.kind) {
        case FmtArgKind::signed_integer:
            put_signed(window, argument.bits);
            return;
        case FmtArgKind::unsigned_integer:
            put_unsigned(window, argument.bits);
            return;
        case FmtArgKind::boolean:
            if (argument.bits != 0U) {
                window.put("true", 4);
            } else {
                window.put("false", 5);
            }
            return;
        case FmtArgKind::text:
            window.put(argument.first, argument.size);
            return;
        case FmtArgKind::binary32:
            put_float(window, argument.bits, binary32_layout);
            return;
        case FmtArgKind::binary64:
            put_float(window, argument.bits, binary64_layout);
            return;
        default:
            // A kind outside the enumerators writes nothing.  The inline
            // functions of the family build each argument, so no such kind
            // occurs, and a report must not end the process.
            return;
    }
}

}  // namespace

std::size_t format_window_(char* out, std::size_t capacity, std::size_t skip, char const* text, FmtArg const* arguments,
                           std::size_t count) noexcept {
    Window window{out, capacity, skip};
    std::size_t next_argument = 0;
    // A byte that is not the terminating zero has a next byte, at worst the
    // terminating zero, so the read of cursor[1] stays in the text.
    for (char const* cursor = text; *cursor != '\0'; ++cursor) {
        char const byte = *cursor;
        if (byte == '{' && cursor[1] == '}') {
            if (next_argument < count) put_argument(window, arguments[next_argument]);
            ++next_argument;
            ++cursor;
        } else if ((byte == '{' || byte == '}') && cursor[1] == byte) {
            window.put(byte);
            ++cursor;
        } else {
            window.put(byte);
        }
    }
    return window.position();
}

}  // namespace foundation::core::detail

namespace foundation::core {

void report(Sink sink, Fmt<> fmt) noexcept { detail::report_(sink, detail::fmt_text_(fmt), nullptr, 0); }

}  // namespace foundation::core

namespace foundation::core::detail {

void report_(Sink sink, char const* text, FmtArg const* arguments, std::size_t count) noexcept {
    char window[report_window_bytes] = {};
    std::size_t skip = 0;
    std::size_t length = 0;
    do {
        length = format_window_(window, report_window_bytes, skip, text, arguments, count);
        std::size_t const rest = length - skip;
        std::size_t const taken = rest < report_window_bytes ? rest : report_window_bytes;
        write_sink_(sink, window, taken);
        skip += taken;
    } while (skip < length);
}

}  // namespace foundation::core::detail
