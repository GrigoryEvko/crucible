#pragma once

// Partial order over the algorithm a floating-point reduction uses.
//
// The families are categories, not ranks.  Compensated summation is not
// a stronger form of pairwise summation, it is a different algorithm, so
// two named families are siblings: neither covers the other, their join
// is the wildcard and their meet is None.  Only the two sentinels are
// ordered against everything.  None claims no family, Any claims all of
// them.
//
// Three siblings sharing one top and one bottom are the classical M3
// lattice, which is not distributive.  The bounded-lattice laws still
// hold, so no consumer may lean on distributivity to simplify a
// join-of-meets.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class RecipeFamily : std::uint8_t {
    Linear = 0,  // naive linear sum; error grows with the term count
    Pairwise = 1,  // divide-and-conquer sum; error grows with its logarithm
    Kahan = 2,  // compensated sum; error stays bounded in the term count
    BlockStable = 3,  // block-wise stable sum; blocks map onto SIMD lanes
    // 4..253 reserved for future recipes
    None = 254,  // bottom: unbound
    Any = 255,  // top: wildcard
};

inline constexpr std::size_t recipe_family_count = std::meta::enumerators_of(^^RecipeFamily).size();

static_assert(recipe_family_count == 6, "RecipeFamily must hold exactly six enumerators. A new family needs "
                                        "an arm in recipe_family_name, a place in leq, join and meet, and an "
                                        "update to this count.");

[[nodiscard]] consteval std::string_view recipe_family_name(RecipeFamily f) noexcept {
    switch (f) {
        case RecipeFamily::Linear:
            return "Linear";
        case RecipeFamily::Pairwise:
            return "Pairwise";
        case RecipeFamily::Kahan:
            return "Kahan";
        case RecipeFamily::BlockStable:
            return "BlockStable";
        case RecipeFamily::None:
            return "None";
        case RecipeFamily::Any:
            return "Any";
        default:
            return std::string_view{"<unknown RecipeFamily>"};
    }
}

struct RecipeFamilyLattice {
    using element_type = RecipeFamily;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return RecipeFamily::None; }
    [[nodiscard]] static constexpr element_type top() noexcept { return RecipeFamily::Any; }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == RecipeFamily::None) return true;
        if (b == RecipeFamily::Any) return true;
        return false;
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (leq(a, b)) return b;
        if (leq(b, a)) return a;
        return RecipeFamily::Any;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (leq(a, b)) return a;
        if (leq(b, a)) return b;
        return RecipeFamily::None;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "RecipeFamilyLattice"; }
};

namespace detail::recipe_family_lattice_self_test {

static_assert(Lattice<RecipeFamilyLattice>);
static_assert(BoundedLattice<RecipeFamilyLattice>);
static_assert(!UnboundedLattice<RecipeFamilyLattice>);
static_assert(!Semiring<RecipeFamilyLattice>);

static_assert(sizeof(RecipeFamily) == 1);
static_assert(std::is_trivially_copyable_v<RecipeFamily>);

[[nodiscard]] consteval bool every_recipe_family_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^RecipeFamily));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (recipe_family_name([:en:]) == std::string_view{"<unknown RecipeFamily>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_recipe_family_has_name(), "recipe_family_name() has no arm for at least one family, so "
                                              "that family reports the '<unknown RecipeFamily>' sentinel.");

static_assert(RecipeFamilyLattice::bottom() == RecipeFamily::None);
static_assert(RecipeFamilyLattice::top() == RecipeFamily::Any);

static_assert(RecipeFamilyLattice::leq(RecipeFamily::Linear, RecipeFamily::Linear));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::BlockStable, RecipeFamily::BlockStable));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::None));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::Any, RecipeFamily::Any));

static_assert(RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Linear));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Kahan));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Any));

static_assert(RecipeFamilyLattice::leq(RecipeFamily::Linear, RecipeFamily::Any));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::Pairwise, RecipeFamily::Any));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::BlockStable, RecipeFamily::Any));

static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Linear, RecipeFamily::Pairwise));
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Pairwise, RecipeFamily::Linear));
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Kahan, RecipeFamily::BlockStable));
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Linear, RecipeFamily::Kahan));

static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::Kahan) == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::None) == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::Any) == RecipeFamily::Any);
static_assert(RecipeFamilyLattice::join(RecipeFamily::Linear, RecipeFamily::Pairwise) == RecipeFamily::Any);
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::BlockStable) == RecipeFamily::Any);

static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::Kahan) == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::Any) == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::None) == RecipeFamily::None);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Linear, RecipeFamily::Pairwise) == RecipeFamily::None);

static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::Kahan) == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::Kahan) == RecipeFamily::Kahan);

static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamilyLattice::bottom()) == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamilyLattice::top()) == RecipeFamily::Kahan);

static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Linear, RecipeFamily::Pairwise)
              && !RecipeFamilyLattice::leq(RecipeFamily::Pairwise, RecipeFamily::Linear));

[[nodiscard]] consteval bool transitivity_witness() noexcept {
    return RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Kahan)
        && RecipeFamilyLattice::leq(RecipeFamily::Kahan, RecipeFamily::Any)
        && RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Any);
}
static_assert(transitivity_witness());

[[nodiscard]] consteval bool associativity_witness() noexcept {
    auto check = [](RecipeFamily a, RecipeFamily b, RecipeFamily c) {
        auto lhs_join = RecipeFamilyLattice::join(RecipeFamilyLattice::join(a, b), c);
        auto rhs_join = RecipeFamilyLattice::join(a, RecipeFamilyLattice::join(b, c));
        auto lhs_meet = RecipeFamilyLattice::meet(RecipeFamilyLattice::meet(a, b), c);
        auto rhs_meet = RecipeFamilyLattice::meet(a, RecipeFamilyLattice::meet(b, c));
        return lhs_join == rhs_join && lhs_meet == rhs_meet;
    };
    return check(RecipeFamily::Linear, RecipeFamily::Pairwise, RecipeFamily::Kahan)
        && check(RecipeFamily::None, RecipeFamily::Kahan, RecipeFamily::Any)
        && check(RecipeFamily::Kahan, RecipeFamily::Any, RecipeFamily::None);
}
static_assert(associativity_witness());

[[nodiscard]] consteval bool absorption_witness() noexcept {
    auto check = [](RecipeFamily a, RecipeFamily b) {
        return RecipeFamilyLattice::join(a, RecipeFamilyLattice::meet(a, b)) == a
            && RecipeFamilyLattice::meet(a, RecipeFamilyLattice::join(a, b)) == a;
    };
    return check(RecipeFamily::Linear, RecipeFamily::Pairwise) && check(RecipeFamily::None, RecipeFamily::Any)
        && check(RecipeFamily::Kahan, RecipeFamily::BlockStable);
}
static_assert(absorption_witness());

[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    RecipeFamily a = RecipeFamily::Linear;
    RecipeFamily b = RecipeFamily::Pairwise;
    RecipeFamily c = RecipeFamily::Kahan;
    auto lhs = RecipeFamilyLattice::meet(a, RecipeFamilyLattice::join(b, c));
    auto rhs = RecipeFamilyLattice::join(RecipeFamilyLattice::meet(a, b), RecipeFamilyLattice::meet(a, c));
    return lhs == RecipeFamily::Linear && rhs == RecipeFamily::None && lhs != rhs;
}
static_assert(non_distributive_witness(), "RecipeFamilyLattice must keep its M3 shape. If this fires, join or "
                                          "meet changed and the partial order is no longer a set of siblings "
                                          "between one bottom and one top.");

static_assert(verify_bounded_lattice_axioms_at<RecipeFamilyLattice>(RecipeFamily::None, RecipeFamily::Kahan,
                                                                    RecipeFamily::Any));
static_assert(verify_bounded_lattice_axioms_at<RecipeFamilyLattice>(RecipeFamily::Linear, RecipeFamily::Pairwise,
                                                                    RecipeFamily::Kahan));
static_assert(verify_bounded_lattice_axioms_at<RecipeFamilyLattice>(RecipeFamily::Kahan, RecipeFamily::BlockStable,
                                                                    RecipeFamily::Any));
static_assert(verify_bounded_lattice_axioms_at<RecipeFamilyLattice>(RecipeFamily::None, RecipeFamily::None,
                                                                    RecipeFamily::Any));

inline void runtime_smoke_test() {
    RecipeFamily bot = RecipeFamilyLattice::bottom();
    RecipeFamily topv = RecipeFamilyLattice::top();
    RecipeFamily kahan = RecipeFamily::Kahan;
    [[maybe_unused]] bool l = RecipeFamilyLattice::leq(bot, topv);
    [[maybe_unused]] auto j = RecipeFamilyLattice::join(kahan, topv);
    [[maybe_unused]] auto m = RecipeFamilyLattice::meet(kahan, bot);

    auto sib_join = RecipeFamilyLattice::join(RecipeFamily::Linear, RecipeFamily::Pairwise);
    if (sib_join != RecipeFamily::Any) std::abort();

    auto sib_meet = RecipeFamilyLattice::meet(RecipeFamily::Linear, RecipeFamily::Pairwise);
    if (sib_meet != RecipeFamily::None) std::abort();

    using RecipeGraded = Graded<ModalityKind::Absolute, RecipeFamilyLattice, int>;
    RecipeGraded v{42, RecipeFamily::Kahan};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

}  // namespace detail::recipe_family_lattice_self_test

}  // namespace crucible::algebra::lattices
