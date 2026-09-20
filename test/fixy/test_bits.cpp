// Sentinel TU for fixy/Bits.h: two Bits over different enums are
// different types that do not compose, the raw door is named, and the
// bit operations agree with the plain integer ones.
//
// The header's self-test pins the rejections through concepts.  This TU
// adds the exhaustive agreement check over a four-flag enum, which is
// where a cast that loses the high bits of a narrow underlying type
// would show.

#include <fixy/Bits.h>

#include <bit>
#include <cstdint>
#include <type_traits>

namespace {

using ::fixy::Bits;

enum class Perm : std::uint8_t {
    Read = 0x01,
    Write = 0x02,
    Exec = 0x04,
    Sticky = 0x08
};
enum class Colour : std::uint16_t {
    Red = 0x0001,
    Blue = 0x0100
};

using PermBits = Bits<Perm>;
using ColourBits = Bits<Colour>;

static_assert(sizeof(PermBits) == sizeof(std::uint8_t));
static_assert(sizeof(ColourBits) == sizeof(std::uint16_t));
static_assert(std::is_trivially_copyable_v<PermBits>);
static_assert(std::is_standard_layout_v<PermBits>);

// An unscoped enum converts to its integer on its own, so it is refused.
enum Loose : unsigned {
    LooseA = 1
};
template <typename E>
concept CanNameBits = requires { typename Bits<E>; };
static_assert(CanNameBits<Perm>);
static_assert(!CanNameBits<Loose>, "an unscoped enum would let a raw integer through");
static_assert(!CanNameBits<int>);

// The raw door is named.  Brace initialization from an integer does not
// reach it.
template <typename W, typename Lit>
concept CanConstructFrom = requires { W{std::declval<Lit>()}; };
static_assert(!CanConstructFrom<PermBits, int>);
static_assert(!CanConstructFrom<PermBits, std::uint8_t>);
static_assert(PermBits::from_raw(0x03).raw() == 0x03);

// Two Bits over different enums share no operator, because the
// operators are hidden friends of each instantiation.
template <typename A, typename B>
concept CanOr = requires(A a, B b) { a | b; };
template <typename A, typename B>
concept CanAnd = requires(A a, B b) { a & b; };
template <typename A, typename B>
concept CanEq = requires(A a, B b) { a == b; };
static_assert(CanOr<PermBits, PermBits>);
static_assert(!CanOr<PermBits, ColourBits>, "two enum-typed bit fields do not compose");
static_assert(!CanAnd<PermBits, ColourBits>);
static_assert(!CanEq<PermBits, ColourBits>);

// A flag of the wrong enum is not an operand either.
static_assert(CanOr<PermBits, Perm>);
static_assert(!CanOr<PermBits, Colour>);

// The initializer-list door folds the flags it is given.
static_assert(PermBits{Perm::Read, Perm::Exec}.raw() == 0x05);
static_assert(PermBits{}.none());
static_assert(PermBits{Perm::Read}.any());

// Every operation agrees with the plain integer one, at every value of
// the four-flag space.
[[nodiscard]] consteval bool operations_agree_exhaustively() noexcept {
    for (unsigned a = 0; a < 16u; ++a) {
        for (unsigned b = 0; b < 16u; ++b) {
            const PermBits x = PermBits::from_raw(static_cast<std::uint8_t>(a));
            const PermBits y = PermBits::from_raw(static_cast<std::uint8_t>(b));

            if ((x | y).raw() != static_cast<std::uint8_t>(a | b)) return false;
            if ((x & y).raw() != static_cast<std::uint8_t>(a & b)) return false;
            if ((x ^ y).raw() != static_cast<std::uint8_t>(a ^ b)) return false;
            if ((~x).raw() != static_cast<std::uint8_t>(~a)) return false;
            if (x.popcount() != std::popcount(static_cast<std::uint8_t>(a))) return false;
            if (x.none() != (a == 0u)) return false;
            if (x.any() != (a != 0u)) return false;
            if ((x == y) != (a == b)) return false;
        }
    }
    return true;
}
static_assert(operations_agree_exhaustively());

// set, unset and toggle move exactly one flag and leave the rest alone.
[[nodiscard]] consteval bool mutators_move_one_flag() noexcept {
    PermBits p{};
    p.set(Perm::Read);
    if (!p.test(Perm::Read) || p.test(Perm::Write)) return false;
    p.set(Perm::Write);
    if (p.raw() != 0x03) return false;
    p.unset(Perm::Read);
    if (p.raw() != 0x02) return false;
    p.toggle(Perm::Exec);
    if (p.raw() != 0x06) return false;
    p.toggle(Perm::Exec);
    if (p.raw() != 0x02) return false;
    p.clear();
    return p.none();
}
static_assert(mutators_move_one_flag());

int check_runtime_word() {
    volatile std::uint8_t seed = 0x05;
    PermBits p = PermBits::from_raw(static_cast<std::uint8_t>(seed));
    if (!p.test(Perm::Read) || !p.test(Perm::Exec)) return 10;
    if (p.test(Perm::Write)) return 11;
    if (p.popcount() != 2) return 12;

    p |= Perm::Write;
    if (p.raw() != 0x07) return 13;
    p &= PermBits{Perm::Read, Perm::Write};
    if (p.raw() != 0x03) return 14;
    p ^= PermBits{Perm::Read};
    if (p.raw() != 0x02) return 15;

    const ColourBits c{Colour::Red, Colour::Blue};
    if (c.raw() != 0x0101) return 16;

    return 0;
}

// The mutators and the whole-word operators, run rather than folded.
// The complement is over the underlying word and not over the declared
// flags, so a four-flag enum in a byte leaves seven bits set.
int check_runtime_flag_algebra() {
    PermBits b{};
    if (!b.none()) return 20;

    b.set(Perm::Read);
    b.set(Perm::Write);
    if (b.popcount() != 2) return 21;

    PermBits copy = b;
    if (copy != b) return 22;

    PermBits joined = b | PermBits{Perm::Exec};
    if (joined.popcount() != 3) return 23;
    if (!joined.test(Perm::Exec)) return 24;

    // The enum-typed and the word-typed right operands agree.
    if ((b | Perm::Exec) != joined) return 25;

    PermBits serialized = PermBits::from_raw(static_cast<std::uint8_t>(0x0F));
    if (!serialized.test(Perm::Read) || !serialized.test(Perm::Write)) return 26;
    if (!serialized.test(Perm::Exec) || !serialized.test(Perm::Sticky)) return 27;
    if (serialized.popcount() != 4) return 28;

    serialized.toggle(Perm::Read);
    if (serialized.test(Perm::Read)) return 29;
    serialized.toggle(Perm::Read);
    if (!serialized.test(Perm::Read)) return 30;

    b.clear();
    if (!b.none()) return 31;

    PermBits accumulated{};
    accumulated |= Perm::Read;
    accumulated |= Perm::Write;
    if (accumulated.popcount() != 2) return 32;

    PermBits complement = ~PermBits{Perm::Read};
    if (complement.test(Perm::Read)) return 33;
    if (complement.popcount() != 7) return 34;

    if (PermBits{Perm::Read, Perm::Exec}.raw() != 0x05) return 35;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_flag_algebra(); rc != 0) return rc;
    if (int rc = check_runtime_word(); rc != 0) return rc;

    return 0;
}
