#pragma once

// Five-tier chain over the consistency guarantee a replicated value
// carries.
//
// The strictest guarantee sits at the top, so `leq(weak, strong)` reads
// "the weaker requirement is subsumed by the stronger guarantee".  A
// STRONG provider satisfies a consumer asking for any tier.
//
// BOUNDED_STALENESS outranks CAUSAL_PREFIX because its bound is
// real-time rather than causal, which is the stricter of the two.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class Consistency : std::uint8_t {
    EVENTUAL = 0,  // weakest — eventually converges
    READ_YOUR_WRITES = 1,  // local observer sees own writes immediately
    CAUSAL_PREFIX = 2,  // observed prefix of causal linearization
    BOUNDED_STALENESS = 3,  // real-time staleness bound (K outer-steps)
    STRONG = 4,  // linearizable; strictest
};

inline constexpr std::size_t consistency_count = std::meta::enumerators_of(^^Consistency).size();

[[nodiscard]] consteval std::string_view consistency_name(Consistency c) noexcept {
    switch (c) {
        case Consistency::EVENTUAL:
            return "EVENTUAL";
        case Consistency::READ_YOUR_WRITES:
            return "READ_YOUR_WRITES";
        case Consistency::CAUSAL_PREFIX:
            return "CAUSAL_PREFIX";
        case Consistency::BOUNDED_STALENESS:
            return "BOUNDED_STALENESS";
        case Consistency::STRONG:
            return "STRONG";
        default:
            return std::string_view{"<unknown Consistency>"};
    }
}

struct ConsistencyLattice : ChainLatticeOps<Consistency> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return Consistency::EVENTUAL; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Consistency::STRONG; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "ConsistencyLattice"; }

    template <Consistency C>
    struct At {
        struct element_type {
            using consistency_value_type = Consistency;
            [[nodiscard]] constexpr operator consistency_value_type() const noexcept { return C; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr Consistency tier = C;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (C) {
                case Consistency::EVENTUAL:
                    return "ConsistencyLattice::At<EVENTUAL>";
                case Consistency::READ_YOUR_WRITES:
                    return "ConsistencyLattice::At<READ_YOUR_WRITES>";
                case Consistency::CAUSAL_PREFIX:
                    return "ConsistencyLattice::At<CAUSAL_PREFIX>";
                case Consistency::BOUNDED_STALENESS:
                    return "ConsistencyLattice::At<BOUNDED_STALENESS>";
                case Consistency::STRONG:
                    return "ConsistencyLattice::At<STRONG>";
                default:
                    return "ConsistencyLattice::At<?>";
            }
        }
    };
};

namespace consistency {
using EventualTier = ConsistencyLattice::At<Consistency::EVENTUAL>;
using ReadYourWritesTier = ConsistencyLattice::At<Consistency::READ_YOUR_WRITES>;
using CausalPrefixTier = ConsistencyLattice::At<Consistency::CAUSAL_PREFIX>;
using BoundedStalenessTier = ConsistencyLattice::At<Consistency::BOUNDED_STALENESS>;
using StrongTier = ConsistencyLattice::At<Consistency::STRONG>;
}  // namespace consistency

namespace detail::consistency_lattice_self_test {

static_assert(consistency_count == 5, "Consistency must hold exactly the five tiers EVENTUAL, "
                                      "READ_YOUR_WRITES, CAUSAL_PREFIX, BOUNDED_STALENESS and STRONG.");

[[nodiscard]] consteval bool every_consistency_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Consistency));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (consistency_name([:en:]) == std::string_view{"<unknown Consistency>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_consistency_has_name(), "consistency_name() has no arm for at least one tier, so that "
                                            "tier reports the '<unknown Consistency>' sentinel.");

static_assert(Lattice<ConsistencyLattice>);
static_assert(BoundedLattice<ConsistencyLattice>);
static_assert(Lattice<consistency::EventualTier>);
static_assert(Lattice<consistency::ReadYourWritesTier>);
static_assert(Lattice<consistency::CausalPrefixTier>);
static_assert(Lattice<consistency::BoundedStalenessTier>);
static_assert(Lattice<consistency::StrongTier>);
static_assert(BoundedLattice<consistency::StrongTier>);

static_assert(!UnboundedLattice<ConsistencyLattice>);
static_assert(!Semiring<ConsistencyLattice>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<consistency::EventualTier::element_type>);
static_assert(std::is_empty_v<consistency::ReadYourWritesTier::element_type>);
static_assert(std::is_empty_v<consistency::CausalPrefixTier::element_type>);
static_assert(std::is_empty_v<consistency::BoundedStalenessTier::element_type>);
static_assert(std::is_empty_v<consistency::StrongTier::element_type>);

static_assert(verify_chain_lattice_exhaustive<ConsistencyLattice>(),
              "ConsistencyLattice's chain-order lattice axioms must hold at "
              "every (Consistency)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<ConsistencyLattice>(),
              "ConsistencyLattice's chain order must satisfy distributivity at "
              "every (Consistency)³ triple.");

static_assert(ConsistencyLattice::leq(Consistency::EVENTUAL, Consistency::READ_YOUR_WRITES));
static_assert(ConsistencyLattice::leq(Consistency::READ_YOUR_WRITES, Consistency::CAUSAL_PREFIX));
static_assert(ConsistencyLattice::leq(Consistency::CAUSAL_PREFIX, Consistency::BOUNDED_STALENESS));
static_assert(ConsistencyLattice::leq(Consistency::BOUNDED_STALENESS, Consistency::STRONG));
static_assert(ConsistencyLattice::leq(Consistency::EVENTUAL, Consistency::STRONG));
static_assert(!ConsistencyLattice::leq(Consistency::STRONG, Consistency::EVENTUAL));
static_assert(!ConsistencyLattice::leq(Consistency::CAUSAL_PREFIX, Consistency::READ_YOUR_WRITES));

static_assert(ConsistencyLattice::bottom() == Consistency::EVENTUAL);
static_assert(ConsistencyLattice::top() == Consistency::STRONG);

static_assert(ConsistencyLattice::join(Consistency::EVENTUAL, Consistency::STRONG) == Consistency::STRONG);
static_assert(ConsistencyLattice::join(Consistency::READ_YOUR_WRITES, Consistency::CAUSAL_PREFIX)
              == Consistency::CAUSAL_PREFIX);
static_assert(ConsistencyLattice::meet(Consistency::EVENTUAL, Consistency::STRONG) == Consistency::EVENTUAL);
static_assert(ConsistencyLattice::meet(Consistency::CAUSAL_PREFIX, Consistency::STRONG) == Consistency::CAUSAL_PREFIX);

static_assert(ConsistencyLattice::name() == "ConsistencyLattice");
static_assert(consistency::EventualTier::name() == "ConsistencyLattice::At<EVENTUAL>");
static_assert(consistency::ReadYourWritesTier::name() == "ConsistencyLattice::At<READ_YOUR_WRITES>");
static_assert(consistency::CausalPrefixTier::name() == "ConsistencyLattice::At<CAUSAL_PREFIX>");
static_assert(consistency::BoundedStalenessTier::name() == "ConsistencyLattice::At<BOUNDED_STALENESS>");
static_assert(consistency::StrongTier::name() == "ConsistencyLattice::At<STRONG>");
static_assert(consistency_name(Consistency::EVENTUAL) == "EVENTUAL");
static_assert(consistency_name(Consistency::READ_YOUR_WRITES) == "READ_YOUR_WRITES");
static_assert(consistency_name(Consistency::CAUSAL_PREFIX) == "CAUSAL_PREFIX");
static_assert(consistency_name(Consistency::BOUNDED_STALENESS) == "BOUNDED_STALENESS");
static_assert(consistency_name(Consistency::STRONG) == "STRONG");

[[nodiscard]] consteval bool every_at_consistency_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Consistency));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ConsistencyLattice::At<([:en:])>::name() == std::string_view{"ConsistencyLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_consistency_has_name(), "ConsistencyLattice::At<C>::name() has no arm for at least one "
                                               "tier, so that tier reports the 'ConsistencyLattice::At<?>' "
                                               "sentinel.");

static_assert(consistency::EventualTier::tier == Consistency::EVENTUAL);
static_assert(consistency::ReadYourWritesTier::tier == Consistency::READ_YOUR_WRITES);
static_assert(consistency::CausalPrefixTier::tier == Consistency::CAUSAL_PREFIX);
static_assert(consistency::BoundedStalenessTier::tier == Consistency::BOUNDED_STALENESS);
static_assert(consistency::StrongTier::tier == Consistency::STRONG);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

// The top tier witnesses the collapse for both class and arithmetic
// values; the other tiers need only one witness each.
template <typename T>
using StrongGraded = Graded<ModalityKind::Absolute, consistency::StrongTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, double);

template <typename T>
using EventualGraded = Graded<ModalityKind::Absolute, consistency::EventualTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(EventualGraded, EightByteValue);

template <typename T>
using CausalGraded = Graded<ModalityKind::Absolute, consistency::CausalPrefixTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CausalGraded, EightByteValue);

inline void runtime_smoke_test() {
    Consistency a = Consistency::EVENTUAL;
    Consistency b = Consistency::STRONG;
    [[maybe_unused]] bool l1 = ConsistencyLattice::leq(a, b);
    [[maybe_unused]] Consistency j1 = ConsistencyLattice::join(a, b);
    [[maybe_unused]] Consistency m1 = ConsistencyLattice::meet(a, b);
    [[maybe_unused]] Consistency bot = ConsistencyLattice::bottom();
    [[maybe_unused]] Consistency top = ConsistencyLattice::top();

    Consistency mid = Consistency::CAUSAL_PREFIX;
    [[maybe_unused]] Consistency j2 = ConsistencyLattice::join(mid, b);
    [[maybe_unused]] Consistency m2 = ConsistencyLattice::meet(mid, a);

    OneByteValue v{42};
    StrongGraded<OneByteValue> initial{v, consistency::StrongTier::bottom()};
    auto widened = initial.weaken(consistency::StrongTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(consistency::StrongTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    consistency::StrongTier::element_type e{};
    [[maybe_unused]] Consistency rec = e;
}

}  // namespace detail::consistency_lattice_self_test

}  // namespace crucible::algebra::lattices
