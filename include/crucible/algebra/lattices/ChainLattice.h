#pragma once

// Chain-order lattice ops over a scoped enum.  Only the ops are shared.
// A derived lattice supplies its own bottom(), top(), name() and any
// per-tier nested templates.  Folding the whole lattice into one
// `template <typename EnumT> ChainLattice` would give every lattice over
// the same enum a single type identity, and each lattice must stay
// distinct.
//
// The ops are constexpr and not consteval, so a consumer's runtime
// precondition can call them under the enforce contract semantic.

#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// A plain `enum E : int` satisfies std::is_enum_v but converts implicitly
// to its underlying integer, which would let arithmetic on a tier value
// compile.  Scoped enums only.
template <typename EnumT>
    requires std::is_scoped_enum_v<EnumT>
struct ChainLatticeOps {
    using element_type = EnumT;

    [[nodiscard]] static constexpr bool leq(EnumT a, EnumT b) noexcept {
        return std::to_underlying(a) <= std::to_underlying(b);
    }
    [[nodiscard]] static constexpr EnumT join(EnumT a, EnumT b) noexcept { return leq(a, b) ? b : a; }
    [[nodiscard]] static constexpr EnumT meet(EnumT a, EnumT b) noexcept { return leq(a, b) ? a : b; }
};

template <typename ChainLattice>
[[nodiscard]] consteval bool verify_chain_lattice_exhaustive() noexcept {
    using EnumT = typename ChainLattice::element_type;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^EnumT));
    // `template for` unrolls into successive scopes that each declare the
    // induction variable, so -Wshadow fires on the body.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_bounded_lattice_axioms_at<ChainLattice>([:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename ChainLattice>
[[nodiscard]] consteval bool verify_chain_lattice_distributive_exhaustive() noexcept {
    using EnumT = typename ChainLattice::element_type;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^EnumT));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_distributive_lattice<ChainLattice>([:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// The self-test enum is independent of every production caller, so the
// base is verified for an arbitrary scoped-enum ordinal layout.
namespace detail::chain_lattice_self_test {

enum class SmokeTier : std::uint8_t {
    Lo = 0,
    Mid = 1,
    Hi = 2
};

struct SmokeChainLattice : ChainLatticeOps<SmokeTier> {
    [[nodiscard]] static constexpr SmokeTier bottom() noexcept { return SmokeTier::Lo; }
    [[nodiscard]] static constexpr SmokeTier top() noexcept { return SmokeTier::Hi; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "SmokeChainLattice"; }
};

static_assert(verify_chain_lattice_exhaustive<SmokeChainLattice>(),
              "ChainLatticeOps must satisfy bounded-lattice axioms for an "
              "arbitrary scoped enum");
static_assert(verify_chain_lattice_distributive_exhaustive<SmokeChainLattice>(),
              "ChainLatticeOps must satisfy distributive-lattice axioms for "
              "an arbitrary scoped enum");

// The volatile operand keeps the call out of constant evaluation, so the
// runtime bodies reach the sanitizers.
inline void runtime_smoke_test() {
    volatile std::uint8_t raw_hi = 2;
    const SmokeTier lo = SmokeTier::Lo;
    const SmokeTier hi = static_cast<SmokeTier>(raw_hi);

    [[maybe_unused]] bool le_dir = SmokeChainLattice::leq(lo, hi);
    [[maybe_unused]] bool ge_dir = SmokeChainLattice::leq(hi, lo);
    [[maybe_unused]] SmokeTier mx = SmokeChainLattice::join(lo, hi);
    [[maybe_unused]] SmokeTier mn = SmokeChainLattice::meet(lo, hi);
    [[maybe_unused]] SmokeTier bot = SmokeChainLattice::bottom();
    [[maybe_unused]] SmokeTier top = SmokeChainLattice::top();
}

}  // namespace detail::chain_lattice_self_test

}  // namespace crucible::algebra::lattices
