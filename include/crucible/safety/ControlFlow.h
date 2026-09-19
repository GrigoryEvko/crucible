#pragma once

// The tier records how the value was produced: the strongest non-local escape
// its producer can take, from Pure through AbortOnly, ThrowOnly and MayLongjmp
// up to MaySignal.  It is a ceiling on capability, not a floor on proof, so the
// bottom tier is the safe one and a consumer admits a value whose tier is at or
// below the ceiling it imposes.
//
// The modality is Absolute because the tier describes the producer, not the
// content.  Mutating the wrapped value cannot change what its producer could
// do, so mutable access needs no re-check.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/ControlFlowLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::ControlFlow;
using ::crucible::algebra::lattices::ControlFlowLattice;

template <ControlFlow Tier, typename T>
class [[nodiscard]] ControlFlowPinned {
public:
    using value_type = T;
    using lattice_type = ControlFlowLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    static constexpr ControlFlow tier = Tier;

private:
    graded_type impl_;

public:
    constexpr ControlFlowPinned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit ControlFlowPinned(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ControlFlowPinned(std::in_place_t,
                                         Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                  && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr ControlFlowPinned(const ControlFlowPinned&) = default;
    constexpr ControlFlowPinned(ControlFlowPinned&&) = default;
    constexpr ControlFlowPinned& operator=(const ControlFlowPinned&) = default;
    constexpr ControlFlowPinned& operator=(ControlFlowPinned&&) = default;
    ~ControlFlowPinned() = default;

    [[nodiscard]] friend constexpr bool operator==(ControlFlowPinned const& a,
                                                   ControlFlowPinned const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(ControlFlowPinned& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(ControlFlowPinned& a, ControlFlowPinned& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <ControlFlow Ceiling>
    static constexpr bool satisfies = ControlFlowLattice::leq(Tier, Ceiling);

    // Over-stating what a producer can do is safe, so widening up the chain is
    // sound.  There is deliberately no operation in the other direction: it
    // would claim the value is safer than its producer proved.
    template <ControlFlow Higher>
        requires(ControlFlowLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr ControlFlowPinned<Higher, T>
    widen() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return ControlFlowPinned<Higher, T>{this->peek()};
    }

    template <ControlFlow Higher>
        requires(ControlFlowLattice::leq(Tier, Higher))
    [[nodiscard]] constexpr ControlFlowPinned<Higher, T> widen() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return ControlFlowPinned<Higher, T>{std::move(impl_).consume()};
    }
};

template <ControlFlow Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr ControlFlowPinned<Tier, T>
mint_control_flow(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return ControlFlowPinned<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

namespace control_flow_pin {
template <typename T>
using Pure = ControlFlowPinned<ControlFlow::Pure, T>;
template <typename T>
using AbortOnly = ControlFlowPinned<ControlFlow::AbortOnly, T>;
template <typename T>
using ThrowOnly = ControlFlowPinned<ControlFlow::ThrowOnly, T>;
template <typename T>
using MayLongjmp = ControlFlowPinned<ControlFlow::MayLongjmp, T>;
template <typename T>
using MaySignal = ControlFlowPinned<ControlFlow::MaySignal, T>;
}  // namespace control_flow_pin

static_assert(sizeof(ControlFlowPinned<ControlFlow::Pure, int>) == sizeof(int));
static_assert(sizeof(ControlFlowPinned<ControlFlow::MaySignal, int>) == sizeof(int));
static_assert(sizeof(ControlFlowPinned<ControlFlow::ThrowOnly, double>) == sizeof(double));
static_assert(sizeof(ControlFlowPinned<ControlFlow::Pure, char>) == sizeof(char));

namespace detail::control_flow_pinned_self_test {

using PureInt = ControlFlowPinned<ControlFlow::Pure, int>;
using SignalInt = ControlFlowPinned<ControlFlow::MaySignal, int>;

inline constexpr PureInt cf_default{};
static_assert(cf_default.peek() == 0);
static_assert(PureInt::tier == ControlFlow::Pure);
static_assert(SignalInt::tier == ControlFlow::MaySignal);
static_assert(PureInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(PureInt::satisfies<ControlFlow::Pure>);
static_assert(PureInt::satisfies<ControlFlow::MaySignal>);
static_assert(SignalInt::satisfies<ControlFlow::MaySignal>);
static_assert(!SignalInt::satisfies<ControlFlow::Pure>);
static_assert(!SignalInt::satisfies<ControlFlow::ThrowOnly>);

inline constexpr auto widened = PureInt{42}.widen<ControlFlow::MaySignal>();
static_assert(widened.peek() == 42);
static_assert(widened.tier == ControlFlow::MaySignal);
inline constexpr auto identity_widen = PureInt{7}.widen<ControlFlow::Pure>();
static_assert(identity_widen.tier == ControlFlow::Pure);

inline constexpr auto minted = mint_control_flow<ControlFlow::AbortOnly, int>(99);
static_assert(minted.peek() == 99 && minted.tier == ControlFlow::AbortOnly);
static_assert(std::is_same_v<control_flow_pin::Pure<int>, PureInt>);
static_assert(std::is_same_v<control_flow_pin::MaySignal<int>, SignalInt>);

static_assert(!std::is_same_v<PureInt, SignalInt>);

static_assert(std::is_copy_constructible_v<PureInt>);
static_assert(std::is_move_constructible_v<PureInt>);

inline void runtime_smoke_test() {
    int seed = 11;
    PureInt p{seed * 2};
    if (p.peek() != 22) std::abort();
    p.peek_mut() = 5;
    if (p.peek() != 5) std::abort();
    auto w = PureInt{seed}.widen<ControlFlow::ThrowOnly>();
    if (w.peek() != 11 || w.tier != ControlFlow::ThrowOnly) std::abort();
    auto m = mint_control_flow<ControlFlow::MaySignal, int>(seed);
    if (std::move(m).consume() != 11) std::abort();
    PureInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::control_flow_pinned_self_test

}  // namespace crucible::safety
