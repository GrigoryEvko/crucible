#pragma once

// A value paired with the numerical strategy it was produced under: a
// tolerance tier and a reduction family.
//
// The tiers form a total order from the loosest bound up to bit-exact.
// The families do not: two named families are incomparable siblings
// under a wildcard that stands for all of them.
//
// Neither axis accumulates.  Joining two values keeps the stricter tier
// and, for two different families, the wildcard.

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

using ::crucible::algebra::lattices::RecipeFamily;
using ::crucible::algebra::lattices::RecipeFamilyLattice;
using ::crucible::algebra::lattices::Tolerance;
using ::crucible::algebra::lattices::ToleranceLattice;

static_assert(!std::is_same_v<Tolerance, RecipeFamily>, "Tolerance and RecipeFamily are structurally distinct C++ "
                                                        "types even though both are byte-backed enums.  Collapsing "
                                                        "them removes the fence that stops the two axes being "
                                                        "passed in the wrong order.");

template <typename T>
class [[nodiscard]] RecipeSpec {
public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<ToleranceLattice, RecipeFamilyLattice>;
    using spec_t = typename lattice_type::element_type;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr spec_t pack(Tolerance tol, RecipeFamily fam) noexcept { return spec_t{tol, fam}; }

public:
    // The default sits at the bottom of both axes.  That is the least
    // committed claim, and by the direction of the admission gate it is
    // also the one that admits almost nothing.
    constexpr RecipeSpec() noexcept(std::is_nothrow_default_constructible_v<T>) : impl_{T{}, lattice_type::bottom()} {}

    constexpr RecipeSpec(T value, Tolerance tier, RecipeFamily fam) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(tier, fam)} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr RecipeSpec(std::in_place_t, Tolerance tier, RecipeFamily fam,
                         Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                  && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(tier, fam)} {}

    [[nodiscard]] static constexpr RecipeSpec
    bitexact_block_stable(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return RecipeSpec{std::move(value), Tolerance::BITEXACT, RecipeFamily::BlockStable};
    }

    // The tolerance here is the strictest tier, not the loosest.  The
    // admission gate asks whether a request sits below the claim, so the
    // top of the tier chain is the value that admits every request.
    [[nodiscard]] static constexpr RecipeSpec wildcard(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return RecipeSpec{std::move(value), Tolerance::BITEXACT, RecipeFamily::Any};
    }

    constexpr RecipeSpec(const RecipeSpec&) = default;
    constexpr RecipeSpec(RecipeSpec&&) = default;
    constexpr RecipeSpec& operator=(const RecipeSpec&) = default;
    constexpr RecipeSpec& operator=(RecipeSpec&&) = default;
    ~RecipeSpec() = default;

    [[nodiscard]] friend constexpr bool operator==(RecipeSpec const& a,
                                                   RecipeSpec const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek() && a.tolerance() == b.tolerance() && a.recipe_family() == b.recipe_family();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    [[nodiscard]] constexpr Tolerance tolerance() const noexcept { return impl_.grade().first; }

    [[nodiscard]] constexpr RecipeFamily recipe_family() const noexcept { return impl_.grade().second; }

    [[nodiscard]] constexpr spec_t spec() const noexcept { return impl_.grade(); }

    constexpr void swap(RecipeSpec& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(RecipeSpec& a, RecipeSpec& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // Two different named families have no common named family, so
    // their join is the wildcard rather than a failure.
    [[nodiscard]] constexpr RecipeSpec
    combine_max(RecipeSpec const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return RecipeSpec{this->peek(), ToleranceLattice::join(this->tolerance(), other.tolerance()),
                          RecipeFamilyLattice::join(this->recipe_family(), other.recipe_family())};
    }

    [[nodiscard]] constexpr RecipeSpec
    combine_max(RecipeSpec const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        Tolerance joined_tier = ToleranceLattice::join(this->tolerance(), other.tolerance());
        RecipeFamily joined_fam = RecipeFamilyLattice::join(this->recipe_family(), other.recipe_family());
        return RecipeSpec{std::move(impl_).consume(), joined_tier, joined_fam};
    }

    // The direction is request below claim on each axis: a request is
    // admitted when this value's claim covers it, not the reverse.
    [[nodiscard]] constexpr bool admits(Tolerance req_tier, RecipeFamily req_family) const noexcept {
        return ToleranceLattice::leq(req_tier, this->tolerance())
            && RecipeFamilyLattice::leq(req_family, this->recipe_family());
    }
};

namespace detail::recipe_spec_layout {

static_assert(sizeof(RecipeSpec<int>) >= sizeof(int) + 2);
static_assert(sizeof(RecipeSpec<double>) >= sizeof(double) + 2);
static_assert(sizeof(RecipeSpec<char>) >= sizeof(char) + 2);

}  // namespace detail::recipe_spec_layout

namespace detail::recipe_spec_self_test {

using RecipeSpecInt = RecipeSpec<int>;
using RecipeSpecDbl = RecipeSpec<double>;

inline constexpr RecipeSpecInt s_default{};
static_assert(s_default.peek() == 0);
static_assert(s_default.tolerance() == Tolerance::RELAXED);
static_assert(s_default.recipe_family() == RecipeFamily::None);

inline constexpr RecipeSpecInt s_explicit{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
static_assert(s_explicit.peek() == 42);
static_assert(s_explicit.tolerance() == Tolerance::ULP_FP16);
static_assert(s_explicit.recipe_family() == RecipeFamily::Kahan);

inline constexpr RecipeSpecInt s_in_place{std::in_place, Tolerance::BITEXACT, RecipeFamily::BlockStable, 7};
static_assert(s_in_place.peek() == 7);
static_assert(s_in_place.tolerance() == Tolerance::BITEXACT);

inline constexpr RecipeSpecInt s_bitexact = RecipeSpecInt::bitexact_block_stable(99);
static_assert(s_bitexact.tolerance() == Tolerance::BITEXACT);
static_assert(s_bitexact.recipe_family() == RecipeFamily::BlockStable);

inline constexpr RecipeSpecInt s_wildcard = RecipeSpecInt::wildcard(11);
static_assert(s_wildcard.tolerance() == Tolerance::BITEXACT);
static_assert(s_wildcard.recipe_family() == RecipeFamily::Any);

[[nodiscard]] consteval bool combine_max_same_axis() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    auto c = a.combine_max(b);
    return c.tolerance() == Tolerance::ULP_FP16 && c.recipe_family() == RecipeFamily::Kahan;
}
static_assert(combine_max_same_axis());

[[nodiscard]] consteval bool combine_max_tier_promotes() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{42, Tolerance::BITEXACT, RecipeFamily::Kahan};
    auto c = a.combine_max(b);
    return c.tolerance() == Tolerance::BITEXACT && c.recipe_family() == RecipeFamily::Kahan;
}
static_assert(combine_max_tier_promotes());

[[nodiscard]] consteval bool combine_max_sibling_families() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Linear};
    RecipeSpecInt b{42, Tolerance::ULP_FP16, RecipeFamily::Pairwise};
    auto c = a.combine_max(b);
    return c.tolerance() == Tolerance::ULP_FP16 && c.recipe_family() == RecipeFamily::Any;
}
static_assert(combine_max_sibling_families());

[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    RecipeSpecInt a{42, Tolerance::BITEXACT, RecipeFamily::Kahan};
    auto c = a.combine_max(a);
    return c.tolerance() == Tolerance::BITEXACT && c.recipe_family() == RecipeFamily::Kahan;
}
static_assert(combine_max_idempotent());

[[nodiscard]] consteval bool admits_within_threshold() noexcept {
    RecipeSpecInt v{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    return v.admits(Tolerance::ULP_FP16, RecipeFamily::Kahan) && v.admits(Tolerance::ULP_FP8, RecipeFamily::Kahan)
        && v.admits(Tolerance::ULP_FP16, RecipeFamily::None) && !v.admits(Tolerance::BITEXACT, RecipeFamily::Kahan)
        && !v.admits(Tolerance::ULP_FP16, RecipeFamily::Pairwise);
}
static_assert(admits_within_threshold());

static_assert(RecipeSpecInt::wildcard(7).admits(Tolerance::ULP_FP32, RecipeFamily::Kahan));

static_assert(RecipeSpecInt{}.admits(Tolerance::RELAXED, RecipeFamily::None));
static_assert(!RecipeSpecInt{}.admits(Tolerance::ULP_FP16, RecipeFamily::Kahan));

static_assert(RecipeSpecInt::value_type_name().ends_with("int"));
static_assert(RecipeSpecInt::lattice_name().size() > 0);

template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    W b{y, Tolerance::BITEXACT, RecipeFamily::Pairwise};
    a.swap(b);
    return a.peek() == y && b.peek() == x && a.tolerance() == Tolerance::BITEXACT
        && b.recipe_family() == RecipeFamily::Kahan;
}
static_assert(swap_exchanges_within<RecipeSpecInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    RecipeSpecInt a{10, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{20, Tolerance::BITEXACT, RecipeFamily::Pairwise};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10 && a.tolerance() == Tolerance::BITEXACT
        && b.recipe_family() == RecipeFamily::Kahan;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    RecipeSpecInt a{10, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    a.peek_mut() = 99;
    return a.peek() == 99 && a.tolerance() == Tolerance::ULP_FP16;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_and_spec() noexcept {
    RecipeSpecInt a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt b{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt c{43, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt d{42, Tolerance::ULP_FP32, RecipeFamily::Kahan};
    RecipeSpecInt e{42, Tolerance::ULP_FP16, RecipeFamily::Pairwise};
    return (a == b) && !(a == c) && !(a == d) && !(a == e);
}
static_assert(equality_compares_value_and_spec());

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

[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    RecipeSpec<MoveOnlyT> a{MoveOnlyT{42}, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpec<MoveOnlyT> b{MoveOnlyT{99}, Tolerance::BITEXACT, RecipeFamily::Kahan};
    auto c = std::move(a).combine_max(b);
    return c.tolerance() == Tolerance::BITEXACT && c.recipe_family() == RecipeFamily::Kahan && c.peek().v == 42;
}
static_assert(combine_max_works_for_move_only());

template <typename W>
concept can_combine_max_lvalue = requires(W const& a, W const& b) {
    { a.combine_max(b) };
};
template <typename W>
concept can_combine_max_rvalue = requires(W&& a, W const& b) {
    { std::move(a).combine_max(b) };
};
static_assert(can_combine_max_lvalue<RecipeSpecInt>);
static_assert(can_combine_max_rvalue<RecipeSpecInt>);
static_assert(!can_combine_max_lvalue<RecipeSpec<MoveOnlyT>>);
static_assert(can_combine_max_rvalue<RecipeSpec<MoveOnlyT>>);

static_assert(RecipeSpecInt::value_type_name().size() > 0);
static_assert(RecipeSpecInt::lattice_name().size() > 0);

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

    RecipeSpecInt left{42, Tolerance::ULP_FP16, RecipeFamily::Linear};
    RecipeSpecInt right{42, Tolerance::ULP_FP16, RecipeFamily::Pairwise};
    auto joined = left.combine_max(right);
    if (joined.recipe_family() != RecipeFamily::Any) std::abort();

    RecipeSpecInt task{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    if (!task.admits(Tolerance::ULP_FP8, RecipeFamily::Kahan)) std::abort();
    if (task.admits(Tolerance::BITEXACT, RecipeFamily::Kahan)) std::abort();

    RecipeSpecInt eq_a{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    RecipeSpecInt eq_b{42, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    if (!(eq_a == eq_b)) std::abort();

    [[maybe_unused]] auto pair = b.spec();
    if (pair.first != Tolerance::ULP_FP16) std::abort();
    if (pair.second != RecipeFamily::Kahan) std::abort();

    RecipeSpecInt orig{55, Tolerance::ULP_FP16, RecipeFamily::Kahan};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::recipe_spec_self_test

}  // namespace crucible::safety
