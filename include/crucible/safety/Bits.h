#pragma once

// ── crucible::safety::Bits<EnumType> ────────────────────────────────
//
// Typed bit-field newtype for enum-classified flag sets.  Replaces
// raw `uint8_t flags` / `uint16_t flags` fields where bits are an
// enum-named set, threading enum-typed safety through every read,
// write, set, unset, toggle, test operation.
//
// ── Production call sites (per WRAP-* tasks) ────────────────────────
//
//   #924  Graph::GraphNode::flags     uint8_t   → Bits<NodeFlags>
//   #950  NumericalRecipe::flags      uint8_t   → Bits<RecipeFlags>
//   #956  Ops.h ExprFlags constants   uint8_t   → Bits<ExprFlags>
//   #1031 SymbolTable::sym_flags      uint8_t   → Bits<SymFlags>
//   #1036 TensorMeta::flags           uint8_t   → Bits<MetaFlags>
//   #1058 TraceRing::op_flags         uint8_t   → Bits<OpFlags>
//
// All six call sites today use a raw integer with `static constexpr
// underlying = 1u << N` constants in the same TU.  The bug class
// caught: a refactor that mixes two unrelated flag enums (e.g.,
// `node.flags |= static_cast<uint8_t>(RecipeFlags::Foo)`) — silent at
// compile time today, becomes a compile error after Bits<E> wrapping
// because Bits<NodeFlags> and Bits<RecipeFlags> are different
// instantiations and do NOT compose.
//
// ── What this header ships ──────────────────────────────────────────
//
//   ScopedEnum<E>         — concept admitting only `enum class` types.
//                           Plain unscoped `enum X { ... }` is rejected.
//
//   Bits<EnumType>        — typed bit-field over EnumType's underlying
//                           type.  sizeof preserved exactly.  Operations
//                           take/return EnumType, never raw integers.
//                           Raw escape requires explicit `.raw()` /
//                           `Bits<E>::from_raw(underlying)` — both
//                           grep-discoverable for review.
//
// ── Public API ──────────────────────────────────────────────────────
//
//   Construction:
//     Bits<E>{}                           — empty bitset (NSDMI = 0)
//     Bits<E>{flag1, flag2, flag3}        — initializer-list
//     Bits<E>::from_raw(underlying)       — explicit raw escape
//                                           (deserialization paths)
//
//   Mutation:
//     set(EnumType)         — bitwise OR
//     unset(EnumType)       — bitwise AND-NOT
//     toggle(EnumType)      — bitwise XOR
//     clear()               — zero all bits
//
//   Query:
//     test(EnumType) const  — bool: is the flag set?
//     none() const          — bool: no flags set
//     any() const           — bool: at least one flag set
//     popcount() const      — int: number of set bits
//
//   Composition:
//     operator|, operator&, operator^, operator~          (Bits, Bits)
//     operator|, operator& (Bits, EnumType) and reverse
//     operator|=, operator&=, operator^=                  (Bits, Bits)
//     operator|=                                         (Bits, EnumType)
//     operator==                                          (defaulted)
//
//   Raw escape (audit-discoverable):
//     raw() const           — underlying_type
//     from_raw(underlying)  — static factory
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — bits_ has NSDMI = 0; default ctor zeroes via NSDMI.
//   TypeSafe — every operation takes/returns EnumType; raw integer
//              cannot enter or escape without explicit .from_raw() /
//              .raw() (both grep-discoverable).  Bits<E1> | Bits<E2>
//              is a compile error (different template instantiations).
//   NullSafe — no pointers; not applicable.
//   MemSafe  — defaulted copy/move/destroy; trivially copyable.
//   BorrowSafe — value type; per-instance, no aliasing surface.
//   ThreadSafe — value type; per-instance.  Concurrent mutation
//                requires a separate AtomicBits wrapper (future work).
//   LeakSafe — no resource ownership.  No allocations, no fd, no thread.
//   DetSafe  — pure structural; bitwise ops on integers; no FP, no
//              reduction order, no kernel emission.  Bit-identical
//              under any compiler flag matrix.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
//   sizeof(Bits<E>) == sizeof(std::underlying_type_t<E>).
//   Every operation compiles to a single integer bit-op or compare.
//   Verified by static_asserts at the end of the header.
//   std::is_trivially_copyable_v<Bits<E>> — memcpy-safe for serializa-
//   tion/replay paths.
//
// ── Why structural (not Graded) ─────────────────────────────────────
//
// A typed bit-field is a STRUCTURAL constraint (the carrier is an
// integer with a phantom enum tag) — not a graded modal value.  A
// SetUnionLattice over EnumType could be defined, but the dispatcher
// (FOUND-D), KernelCache row keying, and Augur per-axis drift do NOT
// consume bit-flag-set lattices.  Bits<E> joins ConstantTime, Pinned,
// Machine, Checked, NotInherited, ScopedView, OwnedRegion as a
// deliberately-not-graded structural wrapper.  Per CLAUDE.md §XVI's
// "Structural wrappers — deliberately not graded" policy.
//
// ── Note on mutual-exclusion ────────────────────────────────────────
//
// Some bitfields (e.g. ExprFlags' is_constant vs is_symbolic) are
// MUTUALLY EXCLUSIVE.  This wrapper does NOT statically enforce
// mutual exclusion — that requires a per-enum trait specifying which
// sets are XorBits.  The caller-facing API simply prevents
// integer-vs-enum confusion (the dominant bug class for the six
// production call sites above).  Mutual-exclusion enforcement is a
// future extension (see the WRAP-Ops-2 task description for the
// ExprFlags mutual-exclusion case).
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §II        — 8 axioms
//   CLAUDE.md §XVI       — safety wrapper catalog (structural family)
//   CLAUDE.md §XVIII HS14 — neg-compile fixture requirement (≥2)

#include <crucible/Platform.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── Concept: scoped (class) enum ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Plain `enum X { ... }` (unscoped) implicitly converts to underlying
// integer, which would defeat Bits<>'s typed surface.  Restricting to
// `enum class X { ... }` ensures the underlying-type access requires
// explicit static_cast at every site.

template <class E>
concept ScopedEnum = std::is_scoped_enum_v<E>;

// ═════════════════════════════════════════════════════════════════════
// ── Bits<EnumType> ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <ScopedEnum EnumType>
class [[nodiscard]] Bits {
public:
    using enum_type       = EnumType;
    using underlying_type = std::underlying_type_t<EnumType>;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::Bits";
    }

private:
    underlying_type bits_{0};

    // Tag for the private from_raw escape — keeps the escape ctor
    // grep-discoverable via the static factory (`Bits<E>::from_raw(...)`)
    // rather than a public underlying-type ctor that could be invoked
    // accidentally via brace-init (`Bits<E>{42}`).
    struct from_raw_tag_t {};
    constexpr Bits(from_raw_tag_t, underlying_type b) noexcept : bits_{b} {}

    [[nodiscard]] static constexpr underlying_type to_underlying(EnumType e) noexcept {
        return static_cast<underlying_type>(e);
    }

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr Bits() noexcept = default;

    // initializer_list ctor — Bits<E>{flag1, flag2, flag3}.
    // Single-flag case (Bits<E>{flag}) routes here via brace-elision.
    constexpr Bits(std::initializer_list<EnumType> flags) noexcept {
        underlying_type acc = 0;
        for (EnumType f : flags) {
            acc = static_cast<underlying_type>(acc | to_underlying(f));
        }
        bits_ = acc;
    }

    // Explicit raw escape — for deserialization paths reading a flag
    // word from disk, or when initializing from a precomputed mask.
    // Named factory makes the escape grep-discoverable
    // (`grep "Bits<.*>::from_raw"`) so reviewers can audit every site.
    [[nodiscard]] static constexpr Bits from_raw(underlying_type b) noexcept {
        return Bits{from_raw_tag_t{}, b};
    }

    // Defaulted copy/move/destroy — value type.
    constexpr Bits(Bits const&)            = default;
    constexpr Bits(Bits&&)                 = default;
    constexpr Bits& operator=(Bits const&) = default;
    constexpr Bits& operator=(Bits&&)      = default;
    ~Bits()                                = default;

    // ── Mutation ────────────────────────────────────────────────────
    constexpr void set(EnumType f) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | to_underlying(f));
    }
    constexpr void unset(EnumType f) noexcept {
        bits_ = static_cast<underlying_type>(
            bits_ & static_cast<underlying_type>(~to_underlying(f)));
    }
    constexpr void toggle(EnumType f) noexcept {
        bits_ = static_cast<underlying_type>(bits_ ^ to_underlying(f));
    }
    constexpr void clear() noexcept { bits_ = 0; }

    // ── Query ───────────────────────────────────────────────────────
    [[nodiscard]] constexpr bool test(EnumType f) const noexcept {
        return (bits_ & to_underlying(f)) != underlying_type{0};
    }
    [[nodiscard]] constexpr bool none() const noexcept {
        return bits_ == underlying_type{0};
    }
    [[nodiscard]] constexpr bool any() const noexcept {
        return bits_ != underlying_type{0};
    }
    [[nodiscard]] constexpr int popcount() const noexcept {
        // std::popcount selects the appropriate POPCNT (x86) / CNT
        // (AArch64) instruction per ISA at -O3.
        return std::popcount(
            static_cast<std::make_unsigned_t<underlying_type>>(bits_));
    }

    // ── Explicit raw escape (audit-discoverable) ────────────────────
    [[nodiscard]] constexpr underlying_type raw() const noexcept { return bits_; }

    // ── Equality ────────────────────────────────────────────────────
    [[nodiscard]] friend constexpr bool operator==(Bits, Bits) noexcept = default;

    // ── Bitwise composition (Bits, Bits) ───────────────────────────
    //
    // All operators preserve the typed surface — Bits<E> | Bits<E>
    // returns Bits<E>; Bits<E1> | Bits<E2> is a compile error (the
    // operators are class-scoped friends, so different template
    // instantiations are different overload sets).
    [[nodiscard]] friend constexpr Bits operator|(Bits a, Bits b) noexcept {
        return Bits{from_raw_tag_t{},
                    static_cast<underlying_type>(a.bits_ | b.bits_)};
    }
    [[nodiscard]] friend constexpr Bits operator&(Bits a, Bits b) noexcept {
        return Bits{from_raw_tag_t{},
                    static_cast<underlying_type>(a.bits_ & b.bits_)};
    }
    [[nodiscard]] friend constexpr Bits operator^(Bits a, Bits b) noexcept {
        return Bits{from_raw_tag_t{},
                    static_cast<underlying_type>(a.bits_ ^ b.bits_)};
    }
    [[nodiscard]] friend constexpr Bits operator~(Bits a) noexcept {
        return Bits{from_raw_tag_t{},
                    static_cast<underlying_type>(~a.bits_)};
    }

    // ── EnumType-mixed forms (convenience) ─────────────────────────
    [[nodiscard]] friend constexpr Bits operator|(Bits a, EnumType e) noexcept {
        return Bits{from_raw_tag_t{},
                    static_cast<underlying_type>(a.bits_ | to_underlying(e))};
    }
    [[nodiscard]] friend constexpr Bits operator|(EnumType e, Bits b) noexcept {
        return b | e;
    }
    [[nodiscard]] friend constexpr Bits operator&(Bits a, EnumType e) noexcept {
        return Bits{from_raw_tag_t{},
                    static_cast<underlying_type>(a.bits_ & to_underlying(e))};
    }

    // ── Compound assignment ────────────────────────────────────────
    constexpr Bits& operator|=(Bits other) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | other.bits_);
        return *this;
    }
    constexpr Bits& operator|=(EnumType e) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | to_underlying(e));
        return *this;
    }
    constexpr Bits& operator&=(Bits other) noexcept {
        bits_ = static_cast<underlying_type>(bits_ & other.bits_);
        return *this;
    }
    constexpr Bits& operator^=(Bits other) noexcept {
        bits_ = static_cast<underlying_type>(bits_ ^ other.bits_);
        return *this;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Layout invariants ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::bits_layout {

enum class TestU8  : std::uint8_t  { A = 0x01, B = 0x02, C = 0x04, D = 0x08 };
enum class TestU16 : std::uint16_t { X = 0x0001, Y = 0x0100 };
enum class TestU32 : std::uint32_t { K = 0x00010000U, L = 0x80000000U };
enum class TestU64 : std::uint64_t { M = 0x0000000100000000ULL };

}  // namespace detail::bits_layout

static_assert(sizeof(Bits<detail::bits_layout::TestU8>)  == sizeof(std::uint8_t));
static_assert(sizeof(Bits<detail::bits_layout::TestU16>) == sizeof(std::uint16_t));
static_assert(sizeof(Bits<detail::bits_layout::TestU32>) == sizeof(std::uint32_t));
static_assert(sizeof(Bits<detail::bits_layout::TestU64>) == sizeof(std::uint64_t));

static_assert(std::is_trivially_copyable_v<Bits<detail::bits_layout::TestU8>>);
static_assert(std::is_trivially_destructible_v<Bits<detail::bits_layout::TestU8>>);
static_assert(std::is_standard_layout_v<Bits<detail::bits_layout::TestU8>>);

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::bits_self_test {

using F = ::crucible::safety::detail::bits_layout::TestU8;
using B = Bits<F>;

// Default ctor — empty.
inline constexpr B b_empty{};
static_assert(b_empty.raw() == 0);
static_assert(b_empty.none());
static_assert(!b_empty.any());
static_assert(b_empty.popcount() == 0);

// Single-flag initializer.
inline constexpr B b_single{F::A};
static_assert(b_single.raw() == 0x01);
static_assert(b_single.test(F::A));
static_assert(!b_single.test(F::B));
static_assert(b_single.popcount() == 1);

// Multi-flag initializer.
inline constexpr B b_multi{F::A, F::C, F::D};
static_assert(b_multi.raw() == (0x01 | 0x04 | 0x08));
static_assert(b_multi.test(F::A));
static_assert(!b_multi.test(F::B));
static_assert(b_multi.test(F::C));
static_assert(b_multi.test(F::D));
static_assert(b_multi.popcount() == 3);

// from_raw — explicit escape.
inline constexpr B b_raw = B::from_raw(0x05);
static_assert(b_raw.raw() == 0x05);
static_assert(b_raw.test(F::A));
static_assert(b_raw.test(F::C));

// set / unset / toggle / clear.
[[nodiscard]] consteval bool mutate_works() noexcept {
    B b{};
    b.set(F::A);
    if (!b.test(F::A)) return false;
    b.set(F::B);
    if (b.popcount() != 2) return false;
    b.unset(F::A);
    if (b.test(F::A)) return false;
    if (!b.test(F::B)) return false;
    b.toggle(F::C);
    if (!b.test(F::C)) return false;
    b.toggle(F::C);
    if (b.test(F::C)) return false;
    b.clear();
    if (!b.none()) return false;
    return true;
}
static_assert(mutate_works());

// Equality — same-instantiation only, order-independent semantics.
[[nodiscard]] consteval bool equality_works() noexcept {
    B x{F::A, F::B};
    B y{F::B, F::A};   // initializer-list order does NOT matter
    B z{F::A, F::C};
    return (x == y) && !(x == z);
}
static_assert(equality_works());

// Bitwise composition.
[[nodiscard]] consteval bool bitwise_works() noexcept {
    B a{F::A, F::B};
    B b{F::B, F::C};
    B u = a | b;
    if (u.popcount() != 3) return false;
    B i = a & b;
    if (i.raw() != 0x02) return false;
    B x = a ^ b;
    if (x.raw() != (0x01 | 0x04)) return false;
    return true;
}
static_assert(bitwise_works());

// operator~ over the underlying — full-width complement.
[[nodiscard]] consteval bool complement_works() noexcept {
    B a{F::A};                     // 0x01
    B na = ~a;
    return na.raw() == static_cast<std::uint8_t>(~static_cast<std::uint8_t>(0x01));
}
static_assert(complement_works());

// Compound assignment with both Bits and EnumType.
[[nodiscard]] consteval bool compound_works() noexcept {
    B b{};
    b |= F::A;
    b |= F::C;
    if (b.popcount() != 2) return false;
    b &= B{F::A};
    if (b.test(F::C)) return false;
    if (!b.test(F::A)) return false;
    b ^= B{F::A};
    if (b.test(F::A)) return false;
    return true;
}
static_assert(compound_works());

// EnumType | Bits — symmetric.
[[nodiscard]] consteval bool enum_or_bits() noexcept {
    B b{F::A};
    B r1 = F::B | b;
    B r2 = b | F::B;
    return r1 == r2 && r1.popcount() == 2;
}
static_assert(enum_or_bits());

// ── Type-system rejections (load-bearing) ───────────────────────────

// Different enum types do NOT compose via |.
template <class B1, class B2>
concept can_or = requires(B1 a, B2 b) { { a | b }; };

using OtherB = Bits<::crucible::safety::detail::bits_layout::TestU16>;
static_assert(!can_or<B, OtherB>,
    "Bits<E1> | Bits<E2> MUST be a compile error.  Different enum-typed "
    "bitfields are different template instantiations and the friend "
    "operators only see same-instantiation pairs.  Without this rejection, "
    "two unrelated flag enums could be silently mixed at the call site.");

// Cross-instantiation equality is also rejected.
template <class B1, class B2>
concept can_eq = requires(B1 a, B2 b) { { a == b } -> std::convertible_to<bool>; };
static_assert(!can_eq<B, OtherB>);

// Integer literal ctor must NOT be reachable.
template <class W, class Lit>
concept can_construct_from = requires { W{std::declval<Lit>()}; };

static_assert(!can_construct_from<B, int>,
    "Bits<E>{42} (raw integer) MUST NOT compile — only "
    "initializer_list<E> ctor is the public one; from_raw is the only "
    "raw escape.  Without this rejection, raw integers leak into the "
    "typed surface.");

static_assert(!can_construct_from<B, std::uint8_t>,
    "Bits<E>{static_cast<uint8_t>(0x05)} MUST NOT compile — even the "
    "exact underlying type cannot enter via brace-init.  Only the "
    "explicit static factory from_raw() admits underlying-type values.");

// Construction from a different scoped-enum type also rejected
// (initializer-list element type does NOT auto-convert).
static_assert(!can_construct_from<B, ::crucible::safety::detail::bits_layout::TestU16>,
    "Bits<TestU8>{TestU16::X} MUST NOT compile — initializer_list "
    "element type is fixed to the wrapper's enum_type.");

// raw() escape works.
[[nodiscard]] consteval bool raw_escape_works() noexcept {
    B b{F::B, F::C};
    return b.raw() == (0x02 | 0x04);
}
static_assert(raw_escape_works());

// Inherits trivial copy/move from underlying — important for memcpy-
// safe serialization paths and zero-cost field placement.
static_assert(std::is_trivially_copyable_v<B>);

// Wrapper-kind diagnostic surface.
static_assert(B::wrapper_kind() == "structural::Bits");

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    // Construction paths.
    B b{};
    if (!b.none()) std::abort();

    // Mutation ladder.
    b.set(F::A);
    b.set(F::B);
    if (b.popcount() != 2) std::abort();

    // Copy and equality.
    B c = b;
    if (c != b) std::abort();

    // Composition.
    B u = b | B{F::C};
    if (u.popcount() != 3) std::abort();
    if (!u.test(F::C)) std::abort();

    // EnumType-mixed.
    B v = b | F::C;
    if (v != u) std::abort();

    // from_raw escape (deserialization path).
    B serialized = B::from_raw(static_cast<std::uint8_t>(0x0F));
    if (!serialized.test(F::A)) std::abort();
    if (!serialized.test(F::B)) std::abort();
    if (!serialized.test(F::C)) std::abort();
    if (!serialized.test(F::D)) std::abort();
    if (serialized.popcount() != 4) std::abort();

    // Toggle round-trip.
    serialized.toggle(F::A);
    if (serialized.test(F::A)) std::abort();
    serialized.toggle(F::A);
    if (!serialized.test(F::A)) std::abort();

    // Clear.
    b.clear();
    if (!b.none()) std::abort();

    // Compound-assign with EnumType.
    B d{};
    d |= F::A;
    d |= F::B;
    if (d.popcount() != 2) std::abort();

    // Complement.
    B e{F::A};
    B ne = ~e;
    if (ne.test(F::A)) std::abort();
    if (ne.popcount() != 7) std::abort();   // 8 bits - 1 set

    // raw() escape.
    if (B{F::A, F::C}.raw() != 0x05) std::abort();
}

}  // namespace detail::bits_self_test

}  // namespace crucible::safety
