#pragma once

// Four-tier chain over the termination promise a function makes about
// its own execution.
//
// The strongest promise sits at the top, so `leq(weak, strong)` reads "a
// weaker-promise consumer is satisfied by a stronger-promise provider".
// A Bounded function is admissible from every call site.  A MayDiverge
// one is the escape hatch and is admissible only where divergence is
// tolerated.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class ProgressClass : std::uint8_t {
    MayDiverge = 0,  // bottom: no termination guarantee
    Terminating = 1,  // halts eventually (no bound)
    Productive = 2,  // halts AND makes observable progress every step
    Bounded = 3,  // top: halts within hard wall-clock budget
};

inline constexpr std::size_t progress_class_count = std::meta::enumerators_of(^^ProgressClass).size();

[[nodiscard]] consteval std::string_view progress_class_name(ProgressClass c) noexcept {
    switch (c) {
        case ProgressClass::MayDiverge:
            return "MayDiverge";
        case ProgressClass::Terminating:
            return "Terminating";
        case ProgressClass::Productive:
            return "Productive";
        case ProgressClass::Bounded:
            return "Bounded";
        default:
            return std::string_view{"<unknown ProgressClass>"};
    }
}

struct ProgressLattice : ChainLatticeOps<ProgressClass> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return ProgressClass::MayDiverge; }
    [[nodiscard]] static constexpr element_type top() noexcept { return ProgressClass::Bounded; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "ProgressLattice"; }

    template <ProgressClass T>
    struct At {
        struct element_type {
            using progress_class_value_type = ProgressClass;
            [[nodiscard]] constexpr operator progress_class_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr ProgressClass cls = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case ProgressClass::MayDiverge:
                    return "ProgressLattice::At<MayDiverge>";
                case ProgressClass::Terminating:
                    return "ProgressLattice::At<Terminating>";
                case ProgressClass::Productive:
                    return "ProgressLattice::At<Productive>";
                case ProgressClass::Bounded:
                    return "ProgressLattice::At<Bounded>";
                default:
                    return "ProgressLattice::At<?>";
            }
        }
    };
};

namespace progress_class {
using MayDivergeClass = ProgressLattice::At<ProgressClass::MayDiverge>;
using TerminatingClass = ProgressLattice::At<ProgressClass::Terminating>;
using ProductiveClass = ProgressLattice::At<ProgressClass::Productive>;
using BoundedClass = ProgressLattice::At<ProgressClass::Bounded>;
}  // namespace progress_class

namespace detail::progress_lattice_self_test {

static_assert(progress_class_count == 4, "ProgressClass must hold exactly the four classes MayDiverge, "
                                         "Terminating, Productive and Bounded.");

[[nodiscard]] consteval bool every_progress_class_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ProgressClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (progress_class_name([:en:]) == std::string_view{"<unknown ProgressClass>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_progress_class_has_name(), "progress_class_name() has no arm for at least one class, so "
                                               "that class reports the '<unknown ProgressClass>' sentinel.");

static_assert(Lattice<ProgressLattice>);
static_assert(BoundedLattice<ProgressLattice>);
static_assert(Lattice<progress_class::MayDivergeClass>);
static_assert(Lattice<progress_class::TerminatingClass>);
static_assert(Lattice<progress_class::ProductiveClass>);
static_assert(Lattice<progress_class::BoundedClass>);
static_assert(BoundedLattice<progress_class::BoundedClass>);

static_assert(!UnboundedLattice<ProgressLattice>);
static_assert(!Semiring<ProgressLattice>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<progress_class::BoundedClass::element_type>);
static_assert(std::is_empty_v<progress_class::ProductiveClass::element_type>);
static_assert(std::is_empty_v<progress_class::TerminatingClass::element_type>);
static_assert(std::is_empty_v<progress_class::MayDivergeClass::element_type>);

static_assert(verify_chain_lattice_exhaustive<ProgressLattice>(),
              "ProgressLattice's chain-order lattice axioms must hold at every "
              "(ProgressClass)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<ProgressLattice>(),
              "ProgressLattice's chain order must satisfy distributivity at "
              "every (ProgressClass)³ triple.");

static_assert(ProgressLattice::leq(ProgressClass::MayDiverge, ProgressClass::Terminating));
static_assert(ProgressLattice::leq(ProgressClass::Terminating, ProgressClass::Productive));
static_assert(ProgressLattice::leq(ProgressClass::Productive, ProgressClass::Bounded));
static_assert(ProgressLattice::leq(ProgressClass::MayDiverge, ProgressClass::Bounded));
static_assert(!ProgressLattice::leq(ProgressClass::Bounded, ProgressClass::MayDiverge));
static_assert(!ProgressLattice::leq(ProgressClass::Productive, ProgressClass::Terminating));
static_assert(!ProgressLattice::leq(ProgressClass::Bounded, ProgressClass::Productive));

static_assert(ProgressLattice::bottom() == ProgressClass::MayDiverge);
static_assert(ProgressLattice::top() == ProgressClass::Bounded);

static_assert(ProgressLattice::join(ProgressClass::MayDiverge, ProgressClass::Bounded) == ProgressClass::Bounded);
static_assert(ProgressLattice::join(ProgressClass::Terminating, ProgressClass::Productive)
              == ProgressClass::Productive);
static_assert(ProgressLattice::meet(ProgressClass::MayDiverge, ProgressClass::Bounded) == ProgressClass::MayDiverge);
static_assert(ProgressLattice::meet(ProgressClass::Productive, ProgressClass::Bounded) == ProgressClass::Productive);

static_assert(ProgressLattice::name() == "ProgressLattice");
static_assert(progress_class::MayDivergeClass::name() == "ProgressLattice::At<MayDiverge>");
static_assert(progress_class::TerminatingClass::name() == "ProgressLattice::At<Terminating>");
static_assert(progress_class::ProductiveClass::name() == "ProgressLattice::At<Productive>");
static_assert(progress_class::BoundedClass::name() == "ProgressLattice::At<Bounded>");

[[nodiscard]] consteval bool every_at_progress_class_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ProgressClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ProgressLattice::At<([:en:])>::name() == std::string_view{"ProgressLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_progress_class_has_name(), "ProgressLattice::At<T>::name() has no arm for at least one "
                                                  "class, so that class reports the 'ProgressLattice::At<?>' "
                                                  "sentinel.");

static_assert(progress_class::MayDivergeClass::cls == ProgressClass::MayDiverge);
static_assert(progress_class::TerminatingClass::cls == ProgressClass::Terminating);
static_assert(progress_class::ProductiveClass::cls == ProgressClass::Productive);
static_assert(progress_class::BoundedClass::cls == ProgressClass::Bounded);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

// The top class witnesses the collapse for both class and arithmetic
// values; the other classes need only one witness each.
template <typename T_>
using BoundedGraded = Graded<ModalityKind::Absolute, progress_class::BoundedClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, double);

template <typename T_>
using ProductiveGraded = Graded<ModalityKind::Absolute, progress_class::ProductiveClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProductiveGraded, EightByteValue);

template <typename T_>
using MayDivergeGraded = Graded<ModalityKind::Absolute, progress_class::MayDivergeClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MayDivergeGraded, EightByteValue);

inline void runtime_smoke_test() {
    ProgressClass a = ProgressClass::MayDiverge;
    ProgressClass b = ProgressClass::Bounded;
    [[maybe_unused]] bool l1 = ProgressLattice::leq(a, b);
    [[maybe_unused]] ProgressClass j1 = ProgressLattice::join(a, b);
    [[maybe_unused]] ProgressClass m1 = ProgressLattice::meet(a, b);
    [[maybe_unused]] ProgressClass bot = ProgressLattice::bottom();
    [[maybe_unused]] ProgressClass topv = ProgressLattice::top();

    ProgressClass term = ProgressClass::Terminating;
    ProgressClass prod = ProgressClass::Productive;
    [[maybe_unused]] ProgressClass j2 = ProgressLattice::join(term, prod);
    [[maybe_unused]] ProgressClass m2 = ProgressLattice::meet(term, prod);

    OneByteValue v{42};
    BoundedGraded<OneByteValue> initial{v, progress_class::BoundedClass::bottom()};
    auto widened = initial.weaken(progress_class::BoundedClass::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(progress_class::BoundedClass::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    progress_class::BoundedClass::element_type e{};
    [[maybe_unused]] ProgressClass rec = e;
}

}  // namespace detail::progress_lattice_self_test

}  // namespace crucible::algebra::lattices
