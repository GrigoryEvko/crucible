#pragma once

// Two-element confidentiality chain, Public below Secret.  A join raises
// the classification of mixed operands and a meet lowers it.
//
// Lowering by meet is not declassification.  Information leaves a
// classified wrapper only through the comonadic counit, which every call
// site names, and never by weakening the grade.
//
// A classified value always sits at the top position, because an
// unclassified value is a plain value rather than a wrapped one.  That
// is what lets the fixed-position sub-lattice carry an empty grade and
// collapse to the size of the payload.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class Conf : std::int8_t {
    Public = 0,
    Secret = 1,
};

inline constexpr std::size_t conf_count = std::meta::enumerators_of(^^Conf).size();

[[nodiscard]] consteval std::string_view conf_name(Conf c) noexcept {
    switch (c) {
        case Conf::Public:
            return "Public";
        case Conf::Secret:
            return "Secret";
        default:
            return std::string_view{"<unknown Conf>"};
    }
}

struct ConfLattice {
    using element_type = Conf;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return Conf::Public; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Conf::Secret; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return std::to_underlying(a) <= std::to_underlying(b);
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return leq(a, b) ? b : a;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "ConfLattice"; }

    template <Conf C>
    struct At {
        struct element_type {
            using conf_value_type = Conf;
            [[nodiscard]] constexpr operator conf_value_type() const noexcept { return C; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr Conf classification = C;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (C) {
                case Conf::Public:
                    return "ConfLattice::At<Public>";
                case Conf::Secret:
                    return "ConfLattice::At<Secret>";
                default:
                    return "ConfLattice::At<?>";
            }
        }
    };
};

namespace conf {
using PublicTier = ConfLattice::At<Conf::Public>;
using SecretTier = ConfLattice::At<Conf::Secret>;
}  // namespace conf

namespace detail::conf_lattice_self_test {

static_assert(conf_count == 2, "Conf must hold exactly the two levels Public and Secret.");

[[nodiscard]] consteval bool every_conf_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Conf));
    // `template for` unrolls into successive scopes that each declare the
    // induction variable, so -Wshadow fires on the body.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (conf_name([:en:]) == std::string_view{"<unknown Conf>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_conf_has_name(), "conf_name() has no arm for at least one level, so that level reports "
                                     "the '<unknown Conf>' sentinel.");

static_assert(Lattice<ConfLattice>);
static_assert(BoundedLattice<ConfLattice>);
static_assert(Lattice<ConfLattice::At<Conf::Public>>);
static_assert(Lattice<ConfLattice::At<Conf::Secret>>);
static_assert(BoundedLattice<ConfLattice::At<Conf::Secret>>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<ConfLattice::At<Conf::Public>::element_type>);
static_assert(std::is_empty_v<ConfLattice::At<Conf::Secret>::element_type>);

[[nodiscard]] consteval bool exhaustive_lattice_check() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Conf));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_bounded_lattice_axioms_at<ConfLattice>([:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(exhaustive_lattice_check(), "ConfLattice's chain-order lattice axioms must hold at every "
                                          "(Conf)³ triple.");

static_assert(ConfLattice::leq(Conf::Public, Conf::Secret), "Public ⊑ Secret in the confidentiality chain.");
static_assert(!ConfLattice::leq(Conf::Secret, Conf::Public),
              "Secret ⋢ Public — declassification doesn't go via lattice "
              "weakening, only via the named declassify counit.");
static_assert(ConfLattice::join(Conf::Public, Conf::Secret) == Conf::Secret,
              "Joining mixed classifications raises to the higher one.");
static_assert(ConfLattice::meet(Conf::Public, Conf::Secret) == Conf::Public,
              "Meeting mixed classifications lowers to the lower one.");

static_assert(ConfLattice::name() == "ConfLattice");
static_assert(ConfLattice::At<Conf::Public>::name() == "ConfLattice::At<Public>");
static_assert(ConfLattice::At<Conf::Secret>::name() == "ConfLattice::At<Secret>");
static_assert(conf_name(Conf::Public) == "Public");
static_assert(conf_name(Conf::Secret) == "Secret");

[[nodiscard]] consteval bool every_at_conf_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Conf));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ConfLattice::At<([:en:])>::name() == std::string_view{"ConfLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_conf_has_name(), "ConfLattice::At<C>::name() has no arm for at least one level, so "
                                        "that level reports the 'ConfLattice::At<?>' sentinel.");

static_assert(conf::PublicTier::classification == Conf::Public);
static_assert(conf::SecretTier::classification == Conf::Secret);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using SecretGraded = Graded<ModalityKind::Comonad, conf::SecretTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, EightByteValue);
// The arithmetic witnesses pin the collapse across the
// trivially-default-constructible split as well as the class one.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, double);

inline void runtime_smoke_test() {
    Conf a = Conf::Public;
    Conf b = Conf::Secret;
    [[maybe_unused]] bool l1 = ConfLattice::leq(a, b);
    [[maybe_unused]] Conf j1 = ConfLattice::join(a, b);
    [[maybe_unused]] Conf m1 = ConfLattice::meet(a, b);
    [[maybe_unused]] Conf bot = ConfLattice::bottom();
    [[maybe_unused]] Conf top = ConfLattice::top();

    OneByteValue v{42};
    SecretGraded<OneByteValue> initial{v, conf::SecretTier::bottom()};
    auto widened = initial.weaken(conf::SecretTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(conf::SecretTier::top());

    // extract() is reachable only because the modality is Comonad.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    conf::SecretTier::element_type e{};
    [[maybe_unused]] Conf c = e;
}

}  // namespace detail::conf_lattice_self_test

}  // namespace crucible::algebra::lattices
