#pragma once

// ── crucible::safety::RecipeSpec<T> ─────────────────────────────────
//
// Per-instance numerical-recipe specification wrapper.  A value of
// type T paired with TWO independent recipe-axis grades — Tolerance
// (chain — RELAXED ⊑ ULP_INT8 ⊑ ... ⊑ BITEXACT) + RecipeFamily
// (partial-order — Linear / Pairwise / Kahan / BlockStable as
// sibling categories with wildcard top) — composed via the binary
// product lattice:
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ProductLattice<ToleranceLattice, RecipeFamilyLattice>,
//                     T>
//   Regime:    4 (per-instance grade with TWO non-empty fields,
//                 2 bytes of grade carried per instance — the
//                 FOURTH and FINAL product-lattice wrapper from
//                 28_04 §4.4.  Last of the four-wrapper Month-3
//                 product-wrapper batch; closes the Budgeted /
//                 EpochVersioned / NumaPlacement / RecipeSpec
//                 catalog from the §4.6 master table.)
//
// Citation: NumericalRecipe.h; MIMIC.md §41 (cross-vendor numerics
// CI); FORGE.md §J.6 (Phase E.RecipeSelect); 28_04_2026_effects.md
// §4.4.4 (FOUND-G75/G76).
//
// THE LOAD-BEARING USE CASE: Forge Phase E.RecipeSelect emits each
// kernel pinned at a (tolerance_tier, recipe_family) pair declaring
// the numerical strategy.  Cross-vendor numerics CI compares two
// backends' outputs at the recipe-pinned tolerance band; the
// dispatcher's recipe-aware fast path admits values only when their
// claimed (tolerance, family) subsumes the consumer's requirement.
//
// ── Algebraic asymmetry — chain × partial-order ────────────────────
//
// Like NumaPlacement (FOUND-G71), RecipeSpec composes two
// algebraically-different component lattices: ToleranceLattice is a
// CHAIN (total order: RELAXED ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑
// ULP_FP32 ⊑ ULP_FP64 ⊑ BITEXACT) while RecipeFamilyLattice is a
// PARTIAL-ORDER (siblings incomparable, wildcards None / Any).  The
// product satisfies BoundedLattice but is non-distributive
// (inherits non-distributivity from RecipeFamilyLattice's M3
// substructure).
//
// ── One composition operation, one admission gate ──────────────────
//
//   .combine_max(other)   — pointwise lattice JOIN.  For Tolerance:
//                           the more-strict (BITEXACT-ward) of two
//                           tolerance tiers.  For RecipeFamily:
//                           same-family preserved; sibling-family
//                           promotes to Any wildcard.
//
//   .admits(req_tier, req_family)
//                         — runtime admission gate; pointwise leq
//                           on both axes.  Production: Forge Phase
//                           E.RecipeSelect dispatch.
//
// ── No relax / no accumulate ───────────────────────────────────────
//
// Same as Budgeted / EpochVersioned / NumaPlacement: grade is
// runtime data; no `relax<>` template.  No `accumulate` either —
// recipes / tolerance tiers don't compose additively.
//
//   Axiom coverage:
//     TypeSafe — Tolerance and RecipeFamily are strong scoped enums;
//                mixing the axes is a compile error.
//     DetSafe — every operation is constexpr.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//   Runtime cost:
//     sizeof(RecipeSpec<T>) >= sizeof(T) + 2 bytes (1+1) plus
//     alignment padding.  Smallest grade footprint of the four
//     product wrappers (Budgeted = 16, EpochVersioned = 16,
//     NumaPlacement = 40, RecipeSpec = 2).
//
// See FOUND-G75 (algebra/lattices/RecipeFamilyLattice.h) for the
// underlying recipe-family lattice; the existing ToleranceLattice
// (ALGEBRA-14) for the tolerance chain; safety/Budgeted.h, safety/
// EpochVersioned.h, safety/NumaPlacement.h for the sister product
// wrappers; NumericalRecipe.h for the production registry.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ProductLattice.h>
#include <crucible/algebra/lattices/RecipeFamilyLattice.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>

#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the two component types into safety:: under canonical names.
using ::crucible::algebra::lattices::RecipeFamily;
using ::crucible::algebra::lattices::RecipeFamilyLattice;
using ::crucible::algebra::lattices::Tolerance;
using ::crucible::algebra::lattices::ToleranceLattice;

// ── Cross-axis disjointness — load-bearing for axis-swap fence ────
//
// Tolerance and RecipeFamily are both uint8_t-backed strong scoped
// enums.  Distinct C++ types — assertion is defensive but ships
// alongside the sister product wrappers' identical fences for
// pattern uniformity.
static_assert(!std::is_same_v<Tolerance, RecipeFamily>,
    "Tolerance and RecipeFamily must be structurally distinct C++ "
    "types.  If this fires, the strong-newtype discipline that "
    "fences RecipeSpec axis-swap bugs has been broken.");

template <typename T>
class [[nodiscard]] RecipeSpec {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<
        ToleranceLattice, RecipeFamilyLattice>;
    using spec_t       = typename lattice_type::element_type;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr spec_t pack(Tolerance tol, RecipeFamily fam) noexcept {
        return spec_t{tol, fam};
    }

public:
    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at (tolerance=RELAXED, family=None).  RELAXED is
    // the chain bottom (no error bound) and None is the partial-order
    // bottom (unbound family) — together the most permissive (= least
    // committed) starting position.
    constexpr RecipeSpec() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, lattice_type::bottom()} {}

    // Explicit construction from value + both spec axes.
    constexpr RecipeSpec(T value, Tolerance tier, RecipeFamily fam)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(tier, fam)} {}

    // In-place T construction with explicit spec pair.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr RecipeSpec(std::in_place_t, Tolerance tier, RecipeFamily fam, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(tier, fam)} {}

    // Convenience factory: BITEXACT + BlockStable — the most
    // restrictive recipe (both axes at top).  Production:
    // canonical Forge Phase E.RecipeSelect output for cross-vendor
    // bit-identical reduction kernels.
    [[nodiscard]] static constexpr RecipeSpec bitexact_block_stable(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return RecipeSpec{std::move(value), Tolerance::BITEXACT,
                          RecipeFamily::BlockStable};
    }

    // Convenience factory: any-tier + Any-family wildcard.  Used for
    // recipe-agnostic data.
    [[nodiscard]] static constexpr RecipeSpec wildcard(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return RecipeSpec{std::move(value), Tolerance::BITEXACT,
                          RecipeFamily::Any};
    }

    // Defaulted copy/move/destroy.
    constexpr RecipeSpec(const RecipeSpec&)            = default;
    constexpr RecipeSpec(RecipeSpec&&)                 = default;
    constexpr RecipeSpec& operator=(const RecipeSpec&) = default;
    constexpr RecipeSpec& operator=(RecipeSpec&&)      = default;
    ~RecipeSpec()                                      = default;

    [[nodiscard]] friend constexpr bool operator==(
        RecipeSpec const& a, RecipeSpec const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek()           == b.peek()
            && a.tolerance()      == b.tolerance()
            && a.recipe_family()  == b.recipe_family();
    }

    // ── Diagnostic names ────────────────────────────────────────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── Per-axis accessors ──────────────────────────────────────────
    [[nodiscard]] constexpr Tolerance tolerance() const noexcept {
        return impl_.grade().first;
    }

    [[nodiscard]] constexpr RecipeFamily recipe_family() const noexcept {
        return impl_.grade().second;
    }

    [[nodiscard]] constexpr spec_t spec() const noexcept {
        return impl_.grade();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(RecipeSpec& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(RecipeSpec& a, RecipeSpec& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── combine_max — pointwise lattice JOIN ───────────────────────
    [[nodiscard]] constexpr RecipeSpec combine_max(RecipeSpec const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return RecipeSpec{
            this->peek(),
            ToleranceLattice::join(this->tolerance(),       other.tolerance()),
            RecipeFamilyLattice::join(this->recipe_family(), other.recipe_family())
        };
    }

    [[nodiscard]] constexpr RecipeSpec combine_max(RecipeSpec const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        Tolerance    joined_tier = ToleranceLattice::join(this->tolerance(),       other.tolerance());
        RecipeFamily joined_fam  = RecipeFamilyLattice::join(this->recipe_family(), other.recipe_family());
        return RecipeSpec{std::move(impl_).consume(), joined_tier, joined_fam};
    }

    // ── admits — runtime admission gate ────────────────────────────
    //
    // Returns true iff this spec's claim SUBSUMES the requested
    // (tier, family) — i.e., the consumer's request fits inside
    // the value's spec.
    //
    // Production usage at Forge Phase E.RecipeSelect:
    //
    //   if (!kernel_output.admits(consumer_req_tier, consumer_req_family))
    //       return reject_recipe_mismatch();
    [[nodiscard]] constexpr bool admits(Tolerance    req_tier,
                                        RecipeFamily req_family) const noexcept
    {
        return ToleranceLattice::leq(req_tier,     this->tolerance())
            && RecipeFamilyLattice::leq(req_family, this->recipe_family());
    }
};

// ── Layout invariants — regime-4 (non-EBO) ──────────────────────────
//
// RecipeSpec<T> carries spec_t = (Tolerance, RecipeFamily) = 1 + 1
// = 2 bytes nominally.  Aggregate alignment of two uint8_t enums
// gives 2 bytes flat (no padding between them); aggregate alignment
// for the wider value_type then dominates the wrapper layout.
namespace detail::recipe_spec_layout {

static_assert(sizeof(RecipeSpec<int>)       >= sizeof(int)    + 2);
static_assert(sizeof(RecipeSpec<double>)    >= sizeof(double) + 2);
static_assert(sizeof(RecipeSpec<char>)      >= sizeof(char)   + 2);

}  // namespace detail::recipe_spec_layout

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::recipe_spec_self_test {

using RecipeSpecInt = RecipeSpec<int>;
using RecipeSpecDbl = RecipeSpec<double>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr RecipeSpecInt s_default{};
static_assert(s_default.peek()          == 0);
static_assert(s_default.tolerance()     == Tolerance::RELAXED);
static_assert(s_default.recipe_family() == RecipeFamily::None);

inline constexpr RecipeSpecInt s_explicit{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
static_assert(s_explicit.peek()          == 42);
static_assert(s_explicit.tolerance()     == Tolerance::ULP_FP16);
static_assert(s_explicit.recipe_family() == RecipeFamily::Kahan);

inline constexpr RecipeSpecInt s_in_place{
    std::in_place, Tolerance::BITEXACT, RecipeFamily::BlockStable, 7};
static_assert(s_in_place.peek()          == 7);
static_assert(s_in_place.tolerance()     == Tolerance::BITEXACT);

// ── Convenience factories ─────────────────────────────────────────
inline constexpr RecipeSpecInt s_bitexact = RecipeSpecInt::bitexact_block_stable(99);
static_assert(s_bitexact.tolerance()     == Tolerance::BITEXACT);
static_assert(s_bitexact.recipe_family() == RecipeFamily::BlockStable);

inline constexpr RecipeSpecInt s_wildcard = RecipeSpecInt::wildcard(11);
static_assert(s_wildcard.tolerance()     == Tolerance::BITEXACT);
static_assert(s_wildcard.recipe_family() == RecipeFamily::Any);

// ── combine_max — lattice join semantics ──────────────────────────
//
// Same-family same-tier preserved.
[[nodiscard]] consteval bool combine_max_same_axis() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    auto          c = a.combine_max(b);
    return c.tolerance()     == Tolerance::ULP_FP16
        && c.recipe_family() == RecipeFamily::Kahan;
}
static_assert(combine_max_same_axis());

// Tier promotes to higher (more strict).  Family preserved when
// same.
[[nodiscard]] consteval bool combine_max_tier_promotes() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{42, Tolerance::BITEXACT, RecipeFamily::Kahan};
    auto          c = a.combine_max(b);
    return c.tolerance()     == Tolerance::BITEXACT       // max tier
        && c.recipe_family() == RecipeFamily::Kahan;      // same family
}
static_assert(combine_max_tier_promotes());

// Sibling families → Any wildcard.
[[nodiscard]] consteval bool combine_max_sibling_families() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Linear};
    RecipeSpecInt b{42, Tolerance::ULP_FP16, RecipeFamily::Pairwise};
    auto          c = a.combine_max(b);
    return c.tolerance()     == Tolerance::ULP_FP16
        && c.recipe_family() == RecipeFamily::Any;        // siblings → top
}
static_assert(combine_max_sibling_families());

// Idempotent.
[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    RecipeSpecInt a{42, Tolerance::BITEXACT, RecipeFamily::Kahan};
    auto          c = a.combine_max(a);
    return c.tolerance()     == Tolerance::BITEXACT
        && c.recipe_family() == RecipeFamily::Kahan;
}
static_assert(combine_max_idempotent());

// ── admits — admission gate semantics ─────────────────────────────
[[nodiscard]] consteval bool admits_within_threshold() noexcept {
    RecipeSpecInt v{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    return  v.admits(Tolerance::ULP_FP16,  RecipeFamily::Kahan)        // exact
        &&  v.admits(Tolerance::ULP_FP8,   RecipeFamily::Kahan)        // tier weaker
        &&  v.admits(Tolerance::ULP_FP16,  RecipeFamily::None)         // family bottom
        && !v.admits(Tolerance::BITEXACT,  RecipeFamily::Kahan)        // tier too strict
        && !v.admits(Tolerance::ULP_FP16,  RecipeFamily::Pairwise);    // wrong family
}
static_assert(admits_within_threshold());

// Wildcard admits any (specific_tier ≤ BITEXACT, specific_family).
static_assert(RecipeSpecInt::wildcard(7).admits(
    Tolerance::ULP_FP32, RecipeFamily::Kahan));

// Default (RELAXED, None) admits only (RELAXED, None).
static_assert(RecipeSpecInt{}.admits(Tolerance::RELAXED, RecipeFamily::None));
static_assert(!RecipeSpecInt{}.admits(Tolerance::ULP_FP16, RecipeFamily::Kahan));

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(RecipeSpecInt::value_type_name().ends_with("int"));
static_assert(RecipeSpecInt::lattice_name().size() > 0);

// ── swap exchanges T values within the same lattice pin ──────────
template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    W b{y, Tolerance::BITEXACT, RecipeFamily::Pairwise};
    a.swap(b);
    return a.peek()          == y
        && b.peek()          == x
        && a.tolerance()     == Tolerance::BITEXACT
        && b.recipe_family() == RecipeFamily::Kahan;
}
static_assert(swap_exchanges_within<RecipeSpecInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    RecipeSpecInt a{10, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{20, Tolerance::BITEXACT, RecipeFamily::Pairwise};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10
        && a.tolerance()     == Tolerance::BITEXACT
        && b.recipe_family() == RecipeFamily::Kahan;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place T mutation ──────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    RecipeSpecInt a{10, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    a.peek_mut() = 99;
    return a.peek() == 99 && a.tolerance() == Tolerance::ULP_FP16;
}
static_assert(peek_mut_works());

// ── operator== — same-lattice, same-T comparison ─────────────────
[[nodiscard]] consteval bool equality_compares_value_and_spec() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt c{43, Tolerance::ULP_FP16, RecipeFamily::Kahan};   // diff value
    RecipeSpecInt d{42, Tolerance::ULP_FP32, RecipeFamily::Kahan};   // diff tier
    RecipeSpecInt e{42, Tolerance::ULP_FP16, RecipeFamily::Pairwise}; // diff family
    return  (a == b)
        && !(a == c)
        && !(a == d)
        && !(a == e);
}
static_assert(equality_compares_value_and_spec());

// ── Move-only T support ──────────────────────────────────────────
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

static_assert(!std::is_copy_constructible_v<RecipeSpec<MoveOnlyT>>);
static_assert(std::is_move_constructible_v<RecipeSpec<MoveOnlyT>>);

// combine_max && rvalue overload for move-only T.
[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    RecipeSpec<MoveOnlyT> a{MoveOnlyT{42}, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpec<MoveOnlyT> b{MoveOnlyT{99}, Tolerance::BITEXACT, RecipeFamily::Kahan};
    auto                  c = std::move(a).combine_max(b);
    return c.tolerance()     == Tolerance::BITEXACT
        && c.recipe_family() == RecipeFamily::Kahan
        && c.peek().v        == 42;
}
static_assert(combine_max_works_for_move_only());

// SFINAE detectors.
template <typename W>
concept can_combine_max_lvalue = requires(W const& a, W const& b) {
    { a.combine_max(b) };
};
template <typename W>
concept can_combine_max_rvalue = requires(W&& a, W const& b) {
    { std::move(a).combine_max(b) };
};
static_assert( can_combine_max_lvalue<RecipeSpecInt>);
static_assert( can_combine_max_rvalue<RecipeSpecInt>);
static_assert(!can_combine_max_lvalue<RecipeSpec<MoveOnlyT>>);
static_assert( can_combine_max_rvalue<RecipeSpec<MoveOnlyT>>);

// ── Stable-name introspection ────────────────────────────────────
static_assert(RecipeSpecInt::value_type_name().size() > 0);
static_assert(RecipeSpecInt::lattice_name().size()    > 0);

// ── Runtime smoke test ────────────────────────────────────────────
inline void runtime_smoke_test() {
    RecipeSpecInt a{};
    RecipeSpecInt b{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt c{std::in_place, Tolerance::BITEXACT, RecipeFamily::BlockStable, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();
    [[maybe_unused]] auto tb = b.tolerance();
    [[maybe_unused]] auto fb = b.recipe_family();

    RecipeSpecInt bx = RecipeSpecInt::bitexact_block_stable(99);
    if (bx.tolerance() != Tolerance::BITEXACT) std::abort();

    RecipeSpecInt wc = RecipeSpecInt::wildcard(11);
    if (wc.recipe_family() != RecipeFamily::Any) std::abort();

    RecipeSpecInt mutable_b{10, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    mutable_b.peek_mut() = 99;
    if (mutable_b.peek() != 99) std::abort();

    RecipeSpecInt sx{1, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt sy{2, Tolerance::BITEXACT, RecipeFamily::Pairwise};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // combine_max with sibling families promotes to Any.
    RecipeSpecInt left {42, Tolerance::ULP_FP16, RecipeFamily::Linear};
    RecipeSpecInt right{42, Tolerance::ULP_FP16, RecipeFamily::Pairwise};
    auto          joined = left.combine_max(right);
    if (joined.recipe_family() != RecipeFamily::Any) std::abort();

    // admits.
    RecipeSpecInt task{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    if (!task.admits(Tolerance::ULP_FP8,  RecipeFamily::Kahan)) std::abort();
    if ( task.admits(Tolerance::BITEXACT, RecipeFamily::Kahan)) std::abort();

    // operator==.
    RecipeSpecInt eq_a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt eq_b{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    if (!(eq_a == eq_b)) std::abort();

    // spec() returns ProductElement.
    [[maybe_unused]] auto pair = b.spec();
    if (pair.first  != Tolerance::ULP_FP16)  std::abort();
    if (pair.second != RecipeFamily::Kahan)  std::abort();

    RecipeSpecInt orig{55, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::recipe_spec_self_test

}  // namespace crucible::safety
