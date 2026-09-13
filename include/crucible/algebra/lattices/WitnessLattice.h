#pragma once

// Chain over how strongly a value's claimed invariant is established.
// bottom is UNWITNESSED and top is FORMALLY_VERIFIED.  join strengthens
// a joint claim to the better of two sources.  meet weakens it, which
// is what a downcast to a less demanding sink does.
//
// The comonadic extract is unrestricted.  Observing a witnessed value
// as a plain value is always sound, because the tier is metadata about
// how the value was established and not a filter over it.  The
// discipline sits on the producer, which must hold the proof before it
// can mint the tier.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class Witness : std::uint8_t {
    UNWITNESSED = 0,  // no proof attached
    TYPE_CHECKED = 1,  // the wrapper and contract discipline accepted it
    TEST_PASSED = 2,  // the measurement gates cleared
    FORMALLY_VERIFIED = 3,  // a machine-checked proof or a vendor attestation
};

inline constexpr std::size_t witness_count = std::meta::enumerators_of(^^Witness).size();

[[nodiscard]] consteval std::string_view witness_name(Witness w) noexcept {
    switch (w) {
        case Witness::UNWITNESSED:
            return "UNWITNESSED";
        case Witness::TYPE_CHECKED:
            return "TYPE_CHECKED";
        case Witness::TEST_PASSED:
            return "TEST_PASSED";
        case Witness::FORMALLY_VERIFIED:
            return "FORMALLY_VERIFIED";
        default:
            return std::string_view{"<unknown Witness>"};
    }
}

struct WitnessLattice : ChainLatticeOps<Witness> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return Witness::UNWITNESSED; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Witness::FORMALLY_VERIFIED; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "WitnessLattice"; }

    template <Witness W>
    struct At {
        struct element_type {
            using witness_value_type = Witness;
            [[nodiscard]] constexpr operator witness_value_type() const noexcept { return W; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr Witness tier = W;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (W) {
                case Witness::UNWITNESSED:
                    return "WitnessLattice::At<UNWITNESSED>";
                case Witness::TYPE_CHECKED:
                    return "WitnessLattice::At<TYPE_CHECKED>";
                case Witness::TEST_PASSED:
                    return "WitnessLattice::At<TEST_PASSED>";
                case Witness::FORMALLY_VERIFIED:
                    return "WitnessLattice::At<FORMALLY_VERIFIED>";
                default:
                    return "WitnessLattice::At<?>";
            }
        }
    };
};

// The Tier suffix keeps these names clear of the enumerators for code
// that pulls both into one scope.
namespace witness {
using UnwitnessedTier = WitnessLattice::At<Witness::UNWITNESSED>;
using TypeCheckedTier = WitnessLattice::At<Witness::TYPE_CHECKED>;
using TestPassedTier = WitnessLattice::At<Witness::TEST_PASSED>;
using FormallyVerifiedTier = WitnessLattice::At<Witness::FORMALLY_VERIFIED>;
}  // namespace witness

namespace detail::witness_lattice_self_test {

static_assert(witness_count == 4, "Witness catalog diverged from {UNWITNESSED, TYPE_CHECKED, "
                                  "TEST_PASSED, FORMALLY_VERIFIED}.  Confirm intent and update the "
                                  "tier shortcuts and producer sites that name the tiers.");

[[nodiscard]] consteval bool every_witness_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Witness));
    // An expansion statement unrolls into successive scopes that each
    // declare the same induction variable, so -Wshadow fires on the
    // body.  The suppression covers the loop only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (witness_name([:en:]) == std::string_view{"<unknown Witness>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_witness_has_name(), "witness_name() switch missing an arm for at least one Witness tier.  "
                                        "Add the arm or the new tier leaks the '<unknown Witness>' sentinel "
                                        "into diagnostic output.");

static_assert(Lattice<WitnessLattice>);
static_assert(BoundedLattice<WitnessLattice>);
static_assert(Lattice<witness::UnwitnessedTier>);
static_assert(Lattice<witness::TypeCheckedTier>);
static_assert(Lattice<witness::TestPassedTier>);
static_assert(Lattice<witness::FormallyVerifiedTier>);
static_assert(BoundedLattice<witness::FormallyVerifiedTier>);

static_assert(!UnboundedLattice<WitnessLattice>);
static_assert(!Semiring<WitnessLattice>);

static_assert(std::is_empty_v<witness::UnwitnessedTier::element_type>);
static_assert(std::is_empty_v<witness::TypeCheckedTier::element_type>);
static_assert(std::is_empty_v<witness::TestPassedTier::element_type>);
static_assert(std::is_empty_v<witness::FormallyVerifiedTier::element_type>);

static_assert(verify_chain_lattice_exhaustive<WitnessLattice>(),
              "WitnessLattice chain-order lattice axioms fail at some triple.  "
              "The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<WitnessLattice>(),
              "WitnessLattice chain fails distributivity at some triple.  A "
              "chain order always satisfies it, so the defect is in join or "
              "meet.");

static_assert(WitnessLattice::leq(Witness::UNWITNESSED, Witness::TYPE_CHECKED));
static_assert(WitnessLattice::leq(Witness::TYPE_CHECKED, Witness::TEST_PASSED));
static_assert(WitnessLattice::leq(Witness::TEST_PASSED, Witness::FORMALLY_VERIFIED));
static_assert(WitnessLattice::leq(Witness::UNWITNESSED, Witness::FORMALLY_VERIFIED));
static_assert(!WitnessLattice::leq(Witness::FORMALLY_VERIFIED, Witness::UNWITNESSED));
static_assert(!WitnessLattice::leq(Witness::TEST_PASSED, Witness::TYPE_CHECKED));
static_assert(!WitnessLattice::leq(Witness::FORMALLY_VERIFIED, Witness::TEST_PASSED));

static_assert(WitnessLattice::bottom() == Witness::UNWITNESSED);
static_assert(WitnessLattice::top() == Witness::FORMALLY_VERIFIED);

static_assert(WitnessLattice::join(Witness::UNWITNESSED, Witness::FORMALLY_VERIFIED) == Witness::FORMALLY_VERIFIED);
static_assert(WitnessLattice::join(Witness::TYPE_CHECKED, Witness::TEST_PASSED) == Witness::TEST_PASSED);
static_assert(WitnessLattice::meet(Witness::UNWITNESSED, Witness::FORMALLY_VERIFIED) == Witness::UNWITNESSED);
static_assert(WitnessLattice::meet(Witness::TEST_PASSED, Witness::FORMALLY_VERIFIED) == Witness::TEST_PASSED);

static_assert(WitnessLattice::name() == "WitnessLattice");
static_assert(witness::UnwitnessedTier::name() == "WitnessLattice::At<UNWITNESSED>");
static_assert(witness::TypeCheckedTier::name() == "WitnessLattice::At<TYPE_CHECKED>");
static_assert(witness::TestPassedTier::name() == "WitnessLattice::At<TEST_PASSED>");
static_assert(witness::FormallyVerifiedTier::name() == "WitnessLattice::At<FORMALLY_VERIFIED>");
static_assert(witness_name(Witness::UNWITNESSED) == "UNWITNESSED");
static_assert(witness_name(Witness::TYPE_CHECKED) == "TYPE_CHECKED");
static_assert(witness_name(Witness::TEST_PASSED) == "TEST_PASSED");
static_assert(witness_name(Witness::FORMALLY_VERIFIED) == "FORMALLY_VERIFIED");

[[nodiscard]] consteval bool every_at_witness_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Witness));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (WitnessLattice::At<([:en:])>::name() == std::string_view{"WitnessLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_witness_has_name(), "WitnessLattice::At<W>::name() switch missing an arm for at least "
                                           "one tier.  Add the arm or the new tier leaks the "
                                           "'WitnessLattice::At<?>' sentinel.");

static_assert(witness::UnwitnessedTier::tier == Witness::UNWITNESSED);
static_assert(witness::TypeCheckedTier::tier == Witness::TYPE_CHECKED);
static_assert(witness::TestPassedTier::tier == Witness::TEST_PASSED);
static_assert(witness::FormallyVerifiedTier::tier == Witness::FORMALLY_VERIFIED);

static_assert(static_cast<Witness>(witness::UnwitnessedTier::element_type{}) == Witness::UNWITNESSED);
static_assert(static_cast<Witness>(witness::TypeCheckedTier::element_type{}) == Witness::TYPE_CHECKED);
static_assert(static_cast<Witness>(witness::TestPassedTier::element_type{}) == Witness::TEST_PASSED);
static_assert(static_cast<Witness>(witness::FormallyVerifiedTier::element_type{}) == Witness::FORMALLY_VERIFIED);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using FormallyVerifiedGraded = Graded<ModalityKind::Comonad, witness::FormallyVerifiedTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, double);

template <typename T>
using TestPassedGraded = Graded<ModalityKind::Comonad, witness::TestPassedTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestPassedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestPassedGraded, EightByteValue);

template <typename T>
using UnwitnessedGraded = Graded<ModalityKind::Comonad, witness::UnwitnessedTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitnessedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitnessedGraded, EightByteValue);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    Witness a = Witness::UNWITNESSED;
    Witness b = Witness::FORMALLY_VERIFIED;
    [[maybe_unused]] bool l1 = WitnessLattice::leq(a, b);
    [[maybe_unused]] Witness j1 = WitnessLattice::join(a, b);
    [[maybe_unused]] Witness m1 = WitnessLattice::meet(a, b);
    [[maybe_unused]] Witness bot = WitnessLattice::bottom();
    [[maybe_unused]] Witness top = WitnessLattice::top();

    Witness mid = Witness::TEST_PASSED;
    [[maybe_unused]] Witness j2 = WitnessLattice::join(mid, b);
    [[maybe_unused]] Witness m2 = WitnessLattice::meet(mid, a);

    OneByteValue v{42};
    FormallyVerifiedGraded<OneByteValue> initial{v, witness::FormallyVerifiedTier::bottom()};
    auto widened = initial.weaken(witness::FormallyVerifiedTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(witness::FormallyVerifiedTier::top());

    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    witness::FormallyVerifiedTier::element_type e{};
    [[maybe_unused]] Witness recovered = e;
}

}  // namespace detail::witness_lattice_self_test

}  // namespace crucible::algebra::lattices
