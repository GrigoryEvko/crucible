#pragma once

// Scope pins the lifetime a value of type T was produced under.  The
// order is
//
//   PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET
//
// and it reads "a narrower lifetime fits inside a wider one", so
// PER_REQUEST is the bottom and PER_FLEET the top.
//
// Subsumption runs opposite to that order.  A producer at a wider scope
// satisfies a consumer at a narrower one, because a fleet-scoped value is
// available inside any single request.  So satisfies<R> holds when R sits
// at or below Scope, and relax<NarrowerScope>() converts down the order.
//
// There is deliberately no widen().  A request-scoped value dies when the
// request ends, and re-labelling it PER_FLEET would carry it across
// requests.  The only way to obtain a PER_FLEET value is to construct one
// at a site that commits at fleet scope.
//
// The name relax reads backwards at first, because narrowing the
// availability window sounds like tightening.  It is not.  Narrowing
// weakens the persistence promise, since the value is no longer offered
// beyond the narrower scope.
//
// The substrate offers an operation that moves a value up the order.  The
// wrapper does not expose it.  That operation is the cross-scope leak
// path.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/LifetimeLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::LifetimeLattice;
using Lifetime_v = ::crucible::algebra::lattices::Lifetime;

template <Lifetime_v Scope, typename T>
class [[nodiscard]] OpaqueLifetime {
public:
    using value_type = T;
    using lattice_type = LifetimeLattice::At<Scope>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr Lifetime_v scope = Scope;

private:
    graded_type impl_;

public:
    // Every constructor claims the bytes were produced under the pinned
    // scope, and no constructor can verify that claim.  The obligation
    // falls on the construction site.  A default-constructed value at the
    // widest scope is the weakest case, because T{} carries no evidence of
    // where it came from.
    constexpr OpaqueLifetime() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit OpaqueLifetime(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit OpaqueLifetime(std::in_place_t,
                                      Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                               && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr OpaqueLifetime(const OpaqueLifetime&) = default;
    constexpr OpaqueLifetime(OpaqueLifetime&&) = default;
    constexpr OpaqueLifetime& operator=(const OpaqueLifetime&) = default;
    constexpr OpaqueLifetime& operator=(OpaqueLifetime&&) = default;
    ~OpaqueLifetime() = default;

    [[nodiscard]] friend constexpr bool operator==(OpaqueLifetime const& a,
                                                   OpaqueLifetime const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(OpaqueLifetime& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(OpaqueLifetime& a, OpaqueLifetime& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    template <Lifetime_v RequiredScope>
    static constexpr bool satisfies = LifetimeLattice::leq(RequiredScope, Scope);

    template <Lifetime_v NarrowerScope>
        requires(LifetimeLattice::leq(NarrowerScope, Scope))
    [[nodiscard]] constexpr OpaqueLifetime<NarrowerScope, T>
    relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return OpaqueLifetime<NarrowerScope, T>{this->peek()};
    }

    template <Lifetime_v NarrowerScope>
        requires(LifetimeLattice::leq(NarrowerScope, Scope))
    [[nodiscard]] constexpr OpaqueLifetime<NarrowerScope, T>
    relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return OpaqueLifetime<NarrowerScope, T>{std::move(impl_).consume()};
    }
};

namespace opaque_lifetime {
template <typename T>
using PerRequest = OpaqueLifetime<Lifetime_v::PER_REQUEST, T>;
template <typename T>
using PerProgram = OpaqueLifetime<Lifetime_v::PER_PROGRAM, T>;
template <typename T>
using PerFleet = OpaqueLifetime<Lifetime_v::PER_FLEET, T>;
}  // namespace opaque_lifetime

namespace detail::opaque_lifetime_layout {

template <typename T>
using FleetL = OpaqueLifetime<Lifetime_v::PER_FLEET, T>;
template <typename T>
using ProgramL = OpaqueLifetime<Lifetime_v::PER_PROGRAM, T>;
template <typename T>
using RequestL = OpaqueLifetime<Lifetime_v::PER_REQUEST, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetL, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetL, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetL, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProgramL, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProgramL, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RequestL, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RequestL, double);

}  // namespace detail::opaque_lifetime_layout

static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_REQUEST, int>) == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>) == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET, int>) == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET, double>) == sizeof(double));

namespace detail::opaque_lifetime_self_test {

using FleetInt = OpaqueLifetime<Lifetime_v::PER_FLEET, int>;
using ProgramInt = OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>;
using RequestInt = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;

inline constexpr FleetInt o_default{};
static_assert(o_default.peek() == 0);
static_assert(o_default.scope == Lifetime_v::PER_FLEET);

inline constexpr FleetInt o_explicit{42};
static_assert(o_explicit.peek() == 42);

static_assert(FleetInt::scope == Lifetime_v::PER_FLEET);
static_assert(ProgramInt::scope == Lifetime_v::PER_PROGRAM);
static_assert(RequestInt::scope == Lifetime_v::PER_REQUEST);

static_assert(FleetInt::satisfies<Lifetime_v::PER_FLEET>);
static_assert(FleetInt::satisfies<Lifetime_v::PER_PROGRAM>);
static_assert(FleetInt::satisfies<Lifetime_v::PER_REQUEST>);

static_assert(ProgramInt::satisfies<Lifetime_v::PER_PROGRAM>);
static_assert(ProgramInt::satisfies<Lifetime_v::PER_REQUEST>);
static_assert(!ProgramInt::satisfies<Lifetime_v::PER_FLEET>);

static_assert(RequestInt::satisfies<Lifetime_v::PER_REQUEST>);
static_assert(!RequestInt::satisfies<Lifetime_v::PER_PROGRAM>);
static_assert(!RequestInt::satisfies<Lifetime_v::PER_FLEET>);

inline constexpr auto from_fleet_to_program = FleetInt{42}.relax<Lifetime_v::PER_PROGRAM>();
static_assert(from_fleet_to_program.peek() == 42);
static_assert(from_fleet_to_program.scope == Lifetime_v::PER_PROGRAM);

inline constexpr auto from_fleet_to_request = FleetInt{99}.relax<Lifetime_v::PER_REQUEST>();
static_assert(from_fleet_to_request.peek() == 99);
static_assert(from_fleet_to_request.scope == Lifetime_v::PER_REQUEST);

inline constexpr auto from_program_to_request = ProgramInt{7}.relax<Lifetime_v::PER_REQUEST>();
static_assert(from_program_to_request.peek() == 7);
static_assert(from_program_to_request.scope == Lifetime_v::PER_REQUEST);

inline constexpr auto from_program_to_self = ProgramInt{8}.relax<Lifetime_v::PER_PROGRAM>();
static_assert(from_program_to_self.peek() == 8);

template <typename W, Lifetime_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<FleetInt, Lifetime_v::PER_PROGRAM>);
static_assert(can_relax<FleetInt, Lifetime_v::PER_REQUEST>);
static_assert(can_relax<ProgramInt, Lifetime_v::PER_REQUEST>);
static_assert(can_relax<ProgramInt, Lifetime_v::PER_PROGRAM>);
static_assert(!can_relax<ProgramInt, Lifetime_v::PER_FLEET>);
static_assert(!can_relax<RequestInt, Lifetime_v::PER_PROGRAM>);
static_assert(!can_relax<RequestInt, Lifetime_v::PER_FLEET>);

static_assert(FleetInt::value_type_name().ends_with("int"));
static_assert(FleetInt::lattice_name() == "LifetimeLattice::At<PER_FLEET>");
static_assert(ProgramInt::lattice_name() == "LifetimeLattice::At<PER_PROGRAM>");
static_assert(RequestInt::lattice_name() == "LifetimeLattice::At<PER_REQUEST>");

[[nodiscard]] consteval bool swap_exchanges_within_same_scope() noexcept {
    FleetInt a{10};
    FleetInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_scope());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    FleetInt a{10};
    FleetInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    FleetInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    FleetInt a{42};
    FleetInt b{42};
    FleetInt c{43};
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

static_assert(can_equality_compare<FleetInt>);
static_assert(!can_equality_compare<OpaqueLifetime<Lifetime_v::PER_FLEET, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    FleetInt a{99};
    auto b = a.relax<Lifetime_v::PER_FLEET>();
    return b.peek() == 99 && b.scope == Lifetime_v::PER_FLEET;
}
static_assert(relax_to_self_is_identity());

static_assert(FleetInt::value_type_name().size() > 0);
static_assert(FleetInt::lattice_name().size() > 0);
static_assert(FleetInt::lattice_name().starts_with("LifetimeLattice::At<"));

static_assert(opaque_lifetime::PerFleet<int>::scope == Lifetime_v::PER_FLEET);
static_assert(opaque_lifetime::PerProgram<int>::scope == Lifetime_v::PER_PROGRAM);
static_assert(opaque_lifetime::PerRequest<int>::scope == Lifetime_v::PER_REQUEST);

static_assert(std::is_same_v<opaque_lifetime::PerFleet<double>, OpaqueLifetime<Lifetime_v::PER_FLEET, double>>);

// Constant evaluation can hide a defect that appears only when the inline
// body runs with arguments the compiler cannot fold.
inline void runtime_smoke_test() {
    FleetInt a{};
    FleetInt b{42};
    FleetInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (FleetInt::scope != Lifetime_v::PER_FLEET) {
        std::abort();
    }

    FleetInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    FleetInt sx{1};
    FleetInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    FleetInt source{77};
    auto narrowed_copy = source.relax<Lifetime_v::PER_PROGRAM>();
    auto narrowed_move = std::move(source).relax<Lifetime_v::PER_REQUEST>();
    [[maybe_unused]] auto rcopy = narrowed_copy.peek();
    [[maybe_unused]] auto rmove = narrowed_move.peek();

    [[maybe_unused]] bool s1 = FleetInt::satisfies<Lifetime_v::PER_REQUEST>;
    [[maybe_unused]] bool s2 = ProgramInt::satisfies<Lifetime_v::PER_FLEET>;

    FleetInt eq_a{42};
    FleetInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    FleetInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    opaque_lifetime::PerFleet<int> alias_form{123};
    opaque_lifetime::PerRequest<double> req_form{3.14};
    [[maybe_unused]] auto av = alias_form.peek();
    [[maybe_unused]] auto rv = req_form.peek();
}

}  // namespace detail::opaque_lifetime_self_test

}  // namespace crucible::safety
