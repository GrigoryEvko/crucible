#pragma once

// ── crucible::algebra::lattices::RecipeFamilyLattice ────────────────
//
// PARTIAL-ORDER lattice over a numerical-recipe FAMILY identifier
// — the algorithmic strategy used to compute a floating-point
// reduction (LINEAR / PAIRWISE / KAHAN / BLOCK_STABLE).  Two
// sentinel positions: None (bottom — unbound) and Any (top —
// wildcard).  Specific family values sit between bottom and top
// as MUTUALLY INCOMPARABLE siblings.
//
// Same partial-order shape as VendorLattice (FOUND-G53) and
// NumaNodeLattice (FOUND-G71); recipe families are CATEGORIES with
// no inherent ordering — a producer using KAHAN is NOT semantically
// "stronger" than a producer using PAIRWISE; they're fundamentally
// different algorithms.
//
// One of two component sub-lattices for the RecipeSpec product
// wrapper from 28_04_2026_effects.md §4.4.4 (FOUND-G75).
//
// Citation: NumericalRecipe.h; MIMIC.md §41 (cross-vendor numerics
// CI tolerance enforcement); FORGE.md §J.6 (Phase E.RecipeSelect).
//
// THE LOAD-BEARING USE CASE: Forge Phase E.RecipeSelect emits a
// kernel pinned at a specific recipe family.  Cross-vendor numerics
// CI compares two backends' outputs only when their recipe families
// agree (or when both are Any-wildcards admitting any family).
// A producer tagged with RecipeFamily::Kahan is admitted at a
// consumer requesting RecipeFamily::Kahan or RecipeFamily::Any —
// rejected at a consumer requesting RecipeFamily::Pairwise.
//
// ── The catalog ────────────────────────────────────────────────────
//
//     Linear      — Naive linear summation (a₀ + a₁ + ... + aₙ).
//                   Lowest numerical stability; fastest to execute;
//                   used for inference paths where ε > 10⁻³ is OK.
//     Pairwise    — Divide-and-conquer pairwise summation.  ε grows
//                   as O(log n) instead of O(n); standard reduction
//                   for Forge's PAIRWISE NumericalRecipe.
//     Kahan       — Kahan compensated summation.  ε grows as O(1)
//                   independent of n; used for high-stakes
//                   accumulator paths (loss surfaces, optimizer
//                   moment buffers).
//     BlockStable — Block-wise stable algorithms (e.g., Higham's
//                   recursive block reduction).  Best stability +
//                   amenable to SIMD blocking; the Forge BITEXACT
//                   recipe family default.
//
//     None  = 254  bottom: unbound — value claims no specific recipe.
//                  Used for sentinel state pre-RecipeSelect and for
//                  recipe-agnostic data (constants, configuration).
//     Any   = 255  top: wildcard — admits any recipe family.  Used
//                  for cross-vendor CI tolerance bands that don't
//                  pin a specific recipe, and for future extension
//                  points where new recipes haven't been added yet.
//
// ── Algebraic shape (partial-order) ────────────────────────────────
//
// Carrier:  RecipeFamily = strong scoped enum : uint8_t.
// Order:    leq(None, anything)            = true   (bottom under all)
//           leq(anything, Any)              = true   (everything under top)
//           leq(FamA, FamA)                 = true   (reflexive)
//           leq(FamA, FamB) when A != B     = false  (siblings)
// Bottom:   RecipeFamily::None
// Top:      RecipeFamily::Any
// Join:     leq-aware least-upper-bound (siblings → Any wildcard)
// Meet:     leq-aware greatest-lower-bound (siblings → None bottom)
//
// NON-distributive — same M3 substructure as NumaNodeLattice.  The
// Lattice concept does NOT require distributivity, so the partial
// order still satisfies BoundedLattice; downstream production code
// uses pointwise join/meet/leq with no distributivity-dependent
// simplifications.
//
//   Axiom coverage:
//     TypeSafe — RecipeFamily is a strong scoped enum; mixing with
//                Tolerance (the sister axis) is a compile error.
//     DetSafe — leq / join / meet are all `constexpr`.
//   Runtime cost:
//     element_type = RecipeFamily = 1 byte.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── RecipeFamily — strong scoped enum over numerical recipes ──────
enum class RecipeFamily : std::uint8_t {
    Linear      = 0,    // naive linear sum
    Pairwise    = 1,    // pairwise summation
    Kahan       = 2,    // Kahan compensated summation
    BlockStable = 3,    // block-wise stable algorithms
    // 4..253 reserved for future recipes
    None        = 254,  // bottom: unbound
    Any         = 255,  // top: wildcard
};

inline constexpr std::size_t recipe_family_count =
    std::meta::enumerators_of(^^RecipeFamily).size();

[[nodiscard]] consteval std::string_view recipe_family_name(
    RecipeFamily f) noexcept {
    switch (f) {
        case RecipeFamily::Linear:      return "Linear";
        case RecipeFamily::Pairwise:    return "Pairwise";
        case RecipeFamily::Kahan:       return "Kahan";
        case RecipeFamily::BlockStable: return "BlockStable";
        case RecipeFamily::None:        return "None";
        case RecipeFamily::Any:         return "Any";
        default:                        return std::string_view{
            "<unknown RecipeFamily>"};
    }
}

// ── RecipeFamilyLattice — partial-order with wildcard top ─────────
struct RecipeFamilyLattice {
    using element_type = RecipeFamily;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return RecipeFamily::None;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return RecipeFamily::Any;
    }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == RecipeFamily::None) return true;
        if (b == RecipeFamily::Any)  return true;
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

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "RecipeFamilyLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::recipe_family_lattice_self_test {

static_assert(Lattice<RecipeFamilyLattice>);
static_assert(BoundedLattice<RecipeFamilyLattice>);
static_assert(!UnboundedLattice<RecipeFamilyLattice>);
static_assert(!Semiring<RecipeFamilyLattice>);

static_assert(sizeof(RecipeFamily) == 1);
static_assert(std::is_trivially_copyable_v<RecipeFamily>);

// Reflection-driven name coverage — every enumerator must have a
// switch arm.
[[nodiscard]] consteval bool every_recipe_family_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^RecipeFamily));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (recipe_family_name([:en:]) ==
            std::string_view{"<unknown RecipeFamily>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_recipe_family_has_name(),
    "recipe_family_name() switch missing arm for at least one family.");

// Bounds.
static_assert(RecipeFamilyLattice::bottom() == RecipeFamily::None);
static_assert(RecipeFamilyLattice::top()    == RecipeFamily::Any);

// Reflexivity.
static_assert(RecipeFamilyLattice::leq(RecipeFamily::Linear,      RecipeFamily::Linear));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::BlockStable, RecipeFamily::BlockStable));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::None,        RecipeFamily::None));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::Any,         RecipeFamily::Any));

// Bottom under everything.
static_assert(RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Linear));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Kahan));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::None, RecipeFamily::Any));

// Everything under top.
static_assert(RecipeFamilyLattice::leq(RecipeFamily::Linear,      RecipeFamily::Any));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::Pairwise,    RecipeFamily::Any));
static_assert(RecipeFamilyLattice::leq(RecipeFamily::BlockStable, RecipeFamily::Any));

// Sibling rejection — load-bearing for partial-order discipline.
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Linear,   RecipeFamily::Pairwise));
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Pairwise, RecipeFamily::Linear));
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Kahan,    RecipeFamily::BlockStable));
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Linear,   RecipeFamily::Kahan));

// Join witnesses.
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::Kahan)
              == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::None)
              == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::Any)
              == RecipeFamily::Any);
static_assert(RecipeFamilyLattice::join(RecipeFamily::Linear, RecipeFamily::Pairwise)
              == RecipeFamily::Any);     // siblings → top
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::BlockStable)
              == RecipeFamily::Any);     // siblings → top

// Meet witnesses.
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::Kahan)
              == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::Any)
              == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::None)
              == RecipeFamily::None);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Linear, RecipeFamily::Pairwise)
              == RecipeFamily::None);    // siblings → bottom

// Idempotence.
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamily::Kahan)
              == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamily::Kahan)
              == RecipeFamily::Kahan);

// Bound identities.
static_assert(RecipeFamilyLattice::join(RecipeFamily::Kahan, RecipeFamilyLattice::bottom())
              == RecipeFamily::Kahan);
static_assert(RecipeFamilyLattice::meet(RecipeFamily::Kahan, RecipeFamilyLattice::top())
              == RecipeFamily::Kahan);

// Antisymmetry — siblings reject in both directions.
static_assert(!RecipeFamilyLattice::leq(RecipeFamily::Linear,   RecipeFamily::Pairwise)
           && !RecipeFamilyLattice::leq(RecipeFamily::Pairwise, RecipeFamily::Linear));

// Transitivity.
[[nodiscard]] consteval bool transitivity_witness() noexcept {
    return  RecipeFamilyLattice::leq(RecipeFamily::None,    RecipeFamily::Kahan)
        &&  RecipeFamilyLattice::leq(RecipeFamily::Kahan,   RecipeFamily::Any)
        &&  RecipeFamilyLattice::leq(RecipeFamily::None,    RecipeFamily::Any);
}
static_assert(transitivity_witness());

// Associativity.
[[nodiscard]] consteval bool associativity_witness() noexcept {
    auto check = [](RecipeFamily a, RecipeFamily b, RecipeFamily c) {
        auto lhs_join = RecipeFamilyLattice::join(RecipeFamilyLattice::join(a, b), c);
        auto rhs_join = RecipeFamilyLattice::join(a, RecipeFamilyLattice::join(b, c));
        auto lhs_meet = RecipeFamilyLattice::meet(RecipeFamilyLattice::meet(a, b), c);
        auto rhs_meet = RecipeFamilyLattice::meet(a, RecipeFamilyLattice::meet(b, c));
        return lhs_join == rhs_join && lhs_meet == rhs_meet;
    };
    return  check(RecipeFamily::Linear,   RecipeFamily::Pairwise, RecipeFamily::Kahan)
         && check(RecipeFamily::None,     RecipeFamily::Kahan,    RecipeFamily::Any)
         && check(RecipeFamily::Kahan,    RecipeFamily::Any,      RecipeFamily::None);
}
static_assert(associativity_witness());

// Absorption.
[[nodiscard]] consteval bool absorption_witness() noexcept {
    auto check = [](RecipeFamily a, RecipeFamily b) {
        return RecipeFamilyLattice::join(a, RecipeFamilyLattice::meet(a, b)) == a
            && RecipeFamilyLattice::meet(a, RecipeFamilyLattice::join(a, b)) == a;
    };
    return  check(RecipeFamily::Linear,      RecipeFamily::Pairwise)
         && check(RecipeFamily::None,        RecipeFamily::Any)
         && check(RecipeFamily::Kahan,       RecipeFamily::BlockStable);
}
static_assert(absorption_witness());

// Non-distributivity (M3 substructure — three siblings sharing top
// + bottom).
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    RecipeFamily a = RecipeFamily::Linear;
    RecipeFamily b = RecipeFamily::Pairwise;
    RecipeFamily c = RecipeFamily::Kahan;
    auto lhs = RecipeFamilyLattice::meet(a, RecipeFamilyLattice::join(b, c));
    auto rhs = RecipeFamilyLattice::join(RecipeFamilyLattice::meet(a, b),
                                         RecipeFamilyLattice::meet(a, c));
    return lhs == RecipeFamily::Linear
        && rhs == RecipeFamily::None
        && lhs != rhs;
}
static_assert(non_distributive_witness(),
    "RecipeFamilyLattice's non-distributivity is a STRUCTURAL CLAIM "
    "(M3 substructure — same as NumaNodeLattice).");

inline void runtime_smoke_test() {
    RecipeFamily              bot   = RecipeFamilyLattice::bottom();
    RecipeFamily              topv  = RecipeFamilyLattice::top();
    RecipeFamily              kahan = RecipeFamily::Kahan;
    [[maybe_unused]] bool     l     = RecipeFamilyLattice::leq(bot, topv);
    [[maybe_unused]] auto     j     = RecipeFamilyLattice::join(kahan, topv);
    [[maybe_unused]] auto     m     = RecipeFamilyLattice::meet(kahan, bot);

    // Sibling join → top wildcard.
    auto sib_join = RecipeFamilyLattice::join(RecipeFamily::Linear,
                                               RecipeFamily::Pairwise);
    if (sib_join != RecipeFamily::Any) std::abort();

    auto sib_meet = RecipeFamilyLattice::meet(RecipeFamily::Linear,
                                               RecipeFamily::Pairwise);
    if (sib_meet != RecipeFamily::None) std::abort();

    // Lattice over Graded substrate.
    using RecipeGraded = Graded<ModalityKind::Absolute, RecipeFamilyLattice, int>;
    RecipeGraded              v{42, RecipeFamily::Kahan};
    [[maybe_unused]] auto     g  = v.grade();
    [[maybe_unused]] auto     vp = v.peek();
}

}  // namespace detail::recipe_family_lattice_self_test

}  // namespace crucible::algebra::lattices
