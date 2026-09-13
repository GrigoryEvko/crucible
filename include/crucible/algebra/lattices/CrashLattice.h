#pragma once

// Four-tier chain over the failure mode a function may exhibit.
//
// The strongest promise sits at the top, so `leq(weak, strong)` reads "a
// weaker-claim function sits below a stronger-claim one".  A NoThrow
// value is admissible everywhere.  An Abort value is admissible only
// where recovery code runs.
//
// Throw and ErrorReturn are not naturally comparable.  One obliges the
// caller to hold an unwind frame, the other to inspect a return value.
// They are linearized as Throw ⊑ ErrorReturn for two reasons.  The tree
// compiles without exceptions, so no gate can demand Throw and the
// resulting "ErrorReturn satisfies Throw" is vacuous.  And the
// ErrorReturn obligation is visible to the compiler while the Throw
// obligation is visible only at run time, so ErrorReturn carries the
// larger static guarantee.  Code that genuinely needs the two
// incomparable must turn this chain into a partial order.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class CrashClass : std::uint8_t {
    Abort = 0,  // bottom: may abort process — admits nowhere
    Throw = 1,  // may throw — banned in -fno-exceptions tree
    ErrorReturn = 2,  // may return error via std::expected
    NoThrow = 3,  // top: no failure mode — admits everywhere
};

inline constexpr std::size_t crash_class_count = std::meta::enumerators_of(^^CrashClass).size();

[[nodiscard]] consteval std::string_view crash_class_name(CrashClass c) noexcept {
    switch (c) {
        case CrashClass::Abort:
            return "Abort";
        case CrashClass::Throw:
            return "Throw";
        case CrashClass::ErrorReturn:
            return "ErrorReturn";
        case CrashClass::NoThrow:
            return "NoThrow";
        default:
            return std::string_view{"<unknown CrashClass>"};
    }
}

struct CrashLattice : ChainLatticeOps<CrashClass> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return CrashClass::Abort; }
    [[nodiscard]] static constexpr element_type top() noexcept { return CrashClass::NoThrow; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "CrashLattice"; }

    template <CrashClass C>
    struct At {
        struct element_type {
            using crash_class_value_type = CrashClass;
            [[nodiscard]] constexpr operator crash_class_value_type() const noexcept { return C; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr CrashClass crash_class = C;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (C) {
                case CrashClass::Abort:
                    return "CrashLattice::At<Abort>";
                case CrashClass::Throw:
                    return "CrashLattice::At<Throw>";
                case CrashClass::ErrorReturn:
                    return "CrashLattice::At<ErrorReturn>";
                case CrashClass::NoThrow:
                    return "CrashLattice::At<NoThrow>";
                default:
                    return "CrashLattice::At<?>";
            }
        }
    };
};

namespace crash_class {
using AbortClass = CrashLattice::At<CrashClass::Abort>;
using ThrowClass = CrashLattice::At<CrashClass::Throw>;
using ErrorReturnClass = CrashLattice::At<CrashClass::ErrorReturn>;
using NoThrowClass = CrashLattice::At<CrashClass::NoThrow>;
}  // namespace crash_class

namespace detail::crash_lattice_self_test {

static_assert(crash_class_count == 4, "CrashClass must hold exactly the four classes Abort, Throw, "
                                      "ErrorReturn and NoThrow.");

[[nodiscard]] consteval bool every_crash_class_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CrashClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (crash_class_name([:en:]) == std::string_view{"<unknown CrashClass>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_crash_class_has_name(), "crash_class_name() has no arm for at least one class, so that "
                                            "class reports the '<unknown CrashClass>' sentinel.");

static_assert(Lattice<CrashLattice>);
static_assert(BoundedLattice<CrashLattice>);
static_assert(Lattice<crash_class::AbortClass>);
static_assert(Lattice<crash_class::ThrowClass>);
static_assert(Lattice<crash_class::ErrorReturnClass>);
static_assert(Lattice<crash_class::NoThrowClass>);
static_assert(BoundedLattice<crash_class::NoThrowClass>);

static_assert(!UnboundedLattice<CrashLattice>);
static_assert(!Semiring<CrashLattice>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<crash_class::AbortClass::element_type>);
static_assert(std::is_empty_v<crash_class::ThrowClass::element_type>);
static_assert(std::is_empty_v<crash_class::ErrorReturnClass::element_type>);
static_assert(std::is_empty_v<crash_class::NoThrowClass::element_type>);

static_assert(verify_chain_lattice_exhaustive<CrashLattice>(), "CrashLattice's chain-order axioms must hold at every "
                                                               "(CrashClass)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<CrashLattice>(),
              "CrashLattice's chain order must satisfy distributivity at "
              "every (CrashClass)³ triple.");

static_assert(CrashLattice::leq(CrashClass::Abort, CrashClass::Throw));
static_assert(CrashLattice::leq(CrashClass::Throw, CrashClass::ErrorReturn));
static_assert(CrashLattice::leq(CrashClass::ErrorReturn, CrashClass::NoThrow));
static_assert(CrashLattice::leq(CrashClass::Abort, CrashClass::NoThrow));
static_assert(!CrashLattice::leq(CrashClass::NoThrow, CrashClass::Abort));
static_assert(!CrashLattice::leq(CrashClass::NoThrow, CrashClass::ErrorReturn));
static_assert(!CrashLattice::leq(CrashClass::ErrorReturn, CrashClass::Throw));
static_assert(!CrashLattice::leq(CrashClass::Throw, CrashClass::Abort));

static_assert(CrashLattice::bottom() == CrashClass::Abort);
static_assert(CrashLattice::top() == CrashClass::NoThrow);

static_assert(CrashLattice::join(CrashClass::Abort, CrashClass::NoThrow) == CrashClass::NoThrow);
static_assert(CrashLattice::join(CrashClass::Throw, CrashClass::Abort) == CrashClass::Throw);
static_assert(CrashLattice::meet(CrashClass::Abort, CrashClass::NoThrow) == CrashClass::Abort);
static_assert(CrashLattice::meet(CrashClass::ErrorReturn, CrashClass::NoThrow) == CrashClass::ErrorReturn);

static_assert(CrashLattice::name() == "CrashLattice");
static_assert(crash_class::AbortClass::name() == "CrashLattice::At<Abort>");
static_assert(crash_class::ThrowClass::name() == "CrashLattice::At<Throw>");
static_assert(crash_class::ErrorReturnClass::name() == "CrashLattice::At<ErrorReturn>");
static_assert(crash_class::NoThrowClass::name() == "CrashLattice::At<NoThrow>");

[[nodiscard]] consteval bool every_at_crash_class_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CrashClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (CrashLattice::At<([:en:])>::name() == std::string_view{"CrashLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_crash_class_has_name(), "CrashLattice::At<C>::name() has no arm for at least one class, "
                                               "so that class reports the 'CrashLattice::At<?>' sentinel.");

static_assert(crash_class::AbortClass::crash_class == CrashClass::Abort);
static_assert(crash_class::ThrowClass::crash_class == CrashClass::Throw);
static_assert(crash_class::ErrorReturnClass::crash_class == CrashClass::ErrorReturn);
static_assert(crash_class::NoThrowClass::crash_class == CrashClass::NoThrow);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

// The top class witnesses the collapse for both class and arithmetic
// values; the other classes need only the class witnesses.
template <typename T_>
using NoThrowGraded = Graded<ModalityKind::Absolute, crash_class::NoThrowClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, double);

template <typename T_>
using ErrorReturnGraded = Graded<ModalityKind::Absolute, crash_class::ErrorReturnClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnGraded, EightByteValue);

template <typename T_>
using ThrowGraded = Graded<ModalityKind::Absolute, crash_class::ThrowClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowGraded, EightByteValue);

template <typename T_>
using AbortGraded = Graded<ModalityKind::Absolute, crash_class::AbortClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortGraded, EightByteValue);

inline void runtime_smoke_test() {
    CrashClass a = CrashClass::Abort;
    CrashClass b = CrashClass::NoThrow;
    [[maybe_unused]] bool l1 = CrashLattice::leq(a, b);
    [[maybe_unused]] CrashClass j1 = CrashLattice::join(a, b);
    [[maybe_unused]] CrashClass m1 = CrashLattice::meet(a, b);
    [[maybe_unused]] CrashClass bot = CrashLattice::bottom();
    [[maybe_unused]] CrashClass topv = CrashLattice::top();

    CrashClass middle = CrashClass::ErrorReturn;
    [[maybe_unused]] CrashClass j2 = CrashLattice::join(middle, a);
    [[maybe_unused]] CrashClass m2 = CrashLattice::meet(middle, b);

    OneByteValue v{42};
    NoThrowGraded<OneByteValue> initial{v, crash_class::NoThrowClass::bottom()};
    auto widened = initial.weaken(crash_class::NoThrowClass::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(crash_class::NoThrowClass::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    crash_class::NoThrowClass::element_type e{};
    [[maybe_unused]] CrashClass rec = e;
}

}  // namespace detail::crash_lattice_self_test

}  // namespace crucible::algebra::lattices
