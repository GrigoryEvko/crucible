#pragma once

// Two chains that share a bottom and a top, not one chain.  Each architecture's
// extensions subsume the ones below them, but nothing relates one architecture
// to the other: code built for one does not execute on the other at all.
//
// A richer instruction set sits higher, so a leq that holds reads as code built
// for the lower level running on a processor that offers the higher one.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// The high nibble names the chain and the low nibble the rank within it, so
// comparing the underlying integers of two values from the same chain gives
// their order directly.  Across chains the integers mean nothing.
enum class SimdIsa : std::uint8_t {
    Scalar = 0x00,  // no SIMD at all, so it runs on any processor
    Sse2 = 0x10,
    Sse3 = 0x11,
    Ssse3 = 0x12,
    Sse41 = 0x13,
    Sse42 = 0x14,
    Avx2 = 0x15,  // AVX2 together with BMI2 and FMA
    Avx512F = 0x16,
    Avx512Bw = 0x17,
    Neon = 0x20,
    NeonFp16 = 0x21,
    NeonDotProduct = 0x22,
    Sve = 0x23,
    Sve2 = 0x24,
    Portable = 0xFF,  // one kernel that runs under any instruction set
};

inline constexpr std::size_t simd_isa_count = std::meta::enumerators_of(^^SimdIsa).size();

[[nodiscard]] consteval std::string_view simd_isa_name(SimdIsa x) noexcept {
    switch (x) {
        case SimdIsa::Scalar:
            return "Scalar";
        case SimdIsa::Sse2:
            return "Sse2";
        case SimdIsa::Sse3:
            return "Sse3";
        case SimdIsa::Ssse3:
            return "Ssse3";
        case SimdIsa::Sse41:
            return "Sse41";
        case SimdIsa::Sse42:
            return "Sse42";
        case SimdIsa::Avx2:
            return "Avx2";
        case SimdIsa::Avx512F:
            return "Avx512F";
        case SimdIsa::Avx512Bw:
            return "Avx512Bw";
        case SimdIsa::Neon:
            return "Neon";
        case SimdIsa::NeonFp16:
            return "NeonFp16";
        case SimdIsa::NeonDotProduct:
            return "NeonDotProduct";
        case SimdIsa::Sve:
            return "Sve";
        case SimdIsa::Sve2:
            return "Sve2";
        case SimdIsa::Portable:
            return "Portable";
        default:
            return std::string_view{"<unknown SimdIsa>"};
    }
}

[[nodiscard]] constexpr bool simd_isa_is_x86(SimdIsa x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(SimdIsa::Sse2) && u <= std::to_underlying(SimdIsa::Avx512Bw);
}
[[nodiscard]] constexpr bool simd_isa_is_arm(SimdIsa x) noexcept {
    const auto u = std::to_underlying(x);
    return u >= std::to_underlying(SimdIsa::Neon) && u <= std::to_underlying(SimdIsa::Sve2);
}
// Scalar and Portable belong to neither chain, so this answers false for both
// of them against anything.  Every caller therefore has to settle those two
// cases before asking.
[[nodiscard]] constexpr bool simd_isa_same_trunk(SimdIsa a, SimdIsa b) noexcept {
    return (simd_isa_is_x86(a) && simd_isa_is_x86(b)) || (simd_isa_is_arm(a) && simd_isa_is_arm(b));
}

struct SimdIsaLattice {
    using element_type = SimdIsa;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return SimdIsa::Scalar; }
    [[nodiscard]] static constexpr element_type top() noexcept { return SimdIsa::Portable; }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == SimdIsa::Scalar) return true;
        if (b == SimdIsa::Portable) return true;
        if (simd_isa_same_trunk(a, b)) {
            return std::to_underlying(a) <= std::to_underlying(b);
        }
        return false;
    }

    // Two instruction sets from different chains have nothing between them, so
    // their least upper bound can only be the top and their greatest lower
    // bound only the bottom.
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == SimdIsa::Scalar) return b;
        if (b == SimdIsa::Scalar) return a;
        if (a == SimdIsa::Portable || b == SimdIsa::Portable) {
            return SimdIsa::Portable;
        }
        if (simd_isa_same_trunk(a, b)) {
            return std::to_underlying(a) >= std::to_underlying(b) ? a : b;
        }
        return SimdIsa::Portable;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == SimdIsa::Portable) return b;
        if (b == SimdIsa::Portable) return a;
        if (a == SimdIsa::Scalar || b == SimdIsa::Scalar) {
            return SimdIsa::Scalar;
        }
        if (simd_isa_same_trunk(a, b)) {
            return std::to_underlying(a) <= std::to_underlying(b) ? a : b;
        }
        return SimdIsa::Scalar;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "SimdIsaLattice"; }

    template <SimdIsa I>
    struct At {
        struct element_type {
            using simd_isa_value_type = SimdIsa;
            [[nodiscard]] constexpr operator simd_isa_value_type() const noexcept { return I; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr SimdIsa isa = I;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (I) {
                case SimdIsa::Scalar:
                    return "SimdIsaLattice::At<Scalar>";
                case SimdIsa::Sse2:
                    return "SimdIsaLattice::At<Sse2>";
                case SimdIsa::Sse3:
                    return "SimdIsaLattice::At<Sse3>";
                case SimdIsa::Ssse3:
                    return "SimdIsaLattice::At<Ssse3>";
                case SimdIsa::Sse41:
                    return "SimdIsaLattice::At<Sse41>";
                case SimdIsa::Sse42:
                    return "SimdIsaLattice::At<Sse42>";
                case SimdIsa::Avx2:
                    return "SimdIsaLattice::At<Avx2>";
                case SimdIsa::Avx512F:
                    return "SimdIsaLattice::At<Avx512F>";
                case SimdIsa::Avx512Bw:
                    return "SimdIsaLattice::At<Avx512Bw>";
                case SimdIsa::Neon:
                    return "SimdIsaLattice::At<Neon>";
                case SimdIsa::NeonFp16:
                    return "SimdIsaLattice::At<NeonFp16>";
                case SimdIsa::NeonDotProduct:
                    return "SimdIsaLattice::At<NeonDotProduct>";
                case SimdIsa::Sve:
                    return "SimdIsaLattice::At<Sve>";
                case SimdIsa::Sve2:
                    return "SimdIsaLattice::At<Sve2>";
                case SimdIsa::Portable:
                    return "SimdIsaLattice::At<Portable>";
                default:
                    return "SimdIsaLattice::At<?>";
            }
        }
    };
};

namespace simd_isa {
using ScalarIsa = SimdIsaLattice::At<SimdIsa::Scalar>;
using Sse2Isa = SimdIsaLattice::At<SimdIsa::Sse2>;
using Sse3Isa = SimdIsaLattice::At<SimdIsa::Sse3>;
using Ssse3Isa = SimdIsaLattice::At<SimdIsa::Ssse3>;
using Sse41Isa = SimdIsaLattice::At<SimdIsa::Sse41>;
using Sse42Isa = SimdIsaLattice::At<SimdIsa::Sse42>;
using Avx2Isa = SimdIsaLattice::At<SimdIsa::Avx2>;
using Avx512fIsa = SimdIsaLattice::At<SimdIsa::Avx512F>;
using Avx512bwIsa = SimdIsaLattice::At<SimdIsa::Avx512Bw>;
using NeonIsa = SimdIsaLattice::At<SimdIsa::Neon>;
using NeonFp16Isa = SimdIsaLattice::At<SimdIsa::NeonFp16>;
using NeonDotIsa = SimdIsaLattice::At<SimdIsa::NeonDotProduct>;
using SveIsa = SimdIsaLattice::At<SimdIsa::Sve>;
using Sve2Isa = SimdIsaLattice::At<SimdIsa::Sve2>;
using PortableIsa = SimdIsaLattice::At<SimdIsa::Portable>;
}  // namespace simd_isa

namespace detail::simd_isa_lattice_self_test {

static_assert(simd_isa_count == 15, "The SimdIsa catalog changed size.  Confirm the intent, then update "
                                    "simd_isa_is_x86 and simd_isa_is_arm, which bound the two chains by "
                                    "underlying value, and kAll in the verifier below.");

[[nodiscard]] consteval bool every_simd_isa_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SimdIsa));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (simd_isa_name([:en:]) == std::string_view{"<unknown SimdIsa>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_simd_isa_has_name(), "simd_isa_name() switch missing an arm for at least one ISA.");

static_assert(Lattice<SimdIsaLattice>);
static_assert(BoundedLattice<SimdIsaLattice>);
static_assert(Lattice<simd_isa::ScalarIsa>);
static_assert(Lattice<simd_isa::Avx2Isa>);
static_assert(Lattice<simd_isa::SveIsa>);
static_assert(Lattice<simd_isa::PortableIsa>);
static_assert(BoundedLattice<simd_isa::PortableIsa>);

static_assert(!UnboundedLattice<SimdIsaLattice>);
static_assert(!Semiring<SimdIsaLattice>);

static_assert(std::is_empty_v<simd_isa::ScalarIsa::element_type>);
static_assert(std::is_empty_v<simd_isa::Avx2Isa::element_type>);
static_assert(std::is_empty_v<simd_isa::SveIsa::element_type>);
static_assert(std::is_empty_v<simd_isa::PortableIsa::element_type>);

static_assert(SimdIsaLattice::bottom() == SimdIsa::Scalar);
static_assert(SimdIsaLattice::top() == SimdIsa::Portable);

static_assert(simd_isa_is_x86(SimdIsa::Sse2));
static_assert(simd_isa_is_x86(SimdIsa::Avx512Bw));
static_assert(!simd_isa_is_x86(SimdIsa::Neon));
static_assert(!simd_isa_is_x86(SimdIsa::Scalar));
static_assert(!simd_isa_is_x86(SimdIsa::Portable));
static_assert(simd_isa_is_arm(SimdIsa::Neon));
static_assert(simd_isa_is_arm(SimdIsa::Sve2));
static_assert(!simd_isa_is_arm(SimdIsa::Avx2));
static_assert(!simd_isa_is_arm(SimdIsa::Scalar));
static_assert(!simd_isa_is_arm(SimdIsa::Portable));
static_assert(simd_isa_same_trunk(SimdIsa::Sse2, SimdIsa::Avx2));
static_assert(simd_isa_same_trunk(SimdIsa::Neon, SimdIsa::Sve2));
static_assert(!simd_isa_same_trunk(SimdIsa::Avx2, SimdIsa::Sve));
static_assert(!simd_isa_same_trunk(SimdIsa::Scalar, SimdIsa::Neon));
static_assert(!simd_isa_same_trunk(SimdIsa::Portable, SimdIsa::Avx2));

static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Scalar));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx2, SimdIsa::Avx2));
static_assert(SimdIsaLattice::leq(SimdIsa::Sve, SimdIsa::Sve));
static_assert(SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Portable));

static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Sse2));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Avx512Bw));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Neon));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Sve2));
static_assert(SimdIsaLattice::leq(SimdIsa::Scalar, SimdIsa::Portable));

static_assert(SimdIsaLattice::leq(SimdIsa::Sse2, SimdIsa::Portable));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx512Bw, SimdIsa::Portable));
static_assert(SimdIsaLattice::leq(SimdIsa::Neon, SimdIsa::Portable));
static_assert(SimdIsaLattice::leq(SimdIsa::Sve2, SimdIsa::Portable));

static_assert(SimdIsaLattice::leq(SimdIsa::Sse2, SimdIsa::Sse3));
static_assert(SimdIsaLattice::leq(SimdIsa::Sse3, SimdIsa::Ssse3));
static_assert(SimdIsaLattice::leq(SimdIsa::Ssse3, SimdIsa::Sse41));
static_assert(SimdIsaLattice::leq(SimdIsa::Sse41, SimdIsa::Sse42));
static_assert(SimdIsaLattice::leq(SimdIsa::Sse42, SimdIsa::Avx2));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx2, SimdIsa::Avx512F));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx512F, SimdIsa::Avx512Bw));
static_assert(SimdIsaLattice::leq(SimdIsa::Sse2, SimdIsa::Avx512Bw));
static_assert(SimdIsaLattice::leq(SimdIsa::Avx2, SimdIsa::Avx512F));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2, SimdIsa::Sse2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx512Bw, SimdIsa::Avx2));

static_assert(SimdIsaLattice::leq(SimdIsa::Neon, SimdIsa::NeonFp16));
static_assert(SimdIsaLattice::leq(SimdIsa::NeonFp16, SimdIsa::NeonDotProduct));
static_assert(SimdIsaLattice::leq(SimdIsa::NeonDotProduct, SimdIsa::Sve));
static_assert(SimdIsaLattice::leq(SimdIsa::Sve, SimdIsa::Sve2));
static_assert(SimdIsaLattice::leq(SimdIsa::Neon, SimdIsa::Sve2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve2, SimdIsa::Neon));

// Every pair drawn from the two different chains must stay incomparable in both
// directions.  Ordering one against the other would admit a binary on a
// processor that cannot decode a single one of its instructions.
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2, SimdIsa::Sve));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve, SimdIsa::Avx2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sse2, SimdIsa::Neon));
static_assert(!SimdIsaLattice::leq(SimdIsa::Neon, SimdIsa::Sse2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx512Bw, SimdIsa::Sve2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve2, SimdIsa::Avx512Bw));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2, SimdIsa::Neon));
static_assert(!SimdIsaLattice::leq(SimdIsa::Neon, SimdIsa::Avx512F));

static_assert(!SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Avx2));
static_assert(!SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Sve));
static_assert(!SimdIsaLattice::leq(SimdIsa::Portable, SimdIsa::Scalar));
static_assert(!SimdIsaLattice::leq(SimdIsa::Avx2, SimdIsa::Scalar));
static_assert(!SimdIsaLattice::leq(SimdIsa::Sve, SimdIsa::Scalar));

static_assert(SimdIsaLattice::join(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Avx2);
static_assert(SimdIsaLattice::meet(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Sse2);
static_assert(SimdIsaLattice::join(SimdIsa::Neon, SimdIsa::Sve) == SimdIsa::Sve);
static_assert(SimdIsaLattice::meet(SimdIsa::Neon, SimdIsa::Sve) == SimdIsa::Neon);
static_assert(SimdIsaLattice::join(SimdIsa::Avx2, SimdIsa::Sve) == SimdIsa::Portable);
static_assert(SimdIsaLattice::meet(SimdIsa::Avx2, SimdIsa::Sve) == SimdIsa::Scalar);
static_assert(SimdIsaLattice::join(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Portable);
static_assert(SimdIsaLattice::meet(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Scalar);
static_assert(SimdIsaLattice::join(SimdIsa::Scalar, SimdIsa::Avx2) == SimdIsa::Avx2);
static_assert(SimdIsaLattice::join(SimdIsa::Scalar, SimdIsa::Portable) == SimdIsa::Portable);
static_assert(SimdIsaLattice::meet(SimdIsa::Portable, SimdIsa::Sve) == SimdIsa::Sve);
static_assert(SimdIsaLattice::meet(SimdIsa::Portable, SimdIsa::Scalar) == SimdIsa::Scalar);
static_assert(SimdIsaLattice::meet(SimdIsa::Scalar, SimdIsa::Avx2) == SimdIsa::Scalar);
static_assert(SimdIsaLattice::join(SimdIsa::Portable, SimdIsa::Neon) == SimdIsa::Portable);
static_assert(SimdIsaLattice::join(SimdIsa::Avx2, SimdIsa::Avx2) == SimdIsa::Avx2);
static_assert(SimdIsaLattice::meet(SimdIsa::Sve, SimdIsa::Sve) == SimdIsa::Sve);

// The shared chain verifier does not apply to a partial order, so the axioms
// are walked by hand over every triple of the fifteen elements.
inline constexpr SimdIsa kAll[] = {
    SimdIsa::Scalar,   SimdIsa::Sse2,           SimdIsa::Sse3,    SimdIsa::Ssse3,    SimdIsa::Sse41,
    SimdIsa::Sse42,    SimdIsa::Avx2,           SimdIsa::Avx512F, SimdIsa::Avx512Bw, SimdIsa::Neon,
    SimdIsa::NeonFp16, SimdIsa::NeonDotProduct, SimdIsa::Sve,     SimdIsa::Sve2,     SimdIsa::Portable,
};

[[nodiscard]] consteval bool verify_partial_order_exhaustive() noexcept {
    using L = SimdIsaLattice;
    for (auto a : kAll) {
        if (!L::leq(a, a)) return false;
        for (auto b : kAll) {
            if (L::leq(a, b) && L::leq(b, a) && a != b) return false;
            if (L::join(a, b) != L::join(b, a)) return false;
            if (L::meet(a, b) != L::meet(b, a)) return false;
            if (L::join(a, a) != a) return false;
            if (L::meet(a, a) != a) return false;
            if (L::join(a, L::meet(a, b)) != a) return false;
            if (L::meet(a, L::join(a, b)) != a) return false;
            if (!L::leq(L::bottom(), a)) return false;
            if (!L::leq(a, L::top())) return false;
            for (auto c : kAll) {
                if (L::leq(a, b) && L::leq(b, c) && !L::leq(a, c)) return false;
                if (L::join(L::join(a, b), c) != L::join(a, L::join(b, c))) return false;
                if (L::meet(L::meet(a, b), c) != L::meet(a, L::meet(b, c))) return false;
                bool by_meet = (L::meet(a, b) == a);
                bool by_join = (L::join(a, b) == b);
                bool by_leq = L::leq(a, b);
                if (by_leq != by_meet) return false;
                if (by_leq != by_join) return false;
            }
        }
    }
    return true;
}
static_assert(verify_partial_order_exhaustive(),
              "The partial-order axioms must hold at every triple of the fifteen "
              "instruction sets.  A failure means leq, join or meet is wrong for some "
              "pair, or the routing between Scalar, Portable, same-chain rank and "
              "cross-chain is wrong.");

// A bounded order with a single top and bottom and with two unordered internal
// chains cannot be distributive, so the failure below is structural rather than
// a defect.  It is pinned so that anyone merging the two chains has to confront
// the assertion first.
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    using L = SimdIsaLattice;
    auto lhs = L::meet(L::join(SimdIsa::Avx2, SimdIsa::Neon), SimdIsa::Sve);
    auto rhs = L::join(L::meet(SimdIsa::Avx2, SimdIsa::Sve), L::meet(SimdIsa::Neon, SimdIsa::Sve));
    return lhs == SimdIsa::Sve && rhs == SimdIsa::Neon && lhs != rhs;
}
static_assert(non_distributive_witness(), "SimdIsaLattice must stay non-distributive.  A failure means either the "
                                          "two chains were collapsed into one, which destroys their "
                                          "incomparability, or an intermediate element was added that closed the "
                                          "distributivity gap.  Audit before resolving.");

static_assert(SimdIsaLattice::name() == "SimdIsaLattice");
static_assert(simd_isa::ScalarIsa::name() == "SimdIsaLattice::At<Scalar>");
static_assert(simd_isa::Avx2Isa::name() == "SimdIsaLattice::At<Avx2>");
static_assert(simd_isa::Avx512bwIsa::name() == "SimdIsaLattice::At<Avx512Bw>");
static_assert(simd_isa::NeonIsa::name() == "SimdIsaLattice::At<Neon>");
static_assert(simd_isa::Sve2Isa::name() == "SimdIsaLattice::At<Sve2>");
static_assert(simd_isa::PortableIsa::name() == "SimdIsaLattice::At<Portable>");

[[nodiscard]] consteval bool every_at_simd_isa_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SimdIsa));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (SimdIsaLattice::At<([:en:])>::name() == std::string_view{"SimdIsaLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_simd_isa_has_name(), "SimdIsaLattice::At<I>::name() switch missing an arm.");

static_assert(simd_isa::Avx2Isa::isa == SimdIsa::Avx2);
static_assert(simd_isa::SveIsa::isa == SimdIsa::Sve);
static_assert(simd_isa::ScalarIsa::isa == SimdIsa::Scalar);
static_assert(simd_isa::PortableIsa::isa == SimdIsa::Portable);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using PortableIsaGraded = Graded<ModalityKind::Absolute, simd_isa::PortableIsa, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableIsaGraded, double);

template <typename T_>
using Avx2Graded = Graded<ModalityKind::Absolute, simd_isa::Avx2Isa, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx2Graded, EightByteValue);

template <typename T_>
using SveGraded = Graded<ModalityKind::Absolute, simd_isa::SveIsa, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SveGraded, EightByteValue);

inline void runtime_smoke_test() {
    SimdIsa a = SimdIsa::Avx2;
    SimdIsa b = SimdIsa::Sve;
    [[maybe_unused]] bool l1 = SimdIsaLattice::leq(a, b);
    [[maybe_unused]] SimdIsa j1 = SimdIsaLattice::join(a, b);
    [[maybe_unused]] SimdIsa m1 = SimdIsaLattice::meet(a, b);
    [[maybe_unused]] SimdIsa bot = SimdIsaLattice::bottom();
    [[maybe_unused]] SimdIsa topv = SimdIsaLattice::top();

    SimdIsa sse2 = SimdIsa::Sse2;
    [[maybe_unused]] bool within = SimdIsaLattice::leq(sse2, a);
    [[maybe_unused]] bool xtrunk = simd_isa_same_trunk(a, b);

    OneByteValue v{42};
    PortableIsaGraded<OneByteValue> initial{v, simd_isa::PortableIsa::bottom()};
    auto widened = initial.weaken(simd_isa::PortableIsa::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto g = widened.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    simd_isa::PortableIsa::element_type e{};
    [[maybe_unused]] SimdIsa rec = e;
}

}  // namespace detail::simd_isa_lattice_self_test

}  // namespace crucible::algebra::lattices
