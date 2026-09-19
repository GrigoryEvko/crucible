#pragma once

// How much of a hold a parent scope keeps on the children it spawns.  A
// stronger hold sits higher, so a leq that holds reads as the stricter tier
// proving everything the looser one promises: a parent that waits for every
// child satisfies a consumer that only asked for a cancellation request.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// FORGET and DETACH behave the same at runtime and are separate tiers for
// their provenance: under FORGET the spawn returned no handle at all, while
// under DETACH the caller was given one and chose to let it go.  A dangling
// reference to a child reads differently in a postmortem depending on which of
// the two it was.
enum class JoinPolicy : std::uint8_t {
    FORGET = 0,  // no handle, so no way to observe the child
    DETACH = 1,  // handle returned, then explicitly released
    ABANDON = 2,  // handle held to scope exit, then dropped unwaited
    CANCEL = 3,  // handle held to scope exit, then a stop request, no wait
    WAIT_DEADLINE = 4,  // bounded wait, then degrade to a looser tier
    JOIN_ALL = 5,  // unconditional wait for every child
};

inline constexpr std::size_t join_policy_count = std::meta::enumerators_of(^^JoinPolicy).size();

[[nodiscard]] consteval std::string_view join_policy_name(JoinPolicy p) noexcept {
    switch (p) {
        case JoinPolicy::FORGET:
            return "FORGET";
        case JoinPolicy::DETACH:
            return "DETACH";
        case JoinPolicy::ABANDON:
            return "ABANDON";
        case JoinPolicy::CANCEL:
            return "CANCEL";
        case JoinPolicy::WAIT_DEADLINE:
            return "WAIT_DEADLINE";
        case JoinPolicy::JOIN_ALL:
            return "JOIN_ALL";
        default:
            return std::string_view{"<unknown JoinPolicy>"};
    }
}

struct JoinPolicyLattice : ChainLatticeOps<JoinPolicy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return JoinPolicy::FORGET; }
    [[nodiscard]] static constexpr element_type top() noexcept { return JoinPolicy::JOIN_ALL; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "JoinPolicyLattice"; }

    template <JoinPolicy P>
    struct At {
        struct element_type {
            using join_policy_value_type = JoinPolicy;
            [[nodiscard]] constexpr operator join_policy_value_type() const noexcept { return P; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr JoinPolicy tier = P;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (P) {
                case JoinPolicy::FORGET:
                    return "JoinPolicyLattice::At<FORGET>";
                case JoinPolicy::DETACH:
                    return "JoinPolicyLattice::At<DETACH>";
                case JoinPolicy::ABANDON:
                    return "JoinPolicyLattice::At<ABANDON>";
                case JoinPolicy::CANCEL:
                    return "JoinPolicyLattice::At<CANCEL>";
                case JoinPolicy::WAIT_DEADLINE:
                    return "JoinPolicyLattice::At<WAIT_DEADLINE>";
                case JoinPolicy::JOIN_ALL:
                    return "JoinPolicyLattice::At<JOIN_ALL>";
                default:
                    return "JoinPolicyLattice::At<?>";
            }
        }
    };
};

// The Tier suffix keeps these names clear of the enumerators they wrap, which
// would otherwise collide in code that pulls both namespaces in.
namespace join_policy {
using ForgetTier = JoinPolicyLattice::At<JoinPolicy::FORGET>;
using DetachTier = JoinPolicyLattice::At<JoinPolicy::DETACH>;
using AbandonTier = JoinPolicyLattice::At<JoinPolicy::ABANDON>;
using CancelTier = JoinPolicyLattice::At<JoinPolicy::CANCEL>;
using WaitDeadlineTier = JoinPolicyLattice::At<JoinPolicy::WAIT_DEADLINE>;
using JoinAllTier = JoinPolicyLattice::At<JoinPolicy::JOIN_ALL>;
}  // namespace join_policy

namespace detail::join_policy_lattice_self_test {

static_assert(join_policy_count == 6, "The JoinPolicy catalog changed size.  Confirm the intent, then update "
                                      "every consumer that shortcuts to a named tier.");

[[nodiscard]] consteval bool every_join_policy_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^JoinPolicy));
    // The expansion statement unrolls into successive scopes that each declare
    // the same induction variable, which the shadow warning reports.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (join_policy_name([:en:]) == std::string_view{"<unknown JoinPolicy>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_join_policy_has_name(), "join_policy_name() switch missing arm for at least one JoinPolicy "
                                            "tier — add the arm or the new tier leaks the '<unknown JoinPolicy>' "
                                            "sentinel into debug output.");

static_assert(Lattice<JoinPolicyLattice>);
static_assert(BoundedLattice<JoinPolicyLattice>);
static_assert(Lattice<join_policy::ForgetTier>);
static_assert(Lattice<join_policy::DetachTier>);
static_assert(Lattice<join_policy::AbandonTier>);
static_assert(Lattice<join_policy::CancelTier>);
static_assert(Lattice<join_policy::WaitDeadlineTier>);
static_assert(Lattice<join_policy::JoinAllTier>);
static_assert(BoundedLattice<join_policy::JoinAllTier>);

static_assert(!UnboundedLattice<JoinPolicyLattice>);
static_assert(!Semiring<JoinPolicyLattice>);

static_assert(std::is_empty_v<join_policy::ForgetTier::element_type>);
static_assert(std::is_empty_v<join_policy::DetachTier::element_type>);
static_assert(std::is_empty_v<join_policy::AbandonTier::element_type>);
static_assert(std::is_empty_v<join_policy::CancelTier::element_type>);
static_assert(std::is_empty_v<join_policy::WaitDeadlineTier::element_type>);
static_assert(std::is_empty_v<join_policy::JoinAllTier::element_type>);

static_assert(verify_chain_lattice_exhaustive<JoinPolicyLattice>(),
              "JoinPolicyLattice's chain-order lattice axioms must hold at every "
              "(JoinPolicy)³ triple — failure indicates a defect in leq/join/meet "
              "or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<JoinPolicyLattice>(),
              "JoinPolicyLattice's chain order must satisfy distributivity at every "
              "(JoinPolicy)³ triple — a chain order always does, so failure would "
              "indicate a defect in join or meet.");

static_assert(JoinPolicyLattice::leq(JoinPolicy::FORGET, JoinPolicy::DETACH));
static_assert(JoinPolicyLattice::leq(JoinPolicy::DETACH, JoinPolicy::ABANDON));
static_assert(JoinPolicyLattice::leq(JoinPolicy::ABANDON, JoinPolicy::CANCEL));
static_assert(JoinPolicyLattice::leq(JoinPolicy::CANCEL, JoinPolicy::WAIT_DEADLINE));
static_assert(JoinPolicyLattice::leq(JoinPolicy::WAIT_DEADLINE, JoinPolicy::JOIN_ALL));
static_assert(JoinPolicyLattice::leq(JoinPolicy::FORGET, JoinPolicy::JOIN_ALL));
static_assert(!JoinPolicyLattice::leq(JoinPolicy::JOIN_ALL, JoinPolicy::FORGET));
static_assert(!JoinPolicyLattice::leq(JoinPolicy::WAIT_DEADLINE, JoinPolicy::CANCEL));
static_assert(!JoinPolicyLattice::leq(JoinPolicy::JOIN_ALL, JoinPolicy::WAIT_DEADLINE));
static_assert(!JoinPolicyLattice::leq(JoinPolicy::ABANDON, JoinPolicy::DETACH));

static_assert(JoinPolicyLattice::bottom() == JoinPolicy::FORGET);
static_assert(JoinPolicyLattice::top() == JoinPolicy::JOIN_ALL);

static_assert(JoinPolicyLattice::join(JoinPolicy::FORGET, JoinPolicy::JOIN_ALL) == JoinPolicy::JOIN_ALL);
static_assert(JoinPolicyLattice::join(JoinPolicy::DETACH, JoinPolicy::CANCEL) == JoinPolicy::CANCEL);
static_assert(JoinPolicyLattice::join(JoinPolicy::ABANDON, JoinPolicy::WAIT_DEADLINE) == JoinPolicy::WAIT_DEADLINE);
static_assert(JoinPolicyLattice::meet(JoinPolicy::FORGET, JoinPolicy::JOIN_ALL) == JoinPolicy::FORGET);
static_assert(JoinPolicyLattice::meet(JoinPolicy::CANCEL, JoinPolicy::JOIN_ALL) == JoinPolicy::CANCEL);
static_assert(JoinPolicyLattice::meet(JoinPolicy::WAIT_DEADLINE, JoinPolicy::JOIN_ALL) == JoinPolicy::WAIT_DEADLINE);

static_assert(JoinPolicyLattice::name() == "JoinPolicyLattice");
static_assert(join_policy::ForgetTier::name() == "JoinPolicyLattice::At<FORGET>");
static_assert(join_policy::DetachTier::name() == "JoinPolicyLattice::At<DETACH>");
static_assert(join_policy::AbandonTier::name() == "JoinPolicyLattice::At<ABANDON>");
static_assert(join_policy::CancelTier::name() == "JoinPolicyLattice::At<CANCEL>");
static_assert(join_policy::WaitDeadlineTier::name() == "JoinPolicyLattice::At<WAIT_DEADLINE>");
static_assert(join_policy::JoinAllTier::name() == "JoinPolicyLattice::At<JOIN_ALL>");
static_assert(join_policy_name(JoinPolicy::FORGET) == "FORGET");
static_assert(join_policy_name(JoinPolicy::DETACH) == "DETACH");
static_assert(join_policy_name(JoinPolicy::ABANDON) == "ABANDON");
static_assert(join_policy_name(JoinPolicy::CANCEL) == "CANCEL");
static_assert(join_policy_name(JoinPolicy::WAIT_DEADLINE) == "WAIT_DEADLINE");
static_assert(join_policy_name(JoinPolicy::JOIN_ALL) == "JOIN_ALL");

[[nodiscard]] consteval bool every_at_join_policy_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^JoinPolicy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (JoinPolicyLattice::At<([:en:])>::name() == std::string_view{"JoinPolicyLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_join_policy_has_name(), "JoinPolicyLattice::At<P>::name() switch missing an arm for at "
                                               "least one tier — add the arm or the new tier leaks the "
                                               "'JoinPolicyLattice::At<?>' sentinel.");

static_assert(join_policy::ForgetTier::tier == JoinPolicy::FORGET);
static_assert(join_policy::DetachTier::tier == JoinPolicy::DETACH);
static_assert(join_policy::AbandonTier::tier == JoinPolicy::ABANDON);
static_assert(join_policy::CancelTier::tier == JoinPolicy::CANCEL);
static_assert(join_policy::WaitDeadlineTier::tier == JoinPolicy::WAIT_DEADLINE);
static_assert(join_policy::JoinAllTier::tier == JoinPolicy::JOIN_ALL);

static_assert(static_cast<JoinPolicy>(join_policy::ForgetTier::element_type{}) == JoinPolicy::FORGET);
static_assert(static_cast<JoinPolicy>(join_policy::DetachTier::element_type{}) == JoinPolicy::DETACH);
static_assert(static_cast<JoinPolicy>(join_policy::AbandonTier::element_type{}) == JoinPolicy::ABANDON);
static_assert(static_cast<JoinPolicy>(join_policy::CancelTier::element_type{}) == JoinPolicy::CANCEL);
static_assert(static_cast<JoinPolicy>(join_policy::WaitDeadlineTier::element_type{}) == JoinPolicy::WAIT_DEADLINE);
static_assert(static_cast<JoinPolicy>(join_policy::JoinAllTier::element_type{}) == JoinPolicy::JOIN_ALL);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using JoinAllGraded = Graded<ModalityKind::Comonad, join_policy::JoinAllTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, double);

// The middle and bottom tiers are pinned as well, so that the invariant is
// known to hold at every tier rather than only at the top.
template <typename T>
using CancelGraded = Graded<ModalityKind::Comonad, join_policy::CancelTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelGraded, EightByteValue);

template <typename T>
using ForgetGraded = Graded<ModalityKind::Comonad, join_policy::ForgetTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetGraded, EightByteValue);

// Calling each operation on runtime operands catches the defects the
// compile-time assertions above cannot see, such as an inline body that only
// ever instantiates in a consteval context.
inline void runtime_smoke_test() {
    JoinPolicy a = JoinPolicy::FORGET;
    JoinPolicy b = JoinPolicy::JOIN_ALL;
    [[maybe_unused]] bool l1 = JoinPolicyLattice::leq(a, b);
    [[maybe_unused]] JoinPolicy j1 = JoinPolicyLattice::join(a, b);
    [[maybe_unused]] JoinPolicy m1 = JoinPolicyLattice::meet(a, b);
    [[maybe_unused]] JoinPolicy bot = JoinPolicyLattice::bottom();
    [[maybe_unused]] JoinPolicy top = JoinPolicyLattice::top();

    JoinPolicy mid_cancel = JoinPolicy::CANCEL;
    JoinPolicy mid_abandon = JoinPolicy::ABANDON;
    [[maybe_unused]] JoinPolicy j2 = JoinPolicyLattice::join(mid_cancel, b);
    [[maybe_unused]] JoinPolicy m2 = JoinPolicyLattice::meet(mid_cancel, a);
    [[maybe_unused]] JoinPolicy j3 = JoinPolicyLattice::join(mid_abandon, mid_cancel);
    [[maybe_unused]] JoinPolicy m3 = JoinPolicyLattice::meet(mid_abandon, mid_cancel);

    OneByteValue v{42};
    JoinAllGraded<OneByteValue> initial{v, join_policy::JoinAllTier::bottom()};
    auto widened = initial.weaken(join_policy::JoinAllTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(join_policy::JoinAllTier::top());

    // Extracting the plain value needs no declassification, because the tier
    // records how the parent waited for the producer rather than restricting
    // who may look at the result.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    join_policy::JoinAllTier::element_type e{};
    [[maybe_unused]] JoinPolicy recovered = e;
}

}  // namespace detail::join_policy_lattice_self_test

}  // namespace crucible::algebra::lattices
