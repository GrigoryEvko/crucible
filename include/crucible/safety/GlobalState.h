#pragma once

// ── crucible::safety::GlobalStatePinned<GlobalState Tier, typename T> ─
//
// FIXY-V-242 (4/5): value-level Graded carrier for the V-241
// GlobalState axis (Stateless ⊏ ConstGlobal ⊏ MutableGlobal ⊏
// InitOrderHazard).  Pins a function's global/static-mutable-state
// interaction hazard into the type.
//
//   Substrate: Graded<ModalityKind::Absolute, GlobalStateLattice::At<Tier>, T>
//   Regime:    1 (zero-cost EBO collapse — sizeof == sizeof(T)).
//
// Absolute modality.  CAPABILITY-CEILING semantics: bottom (Stateless)
// is the safest (no global interaction, trivially replay-safe); top
// (InitOrderHazard) is the worst (static-init-order / Meyers-singleton
// lazy-init hazard — what V-248's S004 detector keys on).
//
//   satisfies<C> := GlobalStateLattice::leq(Tier, C)
//   widen<Higher>()                                  — UP the chain only
//
// Forge hot-path admission imposes ceiling ConstGlobal (may read
// constinit tables; must not touch mutable global state, which would
// defeat content-addressed determinism).
//
// §XXI: `mint_global_state<Tier, T>(args...)`.  HS14 neg fixtures:
// neg_global_state_widen_to_lower.cpp + neg_global_state_mint_wrong_arg.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/GlobalStateLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::GlobalState;
using ::crucible::algebra::lattices::GlobalStateLattice;

template <GlobalState Tier, typename T>
class [[nodiscard]] GlobalStatePinned {
public:
    using value_type   = T;
    using lattice_type = GlobalStateLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;
    static constexpr GlobalState tier = Tier;

private:
    graded_type impl_;

public:
    constexpr GlobalStatePinned() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit GlobalStatePinned(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit GlobalStatePinned(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr GlobalStatePinned(const GlobalStatePinned&)            = default;
    constexpr GlobalStatePinned(GlobalStatePinned&&)                 = default;
    constexpr GlobalStatePinned& operator=(const GlobalStatePinned&) = default;
    constexpr GlobalStatePinned& operator=(GlobalStatePinned&&)      = default;
    ~GlobalStatePinned()                                             = default;

    [[nodiscard]] friend constexpr bool operator==(
        GlobalStatePinned const& a, GlobalStatePinned const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return std::move(impl_).consume(); }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(GlobalStatePinned& other)
        noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(GlobalStatePinned& a, GlobalStatePinned& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <GlobalState Ceiling>
    static constexpr bool satisfies = GlobalStateLattice::leq(Tier, Ceiling);

    template <GlobalState Higher>
        requires (GlobalStateLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr GlobalStatePinned<Higher, T> widen() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    { return GlobalStatePinned<Higher, T>{this->peek()}; }

    template <GlobalState Higher>
        requires (GlobalStateLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr GlobalStatePinned<Higher, T> widen() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return GlobalStatePinned<Higher, T>{std::move(impl_).consume()}; }
};

template <GlobalState Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr GlobalStatePinned<Tier, T> mint_global_state(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return GlobalStatePinned<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace global_state_pin {
    template <typename T> using Stateless       = GlobalStatePinned<GlobalState::Stateless,       T>;
    template <typename T> using ConstGlobal     = GlobalStatePinned<GlobalState::ConstGlobal,     T>;
    template <typename T> using MutableGlobal   = GlobalStatePinned<GlobalState::MutableGlobal,   T>;
    template <typename T> using InitOrderHazard = GlobalStatePinned<GlobalState::InitOrderHazard, T>;
}  // namespace global_state_pin

static_assert(sizeof(GlobalStatePinned<GlobalState::Stateless,       int>)    == sizeof(int));
static_assert(sizeof(GlobalStatePinned<GlobalState::InitOrderHazard, int>)    == sizeof(int));
static_assert(sizeof(GlobalStatePinned<GlobalState::MutableGlobal,   double>) == sizeof(double));
static_assert(sizeof(GlobalStatePinned<GlobalState::Stateless,       char>)   == sizeof(char));

namespace detail::global_state_pinned_self_test {

using StatelessInt = GlobalStatePinned<GlobalState::Stateless,       int>;
using HazardInt    = GlobalStatePinned<GlobalState::InitOrderHazard, int>;

inline constexpr StatelessInt gs_default{};
static_assert(gs_default.peek() == 0);
static_assert(StatelessInt::tier == GlobalState::Stateless);
static_assert(HazardInt::tier == GlobalState::InitOrderHazard);
static_assert(StatelessInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(StatelessInt::satisfies<GlobalState::Stateless>);
static_assert(StatelessInt::satisfies<GlobalState::InitOrderHazard>);
static_assert( HazardInt::satisfies<GlobalState::InitOrderHazard>);
static_assert(!HazardInt::satisfies<GlobalState::Stateless>);
static_assert(!HazardInt::satisfies<GlobalState::MutableGlobal>);

inline constexpr auto widened = StatelessInt{42}.widen<GlobalState::MutableGlobal>();
static_assert(widened.peek() == 42 && widened.tier == GlobalState::MutableGlobal);

inline constexpr auto minted = mint_global_state<GlobalState::ConstGlobal, int>(99);
static_assert(minted.peek() == 99 && minted.tier == GlobalState::ConstGlobal);
static_assert(std::is_same_v<global_state_pin::Stateless<int>, StatelessInt>);
static_assert(std::is_same_v<global_state_pin::InitOrderHazard<int>, HazardInt>);
static_assert(!std::is_same_v<StatelessInt, HazardInt>);
static_assert(std::is_copy_constructible_v<StatelessInt>);

inline void runtime_smoke_test() {
    int seed = 19;
    StatelessInt s{seed * 2};
    if (s.peek() != 38) std::abort();
    s.peek_mut() = 8;
    if (s.peek() != 8) std::abort();
    auto w = StatelessInt{seed}.widen<GlobalState::ConstGlobal>();
    if (w.peek() != 19 || w.tier != GlobalState::ConstGlobal) std::abort();
    auto m = mint_global_state<GlobalState::InitOrderHazard, int>(seed);
    if (std::move(m).consume() != 19) std::abort();
    StatelessInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::global_state_pinned_self_test

}  // namespace crucible::safety
