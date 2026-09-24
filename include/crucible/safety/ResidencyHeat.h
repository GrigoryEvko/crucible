#pragma once

// ResidencyHeat<Tier, T> pins a value to the cache level it is
// resident in.
//
// The tiers form a chain from the slowest access to the fastest.
// Cold is the last level or main memory, Warm is the mid level, and
// Hot is the level nearest the core.
//
// satisfies<Required> asks whether the pinned tier covers what a
// consumer demands: stronger satisfies weaker.  A Hot value is
// admissible wherever Warm is required, because evicting it outward
// is always possible.  A Cold value is not admissible where Hot is
// required, because the consumer would pay a miss it did not budget
// for.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a Hot value is to build one where
// the bytes really are resident at that level.  The substrate's
// weaken(), which does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::ResidencyHeatLattice;
using ResidencyHeatTag_v = ::crucible::algebra::lattices::ResidencyHeatTag;

template <ResidencyHeatTag_v Tier, typename T>
class [[nodiscard]] ResidencyHeat {
public:
    using value_type = T;
    using lattice_type = ResidencyHeatLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr ResidencyHeatTag_v tier = Tier;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a level it was never
    // placed in.  Deleting it would be the truthful choice, but it is
    // kept so the wrapper can sit in an array element or a
    // default-initialized struct field.  A site that knows where the
    // bytes are resident uses the explicit constructor.
    constexpr ResidencyHeat() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit ResidencyHeat(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ResidencyHeat(std::in_place_t,
                                     Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                              && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr ResidencyHeat(const ResidencyHeat&) = default;
    constexpr ResidencyHeat(ResidencyHeat&&) = default;
    constexpr ResidencyHeat& operator=(const ResidencyHeat&) = default;
    constexpr ResidencyHeat& operator=(ResidencyHeat&&) = default;
    ~ResidencyHeat() = default;

    [[nodiscard]] friend constexpr bool operator==(ResidencyHeat const& a,
                                                   ResidencyHeat const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(ResidencyHeat& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(ResidencyHeat& a, ResidencyHeat& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <ResidencyHeatTag_v RequiredTier>
    static constexpr bool satisfies = ResidencyHeatLattice::leq(RequiredTier, Tier);

    template <ResidencyHeatTag_v WeakerTier>
        requires(ResidencyHeatLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr ResidencyHeat<WeakerTier, T>
    relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return ResidencyHeat<WeakerTier, T>{this->peek()};
    }

    template <ResidencyHeatTag_v WeakerTier>
        requires(ResidencyHeatLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr ResidencyHeat<WeakerTier, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return ResidencyHeat<WeakerTier, T>{std::move(impl_).consume()};
    }
};

namespace residency_heat {
template <typename T>
using Hot = ResidencyHeat<ResidencyHeatTag_v::Hot, T>;
template <typename T>
using Warm = ResidencyHeat<ResidencyHeatTag_v::Warm, T>;
template <typename T>
using Cold = ResidencyHeat<ResidencyHeatTag_v::Cold, T>;
}  // namespace residency_heat

namespace detail::residency_heat_layout {

template <typename T>
using HotR = ResidencyHeat<ResidencyHeatTag_v::Hot, T>;
template <typename T>
using WarmR = ResidencyHeat<ResidencyHeatTag_v::Warm, T>;
template <typename T>
using ColdR = ResidencyHeat<ResidencyHeatTag_v::Cold, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotR, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotR, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotR, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmR, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmR, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdR, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdR, double);

}  // namespace detail::residency_heat_layout

static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Hot, int>) == sizeof(int));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Warm, int>) == sizeof(int));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Cold, int>) == sizeof(int));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Hot, double>) == sizeof(double));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Warm, double>) == sizeof(double));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Cold, double>) == sizeof(double));

namespace detail::residency_heat_self_test {

using HotInt = ResidencyHeat<ResidencyHeatTag_v::Hot, int>;
using WarmInt = ResidencyHeat<ResidencyHeatTag_v::Warm, int>;
using ColdInt = ResidencyHeat<ResidencyHeatTag_v::Cold, int>;

inline constexpr HotInt h_default{};
static_assert(h_default.peek() == 0);
static_assert(h_default.tier == ResidencyHeatTag_v::Hot);

inline constexpr HotInt h_explicit{42};
static_assert(h_explicit.peek() == 42);

inline constexpr HotInt h_in_place{std::in_place, 7};
static_assert(h_in_place.peek() == 7);

static_assert(HotInt::tier == ResidencyHeatTag_v::Hot);
static_assert(WarmInt::tier == ResidencyHeatTag_v::Warm);
static_assert(ColdInt::tier == ResidencyHeatTag_v::Cold);

static_assert(HotInt::satisfies<ResidencyHeatTag_v::Hot>);
static_assert(HotInt::satisfies<ResidencyHeatTag_v::Warm>);
static_assert(HotInt::satisfies<ResidencyHeatTag_v::Cold>);

static_assert(WarmInt::satisfies<ResidencyHeatTag_v::Warm>);
static_assert(WarmInt::satisfies<ResidencyHeatTag_v::Cold>);
static_assert(!WarmInt::satisfies<ResidencyHeatTag_v::Hot>,
              "Warm must not satisfy Hot.  A value resident one level out, "
              "passed where the nearest level is required, turns every access "
              "into a miss the caller did not budget for.");

static_assert(ColdInt::satisfies<ResidencyHeatTag_v::Cold>);
static_assert(!ColdInt::satisfies<ResidencyHeatTag_v::Warm>);
static_assert(!ColdInt::satisfies<ResidencyHeatTag_v::Hot>);

inline constexpr auto from_hot_to_warm = HotInt{42}.relax<ResidencyHeatTag_v::Warm>();
static_assert(from_hot_to_warm.peek() == 42);
static_assert(from_hot_to_warm.tier == ResidencyHeatTag_v::Warm);

inline constexpr auto from_hot_to_cold = HotInt{99}.relax<ResidencyHeatTag_v::Cold>();
static_assert(from_hot_to_cold.peek() == 99);
static_assert(from_hot_to_cold.tier == ResidencyHeatTag_v::Cold);

inline constexpr auto from_warm_to_cold = WarmInt{7}.relax<ResidencyHeatTag_v::Cold>();
static_assert(from_warm_to_cold.peek() == 7);

inline constexpr auto from_warm_to_self = WarmInt{8}.relax<ResidencyHeatTag_v::Warm>();
static_assert(from_warm_to_self.peek() == 8);

template <typename W, ResidencyHeatTag_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<HotInt, ResidencyHeatTag_v::Warm>);
static_assert(can_relax<HotInt, ResidencyHeatTag_v::Cold>);
static_assert(can_relax<HotInt, ResidencyHeatTag_v::Hot>);
static_assert(can_relax<WarmInt, ResidencyHeatTag_v::Cold>);
static_assert(can_relax<WarmInt, ResidencyHeatTag_v::Warm>);
static_assert(!can_relax<WarmInt, ResidencyHeatTag_v::Hot>,
              "relax<Hot> on a Warm value must be rejected.  It would claim a "
              "residency the value does not have, and every consumer would "
              "then plan for an access it never gets.");
static_assert(!can_relax<ColdInt, ResidencyHeatTag_v::Warm>);
static_assert(!can_relax<ColdInt, ResidencyHeatTag_v::Hot>);
static_assert(can_relax<ColdInt, ResidencyHeatTag_v::Cold>);

static_assert(HotInt::value_type_name().ends_with("int"));
static_assert(HotInt::lattice_name() == "ResidencyHeatLattice::At<Hot>");
static_assert(WarmInt::lattice_name() == "ResidencyHeatLattice::At<Warm>");
static_assert(ColdInt::lattice_name() == "ResidencyHeatLattice::At<Cold>");

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
static_assert(!can_equality_compare<ResidencyHeat<ResidencyHeatTag_v::Hot, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<ResidencyHeat<ResidencyHeatTag_v::Hot, NoEqualityT>>,
              "ResidencyHeat<Tier, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<ResidencyHeat<ResidencyHeatTag_v::Hot, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    HotInt a{99};
    auto b = a.relax<ResidencyHeatTag_v::Hot>();
    return b.peek() == 99 && b.tier == ResidencyHeatTag_v::Hot;
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

template <typename W, ResidencyHeatTag_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, ResidencyHeatTag_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using HotMoveOnly = ResidencyHeat<ResidencyHeatTag_v::Hot, MoveOnlyT>;
static_assert(can_relax_rvalue<HotMoveOnly, ResidencyHeatTag_v::Warm>,
              "relax on an rvalue must accept a move-only T.  The rvalue "
              "overload moves through consume().");
static_assert(!can_relax_lvalue<HotMoveOnly, ResidencyHeatTag_v::Warm>,
              "relax on a const lvalue must reject a move-only T.  That "
              "overload requires a copy constructor.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    HotMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<ResidencyHeatTag_v::Warm>();
    return dst.peek().v == 77 && dst.tier == ResidencyHeatTag_v::Warm;
}
static_assert(relax_move_only_works());

static_assert(HotInt::value_type_name().size() > 0);
static_assert(HotInt::lattice_name().size() > 0);
static_assert(HotInt::lattice_name().starts_with("ResidencyHeatLattice::At<"));

static_assert(residency_heat::Hot<int>::tier == ResidencyHeatTag_v::Hot);
static_assert(residency_heat::Warm<int>::tier == ResidencyHeatTag_v::Warm);
static_assert(residency_heat::Cold<int>::tier == ResidencyHeatTag_v::Cold);

static_assert(std::is_same_v<residency_heat::Hot<double>, ResidencyHeat<ResidencyHeatTag_v::Hot, double>>);

// A lookup whose budget assumes no miss admits Hot only.
template <typename W>
concept is_l1_admissible = W::template satisfies<ResidencyHeatTag_v::Hot>;

static_assert(is_l1_admissible<HotInt>, "A Hot value must pass a gate that requires Hot.");
static_assert(!is_l1_admissible<WarmInt>, "A Warm value must not pass a gate that requires Hot.  The "
                                          "lookup would then miss on every call and blow the budget the "
                                          "caller planned around.");
static_assert(!is_l1_admissible<ColdInt>, "A Cold value must not pass a gate that requires Hot.");

// A lookup that tolerates one level of spill admits Warm and stronger.
template <typename W>
concept is_warm_lookup_admissible = W::template satisfies<ResidencyHeatTag_v::Warm>;

static_assert(is_warm_lookup_admissible<HotInt>, "A Hot value must pass a gate that requires Warm.");
static_assert(is_warm_lookup_admissible<WarmInt>, "A Warm value must pass a gate that requires Warm.");
static_assert(!is_warm_lookup_admissible<ColdInt>, "A Cold value must not pass a gate that requires Warm.  Cold "
                                                   "sits below Warm, so admitting it turns a bounded lookup into "
                                                   "an unbounded one.");

inline void runtime_smoke_test() {
    HotInt a{};
    HotInt b{42};
    HotInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (HotInt::tier != ResidencyHeatTag_v::Hot) {
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
    auto relaxed_copy = source.relax<ResidencyHeatTag_v::Warm>();
    auto relaxed_move = std::move(source).relax<ResidencyHeatTag_v::Cold>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = HotInt::satisfies<ResidencyHeatTag_v::Warm>;
    [[maybe_unused]] bool s2 = WarmInt::satisfies<ResidencyHeatTag_v::Hot>;

    HotInt eq_a{42};
    HotInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    HotInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    residency_heat::Hot<int> alias_hot{123};
    residency_heat::Warm<int> alias_warm{456};
    residency_heat::Cold<int> alias_cold{789};
    [[maybe_unused]] auto av = alias_hot.peek();
    [[maybe_unused]] auto wv = alias_warm.peek();
    [[maybe_unused]] auto cv = alias_cold.peek();

    [[maybe_unused]] bool can_l1 = is_l1_admissible<HotInt>;
    [[maybe_unused]] bool can_warm = is_warm_lookup_admissible<HotInt>;
}

}  // namespace detail::residency_heat_self_test

}  // namespace crucible::safety
