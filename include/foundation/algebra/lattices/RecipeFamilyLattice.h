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

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

enum class RecipeFamily : std::uint8_t {
    Linear = 0,  // naive linear sum; error grows with the term count
    Pairwise = 1,  // divide-and-conquer sum; error grows with its logarithm
    Kahan = 2,  // compensated sum; error stays bounded in the term count
    BlockStable = 3,  // block-wise stable sum; blocks map onto SIMD lanes
    // 4..253 reserved for future recipes
    None = 254,  // bottom: unbound
    Any = 255,  // top: wildcard
};

inline constexpr std::size_t recipe_family_count = ::foundation::reflect::enum_count<RecipeFamily>;

static_assert(recipe_family_count == 6, "RecipeFamily must hold exactly six enumerators. A new family needs "
                                        "a place in leq, join and meet, and an update to this count.");

// The identifier of f, or "<unknown RecipeFamily>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view recipe_family_name(RecipeFamily f) noexcept {
    return ::foundation::reflect::enum_name(f);
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
static_assert(recipe_family_name(RecipeFamily::BlockStable) == "BlockStable");
static_assert(recipe_family_name(static_cast<RecipeFamily>(7)) == "<unknown RecipeFamily>");

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

}  // namespace foundation::algebra::lattices
