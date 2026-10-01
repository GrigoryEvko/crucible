// The compile-time checks of foundation/algebra/lattices/RecipeFamilyLattice.h.

#include <foundation/algebra/lattices/RecipeFamilyLattice.h>

namespace foundation::algebra::lattices {

static_assert(::foundation::reflect::enum_count<RecipeFamily> == 6,
              "RecipeFamily must hold exactly six enumerators. A new family needs a place in leq, join and meet, "
              "and an update to this count.");

namespace detail::recipe_family_lattice_self_test {

static_assert(Lattice<RecipeFamilyLattice>);
static_assert(BoundedLattice<RecipeFamilyLattice>);
static_assert(!UnboundedLattice<RecipeFamilyLattice>);
static_assert(!Semiring<RecipeFamilyLattice>);

static_assert(sizeof(RecipeFamily) == 1);
static_assert(std::is_trivially_copyable_v<RecipeFamily>);

static_assert(RecipeFamilyLattice::bottom() == RecipeFamily::None);
static_assert(RecipeFamilyLattice::top() == RecipeFamily::Any);

// The bounded-lattice axioms at every triple of the six families, and
// leq, join and meet in agreement at every pair, walked by reflection.
static_assert(verify_enum_lattice_exhaustive<RecipeFamilyLattice>(),
              "The M3 axioms must hold at every triple of the six families.  A "
              "failure means leq, join or meet is wrong for some pair, or the "
              "routing for None, Any, or two distinct named families is wrong.");

// Every pair of distinct named families is a sibling pair: incomparable,
// joined at Any and met at None.
[[nodiscard]] consteval bool named_families_are_siblings() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^RecipeFamily));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            constexpr RecipeFamily a = [:ea:];
            constexpr RecipeFamily b = [:eb:];
            constexpr bool a_named = a != RecipeFamily::None && a != RecipeFamily::Any;
            constexpr bool b_named = b != RecipeFamily::None && b != RecipeFamily::Any;
            if constexpr (a_named && b_named && a != b) {
                if (RecipeFamilyLattice::leq(a, b)) return false;
                if (RecipeFamilyLattice::join(a, b) != RecipeFamily::Any) return false;
                if (RecipeFamilyLattice::meet(a, b) != RecipeFamily::None) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(named_families_are_siblings(), "Two distinct named families must stay incomparable, with Any as "
                                             "their join and None as their meet.");

static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::None) == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::Any) == RecipeFamily::Kahan);

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

static_assert(RecipeFamilyLattice::name() == "RecipeFamilyLattice");

}  // namespace detail::recipe_family_lattice_self_test

}  // namespace foundation::algebra::lattices
