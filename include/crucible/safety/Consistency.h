#pragma once

// Consistency<Level, T> pins a value to the guarantee the protocol
// that committed it gives to a reader.
//
// The levels form a chain from the weakest guarantee to the
// strongest.  EVENTUAL promises only that replicas converge some day.
// READ_YOUR_WRITES adds that a writer sees its own writes.
// CAUSAL_PREFIX adds that causally ordered writes are read in order.
// BOUNDED_STALENESS adds a limit on how far behind a read may fall.
// STRONG admits no staleness at all.
//
// satisfies<Required> asks whether the pinned level covers what a
// consumer demands: stronger satisfies weaker.  A STRONG value is
// admissible wherever CAUSAL_PREFIX is required.  The converse does
// not hold.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a STRONG value is to build one
// where the commit really was strongly consistent.  The substrate's
// weaken(), which does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/ConsistencyLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// The class template below shadows the lattice enum of the same name
// in this namespace, so the enum is re-exported under a distinct
// alias.
using ::crucible::algebra::lattices::ConsistencyLattice;
using Consistency_v = ::crucible::algebra::lattices::Consistency;

template <Consistency_v Level, typename T>
class [[nodiscard]] Consistency {
public:
    using value_type = T;
    using lattice_type = ConsistencyLattice::At<Level>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr Consistency_v level = Level;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a level no commit earned.
    // On a replica that holds nothing yet the claim is vacuously
    // true.  Deleting it would be the truthful choice elsewhere, but
    // it is kept so the wrapper can sit in an array element or a
    // default-initialized struct field.  A site that completed a
    // commit uses the explicit constructor.
    constexpr Consistency() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Consistency(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Consistency(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                             && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr Consistency(const Consistency&) = default;
    constexpr Consistency(Consistency&&) = default;
    constexpr Consistency& operator=(const Consistency&) = default;
    constexpr Consistency& operator=(Consistency&&) = default;
    ~Consistency() = default;

    [[nodiscard]] friend constexpr bool operator==(Consistency const& a,
                                                   Consistency const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    // The level records how the value was committed, not what its
    // bytes hold now, so mutation cannot break the pin.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(Consistency& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Consistency& a, Consistency& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <Consistency_v RequiredLevel>
    static constexpr bool satisfies = ConsistencyLattice::leq(RequiredLevel, Level);

    template <Consistency_v WeakerLevel>
        requires(ConsistencyLattice::leq(WeakerLevel, Level))
    [[nodiscard]] constexpr Consistency<WeakerLevel, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Consistency<WeakerLevel, T>{this->peek()};
    }

    template <Consistency_v WeakerLevel>
        requires(ConsistencyLattice::leq(WeakerLevel, Level))
    [[nodiscard]] constexpr Consistency<WeakerLevel, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Consistency<WeakerLevel, T>{std::move(impl_).consume()};
    }
};

namespace consistency {
template <typename T>
using Eventual = Consistency<Consistency_v::EVENTUAL, T>;
template <typename T>
using ReadYourWrites = Consistency<Consistency_v::READ_YOUR_WRITES, T>;
template <typename T>
using CausalPrefix = Consistency<Consistency_v::CAUSAL_PREFIX, T>;
template <typename T>
using BoundedStaleness = Consistency<Consistency_v::BOUNDED_STALENESS, T>;
template <typename T>
using Strong = Consistency<Consistency_v::STRONG, T>;
}  // namespace consistency

namespace detail::consistency_layout {

template <typename T>
using StrongC = Consistency<Consistency_v::STRONG, T>;
template <typename T>
using BoundedC = Consistency<Consistency_v::BOUNDED_STALENESS, T>;
template <typename T>
using EventualC = Consistency<Consistency_v::EVENTUAL, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongC, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(EventualC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(EventualC, double);

}  // namespace detail::consistency_layout

static_assert(sizeof(Consistency<Consistency_v::EVENTUAL, int>) == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::READ_YOUR_WRITES, int>) == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::CAUSAL_PREFIX, int>) == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::BOUNDED_STALENESS, int>) == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::STRONG, int>) == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::STRONG, double>) == sizeof(double));

namespace detail::consistency_self_test {

using StrongInt = Consistency<Consistency_v::STRONG, int>;
using CausalInt = Consistency<Consistency_v::CAUSAL_PREFIX, int>;
using EventualInt = Consistency<Consistency_v::EVENTUAL, int>;

inline constexpr StrongInt c_default{};
static_assert(c_default.peek() == 0);
static_assert(c_default.level == Consistency_v::STRONG);

inline constexpr StrongInt c_explicit{42};
static_assert(c_explicit.peek() == 42);

static_assert(StrongInt::level == Consistency_v::STRONG);
static_assert(CausalInt::level == Consistency_v::CAUSAL_PREFIX);
static_assert(EventualInt::level == Consistency_v::EVENTUAL);

static_assert(StrongInt::satisfies<Consistency_v::STRONG>);
static_assert(StrongInt::satisfies<Consistency_v::BOUNDED_STALENESS>);
static_assert(StrongInt::satisfies<Consistency_v::CAUSAL_PREFIX>);
static_assert(StrongInt::satisfies<Consistency_v::READ_YOUR_WRITES>);
static_assert(StrongInt::satisfies<Consistency_v::EVENTUAL>);

static_assert(CausalInt::satisfies<Consistency_v::CAUSAL_PREFIX>);
static_assert(CausalInt::satisfies<Consistency_v::READ_YOUR_WRITES>);
static_assert(CausalInt::satisfies<Consistency_v::EVENTUAL>);
static_assert(!CausalInt::satisfies<Consistency_v::BOUNDED_STALENESS>);
static_assert(!CausalInt::satisfies<Consistency_v::STRONG>);

static_assert(EventualInt::satisfies<Consistency_v::EVENTUAL>);
static_assert(!EventualInt::satisfies<Consistency_v::READ_YOUR_WRITES>);
static_assert(!EventualInt::satisfies<Consistency_v::STRONG>);

inline constexpr auto from_strong_to_causal = StrongInt{42}.relax<Consistency_v::CAUSAL_PREFIX>();
static_assert(from_strong_to_causal.peek() == 42);
static_assert(from_strong_to_causal.level == Consistency_v::CAUSAL_PREFIX);

inline constexpr auto from_strong_to_eventual = StrongInt{99}.relax<Consistency_v::EVENTUAL>();
static_assert(from_strong_to_eventual.peek() == 99);
static_assert(from_strong_to_eventual.level == Consistency_v::EVENTUAL);

inline constexpr auto from_causal_to_ryw = CausalInt{7}.relax<Consistency_v::READ_YOUR_WRITES>();
static_assert(from_causal_to_ryw.peek() == 7);
static_assert(from_causal_to_ryw.level == Consistency_v::READ_YOUR_WRITES);

inline constexpr auto from_causal_to_self = CausalInt{8}.relax<Consistency_v::CAUSAL_PREFIX>();
static_assert(from_causal_to_self.peek() == 8);

template <typename W, Consistency_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<StrongInt, Consistency_v::CAUSAL_PREFIX>);
static_assert(can_relax<StrongInt, Consistency_v::EVENTUAL>);
static_assert(can_relax<CausalInt, Consistency_v::READ_YOUR_WRITES>);
static_assert(can_relax<CausalInt, Consistency_v::CAUSAL_PREFIX>);
static_assert(!can_relax<CausalInt, Consistency_v::BOUNDED_STALENESS>);
static_assert(!can_relax<CausalInt, Consistency_v::STRONG>);
static_assert(!can_relax<EventualInt, Consistency_v::READ_YOUR_WRITES>);

// The check below is ends_with rather than an exact match because the
// reflected display string of a type varies with the context it is
// taken in.
static_assert(StrongInt::value_type_name().ends_with("int"));

static_assert(StrongInt::lattice_name() == "ConsistencyLattice::At<STRONG>");
static_assert(CausalInt::lattice_name() == "ConsistencyLattice::At<CAUSAL_PREFIX>");
static_assert(EventualInt::lattice_name() == "ConsistencyLattice::At<EVENTUAL>");

[[nodiscard]] consteval bool swap_exchanges_within_same_level() noexcept {
    StrongInt a{10};
    StrongInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_level());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    StrongInt a{10};
    StrongInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    StrongInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    StrongInt a{42};
    StrongInt b{42};
    StrongInt c{43};
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

static_assert(can_equality_compare<StrongInt>);
static_assert(!can_equality_compare<Consistency<Consistency_v::STRONG, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    StrongInt a{99};
    auto b = a.relax<Consistency_v::STRONG>();
    return b.peek() == 99 && b.level == Consistency_v::STRONG;
}
static_assert(relax_to_self_is_identity());

static_assert(StrongInt::value_type_name().size() > 0);
static_assert(StrongInt::lattice_name().size() > 0);
static_assert(StrongInt::lattice_name().starts_with("ConsistencyLattice::At<"));

static_assert(consistency::Strong<int>::level == Consistency_v::STRONG);
static_assert(consistency::BoundedStaleness<int>::level == Consistency_v::BOUNDED_STALENESS);
static_assert(consistency::CausalPrefix<int>::level == Consistency_v::CAUSAL_PREFIX);
static_assert(consistency::ReadYourWrites<int>::level == Consistency_v::READ_YOUR_WRITES);
static_assert(consistency::Eventual<int>::level == Consistency_v::EVENTUAL);

static_assert(std::is_same_v<consistency::Strong<double>, Consistency<Consistency_v::STRONG, double>>);

// The operations below run outside constant evaluation so that any
// divergence between the constexpr and the runtime path shows up.
inline void runtime_smoke_test() {
    StrongInt a{};
    StrongInt b{42};
    StrongInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (StrongInt::level != Consistency_v::STRONG) {
        std::abort();
    }

    StrongInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    StrongInt sx{1};
    StrongInt sy{2};
    sx.swap(sy);

    using std::swap;
    swap(sx, sy);

    StrongInt source{77};
    auto relaxed_copy = source.relax<Consistency_v::CAUSAL_PREFIX>();
    auto relaxed_move = std::move(source).relax<Consistency_v::EVENTUAL>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = StrongInt::satisfies<Consistency_v::CAUSAL_PREFIX>;
    [[maybe_unused]] bool s2 = CausalInt::satisfies<Consistency_v::STRONG>;

    StrongInt eq_a{42};
    StrongInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    StrongInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    consistency::Strong<int> alias_form{123};
    consistency::CausalPrefix<double> causal_form{3.14};
    [[maybe_unused]] auto av = alias_form.peek();
    [[maybe_unused]] auto cv = causal_form.peek();
}

}  // namespace detail::consistency_self_test

}  // namespace crucible::safety
