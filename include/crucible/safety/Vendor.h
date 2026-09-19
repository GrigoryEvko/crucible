#pragma once

// Vendor<Backend, T> pins a value to the backend that produced it.
//
// The backend order is a partial order, not a chain.  None is the
// bottom, Portable is the top, and the concrete backends between them
// are mutually incomparable.  Portable sits above each concrete
// backend because a portable value runs everywhere a specific one
// does.
//
// satisfies<Required> asks whether the pinned backend covers what a
// consumer demands: stronger satisfies weaker.  Portable therefore
// satisfies every requirement, a concrete backend satisfies only
// itself and None, and two concrete backends satisfy neither.
//
// relax<Weaker> moves down the order and never up.  Moving up would
// claim more portability than the value carries, so the substrate's
// weaken(), which does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_VendorLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::VendorLattice;
using VendorBackend_v = ::crucible::algebra::lattices::VendorBackend;

template <VendorBackend_v Backend, typename T>
class [[nodiscard]] Vendor {
public:
    using value_type = T;
    using lattice_type = VendorLattice::At<Backend>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr VendorBackend_v backend = Backend;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a backend that produced
    // nothing.  Deleting it would be the truthful choice, but it is
    // kept so the wrapper can sit in an array element or a
    // default-initialized struct field.  A site that knows its
    // backend uses the explicit constructor.  Vendor<None, T>{} is
    // the unbound slot.
    constexpr Vendor() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Vendor(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Vendor(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                        && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr Vendor(const Vendor&) = default;
    constexpr Vendor(Vendor&&) = default;
    constexpr Vendor& operator=(const Vendor&) = default;
    constexpr Vendor& operator=(Vendor&&) = default;
    ~Vendor() = default;

    [[nodiscard]] friend constexpr bool operator==(Vendor const& a,
                                                   Vendor const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(Vendor& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Vendor& a, Vendor& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <VendorBackend_v RequiredBackend>
    static constexpr bool satisfies = VendorLattice::leq(RequiredBackend, Backend);

    template <VendorBackend_v WeakerBackend>
        requires(VendorLattice::leq(WeakerBackend, Backend))
    [[nodiscard]] constexpr Vendor<WeakerBackend, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Vendor<WeakerBackend, T>{this->peek()};
    }

    template <VendorBackend_v WeakerBackend>
        requires(VendorLattice::leq(WeakerBackend, Backend))
    [[nodiscard]] constexpr Vendor<WeakerBackend, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Vendor<WeakerBackend, T>{std::move(impl_).consume()};
    }
};

namespace vendor {
template <typename T>
using None = Vendor<VendorBackend_v::None, T>;
template <typename T>
using Cpu = Vendor<VendorBackend_v::CPU, T>;
template <typename T>
using Nv = Vendor<VendorBackend_v::NV, T>;
template <typename T>
using Amd = Vendor<VendorBackend_v::AMD, T>;
template <typename T>
using Tpu = Vendor<VendorBackend_v::TPU, T>;
template <typename T>
using Trn = Vendor<VendorBackend_v::TRN, T>;
template <typename T>
using Cer = Vendor<VendorBackend_v::CER, T>;
template <typename T>
using Portable = Vendor<VendorBackend_v::Portable, T>;
}  // namespace vendor

namespace detail::vendor_layout {

template <typename T>
using PortableV = Vendor<VendorBackend_v::Portable, T>;
template <typename T>
using NvV = Vendor<VendorBackend_v::NV, T>;
template <typename T>
using AmdV = Vendor<VendorBackend_v::AMD, T>;
template <typename T>
using NoneV = Vendor<VendorBackend_v::None, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableV, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableV, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableV, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NvV, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NvV, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AmdV, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AmdV, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneV, int);

}  // namespace detail::vendor_layout

static_assert(sizeof(Vendor<VendorBackend_v::Portable, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::NV, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::AMD, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::TPU, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::TRN, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::CER, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::CPU, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::None, int>) == sizeof(int));
static_assert(sizeof(Vendor<VendorBackend_v::Portable, double>) == sizeof(double));

namespace detail::vendor_self_test {

using PortableInt = Vendor<VendorBackend_v::Portable, int>;
using CpuInt = Vendor<VendorBackend_v::CPU, int>;
using NvInt = Vendor<VendorBackend_v::NV, int>;
using AmdInt = Vendor<VendorBackend_v::AMD, int>;
using TpuInt = Vendor<VendorBackend_v::TPU, int>;
using TrnInt = Vendor<VendorBackend_v::TRN, int>;
using CerInt = Vendor<VendorBackend_v::CER, int>;
using NoneInt = Vendor<VendorBackend_v::None, int>;

inline constexpr NvInt n_default{};
static_assert(n_default.peek() == 0);
static_assert(n_default.backend == VendorBackend_v::NV);

inline constexpr NvInt n_explicit{42};
static_assert(n_explicit.peek() == 42);

inline constexpr NvInt n_in_place{std::in_place, 7};
static_assert(n_in_place.peek() == 7);

static_assert(PortableInt::backend == VendorBackend_v::Portable);
static_assert(CpuInt::backend == VendorBackend_v::CPU);
static_assert(NvInt::backend == VendorBackend_v::NV);
static_assert(AmdInt::backend == VendorBackend_v::AMD);
static_assert(TpuInt::backend == VendorBackend_v::TPU);
static_assert(TrnInt::backend == VendorBackend_v::TRN);
static_assert(CerInt::backend == VendorBackend_v::CER);
static_assert(NoneInt::backend == VendorBackend_v::None);

static_assert(PortableInt::satisfies<VendorBackend_v::Portable>);
static_assert(PortableInt::satisfies<VendorBackend_v::CPU>);
static_assert(PortableInt::satisfies<VendorBackend_v::NV>);
static_assert(PortableInt::satisfies<VendorBackend_v::AMD>);
static_assert(PortableInt::satisfies<VendorBackend_v::TPU>);
static_assert(PortableInt::satisfies<VendorBackend_v::TRN>);
static_assert(PortableInt::satisfies<VendorBackend_v::CER>);
static_assert(PortableInt::satisfies<VendorBackend_v::None>);

static_assert(NvInt::satisfies<VendorBackend_v::NV>);
static_assert(NvInt::satisfies<VendorBackend_v::None>);
static_assert(!NvInt::satisfies<VendorBackend_v::AMD>, "Vendor<NV> must not satisfy AMD.  NV and AMD are incomparable. "
                                                       "If they become comparable, an NV value flows into a function "
                                                       "that requires AMD.");
static_assert(!NvInt::satisfies<VendorBackend_v::TPU>);
static_assert(!NvInt::satisfies<VendorBackend_v::TRN>);
static_assert(!NvInt::satisfies<VendorBackend_v::CER>);
static_assert(!NvInt::satisfies<VendorBackend_v::CPU>);
static_assert(!NvInt::satisfies<VendorBackend_v::Portable>,
              "Vendor<NV> must not satisfy Portable.  Portable demands every "
              "backend; NV provides one.");

static_assert(AmdInt::satisfies<VendorBackend_v::AMD>);
static_assert(AmdInt::satisfies<VendorBackend_v::None>);
static_assert(!AmdInt::satisfies<VendorBackend_v::NV>);
static_assert(!AmdInt::satisfies<VendorBackend_v::TPU>);
static_assert(!AmdInt::satisfies<VendorBackend_v::Portable>);

static_assert(CpuInt::satisfies<VendorBackend_v::CPU>);
static_assert(CpuInt::satisfies<VendorBackend_v::None>);
static_assert(!CpuInt::satisfies<VendorBackend_v::NV>);
static_assert(!CpuInt::satisfies<VendorBackend_v::Portable>);

static_assert(NoneInt::satisfies<VendorBackend_v::None>);
static_assert(!NoneInt::satisfies<VendorBackend_v::NV>);
static_assert(!NoneInt::satisfies<VendorBackend_v::CPU>);
static_assert(!NoneInt::satisfies<VendorBackend_v::Portable>,
              "Vendor<None> must not satisfy Portable.  None carries no "
              "backend, so it satisfies no requirement.");

inline constexpr auto from_portable_to_nv = PortableInt{42}.relax<VendorBackend_v::NV>();
static_assert(from_portable_to_nv.peek() == 42);
static_assert(from_portable_to_nv.backend == VendorBackend_v::NV);

inline constexpr auto from_portable_to_amd = PortableInt{42}.relax<VendorBackend_v::AMD>();
static_assert(from_portable_to_amd.backend == VendorBackend_v::AMD);

inline constexpr auto from_portable_to_none = PortableInt{42}.relax<VendorBackend_v::None>();
static_assert(from_portable_to_none.backend == VendorBackend_v::None);

inline constexpr auto from_nv_to_none = NvInt{99}.relax<VendorBackend_v::None>();
static_assert(from_nv_to_none.peek() == 99);
static_assert(from_nv_to_none.backend == VendorBackend_v::None);

inline constexpr auto from_amd_to_none = AmdInt{99}.relax<VendorBackend_v::None>();
static_assert(from_amd_to_none.backend == VendorBackend_v::None);

inline constexpr auto from_nv_to_nv = NvInt{55}.relax<VendorBackend_v::NV>();
static_assert(from_nv_to_nv.peek() == 55);

inline constexpr auto from_portable_to_portable = PortableInt{77}.relax<VendorBackend_v::Portable>();
static_assert(from_portable_to_portable.peek() == 77);

inline constexpr auto from_none_to_none = NoneInt{0}.relax<VendorBackend_v::None>();
static_assert(from_none_to_none.peek() == 0);

template <typename W, VendorBackend_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<PortableInt, VendorBackend_v::Portable>);
static_assert(can_relax<PortableInt, VendorBackend_v::CPU>);
static_assert(can_relax<PortableInt, VendorBackend_v::NV>);
static_assert(can_relax<PortableInt, VendorBackend_v::AMD>);
static_assert(can_relax<PortableInt, VendorBackend_v::TPU>);
static_assert(can_relax<PortableInt, VendorBackend_v::TRN>);
static_assert(can_relax<PortableInt, VendorBackend_v::CER>);
static_assert(can_relax<PortableInt, VendorBackend_v::None>);

static_assert(can_relax<NvInt, VendorBackend_v::NV>);
static_assert(can_relax<NvInt, VendorBackend_v::None>);
static_assert(can_relax<AmdInt, VendorBackend_v::AMD>);
static_assert(can_relax<AmdInt, VendorBackend_v::None>);
static_assert(can_relax<NoneInt, VendorBackend_v::None>);

static_assert(!can_relax<NvInt, VendorBackend_v::AMD>, "relax<AMD> on a Vendor<NV> must be rejected.  NV and AMD are "
                                                       "incomparable, so retyping one as the other is the cross-vendor "
                                                       "error the partial order exists to catch.");
static_assert(!can_relax<NvInt, VendorBackend_v::TPU>);
static_assert(!can_relax<NvInt, VendorBackend_v::TRN>);
static_assert(!can_relax<NvInt, VendorBackend_v::CER>);
static_assert(!can_relax<NvInt, VendorBackend_v::CPU>);
static_assert(!can_relax<AmdInt, VendorBackend_v::NV>);
static_assert(!can_relax<TpuInt, VendorBackend_v::CER>);

static_assert(!can_relax<NvInt, VendorBackend_v::Portable>,
              "relax<Portable> on a Vendor<NV> must be rejected.  It would "
              "claim a portability the NV-pinned value does not have, erasing "
              "the difference between a value that runs everywhere and one "
              "that runs on NV.");
static_assert(!can_relax<AmdInt, VendorBackend_v::Portable>);
static_assert(!can_relax<NoneInt, VendorBackend_v::NV>,
              "relax<NV> on a Vendor<None> must be rejected.  None carries no "
              "backend to specialize, so the pin would come out of nothing.");
static_assert(!can_relax<NoneInt, VendorBackend_v::Portable>);

static_assert(NvInt::value_type_name().ends_with("int"));
static_assert(NvInt::lattice_name() == "VendorLattice::At<NV>");
static_assert(AmdInt::lattice_name() == "VendorLattice::At<AMD>");
static_assert(PortableInt::lattice_name() == "VendorLattice::At<Portable>");
static_assert(NoneInt::lattice_name() == "VendorLattice::At<None>");

[[nodiscard]] consteval bool swap_exchanges_within_same_backend() noexcept {
    NvInt a{10};
    NvInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_backend());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    NvInt a{10};
    NvInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    NvInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    NvInt a{42};
    NvInt b{42};
    NvInt c{43};
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

static_assert(can_equality_compare<NvInt>);
static_assert(!can_equality_compare<Vendor<VendorBackend_v::NV, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<Vendor<VendorBackend_v::NV, NoEqualityT>>,
              "Vendor<Backend, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<Vendor<VendorBackend_v::NV, NoEqualityT>>);

struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, VendorBackend_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, VendorBackend_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using PortableMoveOnly = Vendor<VendorBackend_v::Portable, MoveOnlyT>;
static_assert(can_relax_rvalue<PortableMoveOnly, VendorBackend_v::NV>,
              "relax on an rvalue must accept a move-only T.  The rvalue "
              "overload moves through consume().");
static_assert(!can_relax_lvalue<PortableMoveOnly, VendorBackend_v::NV>,
              "relax on a const lvalue must reject a move-only T.  That "
              "overload requires a copy constructor.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    PortableMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<VendorBackend_v::AMD>();
    return dst.peek().v == 77 && dst.backend == VendorBackend_v::AMD;
}
static_assert(relax_move_only_works());

static_assert(NvInt::value_type_name().size() > 0);
static_assert(NvInt::lattice_name().size() > 0);
static_assert(NvInt::lattice_name().starts_with("VendorLattice::At<"));

static_assert(vendor::Portable<int>::backend == VendorBackend_v::Portable);
static_assert(vendor::Nv<int>::backend == VendorBackend_v::NV);
static_assert(vendor::Amd<int>::backend == VendorBackend_v::AMD);
static_assert(vendor::None<int>::backend == VendorBackend_v::None);

static_assert(std::is_same_v<vendor::Nv<double>, Vendor<VendorBackend_v::NV, double>>);

template <typename W>
concept is_nv_admissible = W::template satisfies<VendorBackend_v::NV>;

static_assert(is_nv_admissible<PortableInt>, "A Portable value must pass a gate that requires NV.");
static_assert(is_nv_admissible<NvInt>, "An NV value must pass a gate that requires NV.");
static_assert(!is_nv_admissible<AmdInt>, "An AMD value must not pass a gate that requires NV.  The gate "
                                         "is where a cross-vendor mix becomes a compile error instead of "
                                         "a driver rejection at run time.");
static_assert(!is_nv_admissible<TpuInt>);
static_assert(!is_nv_admissible<CpuInt>);
static_assert(!is_nv_admissible<NoneInt>);

template <typename W>
concept is_portable_required = W::template satisfies<VendorBackend_v::Portable>;

static_assert(is_portable_required<PortableInt>, "A Portable value must pass a gate that requires Portable.");
static_assert(!is_portable_required<NvInt>, "An NV value must not pass a gate that requires Portable.  A "
                                            "reference result has to hold on every backend, not on one.");
static_assert(!is_portable_required<AmdInt>);
static_assert(!is_portable_required<CpuInt>);

inline void runtime_smoke_test() {
    NvInt a{};
    NvInt b{42};
    NvInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (NvInt::backend != VendorBackend_v::NV) {
        std::abort();
    }

    NvInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    NvInt sx{1};
    NvInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    PortableInt source{77};
    auto specialized_nv = source.relax<VendorBackend_v::NV>();
    auto specialized_amd = std::move(source).relax<VendorBackend_v::AMD>();
    [[maybe_unused]] auto vnv = specialized_nv.peek();
    [[maybe_unused]] auto vamd = specialized_amd.peek();

    NvInt nv_value{55};
    auto erased = std::move(nv_value).relax<VendorBackend_v::None>();
    [[maybe_unused]] auto verased = erased.peek();

    [[maybe_unused]] bool s1 = PortableInt::satisfies<VendorBackend_v::NV>;
    [[maybe_unused]] bool s2 = NvInt::satisfies<VendorBackend_v::AMD>;

    NvInt eq_a{42};
    NvInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    NvInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    vendor::Portable<int> alias_portable{123};
    vendor::Nv<int> alias_nv{456};
    vendor::Amd<int> alias_amd{789};
    vendor::None<int> alias_none{0};
    [[maybe_unused]] auto vp = alias_portable.peek();
    [[maybe_unused]] auto vn = alias_nv.peek();
    [[maybe_unused]] auto vd = alias_amd.peek();
    [[maybe_unused]] auto vo = alias_none.peek();

    [[maybe_unused]] bool can_nv_pass = is_nv_admissible<PortableInt>;
    [[maybe_unused]] bool can_portable_pass = is_portable_required<PortableInt>;
}

}  // namespace detail::vendor_self_test

}  // namespace crucible::safety
