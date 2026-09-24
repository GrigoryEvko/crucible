#pragma once

// Crash<Class, T> pins a value to the failure mode of the function
// that produced it.
//
// The classes form a chain from the weakest claim to the strongest:
// Abort (the producer may kill the process), then Throw, then
// ErrorReturn, then NoThrow (the producer never fails).
//
// satisfies<Required> asks whether the pinned class covers what a
// consumer demands: stronger satisfies weaker.  A NoThrow value is
// admissible wherever ErrorReturn is accepted, because a value that
// never fails trivially meets a gate prepared for failure.  The
// converse does not hold.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a Crash<NoThrow, T> is to build one
// at a site that genuinely never fails.  The substrate's weaken(),
// which does move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/CrashLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::CrashLattice;
using CrashClass_v = ::crucible::algebra::lattices::CrashClass;

template <CrashClass_v Class, typename T>
class [[nodiscard]] Crash {
public:
    using value_type = T;
    using lattice_type = CrashLattice::At<Class>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr CrashClass_v crash_class = Class;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a class no function
    // claimed.  Deleting it would be the truthful choice, but it is
    // kept so the wrapper can sit in an array element or a
    // default-initialized struct field.  A site that knows its
    // failure mode uses the explicit constructor.
    constexpr Crash() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Crash(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Crash(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                       && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr Crash(const Crash&) = default;
    constexpr Crash(Crash&&) = default;
    constexpr Crash& operator=(const Crash&) = default;
    constexpr Crash& operator=(Crash&&) = default;
    ~Crash() = default;

    [[nodiscard]] friend constexpr bool operator==(Crash const& a,
                                                   Crash const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(Crash& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Crash& a, Crash& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <CrashClass_v RequiredClass>
    static constexpr bool satisfies = CrashLattice::leq(RequiredClass, Class);

    template <CrashClass_v WeakerClass>
        requires(CrashLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Crash<WeakerClass, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Crash<WeakerClass, T>{this->peek()};
    }

    template <CrashClass_v WeakerClass>
        requires(CrashLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Crash<WeakerClass, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Crash<WeakerClass, T>{std::move(impl_).consume()};
    }
};

namespace crash {
template <typename T>
using Abort = Crash<CrashClass_v::Abort, T>;
template <typename T>
using Throw = Crash<CrashClass_v::Throw, T>;
template <typename T>
using ErrorReturn = Crash<CrashClass_v::ErrorReturn, T>;
template <typename T>
using NoThrow = Crash<CrashClass_v::NoThrow, T>;
}  // namespace crash

namespace detail::crash_layout {

template <typename T>
using NoThrowC = Crash<CrashClass_v::NoThrow, T>;
template <typename T>
using ErrorReturnC = Crash<CrashClass_v::ErrorReturn, T>;
template <typename T>
using ThrowC = Crash<CrashClass_v::Throw, T>;
template <typename T>
using AbortC = Crash<CrashClass_v::Abort, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowC, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnC, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowC, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortC, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortC, double);

}  // namespace detail::crash_layout

static_assert(sizeof(Crash<CrashClass_v::NoThrow, char>) == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::NoThrow, int>) == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::NoThrow, double>) == sizeof(double));
static_assert(sizeof(Crash<CrashClass_v::ErrorReturn, char>) == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::ErrorReturn, int>) == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::ErrorReturn, double>) == sizeof(double));
static_assert(sizeof(Crash<CrashClass_v::Throw, char>) == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::Throw, int>) == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::Throw, double>) == sizeof(double));
static_assert(sizeof(Crash<CrashClass_v::Abort, char>) == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::Abort, int>) == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::Abort, double>) == sizeof(double));

namespace detail::crash_self_test {

using NoThrowInt = Crash<CrashClass_v::NoThrow, int>;
using ErrorReturnInt = Crash<CrashClass_v::ErrorReturn, int>;
using ThrowInt = Crash<CrashClass_v::Throw, int>;
using AbortInt = Crash<CrashClass_v::Abort, int>;

inline constexpr NoThrowInt n_default{};
static_assert(n_default.peek() == 0);
static_assert(n_default.crash_class == CrashClass_v::NoThrow);

inline constexpr NoThrowInt n_explicit{42};
static_assert(n_explicit.peek() == 42);

inline constexpr NoThrowInt n_in_place{std::in_place, 7};
static_assert(n_in_place.peek() == 7);

static_assert(NoThrowInt::crash_class == CrashClass_v::NoThrow);
static_assert(ErrorReturnInt::crash_class == CrashClass_v::ErrorReturn);
static_assert(ThrowInt::crash_class == CrashClass_v::Throw);
static_assert(AbortInt::crash_class == CrashClass_v::Abort);

static_assert(NoThrowInt::satisfies<CrashClass_v::NoThrow>);
static_assert(NoThrowInt::satisfies<CrashClass_v::ErrorReturn>);
static_assert(NoThrowInt::satisfies<CrashClass_v::Throw>);
static_assert(NoThrowInt::satisfies<CrashClass_v::Abort>);

static_assert(ErrorReturnInt::satisfies<CrashClass_v::ErrorReturn>);
static_assert(ErrorReturnInt::satisfies<CrashClass_v::Throw>);
static_assert(ErrorReturnInt::satisfies<CrashClass_v::Abort>);
static_assert(!ErrorReturnInt::satisfies<CrashClass_v::NoThrow>,
              "ErrorReturn must not satisfy NoThrow.  A gate that requires "
              "NoThrow omits the failure check, so admitting an "
              "error-returning value there leaves the error state unread.");

static_assert(ThrowInt::satisfies<CrashClass_v::Throw>);
static_assert(ThrowInt::satisfies<CrashClass_v::Abort>);
static_assert(!ThrowInt::satisfies<CrashClass_v::ErrorReturn>);
static_assert(!ThrowInt::satisfies<CrashClass_v::NoThrow>);

static_assert(AbortInt::satisfies<CrashClass_v::Abort>);
static_assert(!AbortInt::satisfies<CrashClass_v::Throw>);
static_assert(!AbortInt::satisfies<CrashClass_v::ErrorReturn>);
static_assert(!AbortInt::satisfies<CrashClass_v::NoThrow>);

inline constexpr auto from_nothrow_to_errorreturn = NoThrowInt{42}.relax<CrashClass_v::ErrorReturn>();
static_assert(from_nothrow_to_errorreturn.peek() == 42);
static_assert(from_nothrow_to_errorreturn.crash_class == CrashClass_v::ErrorReturn);

inline constexpr auto from_nothrow_to_abort = NoThrowInt{99}.relax<CrashClass_v::Abort>();
static_assert(from_nothrow_to_abort.peek() == 99);
static_assert(from_nothrow_to_abort.crash_class == CrashClass_v::Abort);

inline constexpr auto from_errorreturn_to_throw = ErrorReturnInt{7}.relax<CrashClass_v::Throw>();
static_assert(from_errorreturn_to_throw.peek() == 7);

inline constexpr auto from_errorreturn_to_self = ErrorReturnInt{8}.relax<CrashClass_v::ErrorReturn>();
static_assert(from_errorreturn_to_self.peek() == 8);

template <typename W, CrashClass_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<NoThrowInt, CrashClass_v::NoThrow>);
static_assert(can_relax<NoThrowInt, CrashClass_v::ErrorReturn>);
static_assert(can_relax<NoThrowInt, CrashClass_v::Throw>);
static_assert(can_relax<NoThrowInt, CrashClass_v::Abort>);

static_assert(can_relax<ErrorReturnInt, CrashClass_v::ErrorReturn>);
static_assert(can_relax<ErrorReturnInt, CrashClass_v::Throw>);
static_assert(can_relax<ErrorReturnInt, CrashClass_v::Abort>);
static_assert(!can_relax<ErrorReturnInt, CrashClass_v::NoThrow>,
              "relax<NoThrow> on an ErrorReturn value must be rejected.  It "
              "would claim a guarantee the producer does not give, and the "
              "consumer would then skip the error check.");

static_assert(can_relax<ThrowInt, CrashClass_v::Throw>);
static_assert(can_relax<ThrowInt, CrashClass_v::Abort>);
static_assert(!can_relax<ThrowInt, CrashClass_v::ErrorReturn>);
static_assert(!can_relax<ThrowInt, CrashClass_v::NoThrow>);

static_assert(can_relax<AbortInt, CrashClass_v::Abort>);
static_assert(!can_relax<AbortInt, CrashClass_v::Throw>);
static_assert(!can_relax<AbortInt, CrashClass_v::ErrorReturn>);
static_assert(!can_relax<AbortInt, CrashClass_v::NoThrow>,
              "relax<NoThrow> on an Abort value must be rejected.  A value "
              "from a function that may have killed the process cannot claim "
              "that it never fails.");

static_assert(NoThrowInt::value_type_name().ends_with("int"));
static_assert(NoThrowInt::lattice_name() == "CrashLattice::At<NoThrow>");
static_assert(ErrorReturnInt::lattice_name() == "CrashLattice::At<ErrorReturn>");
static_assert(ThrowInt::lattice_name() == "CrashLattice::At<Throw>");
static_assert(AbortInt::lattice_name() == "CrashLattice::At<Abort>");

template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x};
    W b{y};
    a.swap(b);
    return a.peek() == y && b.peek() == x;
}
static_assert(swap_exchanges_within<NoThrowInt>(10, 20));
static_assert(swap_exchanges_within<ErrorReturnInt>(11, 21));
static_assert(swap_exchanges_within<ThrowInt>(12, 22));
static_assert(swap_exchanges_within<AbortInt>(13, 23));

template <typename W>
[[nodiscard]] consteval bool free_swap_within(int x, int y) noexcept {
    W a{x};
    W b{y};
    using std::swap;
    swap(a, b);
    return a.peek() == y && b.peek() == x;
}
static_assert(free_swap_within<NoThrowInt>(10, 20));
static_assert(free_swap_within<ErrorReturnInt>(11, 21));
static_assert(free_swap_within<ThrowInt>(12, 22));
static_assert(free_swap_within<AbortInt>(13, 23));

template <typename W>
[[nodiscard]] consteval bool peek_mut_works_for(int initial, int target) noexcept {
    W a{initial};
    a.peek_mut() = target;
    return a.peek() == target;
}
static_assert(peek_mut_works_for<NoThrowInt>(10, 99));
static_assert(peek_mut_works_for<ErrorReturnInt>(11, 88));
static_assert(peek_mut_works_for<ThrowInt>(12, 77));
static_assert(peek_mut_works_for<AbortInt>(13, 66));

template <typename W>
[[nodiscard]] consteval bool equality_compares_value_bytes_for() noexcept {
    W a{42};
    W b{42};
    W c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes_for<NoThrowInt>());
static_assert(equality_compares_value_bytes_for<ErrorReturnInt>());
static_assert(equality_compares_value_bytes_for<ThrowInt>());
static_assert(equality_compares_value_bytes_for<AbortInt>());

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

static_assert(can_equality_compare<NoThrowInt>);
static_assert(!can_equality_compare<Crash<CrashClass_v::NoThrow, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<Crash<CrashClass_v::NoThrow, NoEqualityT>>,
              "Crash<Class, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<Crash<CrashClass_v::NoThrow, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    NoThrowInt a{99};
    auto b = a.relax<CrashClass_v::NoThrow>();
    return b.peek() == 99 && b.crash_class == CrashClass_v::NoThrow;
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

template <typename W, CrashClass_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, CrashClass_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using NoThrowMoveOnly = Crash<CrashClass_v::NoThrow, MoveOnlyT>;
static_assert(can_relax_rvalue<NoThrowMoveOnly, CrashClass_v::ErrorReturn>,
              "relax on an rvalue must accept a move-only T.  The rvalue "
              "overload moves through consume().");
static_assert(!can_relax_lvalue<NoThrowMoveOnly, CrashClass_v::ErrorReturn>,
              "relax on a const lvalue must reject a move-only T.  That "
              "overload requires a copy constructor.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    NoThrowMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<CrashClass_v::ErrorReturn>();
    return dst.peek().v == 77 && dst.crash_class == CrashClass_v::ErrorReturn;
}
static_assert(relax_move_only_works());

static_assert(NoThrowInt::value_type_name().size() > 0);
static_assert(NoThrowInt::lattice_name().size() > 0);
static_assert(NoThrowInt::lattice_name().starts_with("CrashLattice::At<"));

static_assert(crash::NoThrow<int>::crash_class == CrashClass_v::NoThrow);
static_assert(crash::ErrorReturn<int>::crash_class == CrashClass_v::ErrorReturn);
static_assert(crash::Throw<int>::crash_class == CrashClass_v::Throw);
static_assert(crash::Abort<int>::crash_class == CrashClass_v::Abort);

static_assert(std::is_same_v<crash::NoThrow<double>, Crash<CrashClass_v::NoThrow, double>>);

template <typename W>
concept is_nothrow_admissible = W::template satisfies<CrashClass_v::NoThrow>;

static_assert(is_nothrow_admissible<NoThrowInt>, "A NoThrow value must pass a gate that requires NoThrow.");
static_assert(!is_nothrow_admissible<ErrorReturnInt>, "An ErrorReturn value must not pass a gate that requires "
                                                      "NoThrow.  Such a gate omits the failure check, so it would "
                                                      "read a result that may hold an error.");
static_assert(!is_nothrow_admissible<ThrowInt>);
static_assert(!is_nothrow_admissible<AbortInt>, "An Abort value must not pass a gate that requires NoThrow.");

template <typename W>
concept is_recovery_admissible = W::template satisfies<CrashClass_v::Abort>;

static_assert(is_recovery_admissible<NoThrowInt>, "A NoThrow value must pass the most permissive gate.  A value "
                                                  "that never fails is admissible wherever failure is tolerated.");
static_assert(is_recovery_admissible<ErrorReturnInt>);
static_assert(is_recovery_admissible<ThrowInt>);
static_assert(is_recovery_admissible<AbortInt>, "An Abort value must pass a gate that requires only Abort.");

inline void runtime_smoke_test() {
    NoThrowInt a{};
    NoThrowInt b{42};
    NoThrowInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (NoThrowInt::crash_class != CrashClass_v::NoThrow) {
        std::abort();
    }

    NoThrowInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    NoThrowInt sx{1};
    NoThrowInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    NoThrowInt source{77};
    auto relaxed_copy = source.relax<CrashClass_v::ErrorReturn>();
    auto relaxed_move = std::move(source).relax<CrashClass_v::Abort>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = NoThrowInt::satisfies<CrashClass_v::ErrorReturn>;
    [[maybe_unused]] bool s2 = ErrorReturnInt::satisfies<CrashClass_v::NoThrow>;

    NoThrowInt eq_a{42};
    NoThrowInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    NoThrowInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    crash::NoThrow<int> alias_nothrow{123};
    crash::ErrorReturn<int> alias_errret{456};
    crash::Throw<int> alias_throw{789};
    crash::Abort<int> alias_abort{0};
    [[maybe_unused]] auto vn = alias_nothrow.peek();
    [[maybe_unused]] auto ve = alias_errret.peek();
    [[maybe_unused]] auto vt = alias_throw.peek();
    [[maybe_unused]] auto vab = alias_abort.peek();

    [[maybe_unused]] bool can_nothrow_pass = is_nothrow_admissible<NoThrowInt>;
    [[maybe_unused]] bool can_recovery_pass = is_recovery_admissible<AbortInt>;
}

}  // namespace detail::crash_self_test

}  // namespace crucible::safety
