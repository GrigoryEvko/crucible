#pragma once

// HotPath<Tier, T> pins a value to the operation budget of the code
// that produced it.
//
// The tiers form a chain from the most permissive budget to the most
// restrictive.  Cold may block and do input or output.  Warm may
// allocate.  Hot may do none of those.  Higher in the chain means
// less is permitted.
//
// satisfies<Required> asks whether the pinned tier covers what a
// consumer demands: stricter satisfies looser.  A Hot value is
// admissible wherever Warm is required, because code that never
// allocates has already met the weaker budget.  The converse does not
// hold.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a Hot value is to build one at a
// site that really stayed inside the budget.  The substrate's
// weaken(), which does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/HotPathLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::HotPathLattice;
using HotPathTier_v = ::crucible::algebra::lattices::HotPathTier;

template <HotPathTier_v Tier, typename T>
class [[nodiscard]] HotPath {
public:
    using value_type = T;
    using lattice_type = HotPathLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr HotPathTier_v tier = Tier;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a budget no code kept.
    // Deleting it would be the truthful choice, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.  A site that stayed inside the budget uses the
    // explicit constructor.
    constexpr HotPath() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit HotPath(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit HotPath(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                         && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr HotPath(const HotPath&) = default;
    constexpr HotPath(HotPath&&) = default;
    constexpr HotPath& operator=(const HotPath&) = default;
    constexpr HotPath& operator=(HotPath&&) = default;
    ~HotPath() = default;

    [[nodiscard]] friend constexpr bool operator==(HotPath const& a,
                                                   HotPath const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
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

    constexpr void swap(HotPath& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(HotPath& a, HotPath& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <HotPathTier_v RequiredTier>
    static constexpr bool satisfies = HotPathLattice::leq(RequiredTier, Tier);

    template <HotPathTier_v WeakerTier>
        requires(HotPathLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr HotPath<WeakerTier, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return HotPath<WeakerTier, T>{this->peek()};
    }

    template <HotPathTier_v WeakerTier>
        requires(HotPathLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr HotPath<WeakerTier, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HotPath<WeakerTier, T>{std::move(impl_).consume()};
    }
};

namespace hot_path {
template <typename T>
using Hot = HotPath<HotPathTier_v::Hot, T>;
template <typename T>
using Warm = HotPath<HotPathTier_v::Warm, T>;
template <typename T>
using Cold = HotPath<HotPathTier_v::Cold, T>;
}  // namespace hot_path

namespace detail::hot_path_layout {

template <typename T>
using HotH = HotPath<HotPathTier_v::Hot, T>;
template <typename T>
using WarmH = HotPath<HotPathTier_v::Warm, T>;
template <typename T>
using ColdH = HotPath<HotPathTier_v::Cold, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotH, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotH, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotH, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmH, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmH, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdH, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdH, double);

}  // namespace detail::hot_path_layout

static_assert(sizeof(HotPath<HotPathTier_v::Hot, int>) == sizeof(int));
static_assert(sizeof(HotPath<HotPathTier_v::Warm, int>) == sizeof(int));
static_assert(sizeof(HotPath<HotPathTier_v::Cold, int>) == sizeof(int));
static_assert(sizeof(HotPath<HotPathTier_v::Hot, double>) == sizeof(double));
static_assert(sizeof(HotPath<HotPathTier_v::Warm, double>) == sizeof(double));
static_assert(sizeof(HotPath<HotPathTier_v::Cold, double>) == sizeof(double));

namespace detail::hot_path_self_test {

using HotInt = HotPath<HotPathTier_v::Hot, int>;
using WarmInt = HotPath<HotPathTier_v::Warm, int>;
using ColdInt = HotPath<HotPathTier_v::Cold, int>;

inline constexpr HotInt h_default{};
static_assert(h_default.peek() == 0);
static_assert(h_default.tier == HotPathTier_v::Hot);

inline constexpr HotInt h_explicit{42};
static_assert(h_explicit.peek() == 42);

inline constexpr HotInt h_in_place{std::in_place, 7};
static_assert(h_in_place.peek() == 7);

static_assert(HotInt::tier == HotPathTier_v::Hot);
static_assert(WarmInt::tier == HotPathTier_v::Warm);
static_assert(ColdInt::tier == HotPathTier_v::Cold);

static_assert(HotInt::satisfies<HotPathTier_v::Hot>);
static_assert(HotInt::satisfies<HotPathTier_v::Warm>);
static_assert(HotInt::satisfies<HotPathTier_v::Cold>);

static_assert(WarmInt::satisfies<HotPathTier_v::Warm>);
static_assert(WarmInt::satisfies<HotPathTier_v::Cold>);
static_assert(!WarmInt::satisfies<HotPathTier_v::Hot>, "Warm must not satisfy Hot.  Code that may allocate must not "
                                                       "reach a site whose budget forbids it.");

static_assert(ColdInt::satisfies<HotPathTier_v::Cold>);
static_assert(!ColdInt::satisfies<HotPathTier_v::Warm>);
static_assert(!ColdInt::satisfies<HotPathTier_v::Hot>);

inline constexpr auto from_hot_to_warm = HotInt{42}.relax<HotPathTier_v::Warm>();
static_assert(from_hot_to_warm.peek() == 42);
static_assert(from_hot_to_warm.tier == HotPathTier_v::Warm);

inline constexpr auto from_hot_to_cold = HotInt{99}.relax<HotPathTier_v::Cold>();
static_assert(from_hot_to_cold.peek() == 99);
static_assert(from_hot_to_cold.tier == HotPathTier_v::Cold);

inline constexpr auto from_warm_to_cold = WarmInt{7}.relax<HotPathTier_v::Cold>();
static_assert(from_warm_to_cold.peek() == 7);

inline constexpr auto from_warm_to_self = WarmInt{8}.relax<HotPathTier_v::Warm>();
static_assert(from_warm_to_self.peek() == 8);

template <typename W, HotPathTier_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<HotInt, HotPathTier_v::Warm>);
static_assert(can_relax<HotInt, HotPathTier_v::Cold>);
static_assert(can_relax<HotInt, HotPathTier_v::Hot>);
static_assert(can_relax<WarmInt, HotPathTier_v::Cold>);
static_assert(can_relax<WarmInt, HotPathTier_v::Warm>);
static_assert(!can_relax<WarmInt, HotPathTier_v::Hot>, "relax<Hot> on a Warm value must be rejected.  It would claim a "
                                                       "budget the code did not keep.");
static_assert(!can_relax<ColdInt, HotPathTier_v::Warm>);
static_assert(!can_relax<ColdInt, HotPathTier_v::Hot>);
static_assert(can_relax<ColdInt, HotPathTier_v::Cold>);

static_assert(HotInt::value_type_name().ends_with("int"));
static_assert(HotInt::lattice_name() == "HotPathLattice::At<Hot>");
static_assert(WarmInt::lattice_name() == "HotPathLattice::At<Warm>");
static_assert(ColdInt::lattice_name() == "HotPathLattice::At<Cold>");

[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    HotInt a{10};
    HotInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    HotInt a{10};
    HotInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    HotInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    HotInt a{42};
    HotInt b{42};
    HotInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

struct NoEqualityT {
    int v{0};
    NoEqualityT() = default;
    explicit NoEqualityT(int x) : v{x} {}
    NoEqualityT(NoEqualityT&&) = default;
    NoEqualityT& operator=(NoEqualityT&&) = default;
    NoEqualityT(NoEqualityT const&) = delete;
    NoEqualityT& operator=(NoEqualityT const&) = delete;
};

template <typename W>
concept can_equality_compare = requires(W const& a, W const& b) {
    { a == b } -> std::convertible_to<bool>;
};

static_assert(can_equality_compare<HotInt>);
static_assert(!can_equality_compare<HotPath<HotPathTier_v::Hot, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<HotPath<HotPathTier_v::Hot, NoEqualityT>>,
              "HotPath<Tier, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<HotPath<HotPathTier_v::Hot, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    HotInt a{99};
    auto b = a.relax<HotPathTier_v::Hot>();
    return b.peek() == 99 && b.tier == HotPathTier_v::Hot;
}
static_assert(relax_to_self_is_identity());

struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, HotPathTier_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, HotPathTier_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using HotMoveOnly = HotPath<HotPathTier_v::Hot, MoveOnlyT>;
static_assert(can_relax_rvalue<HotMoveOnly, HotPathTier_v::Warm>,
              "relax on an rvalue must accept a move-only T.  The rvalue "
              "overload moves through consume().");
static_assert(!can_relax_lvalue<HotMoveOnly, HotPathTier_v::Warm>,
              "relax on a const lvalue must reject a move-only T.  That "
              "overload requires a copy constructor.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    HotMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<HotPathTier_v::Warm>();
    return dst.peek().v == 77 && dst.tier == HotPathTier_v::Warm;
}
static_assert(relax_move_only_works());

static_assert(HotInt::value_type_name().size() > 0);
static_assert(HotInt::lattice_name().size() > 0);
static_assert(HotInt::lattice_name().starts_with("HotPathLattice::At<"));

static_assert(hot_path::Hot<int>::tier == HotPathTier_v::Hot);
static_assert(hot_path::Warm<int>::tier == HotPathTier_v::Warm);
static_assert(hot_path::Cold<int>::tier == HotPathTier_v::Cold);

static_assert(std::is_same_v<hot_path::Hot<double>, HotPath<HotPathTier_v::Hot, double>>);

// A foreground site admits Hot only.
template <typename W>
concept is_hot_path_admissible = W::template satisfies<HotPathTier_v::Hot>;

static_assert(is_hot_path_admissible<HotInt>, "A Hot value must pass a gate that requires Hot.");
static_assert(!is_hot_path_admissible<WarmInt>, "A Warm value must not pass a gate that requires Hot.  "
                                                "Background work would otherwise run in the foreground.");
static_assert(!is_hot_path_admissible<ColdInt>, "A Cold value must not pass a gate that requires Hot.");

inline void runtime_smoke_test() {
    HotInt a{};
    HotInt b{42};
    HotInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (HotInt::tier != HotPathTier_v::Hot) {
        std::abort();
    }

    HotInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    HotInt sx{1};
    HotInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    HotInt source{77};
    auto relaxed_copy = source.relax<HotPathTier_v::Warm>();
    auto relaxed_move = std::move(source).relax<HotPathTier_v::Cold>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = HotInt::satisfies<HotPathTier_v::Warm>;
    [[maybe_unused]] bool s2 = WarmInt::satisfies<HotPathTier_v::Hot>;

    HotInt eq_a{42};
    HotInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    HotInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    hot_path::Hot<int> alias_hot{123};
    hot_path::Warm<int> alias_warm{456};
    hot_path::Cold<int> alias_cold{789};
    [[maybe_unused]] auto av = alias_hot.peek();
    [[maybe_unused]] auto wv = alias_warm.peek();
    [[maybe_unused]] auto cv = alias_cold.peek();

    [[maybe_unused]] bool can_hot_pass = is_hot_path_admissible<HotInt>;
    [[maybe_unused]] bool can_warm_pass = is_hot_path_admissible<WarmInt>;
    [[maybe_unused]] bool can_cold_pass = is_hot_path_admissible<ColdInt>;
}

}  // namespace detail::hot_path_self_test

}  // namespace crucible::safety
