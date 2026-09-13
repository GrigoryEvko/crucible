#pragma once

// CipherTier<Tier, T> pins a value to where it is persisted.
//
// The tiers form a chain from the slowest recovery to the fastest.
// Cold is durable remote storage, Warm is local non-volatile disk,
// and Hot is memory on a peer node.  Re-reading a Hot value touches
// neither a disk nor a remote object store.
//
// satisfies<Required> asks whether the pinned tier covers what a
// consumer demands: stronger satisfies weaker.  A Hot value is
// admissible wherever Warm is required, because writing it down to
// disk is always possible.  A Cold value is not admissible where Hot
// is required, because materializing it means waiting on remote reads
// the consumer does not expect.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a Hot value is to build one where
// the bytes really are resident in memory.  The substrate's weaken(),
// which does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/CipherTierLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::CipherTierLattice;
using CipherTierTag_v = ::crucible::algebra::lattices::CipherTierTag;

template <CipherTierTag_v Tier, typename T>
class [[nodiscard]] CipherTier {
public:
    using value_type = T;
    using lattice_type = CipherTierLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr CipherTierTag_v tier = Tier;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a tier that stores nothing.
    // Deleting it would be the truthful choice, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.  A site that knows where the bytes live uses the
    // explicit constructor.
    constexpr CipherTier() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit CipherTier(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit CipherTier(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                            && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr CipherTier(const CipherTier&) = default;
    constexpr CipherTier(CipherTier&&) = default;
    constexpr CipherTier& operator=(const CipherTier&) = default;
    constexpr CipherTier& operator=(CipherTier&&) = default;
    ~CipherTier() = default;

    [[nodiscard]] friend constexpr bool operator==(CipherTier const& a,
                                                   CipherTier const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(CipherTier& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(CipherTier& a, CipherTier& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <CipherTierTag_v RequiredTier>
    static constexpr bool satisfies = CipherTierLattice::leq(RequiredTier, Tier);

    template <CipherTierTag_v WeakerTier>
        requires(CipherTierLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr CipherTier<WeakerTier, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return CipherTier<WeakerTier, T>{this->peek()};
    }

    template <CipherTierTag_v WeakerTier>
        requires(CipherTierLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr CipherTier<WeakerTier, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return CipherTier<WeakerTier, T>{std::move(impl_).consume()};
    }
};

namespace cipher_tier {
template <typename T>
using Hot = CipherTier<CipherTierTag_v::Hot, T>;
template <typename T>
using Warm = CipherTier<CipherTierTag_v::Warm, T>;
template <typename T>
using Cold = CipherTier<CipherTierTag_v::Cold, T>;
}  // namespace cipher_tier

namespace detail::cipher_tier_layout {

template <typename T>
using HotC = CipherTier<CipherTierTag_v::Hot, T>;
template <typename T>
using WarmC = CipherTier<CipherTierTag_v::Warm, T>;
template <typename T>
using ColdC = CipherTier<CipherTierTag_v::Cold, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotC, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdC, double);

}  // namespace detail::cipher_tier_layout

static_assert(sizeof(CipherTier<CipherTierTag_v::Hot, int>) == sizeof(int));
static_assert(sizeof(CipherTier<CipherTierTag_v::Warm, int>) == sizeof(int));
static_assert(sizeof(CipherTier<CipherTierTag_v::Cold, int>) == sizeof(int));
static_assert(sizeof(CipherTier<CipherTierTag_v::Hot, double>) == sizeof(double));
static_assert(sizeof(CipherTier<CipherTierTag_v::Warm, double>) == sizeof(double));
static_assert(sizeof(CipherTier<CipherTierTag_v::Cold, double>) == sizeof(double));

namespace detail::cipher_tier_self_test {

using HotInt = CipherTier<CipherTierTag_v::Hot, int>;
using WarmInt = CipherTier<CipherTierTag_v::Warm, int>;
using ColdInt = CipherTier<CipherTierTag_v::Cold, int>;

inline constexpr HotInt h_default{};
static_assert(h_default.peek() == 0);
static_assert(h_default.tier == CipherTierTag_v::Hot);

inline constexpr HotInt h_explicit{42};
static_assert(h_explicit.peek() == 42);

inline constexpr HotInt h_in_place{std::in_place, 7};
static_assert(h_in_place.peek() == 7);

static_assert(HotInt::tier == CipherTierTag_v::Hot);
static_assert(WarmInt::tier == CipherTierTag_v::Warm);
static_assert(ColdInt::tier == CipherTierTag_v::Cold);

static_assert(HotInt::satisfies<CipherTierTag_v::Hot>);
static_assert(HotInt::satisfies<CipherTierTag_v::Warm>);
static_assert(HotInt::satisfies<CipherTierTag_v::Cold>);

static_assert(WarmInt::satisfies<CipherTierTag_v::Warm>);
static_assert(WarmInt::satisfies<CipherTierTag_v::Cold>);
static_assert(!WarmInt::satisfies<CipherTierTag_v::Hot>,
              "Warm must not satisfy Hot.  A disk-backed value passed where a "
              "memory-resident one is required turns a recovery read into "
              "blocking disk work.");

static_assert(ColdInt::satisfies<CipherTierTag_v::Cold>);
static_assert(!ColdInt::satisfies<CipherTierTag_v::Warm>);
static_assert(!ColdInt::satisfies<CipherTierTag_v::Hot>);

inline constexpr auto from_hot_to_warm = HotInt{42}.relax<CipherTierTag_v::Warm>();
static_assert(from_hot_to_warm.peek() == 42);
static_assert(from_hot_to_warm.tier == CipherTierTag_v::Warm);

inline constexpr auto from_hot_to_cold = HotInt{99}.relax<CipherTierTag_v::Cold>();
static_assert(from_hot_to_cold.peek() == 99);
static_assert(from_hot_to_cold.tier == CipherTierTag_v::Cold);

inline constexpr auto from_warm_to_cold = WarmInt{7}.relax<CipherTierTag_v::Cold>();
static_assert(from_warm_to_cold.peek() == 7);

inline constexpr auto from_warm_to_self = WarmInt{8}.relax<CipherTierTag_v::Warm>();
static_assert(from_warm_to_self.peek() == 8);

template <typename W, CipherTierTag_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<HotInt, CipherTierTag_v::Warm>);
static_assert(can_relax<HotInt, CipherTierTag_v::Cold>);
static_assert(can_relax<HotInt, CipherTierTag_v::Hot>);
static_assert(can_relax<WarmInt, CipherTierTag_v::Cold>);
static_assert(can_relax<WarmInt, CipherTierTag_v::Warm>);
static_assert(!can_relax<WarmInt, CipherTierTag_v::Hot>,
              "relax<Hot> on a Warm value must be rejected.  It would claim a "
              "residency the value does not have, and a later read would skip "
              "the disk work needed to materialize it.");
static_assert(!can_relax<ColdInt, CipherTierTag_v::Warm>);
static_assert(!can_relax<ColdInt, CipherTierTag_v::Hot>);
static_assert(can_relax<ColdInt, CipherTierTag_v::Cold>);

static_assert(HotInt::value_type_name().ends_with("int"));
static_assert(HotInt::lattice_name() == "CipherTierLattice::At<Hot>");
static_assert(WarmInt::lattice_name() == "CipherTierLattice::At<Warm>");
static_assert(ColdInt::lattice_name() == "CipherTierLattice::At<Cold>");

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
static_assert(!can_equality_compare<CipherTier<CipherTierTag_v::Hot, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<CipherTier<CipherTierTag_v::Hot, NoEqualityT>>,
              "CipherTier<Tier, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<CipherTier<CipherTierTag_v::Hot, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    HotInt a{99};
    auto b = a.relax<CipherTierTag_v::Hot>();
    return b.peek() == 99 && b.tier == CipherTierTag_v::Hot;
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

template <typename W, CipherTierTag_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, CipherTierTag_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using HotMoveOnly = CipherTier<CipherTierTag_v::Hot, MoveOnlyT>;
static_assert(can_relax_rvalue<HotMoveOnly, CipherTierTag_v::Warm>,
              "relax on an rvalue must accept a move-only T.  Moving a "
              "uniquely owned payload between tiers must not copy it.");
static_assert(!can_relax_lvalue<HotMoveOnly, CipherTierTag_v::Warm>,
              "relax on a const lvalue must reject a move-only T.  That "
              "overload requires a copy constructor.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    HotMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<CipherTierTag_v::Warm>();
    return dst.peek().v == 77 && dst.tier == CipherTierTag_v::Warm;
}
static_assert(relax_move_only_works());

static_assert(HotInt::value_type_name().size() > 0);
static_assert(HotInt::lattice_name().size() > 0);
static_assert(HotInt::lattice_name().starts_with("CipherTierLattice::At<"));

static_assert(cipher_tier::Hot<int>::tier == CipherTierTag_v::Hot);
static_assert(cipher_tier::Warm<int>::tier == CipherTierTag_v::Warm);
static_assert(cipher_tier::Cold<int>::tier == CipherTierTag_v::Cold);

static_assert(std::is_same_v<cipher_tier::Hot<double>, CipherTier<CipherTierTag_v::Hot, double>>);

// A recovery path that must not block admits Hot only.
template <typename W>
concept is_hot_reshard_admissible = W::template satisfies<CipherTierTag_v::Hot>;

static_assert(is_hot_reshard_admissible<HotInt>, "A Hot value must pass a gate that requires Hot.");
static_assert(!is_hot_reshard_admissible<WarmInt>, "A Warm value must not pass a gate that requires Hot.  The "
                                                   "recovery path would then block on disk reads where the "
                                                   "contract promised a memory read.");
static_assert(!is_hot_reshard_admissible<ColdInt>, "A Cold value must not pass a gate that requires Hot.");

// A path that writes fresh state to disk admits Warm and stronger.
template <typename W>
concept is_warm_publish_admissible = W::template satisfies<CipherTierTag_v::Warm>;

static_assert(is_warm_publish_admissible<HotInt>, "A Hot value must pass a gate that requires Warm.");
static_assert(is_warm_publish_admissible<WarmInt>, "A Warm value must pass a gate that requires Warm.");
static_assert(!is_warm_publish_admissible<ColdInt>, "A Cold value must not pass a gate that requires Warm.  Cold "
                                                    "sits below Warm, so admitting it would republish stale bytes "
                                                    "as if they were freshly written.");

inline void runtime_smoke_test() {
    HotInt a{};
    HotInt b{42};
    HotInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (HotInt::tier != CipherTierTag_v::Hot) {
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
    auto relaxed_copy = source.relax<CipherTierTag_v::Warm>();
    auto relaxed_move = std::move(source).relax<CipherTierTag_v::Cold>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = HotInt::satisfies<CipherTierTag_v::Warm>;
    [[maybe_unused]] bool s2 = WarmInt::satisfies<CipherTierTag_v::Hot>;

    HotInt eq_a{42};
    HotInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    HotInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    cipher_tier::Hot<int> alias_hot{123};
    cipher_tier::Warm<int> alias_warm{456};
    cipher_tier::Cold<int> alias_cold{789};
    [[maybe_unused]] auto av = alias_hot.peek();
    [[maybe_unused]] auto wv = alias_warm.peek();
    [[maybe_unused]] auto cv = alias_cold.peek();

    [[maybe_unused]] bool can_hot_reshard = is_hot_reshard_admissible<HotInt>;
    [[maybe_unused]] bool can_warm_publish = is_warm_publish_admissible<HotInt>;
}

}  // namespace detail::cipher_tier_self_test

}  // namespace crucible::safety
