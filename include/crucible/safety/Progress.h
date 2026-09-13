#pragma once

// Progress<Class, T> pins a value to the termination guarantee of the
// work that produced it.
//
// The classes form a chain from the weakest claim to the strongest.
// MayDiverge may never finish.  Terminating eventually halts, with no
// bound on when.  Productive makes measurable progress on every step.
// Bounded finishes within a stated wall-clock budget.
//
// satisfies<Required> asks whether the pinned class covers what a
// consumer demands: stronger satisfies weaker.  A Bounded value is
// admissible wherever Productive is required.  The converse does not
// hold.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): work that may never finish cannot afterwards claim that
// it does, so the only way to hold a stronger class is to build one
// where the guarantee holds.  The substrate's weaken(), which does
// move up, is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ProgressLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::ProgressLattice;
using ProgressClass_v = ::crucible::algebra::lattices::ProgressClass;

template <ProgressClass_v Class, typename T>
class [[nodiscard]] Progress {
public:
    using value_type = T;
    using lattice_type = ProgressLattice::At<Class>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr ProgressClass_v cls = Class;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a class no work earned.
    // Deleting it would be the truthful choice, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.
    constexpr Progress() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Progress(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Progress(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                          && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr Progress(const Progress&) = default;
    constexpr Progress(Progress&&) = default;
    constexpr Progress& operator=(const Progress&) = default;
    constexpr Progress& operator=(Progress&&) = default;
    ~Progress() = default;

    [[nodiscard]] friend constexpr bool operator==(Progress const& a,
                                                   Progress const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(Progress& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Progress& a, Progress& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <ProgressClass_v RequiredClass>
    static constexpr bool satisfies = ProgressLattice::leq(RequiredClass, Class);

    template <ProgressClass_v WeakerClass>
        requires(ProgressLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Progress<WeakerClass, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Progress<WeakerClass, T>{this->peek()};
    }

    template <ProgressClass_v WeakerClass>
        requires(ProgressLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Progress<WeakerClass, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Progress<WeakerClass, T>{std::move(impl_).consume()};
    }
};

namespace progress {
template <typename T>
using Bounded = Progress<ProgressClass_v::Bounded, T>;
template <typename T>
using Productive = Progress<ProgressClass_v::Productive, T>;
template <typename T>
using Terminating = Progress<ProgressClass_v::Terminating, T>;
template <typename T>
using MayDiverge = Progress<ProgressClass_v::MayDiverge, T>;
}  // namespace progress

namespace detail::progress_layout {

template <typename T>
using BoundedP = Progress<ProgressClass_v::Bounded, T>;
template <typename T>
using ProductiveP = Progress<ProgressClass_v::Productive, T>;
template <typename T>
using MayDivergeP = Progress<ProgressClass_v::MayDiverge, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedP, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedP, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProductiveP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProductiveP, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MayDivergeP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MayDivergeP, double);

}  // namespace detail::progress_layout

static_assert(sizeof(Progress<ProgressClass_v::Bounded, int>) == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::Productive, int>) == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::Terminating, int>) == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::MayDiverge, int>) == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::Bounded, double>) == sizeof(double));

namespace detail::progress_self_test {

using BoundedInt = Progress<ProgressClass_v::Bounded, int>;
using ProductiveInt = Progress<ProgressClass_v::Productive, int>;
using TermInt = Progress<ProgressClass_v::Terminating, int>;
using DivergeInt = Progress<ProgressClass_v::MayDiverge, int>;

inline constexpr BoundedInt p_default{};
static_assert(p_default.peek() == 0);
static_assert(p_default.cls == ProgressClass_v::Bounded);

inline constexpr BoundedInt p_explicit{42};
static_assert(p_explicit.peek() == 42);

inline constexpr BoundedInt p_in_place{std::in_place, 7};
static_assert(p_in_place.peek() == 7);

static_assert(BoundedInt::cls == ProgressClass_v::Bounded);
static_assert(ProductiveInt::cls == ProgressClass_v::Productive);
static_assert(TermInt::cls == ProgressClass_v::Terminating);
static_assert(DivergeInt::cls == ProgressClass_v::MayDiverge);

static_assert(BoundedInt::satisfies<ProgressClass_v::Bounded>);
static_assert(BoundedInt::satisfies<ProgressClass_v::Productive>);
static_assert(BoundedInt::satisfies<ProgressClass_v::Terminating>);
static_assert(BoundedInt::satisfies<ProgressClass_v::MayDiverge>);

static_assert(ProductiveInt::satisfies<ProgressClass_v::Productive>);
static_assert(ProductiveInt::satisfies<ProgressClass_v::Terminating>);
static_assert(ProductiveInt::satisfies<ProgressClass_v::MayDiverge>);
static_assert(!ProductiveInt::satisfies<ProgressClass_v::Bounded>,
              "Productive must not satisfy Bounded.  Progress on every step "
              "says nothing about when the work ends, so it cannot serve a "
              "consumer that holds a deadline.");

static_assert(TermInt::satisfies<ProgressClass_v::Terminating>);
static_assert(TermInt::satisfies<ProgressClass_v::MayDiverge>);
static_assert(!TermInt::satisfies<ProgressClass_v::Productive>);
static_assert(!TermInt::satisfies<ProgressClass_v::Bounded>);

static_assert(DivergeInt::satisfies<ProgressClass_v::MayDiverge>);
static_assert(!DivergeInt::satisfies<ProgressClass_v::Terminating>,
              "MayDiverge must not satisfy Terminating.  Work that may never "
              "finish must not reach a consumer that assumes it does.");
static_assert(!DivergeInt::satisfies<ProgressClass_v::Productive>);
static_assert(!DivergeInt::satisfies<ProgressClass_v::Bounded>);

inline constexpr auto from_bounded_to_productive = BoundedInt{42}.relax<ProgressClass_v::Productive>();
static_assert(from_bounded_to_productive.peek() == 42);
static_assert(from_bounded_to_productive.cls == ProgressClass_v::Productive);

inline constexpr auto from_bounded_to_diverge = BoundedInt{99}.relax<ProgressClass_v::MayDiverge>();
static_assert(from_bounded_to_diverge.peek() == 99);
static_assert(from_bounded_to_diverge.cls == ProgressClass_v::MayDiverge);

inline constexpr auto from_term_to_diverge = TermInt{7}.relax<ProgressClass_v::MayDiverge>();
static_assert(from_term_to_diverge.peek() == 7);

inline constexpr auto from_productive_to_self = ProductiveInt{8}.relax<ProgressClass_v::Productive>();
static_assert(from_productive_to_self.peek() == 8);

template <typename W, ProgressClass_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<BoundedInt, ProgressClass_v::Productive>);
static_assert(can_relax<BoundedInt, ProgressClass_v::MayDiverge>);
static_assert(can_relax<BoundedInt, ProgressClass_v::Bounded>);
static_assert(can_relax<ProductiveInt, ProgressClass_v::Terminating>);
static_assert(can_relax<ProductiveInt, ProgressClass_v::Productive>);
static_assert(!can_relax<ProductiveInt, ProgressClass_v::Bounded>,
              "relax<Bounded> on a Productive value must be rejected.  It "
              "would claim a deadline the work does not meet.");
static_assert(!can_relax<TermInt, ProgressClass_v::Productive>);
static_assert(!can_relax<DivergeInt, ProgressClass_v::Terminating>,
              "relax<Terminating> on a MayDiverge value must be rejected.  "
              "Work that may never finish would otherwise claim that it "
              "halts.");
static_assert(!can_relax<DivergeInt, ProgressClass_v::Bounded>);
static_assert(can_relax<DivergeInt, ProgressClass_v::MayDiverge>);

static_assert(BoundedInt::value_type_name().ends_with("int"));
static_assert(BoundedInt::lattice_name() == "ProgressLattice::At<Bounded>");
static_assert(ProductiveInt::lattice_name() == "ProgressLattice::At<Productive>");
static_assert(TermInt::lattice_name() == "ProgressLattice::At<Terminating>");
static_assert(DivergeInt::lattice_name() == "ProgressLattice::At<MayDiverge>");

[[nodiscard]] consteval bool swap_exchanges_within_same_class() noexcept {
    BoundedInt a{10};
    BoundedInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_class());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    BoundedInt a{10};
    BoundedInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    BoundedInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    BoundedInt a{42};
    BoundedInt b{42};
    BoundedInt c{43};
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

static_assert(can_equality_compare<BoundedInt>);
static_assert(!can_equality_compare<Progress<ProgressClass_v::Bounded, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<Progress<ProgressClass_v::Bounded, NoEqualityT>>,
              "Progress<Class, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<Progress<ProgressClass_v::Bounded, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    BoundedInt a{99};
    auto b = a.relax<ProgressClass_v::Bounded>();
    return b.peek() == 99 && b.cls == ProgressClass_v::Bounded;
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

template <typename W, ProgressClass_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, ProgressClass_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using BoundedMoveOnly = Progress<ProgressClass_v::Bounded, MoveOnlyT>;
static_assert(can_relax_rvalue<BoundedMoveOnly, ProgressClass_v::Productive>,
              "relax on an rvalue must accept a move-only T.");
static_assert(!can_relax_lvalue<BoundedMoveOnly, ProgressClass_v::Productive>,
              "relax on a const lvalue must reject a move-only T.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    BoundedMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<ProgressClass_v::Productive>();
    return dst.peek().v == 77 && dst.cls == ProgressClass_v::Productive;
}
static_assert(relax_move_only_works());

static_assert(BoundedInt::value_type_name().size() > 0);
static_assert(BoundedInt::lattice_name().size() > 0);
static_assert(BoundedInt::lattice_name().starts_with("ProgressLattice::At<"));

static_assert(progress::Bounded<int>::cls == ProgressClass_v::Bounded);
static_assert(progress::Productive<int>::cls == ProgressClass_v::Productive);
static_assert(progress::Terminating<int>::cls == ProgressClass_v::Terminating);
static_assert(progress::MayDiverge<int>::cls == ProgressClass_v::MayDiverge);

static_assert(std::is_same_v<progress::Bounded<double>, Progress<ProgressClass_v::Bounded, double>>);

// A site holding a deadline admits Bounded only.
template <typename W>
concept is_deadline_admissible = W::template satisfies<ProgressClass_v::Bounded>;

static_assert(is_deadline_admissible<BoundedInt>, "A Bounded value must pass a gate that requires Bounded.");
static_assert(!is_deadline_admissible<ProductiveInt>, "A Productive value must not pass a gate that requires "
                                                      "Bounded.  Per-step progress carries no deadline.");
static_assert(!is_deadline_admissible<TermInt>, "A Terminating value must not pass a gate that requires "
                                                "Bounded.  Eventual halt carries no deadline.");
static_assert(!is_deadline_admissible<DivergeInt>, "A MayDiverge value must not pass a gate that requires "
                                                   "Bounded.");

// A site that only needs forward motion admits Productive and stronger.
template <typename W>
concept is_productive_admissible = W::template satisfies<ProgressClass_v::Productive>;

static_assert(is_productive_admissible<BoundedInt>);
static_assert(is_productive_admissible<ProductiveInt>);
static_assert(!is_productive_admissible<TermInt>, "A Terminating value must not pass a gate that requires "
                                                  "Productive.  Halting some day is not progress on every step.");
static_assert(!is_productive_admissible<DivergeInt>);

inline void runtime_smoke_test() {
    BoundedInt a{};
    BoundedInt b{42};
    BoundedInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (BoundedInt::cls != ProgressClass_v::Bounded) {
        std::abort();
    }

    BoundedInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    BoundedInt sx{1};
    BoundedInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    BoundedInt source{77};
    auto relaxed_copy = source.relax<ProgressClass_v::Productive>();
    auto relaxed_move = std::move(source).relax<ProgressClass_v::MayDiverge>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = BoundedInt::satisfies<ProgressClass_v::Productive>;
    [[maybe_unused]] bool s2 = DivergeInt::satisfies<ProgressClass_v::Bounded>;

    BoundedInt eq_a{42};
    BoundedInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    BoundedInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    progress::Bounded<int> alias_bounded{123};
    progress::Productive<int> alias_prod{456};
    progress::MayDiverge<int> alias_diverge{789};
    [[maybe_unused]] auto bv = alias_bounded.peek();
    [[maybe_unused]] auto pv = alias_prod.peek();
    [[maybe_unused]] auto dv = alias_diverge.peek();

    [[maybe_unused]] bool can_bounded_pass = is_deadline_admissible<BoundedInt>;
    [[maybe_unused]] bool can_productive_pass = is_deadline_admissible<ProductiveInt>;
    [[maybe_unused]] bool can_diverge_pass = is_deadline_admissible<DivergeInt>;
}

}  // namespace detail::progress_self_test

}  // namespace crucible::safety
