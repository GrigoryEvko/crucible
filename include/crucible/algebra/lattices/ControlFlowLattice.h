#pragma once

// Chain over the non-local control transfers a function may perform.
// bottom is Pure, which always returns normally, and top is MaySignal.
// Each tier permits every escape below it plus its own, and a function
// declaring a tier asserts that its actual escape set fits inside that
// tier's set.
//
// The order ranks how hard each escape is to reason about, not how
// severe it is.  Abort sits just above Pure because it has exactly one
// outcome and searches no handler.  A throw sits above abort because it
// has many.  A longjmp sits above a throw because it skips the
// destructors a throw runs, so it can leak where a throw would not.  A
// signal sits at the top because it arrives at any instruction
// boundary.
//
// join is escape union, which is the propagation reading: a region
// containing one throwing call may throw.  It is not a minimization.  A
// gate that admits only the narrowest escape set takes the meet.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class ControlFlow : std::uint8_t {
    Pure = 0,  // always returns normally
    AbortOnly = 1,  // std::abort, std::terminate, __builtin_trap — no unwinding
    ThrowOnly = 2,  // a C++ exception — unwinds and runs destructors
    MayLongjmp = 3,  // longjmp — jumps and skips destructors
    MaySignal = 4,  // raises or delivers an asynchronous signal
};

[[nodiscard]] consteval std::string_view control_flow_name(ControlFlow t) noexcept {
    switch (t) {
        case ControlFlow::Pure:
            return "Pure";
        case ControlFlow::AbortOnly:
            return "AbortOnly";
        case ControlFlow::ThrowOnly:
            return "ThrowOnly";
        case ControlFlow::MayLongjmp:
            return "MayLongjmp";
        case ControlFlow::MaySignal:
            return "MaySignal";
        default:
            return std::string_view{"<unknown ControlFlow>"};
    }
}

struct ControlFlowLattice : ChainLatticeOps<ControlFlow> {
    [[nodiscard]] static constexpr ControlFlow bottom() noexcept { return ControlFlow::Pure; }
    [[nodiscard]] static constexpr ControlFlow top() noexcept { return ControlFlow::MaySignal; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "ControlFlowLattice"; }

    template <ControlFlow T>
    struct At {
        struct element_type {
            using control_flow_value_type = ControlFlow;
            [[nodiscard]] constexpr operator control_flow_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr ControlFlow tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case ControlFlow::Pure:
                    return "ControlFlowLattice::At<Pure>";
                case ControlFlow::AbortOnly:
                    return "ControlFlowLattice::At<AbortOnly>";
                case ControlFlow::ThrowOnly:
                    return "ControlFlowLattice::At<ThrowOnly>";
                case ControlFlow::MayLongjmp:
                    return "ControlFlowLattice::At<MayLongjmp>";
                case ControlFlow::MaySignal:
                    return "ControlFlowLattice::At<MaySignal>";
                default:
                    return "ControlFlowLattice::At<?>";
            }
        }
    };
};

namespace detail::control_flow_lattice_self_test {

inline constexpr std::size_t control_flow_count = std::meta::enumerators_of(^^ControlFlow).size();

static_assert(control_flow_count == 5, "ControlFlow diverged from {Pure, AbortOnly, ThrowOnly, MayLongjmp, "
                                       "MaySignal}.  A new escape tier appends at the next free ordinal "
                                       "and needs the matching control_flow_name() arm and At<T>::name() "
                                       "arm.  Reusing an existing ordinal silently changes every stored "
                                       "row hash.");

static_assert(std::to_underlying(ControlFlow::Pure) == 0);

static_assert(std::to_underlying(ControlFlow::MaySignal) == 4);

static_assert(std::is_same_v<std::underlying_type_t<ControlFlow>, std::uint8_t>);

[[nodiscard]] consteval bool every_control_flow_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ControlFlow));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = control_flow_name([:en:]);
        if (n == std::string_view{"<unknown ControlFlow>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_control_flow_has_name(), "control_flow_name() switch missing an arm for at least one "
                                             "ControlFlow enumerator.  Add the arm or the new tier leaks the "
                                             "'<unknown ControlFlow>' sentinel.");

static_assert(::crucible::algebra::Lattice<ControlFlowLattice>);
static_assert(::crucible::algebra::BoundedLattice<ControlFlowLattice>);
static_assert(!::crucible::algebra::Semiring<ControlFlowLattice>);

static_assert(verify_chain_lattice_exhaustive<ControlFlowLattice>(),
              "ControlFlowLattice chain-order lattice axioms failed at some triple "
              "— leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<ControlFlowLattice>(),
              "ControlFlowLattice chain failed distributivity check — leq/join/meet "
              "defect.");

static_assert(ControlFlowLattice::bottom() == ControlFlow::Pure);
static_assert(ControlFlowLattice::top() == ControlFlow::MaySignal);

static_assert(ControlFlowLattice::name() == std::string_view{"ControlFlowLattice"});

static_assert(ControlFlowLattice::leq(ControlFlow::Pure, ControlFlow::MaySignal));
static_assert(!ControlFlowLattice::leq(ControlFlow::MaySignal, ControlFlow::Pure));

static_assert(ControlFlowLattice::leq(ControlFlow::Pure, ControlFlow::AbortOnly));
static_assert(ControlFlowLattice::leq(ControlFlow::AbortOnly, ControlFlow::ThrowOnly));
static_assert(ControlFlowLattice::leq(ControlFlow::ThrowOnly, ControlFlow::MayLongjmp));
static_assert(ControlFlowLattice::leq(ControlFlow::MayLongjmp, ControlFlow::MaySignal));

static_assert(!ControlFlowLattice::leq(ControlFlow::AbortOnly, ControlFlow::Pure));
static_assert(!ControlFlowLattice::leq(ControlFlow::MaySignal, ControlFlow::MayLongjmp));

static_assert(ControlFlowLattice::join(ControlFlow::ThrowOnly, ControlFlow::MayLongjmp) == ControlFlow::MayLongjmp);
static_assert(ControlFlowLattice::meet(ControlFlow::MaySignal, ControlFlow::AbortOnly) == ControlFlow::AbortOnly);

static_assert(ControlFlowLattice::join(ControlFlow::Pure, ControlFlow::MaySignal) == ControlFlow::MaySignal,
              "join returns the widest escape set, MaySignal.  A consumer that "
              "reads composition as escape minimization would silently admit an "
              "asynchronous signal.  A gate that wants the Pure floor calls "
              "meet.");
static_assert(ControlFlowLattice::meet(ControlFlow::Pure, ControlFlow::MaySignal) == ControlFlow::Pure,
              "meet returns the narrowest escape set, Pure.  A gate that admits "
              "only what every participant claims calls meet.");

static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::Pure>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::AbortOnly>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::ThrowOnly>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::MayLongjmp>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::MaySignal>::element_type>);

static_assert(ControlFlowLattice::At<ControlFlow::ThrowOnly>::tier == ControlFlow::ThrowOnly);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void control_flow_lattice_runtime_smoke_test() {
    ControlFlow a = ControlFlow::Pure;
    ControlFlow b = ControlFlow::MaySignal;
    [[maybe_unused]] bool rl = ControlFlowLattice::leq(a, b);
    [[maybe_unused]] ControlFlow rj = ControlFlowLattice::join(a, b);
    [[maybe_unused]] ControlFlow rm = ControlFlowLattice::meet(a, b);

    ControlFlow c = ControlFlow::ThrowOnly;
    ControlFlow d = ControlFlow::MayLongjmp;
    [[maybe_unused]] ControlFlow rj2 = ControlFlowLattice::join(c, d);
    [[maybe_unused]] ControlFlow rm2 = ControlFlowLattice::meet(c, d);

    ControlFlowLattice::At<ControlFlow::AbortOnly>::element_type abort_pin{};
    [[maybe_unused]] ControlFlow abort_recovered = abort_pin;
}

}  // namespace detail::control_flow_lattice_self_test

}  // namespace crucible::algebra::lattices
