#pragma once

// W pins the SIMD instruction-set capability a value was produced with.
// The capabilities form a partial order over two trunks, one for x86
// running up to Avx512Bw and one for ARM running from Neon up to Sve2.
// The trunks meet only at the shared bottom Scalar and the shared top
// Portable.
//
// Capabilities on different trunks are incomparable.  An x86 value never
// runs on ARM hardware, so neither trunk satisfies the other.
//
// W is the capability the value has.  A consumer requirement R is met
// when W subsumes R, which means R sits at or below W.  More capable is
// higher, so a value built for Avx512Bw meets an Avx2 requirement and a
// value built for Avx2 does not meet an Avx512Bw one.
//
// relax<Weaker>() is sound because under-claiming capability only narrows
// where the value is offered.  It never claims the value runs somewhere
// it cannot.  Relaxing up, or across to an incomparable trunk, is a
// compile error.  It would assert the value runs on hardware it was never
// legalized for.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/SimdIsaLattice.h>

#include <concepts>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::SimdIsaLattice;
using SimdIsa_v = ::crucible::algebra::lattices::SimdIsa;

template <SimdIsa_v W, typename T>
class [[nodiscard]] SimdWidthPinned {
public:
    using value_type = T;
    using lattice_type = SimdIsaLattice::At<W>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr SimdIsa_v isa = W;

private:
    graded_type impl_;

public:
    constexpr SimdWidthPinned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit SimdWidthPinned(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit SimdWidthPinned(std::in_place_t,
                                       Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr SimdWidthPinned(const SimdWidthPinned&) = default;
    constexpr SimdWidthPinned(SimdWidthPinned&&) = default;
    constexpr SimdWidthPinned& operator=(const SimdWidthPinned&) = default;
    constexpr SimdWidthPinned& operator=(SimdWidthPinned&&) = default;
    ~SimdWidthPinned() = default;

    [[nodiscard]] friend constexpr bool operator==(SimdWidthPinned const& a,
                                                   SimdWidthPinned const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(SimdWidthPinned& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(SimdWidthPinned& a, SimdWidthPinned& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <SimdIsa_v Required>
    static constexpr bool satisfies = SimdIsaLattice::leq(Required, W);

    template <SimdIsa_v Weaker>
        requires(SimdIsaLattice::leq(Weaker, W))
    [[nodiscard]] constexpr SimdWidthPinned<Weaker, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return SimdWidthPinned<Weaker, T>{this->peek()};
    }

    template <SimdIsa_v Weaker>
        requires(SimdIsaLattice::leq(Weaker, W))
    [[nodiscard]] constexpr SimdWidthPinned<Weaker, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return SimdWidthPinned<Weaker, T>{std::move(impl_).consume()};
    }
};

template <SimdIsa_v W, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr SimdWidthPinned<W, T>
mint_simd_width_pinned(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return SimdWidthPinned<W, T>{std::in_place, std::forward<Args>(args)...};
}

namespace simd_pin {
template <typename T>
using Scalar = SimdWidthPinned<SimdIsa_v::Scalar, T>;
template <typename T>
using Sse2 = SimdWidthPinned<SimdIsa_v::Sse2, T>;
template <typename T>
using Sse3 = SimdWidthPinned<SimdIsa_v::Sse3, T>;
template <typename T>
using Ssse3 = SimdWidthPinned<SimdIsa_v::Ssse3, T>;
template <typename T>
using Sse41 = SimdWidthPinned<SimdIsa_v::Sse41, T>;
template <typename T>
using Sse42 = SimdWidthPinned<SimdIsa_v::Sse42, T>;
template <typename T>
using Avx2 = SimdWidthPinned<SimdIsa_v::Avx2, T>;
template <typename T>
using Avx512F = SimdWidthPinned<SimdIsa_v::Avx512F, T>;
template <typename T>
using Avx512Bw = SimdWidthPinned<SimdIsa_v::Avx512Bw, T>;
template <typename T>
using Neon = SimdWidthPinned<SimdIsa_v::Neon, T>;
template <typename T>
using NeonFp16 = SimdWidthPinned<SimdIsa_v::NeonFp16, T>;
template <typename T>
using NeonDotProduct = SimdWidthPinned<SimdIsa_v::NeonDotProduct, T>;
template <typename T>
using Sve = SimdWidthPinned<SimdIsa_v::Sve, T>;
template <typename T>
using Sve2 = SimdWidthPinned<SimdIsa_v::Sve2, T>;
template <typename T>
using Portable = SimdWidthPinned<SimdIsa_v::Portable, T>;
}  // namespace simd_pin

namespace detail::simd_width_pinned_layout {

template <typename T>
using ScalarSw = SimdWidthPinned<SimdIsa_v::Scalar, T>;
template <typename T>
using Avx2Sw = SimdWidthPinned<SimdIsa_v::Avx2, T>;
template <typename T>
using Avx512BwSw = SimdWidthPinned<SimdIsa_v::Avx512Bw, T>;
template <typename T>
using NeonSw = SimdWidthPinned<SimdIsa_v::Neon, T>;
template <typename T>
using PortableSw = SimdWidthPinned<SimdIsa_v::Portable, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(ScalarSw, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ScalarSw, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx2Sw, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx2Sw, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Avx512BwSw, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NeonSw, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableSw, int);

}  // namespace detail::simd_width_pinned_layout

static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Scalar, int>) == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Avx2, int>) == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Avx512Bw, int>) == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Neon, int>) == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Sve2, int>) == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Portable, int>) == sizeof(int));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Avx2, double>) == sizeof(double));
static_assert(sizeof(SimdWidthPinned<SimdIsa_v::Scalar, char>) == sizeof(char));

namespace detail::simd_width_pinned_self_test {

using ScalarInt = SimdWidthPinned<SimdIsa_v::Scalar, int>;
using Sse2Int = SimdWidthPinned<SimdIsa_v::Sse2, int>;
using Avx2Int = SimdWidthPinned<SimdIsa_v::Avx2, int>;
using Avx512BwInt = SimdWidthPinned<SimdIsa_v::Avx512Bw, int>;
using NeonInt = SimdWidthPinned<SimdIsa_v::Neon, int>;
using Sve2Int = SimdWidthPinned<SimdIsa_v::Sve2, int>;
using PortableInt = SimdWidthPinned<SimdIsa_v::Portable, int>;

inline constexpr Avx2Int a_default{};
static_assert(a_default.peek() == 0);
static_assert(Avx2Int::isa == SimdIsa_v::Avx2);

inline constexpr Avx2Int a_explicit{42};
static_assert(a_explicit.peek() == 42);

inline constexpr Avx2Int a_in_place{std::in_place, 7};
static_assert(a_in_place.peek() == 7);

static_assert(ScalarInt::isa == SimdIsa_v::Scalar);
static_assert(PortableInt::isa == SimdIsa_v::Portable);
static_assert(ScalarInt::modality == ::crucible::algebra::ModalityKind::Absolute);

static_assert(PortableInt::satisfies<SimdIsa_v::Scalar>);
static_assert(PortableInt::satisfies<SimdIsa_v::Avx2>);
static_assert(PortableInt::satisfies<SimdIsa_v::Neon>);
static_assert(PortableInt::satisfies<SimdIsa_v::Portable>);

static_assert(Avx512BwInt::satisfies<SimdIsa_v::Avx2>,
              "SimdWidthPinned<Avx512Bw>::satisfies<Avx2> must be true. AVX512BW hardware runs AVX2 code, because "
              "Avx2 sits below Avx512Bw on the x86 trunk.");
static_assert(Avx512BwInt::satisfies<SimdIsa_v::Avx512Bw>);
static_assert(!Avx512BwInt::satisfies<SimdIsa_v::Neon>,
              "SimdWidthPinned<Avx512Bw>::satisfies<Neon> must be false. An x86 value never runs on ARM, because the "
              "two trunks are incomparable.");

static_assert(!Avx2Int::satisfies<SimdIsa_v::Avx512Bw>,
              "SimdWidthPinned<Avx2>::satisfies<Avx512Bw> must be false. An AVX2 provider lacks AVX512 "
              "instructions.");

static_assert(!Avx2Int::satisfies<SimdIsa_v::Neon>);
static_assert(!NeonInt::satisfies<SimdIsa_v::Avx2>);
static_assert(Sve2Int::satisfies<SimdIsa_v::Neon>,
              "SimdWidthPinned<Sve2>::satisfies<Neon> must be true. SVE2 subsumes NEON within the same trunk.");

static_assert(Avx2Int::satisfies<SimdIsa_v::Scalar>);
static_assert(NeonInt::satisfies<SimdIsa_v::Scalar>);
static_assert(ScalarInt::satisfies<SimdIsa_v::Scalar>);
static_assert(!ScalarInt::satisfies<SimdIsa_v::Avx2>, "A scalar-only provider does not subsume an AVX2 requirement.");

inline constexpr auto avx512_to_avx2 = Avx512BwInt{42}.relax<SimdIsa_v::Avx2>();
static_assert(avx512_to_avx2.peek() == 42 && avx512_to_avx2.isa == SimdIsa_v::Avx2);

inline constexpr auto avx2_to_scalar = Avx2Int{9}.relax<SimdIsa_v::Scalar>();
static_assert(avx2_to_scalar.peek() == 9 && avx2_to_scalar.isa == SimdIsa_v::Scalar);

inline constexpr auto portable_to_neon = PortableInt{5}.relax<SimdIsa_v::Neon>();
static_assert(portable_to_neon.peek() == 5 && portable_to_neon.isa == SimdIsa_v::Neon);

inline constexpr auto avx2_reflexive = Avx2Int{55}.relax<SimdIsa_v::Avx2>();
static_assert(avx2_reflexive.peek() == 55);

template <typename W2, SimdIsa_v Target>
concept can_relax = requires(W2 w) {
    { std::move(w).template relax<Target>() };
};

static_assert(can_relax<Avx512BwInt, SimdIsa_v::Avx2>);
static_assert(can_relax<Avx2Int, SimdIsa_v::Scalar>);
static_assert(can_relax<PortableInt, SimdIsa_v::Neon>);
static_assert(can_relax<Avx2Int, SimdIsa_v::Avx2>);
static_assert(!can_relax<Avx2Int, SimdIsa_v::Avx512Bw>,
              "relax<Avx512Bw> on a SimdWidthPinned<Avx2> wrapper must be rejected. Claiming a value runs AVX512 when "
              "it was only legalized for AVX2 would offer it to hardware it cannot use.");
static_assert(!can_relax<Avx2Int, SimdIsa_v::Neon>,
              "relax<Neon> on a SimdWidthPinned<Avx2> wrapper must be rejected. The two trunks are incomparable.");
static_assert(!can_relax<NeonInt, SimdIsa_v::Avx2>);
static_assert(!can_relax<ScalarInt, SimdIsa_v::Avx2>);

static_assert(Avx2Int::value_type_name().ends_with("int"));
static_assert(Avx2Int::lattice_name() == "SimdIsaLattice::At<Avx2>");
static_assert(Avx512BwInt::lattice_name() == "SimdIsaLattice::At<Avx512Bw>");
static_assert(NeonInt::lattice_name() == "SimdIsaLattice::At<Neon>");

[[nodiscard]] consteval bool swap_exchanges_within_same_isa() noexcept {
    Avx2Int a{10};
    Avx2Int b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_isa());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    Avx2Int a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    Avx2Int a{42};
    Avx2Int b{42};
    Avx2Int c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

static_assert(std::is_same_v<simd_pin::Avx2<int>, Avx2Int>);
static_assert(std::is_same_v<simd_pin::Portable<int>, PortableInt>);
static_assert(simd_pin::Avx512Bw<double>::isa == SimdIsa_v::Avx512Bw);
static_assert(!std::is_same_v<Avx2Int, NeonInt>);
static_assert(std::is_copy_constructible_v<Avx2Int>);

inline constexpr auto minted = mint_simd_width_pinned<SimdIsa_v::Avx512Bw, int>(99);
static_assert(minted.peek() == 99 && minted.isa == SimdIsa_v::Avx512Bw);

template <typename Provider>
concept runs_on_avx2_host = Provider::template satisfies<SimdIsa_v::Avx2>;

static_assert(runs_on_avx2_host<Avx2Int>, "An AVX2 kernel must pass an AVX2-host legalization gate.");
static_assert(runs_on_avx2_host<Avx512BwInt>, "An AVX512BW kernel must pass an AVX2-host gate.");
static_assert(!runs_on_avx2_host<Sse2Int>, "An SSE2-only kernel must be rejected at an AVX2-host gate. It does not "
                                           "subsume the AVX2 requirement.");
static_assert(!runs_on_avx2_host<NeonInt>, "A NEON kernel must be rejected at an AVX2-host gate, because the two "
                                           "trunks are incomparable.");

// Constant evaluation can hide a defect that appears only when the inline
// body runs with arguments the compiler cannot fold.
inline void runtime_smoke_test() {
    int seed = 21;
    Avx2Int n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto r = Avx512BwInt{seed}.relax<SimdIsa_v::Avx2>();
    if (r.peek() != 21 || r.isa != SimdIsa_v::Avx2) std::abort();

    auto m = mint_simd_width_pinned<SimdIsa_v::Sve2, int>(seed);
    if (std::move(m).consume() != 21) std::abort();

    Avx2Int a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool s1 = Avx512BwInt::satisfies<SimdIsa_v::Avx2>;
    [[maybe_unused]] bool s2 = Avx2Int::satisfies<SimdIsa_v::Neon>;
    if (!s1 || s2) std::abort();

    simd_pin::Scalar<int> alias_scalar{0};
    simd_pin::Sve2<int> alias_sve{456};
    if (alias_scalar.peek() != 0 || alias_sve.peek() != 456) std::abort();
}

}  // namespace detail::simd_width_pinned_self_test

}  // namespace crucible::safety
