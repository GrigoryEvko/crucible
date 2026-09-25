#pragma once

// Pins the memory ordering a value was published under, so a consumer
// that needs a minimum ordering can turn away anything published under a
// weaker fence.
//
// The tier is a floor, not a ceiling.  A stronger fence really does
// provide everything a weaker one promises, so a value published under
// the strongest ordering satisfies a consumer that asks only for
// acquire-release.  Relabelling downward is therefore sound: it claims
// less than the value provides.  Relabelling upward is not, and is a
// compile error, because it would claim an ordering that was never
// issued and fool a consumer that depends on it.
//
// The wrapper issues no fence of its own.  It is the witness that one
// was issued elsewhere.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_BarrierStrengthLattice.h>

#include <concepts>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::BarrierStrengthLattice;
using BarrierStrength_v = ::crucible::algebra::lattices::BarrierStrength;

template <BarrierStrength_v Tier, typename T>
class [[nodiscard]] BarrierGuarded {
public:
    using value_type = T;
    using lattice_type = BarrierStrengthLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr BarrierStrength_v tier = Tier;

private:
    graded_type impl_;

public:
    constexpr BarrierGuarded() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit BarrierGuarded(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit BarrierGuarded(std::in_place_t,
                                      Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                               && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr BarrierGuarded(const BarrierGuarded&) = default;
    constexpr BarrierGuarded(BarrierGuarded&&) = default;
    constexpr BarrierGuarded& operator=(const BarrierGuarded&) = default;
    constexpr BarrierGuarded& operator=(BarrierGuarded&&) = default;
    ~BarrierGuarded() = default;

    [[nodiscard]] friend constexpr bool operator==(BarrierGuarded const& a,
                                                   BarrierGuarded const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(BarrierGuarded& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(BarrierGuarded& a, BarrierGuarded& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <BarrierStrength_v Required>
    static constexpr bool satisfies = BarrierStrengthLattice::leq(Required, Tier);

    template <BarrierStrength_v Lower>
        requires(BarrierStrengthLattice::leq(Lower, Tier))
    [[nodiscard]] constexpr BarrierGuarded<Lower, T> weaken() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return BarrierGuarded<Lower, T>{this->peek()};
    }

    template <BarrierStrength_v Lower>
        requires(BarrierStrengthLattice::leq(Lower, Tier))
    [[nodiscard]] constexpr BarrierGuarded<Lower, T> weaken() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return BarrierGuarded<Lower, T>{std::move(impl_).consume()};
    }
};

template <BarrierStrength_v Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr BarrierGuarded<Tier, T>
mint_barrier_guarded(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return BarrierGuarded<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace barrier_pin {
template <typename T>
using None = BarrierGuarded<BarrierStrength_v::None, T>;
template <typename T>
using CompilerBarrier = BarrierGuarded<BarrierStrength_v::CompilerBarrier, T>;
template <typename T>
using AcquireLoad = BarrierGuarded<BarrierStrength_v::AcquireLoad, T>;
template <typename T>
using ReleaseStore = BarrierGuarded<BarrierStrength_v::ReleaseStore, T>;
template <typename T>
using AcqRel = BarrierGuarded<BarrierStrength_v::AcqRel, T>;
template <typename T>
using SeqCst = BarrierGuarded<BarrierStrength_v::SeqCst, T>;
template <typename T>
using FullFence = BarrierGuarded<BarrierStrength_v::FullFence, T>;
}  // namespace barrier_pin

namespace detail::barrier_guarded_layout {

template <typename T>
using NoneBg = BarrierGuarded<BarrierStrength_v::None, T>;
template <typename T>
using AcqRelBg = BarrierGuarded<BarrierStrength_v::AcqRel, T>;
template <typename T>
using SeqCstBg = BarrierGuarded<BarrierStrength_v::SeqCst, T>;
template <typename T>
using FullFenceBg = BarrierGuarded<BarrierStrength_v::FullFence, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneBg, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneBg, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelBg, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelBg, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstBg, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FullFenceBg, int);

}  // namespace detail::barrier_guarded_layout

static_assert(sizeof(BarrierGuarded<BarrierStrength_v::None, int>) == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::CompilerBarrier, int>) == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::AcquireLoad, int>) == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::ReleaseStore, int>) == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::AcqRel, int>) == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::SeqCst, int>) == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::FullFence, int>) == sizeof(int));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::AcqRel, double>) == sizeof(double));
static_assert(sizeof(BarrierGuarded<BarrierStrength_v::None, char>) == sizeof(char));

namespace detail::barrier_guarded_self_test {

using NoneInt = BarrierGuarded<BarrierStrength_v::None, int>;
using CompInt = BarrierGuarded<BarrierStrength_v::CompilerBarrier, int>;
using AcqInt = BarrierGuarded<BarrierStrength_v::AcquireLoad, int>;
using AcqRelInt = BarrierGuarded<BarrierStrength_v::AcqRel, int>;
using SeqCstInt = BarrierGuarded<BarrierStrength_v::SeqCst, int>;
using FullInt = BarrierGuarded<BarrierStrength_v::FullFence, int>;

inline constexpr AcqRelInt a_default{};
static_assert(a_default.peek() == 0);
static_assert(AcqRelInt::tier == BarrierStrength_v::AcqRel);

inline constexpr AcqRelInt a_explicit{42};
static_assert(a_explicit.peek() == 42);

inline constexpr AcqRelInt a_in_place{std::in_place, 7};
static_assert(a_in_place.peek() == 7);

static_assert(NoneInt::tier == BarrierStrength_v::None);
static_assert(FullInt::tier == BarrierStrength_v::FullFence);
static_assert(NoneInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(FullInt::satisfies<BarrierStrength_v::None>);
static_assert(FullInt::satisfies<BarrierStrength_v::AcqRel>);
static_assert(FullInt::satisfies<BarrierStrength_v::SeqCst>);
static_assert(FullInt::satisfies<BarrierStrength_v::FullFence>);

static_assert(SeqCstInt::satisfies<BarrierStrength_v::SeqCst>);
static_assert(SeqCstInt::satisfies<BarrierStrength_v::AcqRel>,
              "A sequentially-consistent publication satisfies an acquire-release "
              "floor.  It provides everything that floor asks for.");
static_assert(!SeqCstInt::satisfies<BarrierStrength_v::FullFence>,
              "A sequentially-consistent value does not satisfy a full-fence floor.  "
              "It carries no standalone fence.");

static_assert(NoneInt::satisfies<BarrierStrength_v::None>);
static_assert(!NoneInt::satisfies<BarrierStrength_v::CompilerBarrier>);
static_assert(!NoneInt::satisfies<BarrierStrength_v::AcqRel>);

static_assert(!AcqInt::satisfies<BarrierStrength_v::AcqRel>,
              "An acquire-only value does not satisfy an acquire-release floor.  It "
              "lacks the release half that floor asks for.");

inline constexpr auto full_to_acqrel = FullInt{42}.weaken<BarrierStrength_v::AcqRel>();
static_assert(full_to_acqrel.peek() == 42 && full_to_acqrel.tier == BarrierStrength_v::AcqRel);

inline constexpr auto seqcst_to_none = SeqCstInt{9}.weaken<BarrierStrength_v::None>();
static_assert(seqcst_to_none.peek() == 9 && seqcst_to_none.tier == BarrierStrength_v::None);

inline constexpr auto acqrel_reflexive = AcqRelInt{55}.weaken<BarrierStrength_v::AcqRel>();
static_assert(acqrel_reflexive.peek() == 55);

template <typename W, BarrierStrength_v Target>
concept can_weaken = requires(W w) {
    { std::move(w).template weaken<Target>() };
};

static_assert(can_weaken<FullInt, BarrierStrength_v::None>);
static_assert(can_weaken<SeqCstInt, BarrierStrength_v::AcqRel>);
static_assert(can_weaken<AcqRelInt, BarrierStrength_v::AcqRel>);
static_assert(!can_weaken<AcqInt, BarrierStrength_v::SeqCst>,
              "Relabelling an acquire-only value as sequentially consistent is "
              "rejected.  It would claim a fence stronger than the one issued and "
              "fool a consumer that depends on the stronger ordering.");
static_assert(!can_weaken<NoneInt, BarrierStrength_v::CompilerBarrier>);
static_assert(!can_weaken<CompInt, BarrierStrength_v::AcqRel>);

static_assert(AcqRelInt::value_type_name().ends_with("int"));
static_assert(AcqRelInt::lattice_name() == "BarrierStrengthLattice::At<AcqRel>");
static_assert(SeqCstInt::lattice_name() == "BarrierStrengthLattice::At<SeqCst>");
static_assert(FullInt::lattice_name() == "BarrierStrengthLattice::At<FullFence>");

[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    AcqRelInt a{10};
    AcqRelInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    AcqRelInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    AcqRelInt a{42};
    AcqRelInt b{42};
    AcqRelInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

static_assert(std::is_same_v<barrier_pin::AcqRel<int>, AcqRelInt>);
static_assert(std::is_same_v<barrier_pin::FullFence<int>, FullInt>);
static_assert(barrier_pin::SeqCst<double>::tier == BarrierStrength_v::SeqCst);
static_assert(!std::is_same_v<AcqRelInt, SeqCstInt>);
static_assert(std::is_copy_constructible_v<AcqRelInt>);

inline constexpr auto minted = mint_barrier_guarded<BarrierStrength_v::SeqCst, int>(99);
static_assert(minted.peek() == 99 && minted.tier == BarrierStrength_v::SeqCst);

template <typename W>
concept needs_acquire_floor = W::template satisfies<BarrierStrength_v::AcquireLoad>;

static_assert(needs_acquire_floor<AcqInt>, "An acquire-published value passes an acquire floor.");
static_assert(needs_acquire_floor<SeqCstInt>, "A sequentially-consistent publication passes an acquire floor.");
static_assert(!needs_acquire_floor<NoneInt>, "A value published under no barrier is rejected at an acquire floor.  "
                                             "It carries no cross-thread ordering guarantee.");
static_assert(!needs_acquire_floor<CompInt>, "A value published under a compiler barrier alone is rejected at an "
                                             "acquire floor.  An optimizer barrier issues no hardware ordering.");

// The arguments here are non-constant on purpose.  A pure static_assert
// suite masks bugs that only appear when the body is instantiated for
// runtime evaluation.
inline void runtime_smoke_test() {
    int seed = 21;
    AcqRelInt n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto w = FullInt{seed}.weaken<BarrierStrength_v::AcqRel>();
    if (w.peek() != 21 || w.tier != BarrierStrength_v::AcqRel) std::abort();

    auto m = mint_barrier_guarded<BarrierStrength_v::SeqCst, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    AcqRelInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool s1 = SeqCstInt::satisfies<BarrierStrength_v::AcqRel>;
    [[maybe_unused]] bool s2 = AcqInt::satisfies<BarrierStrength_v::AcqRel>;
    if (!s1 || s2) std::abort();

    barrier_pin::None<int> alias_none{0};
    barrier_pin::FullFence<int> alias_full{456};
    if (alias_none.peek() != 0 || alias_full.peek() != 456) std::abort();
}

}  // namespace detail::barrier_guarded_self_test

}  // namespace crucible::safety
