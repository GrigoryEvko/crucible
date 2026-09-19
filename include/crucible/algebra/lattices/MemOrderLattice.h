#pragma once

// Chain over the memory orderings a function's atomic operations may
// use.  bottom is SeqCst, the most constrained and most expensive
// ordering.  top is Relaxed, which constrains nothing.  leq(a, b) reads
// "a is admitted wherever b is", so a Relaxed function is admitted
// everywhere and a SeqCst function only where SeqCst is tolerated.
// join climbs toward Relaxed and meet descends toward SeqCst, so the
// strictest-wins composition is meet, not join.
//
// Acquire and Release are incomparable in the C++ memory model: one
// fences the load side, the other the store side.  The chain places
// Release below Acquire so that admission gating has a single answer.
// That order carries no claim that either ordering substitutes for the
// other.
//
// There is no Consume tier.  Compilers promote consume to acquire, so a
// caller that would reach for it declares Acquire.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// The ordinals run opposite to std::memory_order so that ordinal 0 is
// the lattice bottom, as it is on every other chain here.
enum class MemOrderTag : std::uint8_t {
    SeqCst = 0,  // total-order fence
    AcqRel = 1,  // read-modify-write, both directions
    Release = 2,  // store-side directional fence
    Acquire = 3,  // load-side directional fence
    Relaxed = 4,  // atomicity only, no fence
};

inline constexpr std::size_t mem_order_tag_count = std::meta::enumerators_of(^^MemOrderTag).size();

[[nodiscard]] consteval std::string_view mem_order_tag_name(MemOrderTag t) noexcept {
    switch (t) {
        case MemOrderTag::SeqCst:
            return "SeqCst";
        case MemOrderTag::AcqRel:
            return "AcqRel";
        case MemOrderTag::Release:
            return "Release";
        case MemOrderTag::Acquire:
            return "Acquire";
        case MemOrderTag::Relaxed:
            return "Relaxed";
        default:
            return std::string_view{"<unknown MemOrderTag>"};
    }
}

struct MemOrderLattice : ChainLatticeOps<MemOrderTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return MemOrderTag::SeqCst; }
    [[nodiscard]] static constexpr element_type top() noexcept { return MemOrderTag::Relaxed; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "MemOrderLattice"; }

    template <MemOrderTag T>
    struct At {
        struct element_type {
            using mem_order_tag_value_type = MemOrderTag;
            [[nodiscard]] constexpr operator mem_order_tag_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr MemOrderTag tag = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case MemOrderTag::SeqCst:
                    return "MemOrderLattice::At<SeqCst>";
                case MemOrderTag::AcqRel:
                    return "MemOrderLattice::At<AcqRel>";
                case MemOrderTag::Release:
                    return "MemOrderLattice::At<Release>";
                case MemOrderTag::Acquire:
                    return "MemOrderLattice::At<Acquire>";
                case MemOrderTag::Relaxed:
                    return "MemOrderLattice::At<Relaxed>";
                default:
                    return "MemOrderLattice::At<?>";
            }
        }
    };
};

namespace mem_order_tag {
using SeqCstTag = MemOrderLattice::At<MemOrderTag::SeqCst>;
using AcqRelTag = MemOrderLattice::At<MemOrderTag::AcqRel>;
using ReleaseTag = MemOrderLattice::At<MemOrderTag::Release>;
using AcquireTag = MemOrderLattice::At<MemOrderTag::Acquire>;
using RelaxedTag = MemOrderLattice::At<MemOrderTag::Relaxed>;
}  // namespace mem_order_tag

namespace detail::mem_order_lattice_self_test {

static_assert(mem_order_tag_count == 5, "MemOrderTag catalog diverged from {SeqCst, AcqRel, Release, "
                                        "Acquire, Relaxed}.  Confirm intent and update the hot-path "
                                        "admission gates.");

[[nodiscard]] consteval bool every_mem_order_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^MemOrderTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (mem_order_tag_name([:en:]) == std::string_view{"<unknown MemOrderTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_mem_order_tag_has_name(), "mem_order_tag_name() switch missing arm for at least one tag.");

static_assert(Lattice<MemOrderLattice>);
static_assert(BoundedLattice<MemOrderLattice>);
static_assert(Lattice<mem_order_tag::SeqCstTag>);
static_assert(Lattice<mem_order_tag::AcqRelTag>);
static_assert(Lattice<mem_order_tag::ReleaseTag>);
static_assert(Lattice<mem_order_tag::AcquireTag>);
static_assert(Lattice<mem_order_tag::RelaxedTag>);
static_assert(BoundedLattice<mem_order_tag::RelaxedTag>);

static_assert(!UnboundedLattice<MemOrderLattice>);
static_assert(!Semiring<MemOrderLattice>);

static_assert(std::is_empty_v<mem_order_tag::RelaxedTag::element_type>);
static_assert(std::is_empty_v<mem_order_tag::AcqRelTag::element_type>);
static_assert(std::is_empty_v<mem_order_tag::SeqCstTag::element_type>);

static_assert(verify_chain_lattice_exhaustive<MemOrderLattice>(),
              "MemOrderLattice chain-order lattice axioms fail at some triple.  "
              "The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<MemOrderLattice>(),
              "MemOrderLattice chain fails distributivity at some triple.");

static_assert(MemOrderLattice::leq(MemOrderTag::SeqCst, MemOrderTag::AcqRel));
static_assert(MemOrderLattice::leq(MemOrderTag::AcqRel, MemOrderTag::Release));
static_assert(MemOrderLattice::leq(MemOrderTag::Release, MemOrderTag::Acquire));
static_assert(MemOrderLattice::leq(MemOrderTag::Acquire, MemOrderTag::Relaxed));
static_assert(MemOrderLattice::leq(MemOrderTag::SeqCst, MemOrderTag::Relaxed));
static_assert(!MemOrderLattice::leq(MemOrderTag::Relaxed, MemOrderTag::SeqCst));
static_assert(!MemOrderLattice::leq(MemOrderTag::Relaxed, MemOrderTag::Acquire));
static_assert(!MemOrderLattice::leq(MemOrderTag::AcqRel, MemOrderTag::SeqCst));

static_assert(MemOrderLattice::bottom() == MemOrderTag::SeqCst);
static_assert(MemOrderLattice::top() == MemOrderTag::Relaxed);

static_assert(MemOrderLattice::join(MemOrderTag::SeqCst, MemOrderTag::Relaxed) == MemOrderTag::Relaxed);
static_assert(MemOrderLattice::join(MemOrderTag::AcqRel, MemOrderTag::Release) == MemOrderTag::Release);
static_assert(MemOrderLattice::meet(MemOrderTag::SeqCst, MemOrderTag::Relaxed) == MemOrderTag::SeqCst);
static_assert(MemOrderLattice::meet(MemOrderTag::Acquire, MemOrderTag::Relaxed) == MemOrderTag::Acquire);

static_assert(MemOrderLattice::meet(MemOrderTag::AcqRel, MemOrderTag::Relaxed) == MemOrderTag::AcqRel,
              "meet gives the strictest-wins reading on this chain, because the "
              "bottom is SeqCst.  meet(AcqRel, Relaxed) returns AcqRel, the "
              "stricter of the two.");
static_assert(MemOrderLattice::meet(MemOrderTag::Release, MemOrderTag::Acquire) == MemOrderTag::Release,
              "meet(Release, Acquire) returns Release, the lower ordinal.  The "
              "C++ memory model leaves the two incomparable.  This chain "
              "linearizes them so that gating has a single answer.");
static_assert(MemOrderLattice::join(MemOrderTag::Release, MemOrderTag::Acquire) == MemOrderTag::Acquire,
              "join(Release, Acquire) returns Acquire, the higher ordinal and the "
              "weaker ordering.  A consumer that wants strictest-wins calls meet, "
              "not join.");

static_assert(MemOrderLattice::name() == "MemOrderLattice");
static_assert(mem_order_tag::SeqCstTag::name() == "MemOrderLattice::At<SeqCst>");
static_assert(mem_order_tag::AcqRelTag::name() == "MemOrderLattice::At<AcqRel>");
static_assert(mem_order_tag::ReleaseTag::name() == "MemOrderLattice::At<Release>");
static_assert(mem_order_tag::AcquireTag::name() == "MemOrderLattice::At<Acquire>");
static_assert(mem_order_tag::RelaxedTag::name() == "MemOrderLattice::At<Relaxed>");

[[nodiscard]] consteval bool every_at_mem_order_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^MemOrderTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (MemOrderLattice::At<([:en:])>::name() == std::string_view{"MemOrderLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_mem_order_tag_has_name(), "MemOrderLattice::At<T>::name() switch missing an arm for at "
                                                 "least one tag.");

static_assert(mem_order_tag::SeqCstTag::tag == MemOrderTag::SeqCst);
static_assert(mem_order_tag::AcqRelTag::tag == MemOrderTag::AcqRel);
static_assert(mem_order_tag::ReleaseTag::tag == MemOrderTag::Release);
static_assert(mem_order_tag::AcquireTag::tag == MemOrderTag::Acquire);
static_assert(mem_order_tag::RelaxedTag::tag == MemOrderTag::Relaxed);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using RelaxedGraded = Graded<ModalityKind::Absolute, mem_order_tag::RelaxedTag, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxedGraded, double);

template <typename T_>
using AcqRelGraded = Graded<ModalityKind::Absolute, mem_order_tag::AcqRelTag, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelGraded, EightByteValue);

template <typename T_>
using SeqCstGraded = Graded<ModalityKind::Absolute, mem_order_tag::SeqCstTag, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstGraded, EightByteValue);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    MemOrderTag a = MemOrderTag::SeqCst;
    MemOrderTag b = MemOrderTag::Relaxed;
    [[maybe_unused]] bool l1 = MemOrderLattice::leq(a, b);
    [[maybe_unused]] MemOrderTag j1 = MemOrderLattice::join(a, b);
    [[maybe_unused]] MemOrderTag m1 = MemOrderLattice::meet(a, b);
    [[maybe_unused]] MemOrderTag bot = MemOrderLattice::bottom();
    [[maybe_unused]] MemOrderTag topv = MemOrderLattice::top();

    MemOrderTag acq = MemOrderTag::Acquire;
    MemOrderTag rel = MemOrderTag::Release;
    [[maybe_unused]] MemOrderTag j2 = MemOrderLattice::join(acq, rel);
    [[maybe_unused]] MemOrderTag m2 = MemOrderLattice::meet(acq, rel);

    OneByteValue v{42};
    RelaxedGraded<OneByteValue> initial{v, mem_order_tag::RelaxedTag::bottom()};
    auto widened = initial.weaken(mem_order_tag::RelaxedTag::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(mem_order_tag::RelaxedTag::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    mem_order_tag::RelaxedTag::element_type e{};
    [[maybe_unused]] MemOrderTag rec = e;
}

}  // namespace detail::mem_order_lattice_self_test

}  // namespace crucible::algebra::lattices
