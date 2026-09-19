#pragma once

// Four-tier chain over how a function's stack footprint is bounded, and
// by what.  Declaring a tier asserts that the actual stack use is
// contained in the set that tier allows.
//
// The bound weakens upward, which inverts the usual reading of the
// operators.  A join returns the weaker of two bounds, which is right
// for propagation: a caller inherits the loosest bound among its
// callees.  It is wrong for admission.  A gate that wants the tightest
// bound every participant claims must call meet, because join hands back
// the loosest party's budget.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class StackUse : std::uint8_t {
    ConstantFrame = 0,  // bottom — compile-time-constant frame; overflow impossible
    BoundedByParam = 1,  // stack ≤ static f(params) (bounded recursion depth / bounded VLA)
    BoundedDynamic = 2,  // stack bounded only by a runtime guard / ceiling
    Unbounded = 3,  // top — no static or dynamic bound
};

[[nodiscard]] consteval std::string_view stack_use_name(StackUse t) noexcept {
    switch (t) {
        case StackUse::ConstantFrame:
            return "ConstantFrame";
        case StackUse::BoundedByParam:
            return "BoundedByParam";
        case StackUse::BoundedDynamic:
            return "BoundedDynamic";
        case StackUse::Unbounded:
            return "Unbounded";
        default:
            return std::string_view{"<unknown StackUse>"};
    }
}

struct StackUseLattice : ChainLatticeOps<StackUse> {
    [[nodiscard]] static constexpr StackUse bottom() noexcept { return StackUse::ConstantFrame; }
    [[nodiscard]] static constexpr StackUse top() noexcept { return StackUse::Unbounded; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "StackUseLattice"; }

    template <StackUse T>
    struct At {
        struct element_type {
            using stack_use_value_type = StackUse;
            [[nodiscard]] constexpr operator stack_use_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr StackUse tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case StackUse::ConstantFrame:
                    return "StackUseLattice::At<ConstantFrame>";
                case StackUse::BoundedByParam:
                    return "StackUseLattice::At<BoundedByParam>";
                case StackUse::BoundedDynamic:
                    return "StackUseLattice::At<BoundedDynamic>";
                case StackUse::Unbounded:
                    return "StackUseLattice::At<Unbounded>";
                default:
                    return "StackUseLattice::At<?>";
            }
        }
    };
};

namespace detail::stack_use_lattice_self_test {

inline constexpr std::size_t stack_use_count = std::meta::enumerators_of(^^StackUse).size();

static_assert(stack_use_count == 4, "StackUse must hold exactly the four tiers ConstantFrame, "
                                    "BoundedByParam, BoundedDynamic and Unbounded.  A new tier appends at "
                                    "the next free ordinal and needs an arm in stack_use_name and in "
                                    "At<T>'s name().");

static_assert(std::to_underlying(StackUse::ConstantFrame) == 0);
static_assert(std::to_underlying(StackUse::Unbounded) == 3);
static_assert(std::is_same_v<std::underlying_type_t<StackUse>, std::uint8_t>);

[[nodiscard]] consteval bool every_stack_use_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^StackUse));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = stack_use_name([:en:]);
        if (n == std::string_view{"<unknown StackUse>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_stack_use_has_name(), "stack_use_name() has no arm for at least one tier, so that tier "
                                          "reports the '<unknown StackUse>' sentinel.");

static_assert(::crucible::algebra::Lattice<StackUseLattice>);
static_assert(::crucible::algebra::BoundedLattice<StackUseLattice>);
static_assert(!::crucible::algebra::Semiring<StackUseLattice>);

static_assert(verify_chain_lattice_exhaustive<StackUseLattice>(),
              "StackUseLattice's chain-order lattice axioms must hold at every "
              "(StackUse)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<StackUseLattice>(),
              "StackUseLattice's chain order must satisfy distributivity at every "
              "(StackUse)³ triple.");

static_assert(StackUseLattice::bottom() == StackUse::ConstantFrame);
static_assert(StackUseLattice::top() == StackUse::Unbounded);
static_assert(StackUseLattice::name() == std::string_view{"StackUseLattice"});

static_assert(StackUseLattice::leq(StackUse::ConstantFrame, StackUse::Unbounded));
static_assert(!StackUseLattice::leq(StackUse::Unbounded, StackUse::ConstantFrame));

static_assert(StackUseLattice::leq(StackUse::ConstantFrame, StackUse::BoundedByParam));
static_assert(StackUseLattice::leq(StackUse::BoundedByParam, StackUse::BoundedDynamic));
static_assert(StackUseLattice::leq(StackUse::BoundedDynamic, StackUse::Unbounded));

static_assert(!StackUseLattice::leq(StackUse::BoundedByParam, StackUse::ConstantFrame));
static_assert(!StackUseLattice::leq(StackUse::Unbounded, StackUse::BoundedDynamic));

static_assert(StackUseLattice::join(StackUse::BoundedByParam, StackUse::BoundedDynamic) == StackUse::BoundedDynamic);
static_assert(StackUseLattice::join(StackUse::ConstantFrame, StackUse::BoundedByParam) == StackUse::BoundedByParam);
static_assert(StackUseLattice::meet(StackUse::Unbounded, StackUse::BoundedByParam) == StackUse::BoundedByParam);

// The two assertions below pin the polarity.  Inverting the chain reds
// them in lockstep and stops a gate from being written against the wrong
// operator.
static_assert(StackUseLattice::join(StackUse::ConstantFrame, StackUse::Unbounded) == StackUse::Unbounded,
              "StackUseLattice's join returns the weaker stack bound of the two "
              "operands.  A consumer that treats composition as strictest-wins "
              "would silently inherit Unbounded.  Use meet for the tightest "
              "bound.");
static_assert(StackUseLattice::meet(StackUse::ConstantFrame, StackUse::Unbounded) == StackUse::ConstantFrame,
              "StackUseLattice's meet returns the tightest stack bound of the two "
              "operands.  An admission gate that wants only the bound every "
              "participant claims must call meet, not join.");

static_assert(std::is_empty_v<StackUseLattice::At<StackUse::ConstantFrame>::element_type>);
static_assert(std::is_empty_v<StackUseLattice::At<StackUse::BoundedByParam>::element_type>);
static_assert(std::is_empty_v<StackUseLattice::At<StackUse::BoundedDynamic>::element_type>);
static_assert(std::is_empty_v<StackUseLattice::At<StackUse::Unbounded>::element_type>);
static_assert(StackUseLattice::At<StackUse::BoundedByParam>::tier == StackUse::BoundedByParam);

inline void stack_use_lattice_runtime_smoke_test() {
    StackUse a = StackUse::ConstantFrame;
    StackUse b = StackUse::Unbounded;
    [[maybe_unused]] bool rl = StackUseLattice::leq(a, b);
    [[maybe_unused]] StackUse rj = StackUseLattice::join(a, b);
    [[maybe_unused]] StackUse rm = StackUseLattice::meet(a, b);

    StackUse c = StackUse::BoundedByParam;
    StackUse d = StackUse::BoundedDynamic;
    [[maybe_unused]] StackUse rj2 = StackUseLattice::join(c, d);
    [[maybe_unused]] StackUse rm2 = StackUseLattice::meet(c, d);

    StackUseLattice::At<StackUse::BoundedDynamic>::element_type bd_pin{};
    [[maybe_unused]] StackUse bd_recovered = bd_pin;
}

}  // namespace detail::stack_use_lattice_self_test

}  // namespace crucible::algebra::lattices
