#pragma once

// MemOrder<Tag, T> pins a value to the heaviest memory ordering that
// operations on it may use.
//
// The tags form a chain from the heaviest fence to the lightest:
// SeqCst, then AcqRel, Release, Acquire, then Relaxed.  Higher in the
// chain means less ordering is imposed, so Relaxed is the strongest
// claim a producer can make and SeqCst the weakest.
//
// satisfies<Required> asks whether the pinned tag covers what a
// consumer demands: a lighter ordering satisfies a heavier one.  A
// Relaxed value is admissible wherever AcqRel is required.  A SeqCst
// value is admissible only where SeqCst is asked for.
//
// The chain puts Acquire above Release only because the underlying
// enum orders them that way.  It is not a claim that the two orderings
// are interchangeable.  A site names the ordering it needs, and
// relaxing an Acquire value to Release says only that the site accepts
// being treated as the lower of the two.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): a value fenced with a total order cannot afterwards claim
// it needed no fence.  The substrate's weaken(), which does move up,
// is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/MemOrderLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::MemOrderLattice;
using MemOrderTag_v = ::crucible::algebra::lattices::MemOrderTag;

template <MemOrderTag_v Tag, typename T>
class [[nodiscard]] MemOrder {
public:
    using value_type = T;
    using lattice_type = MemOrderLattice::At<Tag>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr MemOrderTag_v tag = Tag;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a tag no operation used.
    // Deleting it would be the truthful choice, but it is kept so the
    // wrapper can sit in an array element or a default-initialized
    // struct field.
    constexpr MemOrder() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit MemOrder(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit MemOrder(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                          && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr MemOrder(const MemOrder&) = default;
    constexpr MemOrder(MemOrder&&) = default;
    constexpr MemOrder& operator=(const MemOrder&) = default;
    constexpr MemOrder& operator=(MemOrder&&) = default;
    ~MemOrder() = default;

    [[nodiscard]] friend constexpr bool operator==(MemOrder const& a,
                                                   MemOrder const& b) noexcept(noexcept(a.peek() == b.peek()))
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

    constexpr void swap(MemOrder& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(MemOrder& a, MemOrder& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <MemOrderTag_v RequiredTag>
    static constexpr bool satisfies = MemOrderLattice::leq(RequiredTag, Tag);

    template <MemOrderTag_v WeakerTag>
        requires(MemOrderLattice::leq(WeakerTag, Tag))
    [[nodiscard]] constexpr MemOrder<WeakerTag, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return MemOrder<WeakerTag, T>{this->peek()};
    }

    template <MemOrderTag_v WeakerTag>
        requires(MemOrderLattice::leq(WeakerTag, Tag))
    [[nodiscard]] constexpr MemOrder<WeakerTag, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return MemOrder<WeakerTag, T>{std::move(impl_).consume()};
    }
};

namespace mem_order {
template <typename T>
using Relaxed = MemOrder<MemOrderTag_v::Relaxed, T>;
template <typename T>
using Acquire = MemOrder<MemOrderTag_v::Acquire, T>;
template <typename T>
using Release = MemOrder<MemOrderTag_v::Release, T>;
template <typename T>
using AcqRel = MemOrder<MemOrderTag_v::AcqRel, T>;
template <typename T>
using SeqCst = MemOrder<MemOrderTag_v::SeqCst, T>;
}  // namespace mem_order

namespace detail::mem_order_layout {

template <typename T>
using RelaxM = MemOrder<MemOrderTag_v::Relaxed, T>;
template <typename T>
using AcqRelM = MemOrder<MemOrderTag_v::AcqRel, T>;
template <typename T>
using SeqCstM = MemOrder<MemOrderTag_v::SeqCst, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxM, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxM, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxM, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelM, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelM, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstM, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstM, double);

}  // namespace detail::mem_order_layout

static_assert(sizeof(MemOrder<MemOrderTag_v::Relaxed, int>) == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, int>) == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::Release, int>) == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::AcqRel, int>) == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::SeqCst, int>) == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::Relaxed, double>) == sizeof(double));

namespace detail::mem_order_self_test {

using RelaxInt = MemOrder<MemOrderTag_v::Relaxed, int>;
using AcqInt = MemOrder<MemOrderTag_v::Acquire, int>;
using RelInt = MemOrder<MemOrderTag_v::Release, int>;
using AcqRelInt = MemOrder<MemOrderTag_v::AcqRel, int>;
using SeqCstInt = MemOrder<MemOrderTag_v::SeqCst, int>;

inline constexpr RelaxInt m_default{};
static_assert(m_default.peek() == 0);
static_assert(m_default.tag == MemOrderTag_v::Relaxed);

inline constexpr RelaxInt m_explicit{42};
static_assert(m_explicit.peek() == 42);

inline constexpr RelaxInt m_in_place{std::in_place, 7};
static_assert(m_in_place.peek() == 7);

static_assert(RelaxInt::tag == MemOrderTag_v::Relaxed);
static_assert(AcqInt::tag == MemOrderTag_v::Acquire);
static_assert(RelInt::tag == MemOrderTag_v::Release);
static_assert(AcqRelInt::tag == MemOrderTag_v::AcqRel);
static_assert(SeqCstInt::tag == MemOrderTag_v::SeqCst);

static_assert(RelaxInt::satisfies<MemOrderTag_v::Relaxed>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::Acquire>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::Release>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::AcqRel>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::SeqCst>);

static_assert(AcqInt::satisfies<MemOrderTag_v::Acquire>);
static_assert(AcqInt::satisfies<MemOrderTag_v::Release>);
static_assert(AcqInt::satisfies<MemOrderTag_v::AcqRel>);
static_assert(AcqInt::satisfies<MemOrderTag_v::SeqCst>);
static_assert(!AcqInt::satisfies<MemOrderTag_v::Relaxed>);

static_assert(RelInt::satisfies<MemOrderTag_v::Release>);
static_assert(RelInt::satisfies<MemOrderTag_v::AcqRel>);
static_assert(RelInt::satisfies<MemOrderTag_v::SeqCst>);
static_assert(!RelInt::satisfies<MemOrderTag_v::Acquire>);
static_assert(!RelInt::satisfies<MemOrderTag_v::Relaxed>);

static_assert(AcqRelInt::satisfies<MemOrderTag_v::AcqRel>);
static_assert(AcqRelInt::satisfies<MemOrderTag_v::SeqCst>);
static_assert(!AcqRelInt::satisfies<MemOrderTag_v::Release>,
              "AcqRel must not satisfy Release.  Release sits above AcqRel in "
              "the chain, so a combined read-modify-write value must not "
              "reach a site that admits only the lighter ordering.");
static_assert(!AcqRelInt::satisfies<MemOrderTag_v::Acquire>);
static_assert(!AcqRelInt::satisfies<MemOrderTag_v::Relaxed>);

static_assert(SeqCstInt::satisfies<MemOrderTag_v::SeqCst>);
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::AcqRel>,
              "SeqCst must not satisfy AcqRel.  A value that needs a total "
              "order must not reach a site whose budget assumed the lighter "
              "fence.");
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::Release>);
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::Acquire>);
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::Relaxed>);

inline constexpr auto from_relax_to_acq = RelaxInt{42}.relax<MemOrderTag_v::Acquire>();
static_assert(from_relax_to_acq.peek() == 42);
static_assert(from_relax_to_acq.tag == MemOrderTag_v::Acquire);

inline constexpr auto from_relax_to_seqcst = RelaxInt{99}.relax<MemOrderTag_v::SeqCst>();
static_assert(from_relax_to_seqcst.peek() == 99);
static_assert(from_relax_to_seqcst.tag == MemOrderTag_v::SeqCst);

inline constexpr auto from_acqrel_to_seqcst = AcqRelInt{7}.relax<MemOrderTag_v::SeqCst>();
static_assert(from_acqrel_to_seqcst.peek() == 7);

inline constexpr auto from_acqrel_to_self = AcqRelInt{8}.relax<MemOrderTag_v::AcqRel>();
static_assert(from_acqrel_to_self.peek() == 8);

template <typename W, MemOrderTag_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<RelaxInt, MemOrderTag_v::Acquire>);
static_assert(can_relax<RelaxInt, MemOrderTag_v::SeqCst>);
static_assert(can_relax<RelaxInt, MemOrderTag_v::Relaxed>);
static_assert(can_relax<AcqRelInt, MemOrderTag_v::SeqCst>);
static_assert(can_relax<AcqRelInt, MemOrderTag_v::AcqRel>);
static_assert(!can_relax<AcqRelInt, MemOrderTag_v::Acquire>,
              "relax<Acquire> on an AcqRel value must be rejected.  It would "
              "claim a lighter ordering than the operations actually use.");
static_assert(!can_relax<AcqRelInt, MemOrderTag_v::Relaxed>);
static_assert(!can_relax<SeqCstInt, MemOrderTag_v::AcqRel>,
              "relax<AcqRel> on a SeqCst value must be rejected.  A value "
              "that needs a total order cannot claim it needs less.");
static_assert(!can_relax<SeqCstInt, MemOrderTag_v::Relaxed>);
static_assert(can_relax<SeqCstInt, MemOrderTag_v::SeqCst>);

static_assert(RelaxInt::value_type_name().ends_with("int"));
static_assert(RelaxInt::lattice_name() == "MemOrderLattice::At<Relaxed>");
static_assert(AcqInt::lattice_name() == "MemOrderLattice::At<Acquire>");
static_assert(RelInt::lattice_name() == "MemOrderLattice::At<Release>");
static_assert(AcqRelInt::lattice_name() == "MemOrderLattice::At<AcqRel>");
static_assert(SeqCstInt::lattice_name() == "MemOrderLattice::At<SeqCst>");

[[nodiscard]] consteval bool swap_exchanges_within_same_tag() noexcept {
    RelaxInt a{10};
    RelaxInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tag());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    RelaxInt a{10};
    RelaxInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    RelaxInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    RelaxInt a{42};
    RelaxInt b{42};
    RelaxInt c{43};
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

static_assert(can_equality_compare<RelaxInt>);
static_assert(!can_equality_compare<MemOrder<MemOrderTag_v::Relaxed, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<MemOrder<MemOrderTag_v::Relaxed, NoEqualityT>>,
              "MemOrder<Tag, T> must inherit deletion of T's copy "
              "constructor.");
static_assert(std::is_move_constructible_v<MemOrder<MemOrderTag_v::Relaxed, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    RelaxInt a{99};
    auto b = a.relax<MemOrderTag_v::Relaxed>();
    return b.peek() == 99 && b.tag == MemOrderTag_v::Relaxed;
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

template <typename W, MemOrderTag_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, MemOrderTag_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using RelaxMoveOnly = MemOrder<MemOrderTag_v::Relaxed, MoveOnlyT>;
static_assert(can_relax_rvalue<RelaxMoveOnly, MemOrderTag_v::Acquire>, "relax on an rvalue must accept a move-only T.");
static_assert(!can_relax_lvalue<RelaxMoveOnly, MemOrderTag_v::Acquire>,
              "relax on a const lvalue must reject a move-only T.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    RelaxMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<MemOrderTag_v::Acquire>();
    return dst.peek().v == 77 && dst.tag == MemOrderTag_v::Acquire;
}
static_assert(relax_move_only_works());

static_assert(RelaxInt::value_type_name().size() > 0);
static_assert(RelaxInt::lattice_name().size() > 0);
static_assert(RelaxInt::lattice_name().starts_with("MemOrderLattice::At<"));

static_assert(mem_order::Relaxed<int>::tag == MemOrderTag_v::Relaxed);
static_assert(mem_order::Acquire<int>::tag == MemOrderTag_v::Acquire);
static_assert(mem_order::Release<int>::tag == MemOrderTag_v::Release);
static_assert(mem_order::AcqRel<int>::tag == MemOrderTag_v::AcqRel);
static_assert(mem_order::SeqCst<int>::tag == MemOrderTag_v::SeqCst);

static_assert(std::is_same_v<mem_order::Relaxed<double>, MemOrder<MemOrderTag_v::Relaxed, double>>);

// A site that admits no total-order fence requires AcqRel or lighter.
template <typename W>
concept is_hot_path_atomic_admissible = W::template satisfies<MemOrderTag_v::AcqRel>;

static_assert(is_hot_path_atomic_admissible<RelaxInt>, "A Relaxed value must pass a gate that requires AcqRel.");
static_assert(is_hot_path_atomic_admissible<AcqInt>, "An Acquire value must pass a gate that requires AcqRel.");
static_assert(is_hot_path_atomic_admissible<RelInt>, "A Release value must pass a gate that requires AcqRel.");
static_assert(is_hot_path_atomic_admissible<AcqRelInt>, "An AcqRel value must pass a gate that requires AcqRel.  It "
                                                        "sits exactly at the boundary.");
static_assert(!is_hot_path_atomic_admissible<SeqCstInt>,
              "A SeqCst value must not pass a gate that requires AcqRel.  It "
              "would put a total-order fence where the site budgeted for a "
              "lighter one.");

inline void runtime_smoke_test() {
    RelaxInt a{};
    RelaxInt b{42};
    RelaxInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (RelaxInt::tag != MemOrderTag_v::Relaxed) {
        std::abort();
    }

    RelaxInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    RelaxInt sx{1};
    RelaxInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    RelaxInt source{77};
    auto relaxed_copy = source.relax<MemOrderTag_v::Acquire>();
    auto relaxed_move = std::move(source).relax<MemOrderTag_v::SeqCst>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = RelaxInt::satisfies<MemOrderTag_v::AcqRel>;
    [[maybe_unused]] bool s2 = SeqCstInt::satisfies<MemOrderTag_v::Relaxed>;

    RelaxInt eq_a{42};
    RelaxInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    RelaxInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    mem_order::Relaxed<int> alias_relax{123};
    mem_order::AcqRel<int> alias_acqrel{456};
    mem_order::SeqCst<int> alias_seqcst{789};
    [[maybe_unused]] auto rv = alias_relax.peek();
    [[maybe_unused]] auto av = alias_acqrel.peek();
    [[maybe_unused]] auto sv = alias_seqcst.peek();

    [[maybe_unused]] bool can_relax_pass = is_hot_path_atomic_admissible<RelaxInt>;
    [[maybe_unused]] bool can_acqrel_pass = is_hot_path_atomic_admissible<AcqRelInt>;
    [[maybe_unused]] bool can_seqcst_pass = is_hot_path_atomic_admissible<SeqCstInt>;
}

}  // namespace detail::mem_order_self_test

}  // namespace crucible::safety
