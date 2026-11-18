#pragma once

// Partial order over the algorithm a floating-point reduction uses: the
// FlatLattice of RecipeFamily.
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
#include <foundation/algebra/lattices/FlatLattice.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
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

// A family that covers more requested families is the stronger claim.  Any
// covers all of them and is the strongest.
struct RecipeFamilyLattice : FlatLattice<RecipeFamily, RecipeFamily::None, RecipeFamily::Any> {
    [[nodiscard]] static consteval std::string_view name() noexcept { return "RecipeFamilyLattice"; }
};

}  // namespace foundation::algebra::lattices
