#pragma once

// S pins the memory-visibility scope a publication was released under.
// The scopes form a partial order over two trunks that meet only at the
// ends:
//
//   accelerator:       Warp ⊑ Cta ⊑ Cluster ⊑ Gpu
//   ARM shareability:  Inner ⊑ Outer
//   shared bottom Thread, shared top System
//
// Scopes on different trunks are incomparable.  A block scope has no
// ordering relation to an inner-shareable domain, so neither one
// satisfies the other.
//
// S is the scope the fence publishes at.  A consumer requirement R is met
// when S subsumes R, which means R sits at or below S.  Wider visibility
// is higher, so a device-wide fence meets a block-scope requirement and a
// block-scope fence does not meet a device-wide one.
//
// relax<Narrower>() is sound because a wider fence really does publish at
// every narrower scope it dominates.  A device-wide fence already makes
// the writes visible at block scope.  Re-labelling narrows where the value
// is offered and never overstates what the fence covered.  Relaxing up, or
// across to an incomparable trunk, is a compile error.  It would assert
// the value is visible to observers the fence never reached.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_MemoryScopeLattice.h>

#include <concepts>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::MemoryScopeLattice;
using MemoryScope_v = ::crucible::algebra::lattices::MemoryScope;

template <MemoryScope_v S, typename T>
class [[nodiscard]] ScopedFence {
public:
    using value_type = T;
    using lattice_type = MemoryScopeLattice::At<S>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr MemoryScope_v scope = S;

private:
    graded_type impl_;

public:
    constexpr ScopedFence() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit ScopedFence(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ScopedFence(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                             && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr ScopedFence(const ScopedFence&) = default;
    constexpr ScopedFence(ScopedFence&&) = default;
    constexpr ScopedFence& operator=(const ScopedFence&) = default;
    constexpr ScopedFence& operator=(ScopedFence&&) = default;
    ~ScopedFence() = default;

    [[nodiscard]] friend constexpr bool operator==(ScopedFence const& a,
                                                   ScopedFence const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(ScopedFence& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(ScopedFence& a, ScopedFence& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <MemoryScope_v Required>
    static constexpr bool satisfies = MemoryScopeLattice::leq(Required, S);

    template <MemoryScope_v Narrower>
        requires(MemoryScopeLattice::leq(Narrower, S))
    [[nodiscard]] constexpr ScopedFence<Narrower, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return ScopedFence<Narrower, T>{this->peek()};
    }

    template <MemoryScope_v Narrower>
        requires(MemoryScopeLattice::leq(Narrower, S))
    [[nodiscard]] constexpr ScopedFence<Narrower, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return ScopedFence<Narrower, T>{std::move(impl_).consume()};
    }
};

template <MemoryScope_v S, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr ScopedFence<S, T>
mint_scoped_fence(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return ScopedFence<S, T>{std::in_place, std::forward<Args>(args)...};
}

namespace scoped_fence {
template <typename T>
using Thread = ScopedFence<MemoryScope_v::Thread, T>;
template <typename T>
using Warp = ScopedFence<MemoryScope_v::Warp, T>;
template <typename T>
using Cta = ScopedFence<MemoryScope_v::Cta, T>;
template <typename T>
using Cluster = ScopedFence<MemoryScope_v::Cluster, T>;
template <typename T>
using Gpu = ScopedFence<MemoryScope_v::Gpu, T>;
template <typename T>
using Inner = ScopedFence<MemoryScope_v::Inner, T>;
template <typename T>
using Outer = ScopedFence<MemoryScope_v::Outer, T>;
template <typename T>
using System = ScopedFence<MemoryScope_v::System, T>;
}  // namespace scoped_fence

namespace detail::scoped_fence_layout {

template <typename T>
using ThreadSf = ScopedFence<MemoryScope_v::Thread, T>;
template <typename T>
using CtaSf = ScopedFence<MemoryScope_v::Cta, T>;
template <typename T>
using GpuSf = ScopedFence<MemoryScope_v::Gpu, T>;
template <typename T>
using InnerSf = ScopedFence<MemoryScope_v::Inner, T>;
template <typename T>
using SystemSf = ScopedFence<MemoryScope_v::System, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThreadSf, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThreadSf, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CtaSf, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CtaSf, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(GpuSf, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(InnerSf, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SystemSf, int);

}  // namespace detail::scoped_fence_layout

static_assert(sizeof(ScopedFence<MemoryScope_v::Thread, int>) == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Cta, int>) == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Gpu, int>) == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Inner, int>) == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Outer, int>) == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::System, int>) == sizeof(int));
static_assert(sizeof(ScopedFence<MemoryScope_v::Cta, double>) == sizeof(double));
static_assert(sizeof(ScopedFence<MemoryScope_v::Thread, char>) == sizeof(char));

namespace detail::scoped_fence_self_test {

using ThreadInt = ScopedFence<MemoryScope_v::Thread, int>;
using WarpInt = ScopedFence<MemoryScope_v::Warp, int>;
using CtaInt = ScopedFence<MemoryScope_v::Cta, int>;
using ClusterInt = ScopedFence<MemoryScope_v::Cluster, int>;
using GpuInt = ScopedFence<MemoryScope_v::Gpu, int>;
using InnerInt = ScopedFence<MemoryScope_v::Inner, int>;
using OuterInt = ScopedFence<MemoryScope_v::Outer, int>;
using SystemInt = ScopedFence<MemoryScope_v::System, int>;

inline constexpr CtaInt c_default{};
static_assert(c_default.peek() == 0);
static_assert(CtaInt::scope == MemoryScope_v::Cta);

inline constexpr CtaInt c_explicit{42};
static_assert(c_explicit.peek() == 42);

inline constexpr CtaInt c_in_place{std::in_place, 7};
static_assert(c_in_place.peek() == 7);

static_assert(ThreadInt::scope == MemoryScope_v::Thread);
static_assert(SystemInt::scope == MemoryScope_v::System);
static_assert(ThreadInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(SystemInt::satisfies<MemoryScope_v::Thread>);
static_assert(SystemInt::satisfies<MemoryScope_v::Cta>);
static_assert(SystemInt::satisfies<MemoryScope_v::Inner>);
static_assert(SystemInt::satisfies<MemoryScope_v::System>);

static_assert(GpuInt::satisfies<MemoryScope_v::Cta>,
              "ScopedFence<Gpu>::satisfies<Cta> must be true. A device-wide fence publishes at block scope too, "
              "because Cta sits below Gpu on the accelerator trunk.");
static_assert(GpuInt::satisfies<MemoryScope_v::Gpu>);
static_assert(!GpuInt::satisfies<MemoryScope_v::Inner>,
              "ScopedFence<Gpu>::satisfies<Inner> must be false. A device fence has no ordering relation to an "
              "inner-shareable domain, because the two trunks are incomparable.");

static_assert(!CtaInt::satisfies<MemoryScope_v::Gpu>,
              "ScopedFence<Cta>::satisfies<Gpu> must be false. A block-scope fence is too narrow for a device-wide "
              "requirement.");

static_assert(!CtaInt::satisfies<MemoryScope_v::Inner>);
static_assert(!InnerInt::satisfies<MemoryScope_v::Cta>);
static_assert(OuterInt::satisfies<MemoryScope_v::Inner>,
              "ScopedFence<Outer>::satisfies<Inner> must be true. An outer-shareable fence subsumes an "
              "inner-shareable requirement within the same trunk.");

static_assert(CtaInt::satisfies<MemoryScope_v::Thread>);
static_assert(InnerInt::satisfies<MemoryScope_v::Thread>);
static_assert(ThreadInt::satisfies<MemoryScope_v::Thread>);
static_assert(!ThreadInt::satisfies<MemoryScope_v::Cta>,
              "A thread-local provider does not subsume a block-scope requirement.");

inline constexpr auto gpu_to_cta = GpuInt{42}.relax<MemoryScope_v::Cta>();
static_assert(gpu_to_cta.peek() == 42 && gpu_to_cta.scope == MemoryScope_v::Cta);

inline constexpr auto cta_to_thread = CtaInt{9}.relax<MemoryScope_v::Thread>();
static_assert(cta_to_thread.peek() == 9 && cta_to_thread.scope == MemoryScope_v::Thread);

inline constexpr auto system_to_inner = SystemInt{5}.relax<MemoryScope_v::Inner>();
static_assert(system_to_inner.peek() == 5 && system_to_inner.scope == MemoryScope_v::Inner);

inline constexpr auto cta_reflexive = CtaInt{55}.relax<MemoryScope_v::Cta>();
static_assert(cta_reflexive.peek() == 55);

template <typename W2, MemoryScope_v Target>
concept can_relax = requires(W2 w) {
    { std::move(w).template relax<Target>() };
};

static_assert(can_relax<GpuInt, MemoryScope_v::Cta>);
static_assert(can_relax<CtaInt, MemoryScope_v::Thread>);
static_assert(can_relax<SystemInt, MemoryScope_v::Inner>);
static_assert(can_relax<CtaInt, MemoryScope_v::Cta>);
static_assert(!can_relax<CtaInt, MemoryScope_v::Gpu>,
              "relax<Gpu> on a ScopedFence<Cta> wrapper must be rejected. Claiming a value is device-visible when it "
              "was only published at block scope would offer it to observers the fence never reached.");
static_assert(!can_relax<CtaInt, MemoryScope_v::Inner>,
              "relax<Inner> on a ScopedFence<Cta> wrapper must be rejected. The two trunks are incomparable.");
static_assert(!can_relax<InnerInt, MemoryScope_v::Cta>);
static_assert(!can_relax<ThreadInt, MemoryScope_v::Cta>);

static_assert(CtaInt::value_type_name().ends_with("int"));
static_assert(CtaInt::lattice_name() == "MemoryScopeLattice::At<Cta>");
static_assert(GpuInt::lattice_name() == "MemoryScopeLattice::At<Gpu>");
static_assert(InnerInt::lattice_name() == "MemoryScopeLattice::At<Inner>");

[[nodiscard]] consteval bool swap_exchanges_within_same_scope() noexcept {
    CtaInt a{10};
    CtaInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_scope());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    CtaInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    CtaInt a{42};
    CtaInt b{42};
    CtaInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

static_assert(std::is_same_v<scoped_fence::Cta<int>, CtaInt>);
static_assert(std::is_same_v<scoped_fence::System<int>, SystemInt>);
static_assert(scoped_fence::Gpu<double>::scope == MemoryScope_v::Gpu);
static_assert(!std::is_same_v<CtaInt, InnerInt>);
static_assert(std::is_copy_constructible_v<CtaInt>);

inline constexpr auto minted = mint_scoped_fence<MemoryScope_v::Gpu, int>(99);
static_assert(minted.peek() == 99 && minted.scope == MemoryScope_v::Gpu);

template <typename Provider>
concept covers_cta_requirement = Provider::template satisfies<MemoryScope_v::Cta>;

static_assert(covers_cta_requirement<CtaInt>, "A block-scope fence must pass a Cta-requirement gate.");
static_assert(covers_cta_requirement<GpuInt>, "A device-wide fence must pass a Cta-requirement gate.");
static_assert(!covers_cta_requirement<WarpInt>,
              "A warp-only fence must be rejected at a Cta-requirement gate. It does not subsume the block-scope "
              "requirement.");
static_assert(!covers_cta_requirement<InnerInt>, "An inner-shareable fence must be rejected at a Cta-requirement "
                                                 "gate, because the two trunks are incomparable.");

// Constant evaluation can hide a defect that appears only when the inline
// body runs with arguments the compiler cannot fold.
inline void runtime_smoke_test() {
    int seed = 21;
    CtaInt n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto r = GpuInt{seed}.relax<MemoryScope_v::Cta>();
    if (r.peek() != 21 || r.scope != MemoryScope_v::Cta) std::abort();

    auto m = mint_scoped_fence<MemoryScope_v::Outer, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    CtaInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool s1 = GpuInt::satisfies<MemoryScope_v::Cta>;
    [[maybe_unused]] bool s2 = CtaInt::satisfies<MemoryScope_v::Inner>;
    if (!s1 || s2) std::abort();

    scoped_fence::Thread<int> alias_thread{0};
    scoped_fence::Outer<int> alias_outer{456};
    if (alias_thread.peek() != 0 || alias_outer.peek() != 456) std::abort();
}

}  // namespace detail::scoped_fence_self_test

}  // namespace crucible::safety
