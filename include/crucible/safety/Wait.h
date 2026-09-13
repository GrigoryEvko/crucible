#pragma once

// Wait<Strategy, T> pins a value to the most expensive way a holder
// may wait on it.
//
// The strategies form a chain from the most permissive to the most
// restrictive.  Block enters the kernel and may sleep without bound.
// Park suspends the thread.  AcquireWait waits on an atomic.
// UmwaitC01 waits in a low-power core state.  BoundedSpin spins with
// backoff.  SpinPause spins with only a pause hint.  Higher in the
// chain means less is permitted.
//
// satisfies<Required> asks whether the pinned strategy covers what a
// consumer demands: stricter satisfies looser.  A SpinPause value is
// admissible wherever Park is required, because a holder that never
// parks certainly never parks too much.  The converse does not hold.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a SpinPause value is to build one
// at a site that really only spins.  The substrate's weaken(), which
// does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/WaitLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::WaitLattice;
using WaitStrategy_v = ::crucible::algebra::lattices::WaitStrategy;

template <WaitStrategy_v Strategy, typename T>
class [[nodiscard]] Wait {
public:
    using value_type = T;
    using lattice_type = WaitLattice::At<Strategy>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr WaitStrategy_v strategy = Strategy;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a strategy no site honored.
    // Deleting it would be the truthful choice, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.
    constexpr Wait() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Wait(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Wait(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                      && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr Wait(const Wait&) = default;
    constexpr Wait(Wait&&) = default;
    constexpr Wait& operator=(const Wait&) = default;
    constexpr Wait& operator=(Wait&&) = default;
    ~Wait() = default;

    [[nodiscard]] friend constexpr bool operator==(Wait const& a,
                                                   Wait const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(Wait& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Wait& a, Wait& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <WaitStrategy_v RequiredStrategy>
    static constexpr bool satisfies = WaitLattice::leq(RequiredStrategy, Strategy);

    template <WaitStrategy_v WeakerStrategy>
        requires(WaitLattice::leq(WeakerStrategy, Strategy))
    [[nodiscard]] constexpr Wait<WeakerStrategy, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Wait<WeakerStrategy, T>{this->peek()};
    }

    template <WaitStrategy_v WeakerStrategy>
        requires(WaitLattice::leq(WeakerStrategy, Strategy))
    [[nodiscard]] constexpr Wait<WeakerStrategy, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Wait<WeakerStrategy, T>{std::move(impl_).consume()};
    }
};

namespace wait {
template <typename T>
using SpinPause = Wait<WaitStrategy_v::SpinPause, T>;
template <typename T>
using BoundedSpin = Wait<WaitStrategy_v::BoundedSpin, T>;
template <typename T>
using UmwaitC01 = Wait<WaitStrategy_v::UmwaitC01, T>;
template <typename T>
using AcquireWait = Wait<WaitStrategy_v::AcquireWait, T>;
template <typename T>
using Park = Wait<WaitStrategy_v::Park, T>;
template <typename T>
using Block = Wait<WaitStrategy_v::Block, T>;
}  // namespace wait

namespace detail::wait_layout {

template <typename T>
using SpinW = Wait<WaitStrategy_v::SpinPause, T>;
template <typename T>
using ParkW = Wait<WaitStrategy_v::Park, T>;
template <typename T>
using BlockW = Wait<WaitStrategy_v::Block, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinW, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SpinW, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ParkW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ParkW, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BlockW, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BlockW, double);

}  // namespace detail::wait_layout

static_assert(sizeof(Wait<WaitStrategy_v::SpinPause, int>) == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::BoundedSpin, int>) == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::UmwaitC01, int>) == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::AcquireWait, int>) == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::Park, int>) == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::Block, int>) == sizeof(int));
static_assert(sizeof(Wait<WaitStrategy_v::SpinPause, double>) == sizeof(double));

namespace detail::wait_self_test {

using SpinInt = Wait<WaitStrategy_v::SpinPause, int>;
using BoundInt = Wait<WaitStrategy_v::BoundedSpin, int>;
using UmwaitInt = Wait<WaitStrategy_v::UmwaitC01, int>;
using FutexInt = Wait<WaitStrategy_v::AcquireWait, int>;
using ParkInt = Wait<WaitStrategy_v::Park, int>;
using BlockInt = Wait<WaitStrategy_v::Block, int>;

inline constexpr SpinInt w_default{};
static_assert(w_default.peek() == 0);
static_assert(w_default.strategy == WaitStrategy_v::SpinPause);

inline constexpr SpinInt w_explicit{42};
static_assert(w_explicit.peek() == 42);

inline constexpr SpinInt w_in_place{std::in_place, 7};
static_assert(w_in_place.peek() == 7);

static_assert(SpinInt::strategy == WaitStrategy_v::SpinPause);
static_assert(BoundInt::strategy == WaitStrategy_v::BoundedSpin);
static_assert(UmwaitInt::strategy == WaitStrategy_v::UmwaitC01);
static_assert(FutexInt::strategy == WaitStrategy_v::AcquireWait);
static_assert(ParkInt::strategy == WaitStrategy_v::Park);
static_assert(BlockInt::strategy == WaitStrategy_v::Block);

static_assert(SpinInt::satisfies<WaitStrategy_v::SpinPause>);
static_assert(SpinInt::satisfies<WaitStrategy_v::BoundedSpin>);
static_assert(SpinInt::satisfies<WaitStrategy_v::UmwaitC01>);
static_assert(SpinInt::satisfies<WaitStrategy_v::AcquireWait>);
static_assert(SpinInt::satisfies<WaitStrategy_v::Park>);
static_assert(SpinInt::satisfies<WaitStrategy_v::Block>);

static_assert(FutexInt::satisfies<WaitStrategy_v::AcquireWait>);
static_assert(FutexInt::satisfies<WaitStrategy_v::Park>);
static_assert(FutexInt::satisfies<WaitStrategy_v::Block>);
static_assert(!FutexInt::satisfies<WaitStrategy_v::UmwaitC01>);
static_assert(!FutexInt::satisfies<WaitStrategy_v::BoundedSpin>);
static_assert(!FutexInt::satisfies<WaitStrategy_v::SpinPause>,
              "AcquireWait must not satisfy SpinPause.  A wait that suspends "
              "must not reach a site whose only permitted wait is a spin.");

static_assert(ParkInt::satisfies<WaitStrategy_v::Park>);
static_assert(ParkInt::satisfies<WaitStrategy_v::Block>);
static_assert(!ParkInt::satisfies<WaitStrategy_v::AcquireWait>);
static_assert(!ParkInt::satisfies<WaitStrategy_v::SpinPause>);

static_assert(BlockInt::satisfies<WaitStrategy_v::Block>);
static_assert(!BlockInt::satisfies<WaitStrategy_v::Park>);
static_assert(!BlockInt::satisfies<WaitStrategy_v::AcquireWait>);
static_assert(!BlockInt::satisfies<WaitStrategy_v::SpinPause>);

inline constexpr auto from_spin_to_bound = SpinInt{42}.relax<WaitStrategy_v::BoundedSpin>();
static_assert(from_spin_to_bound.peek() == 42);
static_assert(from_spin_to_bound.strategy == WaitStrategy_v::BoundedSpin);

inline constexpr auto from_spin_to_block = SpinInt{99}.relax<WaitStrategy_v::Block>();
static_assert(from_spin_to_block.peek() == 99);
static_assert(from_spin_to_block.strategy == WaitStrategy_v::Block);

inline constexpr auto from_umwait_to_park = UmwaitInt{7}.relax<WaitStrategy_v::Park>();
static_assert(from_umwait_to_park.peek() == 7);

inline constexpr auto from_futex_to_self = FutexInt{8}.relax<WaitStrategy_v::AcquireWait>();
static_assert(from_futex_to_self.peek() == 8);

template <typename W, WaitStrategy_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<SpinInt, WaitStrategy_v::BoundedSpin>);
static_assert(can_relax<SpinInt, WaitStrategy_v::Block>);
static_assert(can_relax<SpinInt, WaitStrategy_v::SpinPause>);
static_assert(can_relax<UmwaitInt, WaitStrategy_v::AcquireWait>);
static_assert(can_relax<UmwaitInt, WaitStrategy_v::UmwaitC01>);
static_assert(!can_relax<UmwaitInt, WaitStrategy_v::BoundedSpin>,
              "relax<BoundedSpin> on an UmwaitC01 value must be rejected.  It "
              "would claim a tighter wait discipline than the value has.");
static_assert(!can_relax<UmwaitInt, WaitStrategy_v::SpinPause>);
static_assert(!can_relax<ParkInt, WaitStrategy_v::AcquireWait>);
static_assert(!can_relax<BlockInt, WaitStrategy_v::Park>);
static_assert(can_relax<BlockInt, WaitStrategy_v::Block>);

static_assert(SpinInt::value_type_name().ends_with("int"));
static_assert(SpinInt::lattice_name() == "WaitLattice::At<SpinPause>");
static_assert(BoundInt::lattice_name() == "WaitLattice::At<BoundedSpin>");
static_assert(UmwaitInt::lattice_name() == "WaitLattice::At<UmwaitC01>");
static_assert(FutexInt::lattice_name() == "WaitLattice::At<AcquireWait>");
static_assert(ParkInt::lattice_name() == "WaitLattice::At<Park>");
static_assert(BlockInt::lattice_name() == "WaitLattice::At<Block>");

[[nodiscard]] consteval bool swap_exchanges_within_same_strategy() noexcept {
    SpinInt a{10};
    SpinInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_strategy());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    SpinInt a{10};
    SpinInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    SpinInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    SpinInt a{42};
    SpinInt b{42};
    SpinInt c{43};
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

static_assert(can_equality_compare<SpinInt>);
static_assert(!can_equality_compare<Wait<WaitStrategy_v::SpinPause, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<Wait<WaitStrategy_v::SpinPause, NoEqualityT>>,
              "Wait<Strategy, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<Wait<WaitStrategy_v::SpinPause, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    SpinInt a{99};
    auto b = a.relax<WaitStrategy_v::SpinPause>();
    return b.peek() == 99 && b.strategy == WaitStrategy_v::SpinPause;
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

template <typename W, WaitStrategy_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, WaitStrategy_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using SpinMoveOnly = Wait<WaitStrategy_v::SpinPause, MoveOnlyT>;
static_assert(can_relax_rvalue<SpinMoveOnly, WaitStrategy_v::Park>, "relax on an rvalue must accept a move-only T.");
static_assert(!can_relax_lvalue<SpinMoveOnly, WaitStrategy_v::Park>,
              "relax on a const lvalue must reject a move-only T.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    SpinMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<WaitStrategy_v::Park>();
    return dst.peek().v == 77 && dst.strategy == WaitStrategy_v::Park;
}
static_assert(relax_move_only_works());

static_assert(SpinInt::value_type_name().size() > 0);
static_assert(SpinInt::lattice_name().size() > 0);
static_assert(SpinInt::lattice_name().starts_with("WaitLattice::At<"));

static_assert(wait::SpinPause<int>::strategy == WaitStrategy_v::SpinPause);
static_assert(wait::BoundedSpin<int>::strategy == WaitStrategy_v::BoundedSpin);
static_assert(wait::UmwaitC01<int>::strategy == WaitStrategy_v::UmwaitC01);
static_assert(wait::AcquireWait<int>::strategy == WaitStrategy_v::AcquireWait);
static_assert(wait::Park<int>::strategy == WaitStrategy_v::Park);
static_assert(wait::Block<int>::strategy == WaitStrategy_v::Block);

static_assert(std::is_same_v<wait::SpinPause<double>, Wait<WaitStrategy_v::SpinPause, double>>);

// A site whose only permitted wait is a spin admits SpinPause only.
template <typename W>
concept is_hot_path_waiter_admissible = W::template satisfies<WaitStrategy_v::SpinPause>;

static_assert(is_hot_path_waiter_admissible<SpinInt>, "A SpinPause value must pass a gate that requires SpinPause.");
static_assert(!is_hot_path_waiter_admissible<BoundInt>, "A BoundedSpin value must not pass a gate that requires "
                                                        "SpinPause.  Its backoff can exceed what a bare pause costs.");
static_assert(!is_hot_path_waiter_admissible<FutexInt>, "An AcquireWait value must not pass a gate that requires "
                                                        "SpinPause.");
static_assert(!is_hot_path_waiter_admissible<ParkInt>, "A Park value must not pass a gate that requires SpinPause.");
static_assert(!is_hot_path_waiter_admissible<BlockInt>, "A Block value must not pass a gate that requires SpinPause.");

inline void runtime_smoke_test() {
    SpinInt a{};
    SpinInt b{42};
    SpinInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (SpinInt::strategy != WaitStrategy_v::SpinPause) {
        std::abort();
    }

    SpinInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    SpinInt sx{1};
    SpinInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    SpinInt source{77};
    auto relaxed_copy = source.relax<WaitStrategy_v::BoundedSpin>();
    auto relaxed_move = std::move(source).relax<WaitStrategy_v::Park>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = SpinInt::satisfies<WaitStrategy_v::Park>;
    [[maybe_unused]] bool s2 = ParkInt::satisfies<WaitStrategy_v::SpinPause>;

    SpinInt eq_a{42};
    SpinInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    SpinInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    wait::SpinPause<int> alias_spin{123};
    wait::Park<int> alias_park{456};
    wait::Block<int> alias_block{789};
    [[maybe_unused]] auto sv = alias_spin.peek();
    [[maybe_unused]] auto pv = alias_park.peek();
    [[maybe_unused]] auto bv = alias_block.peek();

    [[maybe_unused]] bool can_spin_pass = is_hot_path_waiter_admissible<SpinInt>;
    [[maybe_unused]] bool can_futex_pass = is_hot_path_waiter_admissible<FutexInt>;
    [[maybe_unused]] bool can_block_pass = is_hot_path_waiter_admissible<BlockInt>;
}

}  // namespace detail::wait_self_test

}  // namespace crucible::safety
