#pragma once

// Total-order chain over the hardware-instruction classes a function may
// issue.  Each tier admits every class below it plus its own, so bottom
// is the narrowest claim and top the widest.  join widens the admitted
// set.  meet tightens it.
//
// A tier states intent, not authority.  Declaring the privileged tier
// does not grant ring-0 access.  That needs a separate ownership token.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class HwInstruction : std::uint8_t {
    NoneAllowed = 0,  // no hardware-specific instruction at all
    Scalar = 1,  // scalar arithmetic and control flow, no SIMD
    Vectorizable = 2,  // SIMD intrinsics
    NonDeterministicTsc = 3,  // rdtsc, rdtscp
    PrivilegedMsr = 4,  // rdmsr, wrmsr, IN, OUT
};

[[nodiscard]] consteval std::string_view hw_instruction_name(HwInstruction t) noexcept {
    switch (t) {
        case HwInstruction::NoneAllowed:
            return "NoneAllowed";
        case HwInstruction::Scalar:
            return "Scalar";
        case HwInstruction::Vectorizable:
            return "Vectorizable";
        case HwInstruction::NonDeterministicTsc:
            return "NonDeterministicTsc";
        case HwInstruction::PrivilegedMsr:
            return "PrivilegedMsr";
        default:
            return std::string_view{"<unknown HwInstruction>"};
    }
}

struct HwInstructionLattice : ChainLatticeOps<HwInstruction> {
    [[nodiscard]] static constexpr HwInstruction bottom() noexcept { return HwInstruction::NoneAllowed; }
    [[nodiscard]] static constexpr HwInstruction top() noexcept { return HwInstruction::PrivilegedMsr; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "HwInstructionLattice"; }

    template <HwInstruction T>
    struct At {
        struct element_type {
            using hw_instruction_value_type = HwInstruction;
            [[nodiscard]] constexpr operator hw_instruction_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr HwInstruction tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case HwInstruction::NoneAllowed:
                    return "HwInstructionLattice::At<NoneAllowed>";
                case HwInstruction::Scalar:
                    return "HwInstructionLattice::At<Scalar>";
                case HwInstruction::Vectorizable:
                    return "HwInstructionLattice::At<Vectorizable>";
                case HwInstruction::NonDeterministicTsc:
                    return "HwInstructionLattice::At<NonDeterministicTsc>";
                case HwInstruction::PrivilegedMsr:
                    return "HwInstructionLattice::At<PrivilegedMsr>";
                default:
                    return "HwInstructionLattice::At<?>";
            }
        }
    };
};

namespace detail::hw_instruction_lattice_self_test {

inline constexpr std::size_t hw_instruction_count = std::meta::enumerators_of(^^HwInstruction).size();

static_assert(hw_instruction_count == 5, "HwInstruction diverged from {NoneAllowed, Scalar, Vectorizable, "
                                         "NonDeterministicTsc, PrivilegedMsr}.  A new capability tier appends "
                                         "at the next free ordinal and needs the matching "
                                         "hw_instruction_name() arm and At<T>::name() arm.  Reusing an "
                                         "existing ordinal silently changes every stored row hash.");

static_assert(std::to_underlying(HwInstruction::NoneAllowed) == 0);

static_assert(std::to_underlying(HwInstruction::PrivilegedMsr) == 4);

static_assert(std::is_same_v<std::underlying_type_t<HwInstruction>, std::uint8_t>);

[[nodiscard]] consteval bool every_hw_instruction_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^HwInstruction));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto candidate = hw_instruction_name([:en:]);
        if (candidate == std::string_view{"<unknown HwInstruction>"}) return false;
        if (candidate.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_hw_instruction_has_name(), "hw_instruction_name() switch missing an arm for at least one "
                                               "HwInstruction enumerator.");

static_assert(::crucible::algebra::Lattice<HwInstructionLattice>);
static_assert(::crucible::algebra::BoundedLattice<HwInstructionLattice>);
static_assert(!::crucible::algebra::Semiring<HwInstructionLattice>);

static_assert(verify_chain_lattice_exhaustive<HwInstructionLattice>(),
              "HwInstructionLattice chain-order lattice axioms failed at some "
              "triple — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<HwInstructionLattice>(),
              "HwInstructionLattice chain failed distributivity — leq/join/meet "
              "defect.");

static_assert(HwInstructionLattice::bottom() == HwInstruction::NoneAllowed);
static_assert(HwInstructionLattice::top() == HwInstruction::PrivilegedMsr);

static_assert(HwInstructionLattice::name() == std::string_view{"HwInstructionLattice"});

static_assert(HwInstructionLattice::leq(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr));
static_assert(!HwInstructionLattice::leq(HwInstruction::PrivilegedMsr, HwInstruction::NoneAllowed));

static_assert(HwInstructionLattice::leq(HwInstruction::NoneAllowed, HwInstruction::Scalar));
static_assert(HwInstructionLattice::leq(HwInstruction::Scalar, HwInstruction::Vectorizable));
static_assert(HwInstructionLattice::leq(HwInstruction::Vectorizable, HwInstruction::NonDeterministicTsc));
static_assert(HwInstructionLattice::leq(HwInstruction::NonDeterministicTsc, HwInstruction::PrivilegedMsr));
static_assert(HwInstructionLattice::leq(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr));
static_assert(HwInstructionLattice::leq(HwInstruction::Scalar, HwInstruction::NonDeterministicTsc));

static_assert(!HwInstructionLattice::leq(HwInstruction::Scalar, HwInstruction::NoneAllowed));
static_assert(!HwInstructionLattice::leq(HwInstruction::PrivilegedMsr, HwInstruction::NonDeterministicTsc));
static_assert(!HwInstructionLattice::leq(HwInstruction::NonDeterministicTsc, HwInstruction::Vectorizable));

static_assert(HwInstructionLattice::join(HwInstruction::Vectorizable, HwInstruction::NonDeterministicTsc)
              == HwInstruction::NonDeterministicTsc);
static_assert(HwInstructionLattice::join(HwInstruction::NoneAllowed, HwInstruction::Scalar) == HwInstruction::Scalar);

static_assert(HwInstructionLattice::join(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr)
                  == HwInstruction::PrivilegedMsr,
              "join(NoneAllowed, PrivilegedMsr) returns PrivilegedMsr, the "
              "widest capability.  A consumer that reads composition as "
              "capability minimization would silently inherit MSR access.  "
              "Minimizing capability calls meet, not join.");

static_assert(HwInstructionLattice::meet(HwInstruction::PrivilegedMsr, HwInstruction::Scalar) == HwInstruction::Scalar);

static_assert(HwInstructionLattice::meet(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr)
                  == HwInstruction::NoneAllowed,
              "meet(NoneAllowed, PrivilegedMsr) returns NoneAllowed, the "
              "bottom.  An admission gate that grants only what every "
              "participant admits calls meet.  Calling join grants the widest "
              "participant's capabilities.");

static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::NoneAllowed>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::Scalar>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::Vectorizable>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::NonDeterministicTsc>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::PrivilegedMsr>::element_type>);

static_assert(HwInstructionLattice::At<HwInstruction::Vectorizable>::tier == HwInstruction::Vectorizable);

[[nodiscard]] consteval bool every_at_hw_instruction_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^HwInstruction));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (HwInstructionLattice::At<([:en:])>::name() == std::string_view{"HwInstructionLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_hw_instruction_has_name(), "HwInstructionLattice::At<I>::name() switch missing an arm.");

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using NoneAllowedGraded = Graded<ModalityKind::Absolute, HwInstructionLattice::At<HwInstruction::NoneAllowed>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, double);

template <typename T_>
using PrivilegedMsrGraded = Graded<ModalityKind::Absolute, HwInstructionLattice::At<HwInstruction::PrivilegedMsr>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PrivilegedMsrGraded, EightByteValue);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void hw_instruction_lattice_runtime_smoke_test() {
    HwInstruction a = HwInstruction::NoneAllowed;
    HwInstruction b = HwInstruction::PrivilegedMsr;
    [[maybe_unused]] bool rl = HwInstructionLattice::leq(a, b);
    [[maybe_unused]] HwInstruction rj = HwInstructionLattice::join(a, b);
    [[maybe_unused]] HwInstruction rm = HwInstructionLattice::meet(a, b);
    [[maybe_unused]] HwInstruction bot = HwInstructionLattice::bottom();
    [[maybe_unused]] HwInstruction topv = HwInstructionLattice::top();

    HwInstruction prod = HwInstruction::Vectorizable;
    HwInstruction bench = HwInstruction::NonDeterministicTsc;
    [[maybe_unused]] HwInstruction rj2 = HwInstructionLattice::join(prod, bench);
    [[maybe_unused]] HwInstruction rm2 = HwInstructionLattice::meet(prod, bench);
    [[maybe_unused]] bool prod_admits_bench = HwInstructionLattice::leq(prod, bench);

    HwInstructionLattice::At<HwInstruction::Vectorizable>::element_type vec_pin{};
    [[maybe_unused]] HwInstruction vec_recovered = vec_pin;

    OneByteValue payload{7};
    NoneAllowedGraded<OneByteValue> initial{payload, HwInstructionLattice::At<HwInstruction::NoneAllowed>::bottom()};
    auto widened = initial.weaken(HwInstructionLattice::At<HwInstruction::NoneAllowed>::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto grade = widened.grade();
    [[maybe_unused]] auto peeked = composed.peek().c;
}

}  // namespace detail::hw_instruction_lattice_self_test

}  // namespace crucible::algebra::lattices
