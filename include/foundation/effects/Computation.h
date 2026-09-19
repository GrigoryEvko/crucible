#pragma once

// A Computation<R, T> is a value of type T whose production may use
// the effects in row R.  Hot-path code holds the empty row and so can
// call nothing effectful.  The row lives in the type alone; the stored
// grade is an empty singleton, so the carrier is the size of T however
// many atoms R names.
//
// It is one instantiation of the graded substrate, not a parallel
// hierarchy: ComputationGraded<R, T> is Graded<Relative, At<Es...>, T>
// and Computation<R, T> derives from it with no members of its own.
// The modality is Relative because a row carries neither a unit nor a
// counit.  A value cannot be injected into a row, since the row is
// already the typing context, and it cannot be extracted out of one
// short of closing the universe.  What a row does carry is propagation,
// which `weaken` widens up the lattice, and the derived class adds the
// row-level operations the substrate cannot express — the empty-row
// extract, the type-changing weaken, map, then, and the lifts.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::effects {

// R must be a Row.  The constraint is structural rather than a concept:
// the lift from a row to a lattice point has no primary template, so a
// non-Row argument yields an incomplete type at the instantiation point.
template <typename R, typename T>
using ComputationGraded =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Relative, effect_row_to_at_t<R>, T>;

static_assert(std::is_same_v<typename ComputationGraded<Row<>, int>::value_type, int>);

static_assert(std::is_same_v<typename ComputationGraded<Row<>, int>::lattice_type, EffectRowLattice::At<>>);

static_assert(
    std::is_same_v<typename ComputationGraded<Row<Effect::Bg>, int>::lattice_type, EffectRowLattice::At<Effect::Bg>>);

static_assert(std::is_same_v<typename ComputationGraded<Row<Effect::Alloc, Effect::IO>, int>::lattice_type,
                             EffectRowLattice::At<Effect::Alloc, Effect::IO>>);

static_assert(ComputationGraded<Row<>, int>::modality == ::foundation::algebra::ModalityKind::Relative);

static_assert(ComputationGraded<Row<Effect::Bg>, double>::modality == ::foundation::algebra::ModalityKind::Relative);

static_assert(std::is_empty_v<typename ComputationGraded<Row<>, int>::grade_type>);
static_assert(std::is_empty_v<typename ComputationGraded<Row<Effect::Bg>, int>::grade_type>);
static_assert(
    std::is_empty_v<typename ComputationGraded<
        Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, int>::grade_type>);

// The modality and lattice names are stable spellings and can be
// compared.  The value-type name is not: it is reflection-derived and
// its text varies with translation-unit context, so no assertion here
// compares it.

static_assert(ComputationGraded<Row<>, int>::modality_name() == "Relative");
static_assert(ComputationGraded<Row<Effect::Bg>, int>::lattice_name() == "EffectRow::At");

// The row is type-level only, so the carrier collapses to the payload's
// own footprint however many atoms the row names.

namespace detail::computation_graded_layout {

struct EmptyValue {};
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

static_assert(sizeof(ComputationGraded<Row<>, EmptyValue>) == 1);
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, EmptyValue>) == 1);

static_assert(sizeof(ComputationGraded<Row<>, int>) == sizeof(int));
static_assert(sizeof(ComputationGraded<Row<Effect::Alloc>, int>) == sizeof(int));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, int>) == sizeof(int));
static_assert(
    sizeof(
        ComputationGraded<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, int>)
    == sizeof(int));

static_assert(sizeof(ComputationGraded<Row<>, OneByteValue>) == sizeof(OneByteValue));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, OneByteValue>) == sizeof(OneByteValue));

static_assert(sizeof(ComputationGraded<Row<>, EightByteValue>) == sizeof(EightByteValue));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, EightByteValue>) == sizeof(EightByteValue));

static_assert(alignof(ComputationGraded<Row<>, int>) == alignof(int));
static_assert(alignof(ComputationGraded<Row<Effect::Bg>, EightByteValue>) == alignof(EightByteValue));

template <typename T>
using CompOverEmpty = ComputationGraded<Row<>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, EightByteValue);

template <typename T>
using CompOverBg = ComputationGraded<Row<Effect::Bg>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverBg, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverBg, EightByteValue);

template <typename T>
using CompOverAll =
    ComputationGraded<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, EightByteValue);

}  // namespace detail::computation_graded_layout

// EmptyRow must stay a transparent alias.  Turning it into a distinct
// type would give it a distinct carrier, and these fire.
static_assert(std::is_same_v<ComputationGraded<EmptyRow, int>, ComputationGraded<Row<>, int>>);
static_assert(std::is_same_v<typename ComputationGraded<EmptyRow, int>::lattice_type,
                             typename ComputationGraded<Row<>, int>::lattice_type>);
static_assert(sizeof(ComputationGraded<EmptyRow, int>) == sizeof(ComputationGraded<Row<>, int>));

// The substrate publishes at_bottom only for a lattice that is bounded
// below.  The row lattice is, so the factory must reach every
// instantiation.

namespace detail::computation_graded_caps {
template <typename G>
concept HasAtBottom = requires(typename G::value_type v) { G::at_bottom(v); };
}  // namespace detail::computation_graded_caps

static_assert(detail::computation_graded_caps::HasAtBottom<ComputationGraded<Row<>, int>>);
static_assert(detail::computation_graded_caps::HasAtBottom<ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(
    detail::computation_graded_caps::HasAtBottom<
        ComputationGraded<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, int>>);

static_assert(std::is_default_constructible_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_copy_constructible_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_move_constructible_v<ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(std::is_copy_assignable_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_move_assignable_v<ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(std::is_destructible_v<ComputationGraded<Row<>, int>>);

// Persistence copies these objects byte-wise, so the carrier must not
// cost the payload its trivial copyability.
static_assert(std::is_trivially_copyable_v<int> == std::is_trivially_copyable_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_trivially_copyable_v<int>
              == std::is_trivially_copyable_v<ComputationGraded<Row<Effect::Bg>, int>>);

namespace detail::computation_graded_caps {

template <typename G>
concept HasPeekMut = requires(G& g) { g.peek_mut(); };
template <typename G>
concept HasSwap = requires(G& a, G& b) { a.swap(b); };
template <typename G>
concept HasComonadExtract = requires(G g) { std::move(g).extract(); };
template <typename G>
concept HasRelMonadInject = requires { G::inject(typename G::value_type{}, typename G::grade_type{}); };
template <typename G>
concept HasWeaken = requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };
template <typename G>
concept HasCompose = requires(G g, G const& o) { std::move(g).compose(o); };

using G_pure = ComputationGraded<Row<>, int>;
using G_bg = ComputationGraded<Row<Effect::Bg>, int>;

// peek_mut and swap reach a Relative-modality carrier here because the
// substrate gates them on an absolute modality OR an empty grade, and
// the row grade is empty.
static_assert(HasPeekMut<G_pure>);
static_assert(HasPeekMut<G_bg>);
static_assert(HasSwap<G_pure>);
static_assert(HasSwap<G_bg>);

static_assert(!HasComonadExtract<G_pure>);
static_assert(!HasComonadExtract<G_bg>);
static_assert(!HasRelMonadInject<G_pure>);
static_assert(!HasRelMonadInject<G_bg>);

static_assert(HasWeaken<G_pure>);
static_assert(HasWeaken<G_bg>);
static_assert(HasCompose<G_pure>);
static_assert(HasCompose<G_bg>);

}  // namespace detail::computation_graded_caps

// Drives every accessor with non-constant arguments, where inline-body
// and constant-evaluation regressions surface that the assertions above
// cannot see.
inline void runtime_smoke_test_computation_graded() noexcept {
    using G_pure = ComputationGraded<Row<>, int>;
    using G_bg = ComputationGraded<Row<Effect::Bg>, int>;

    int value_runtime = 42;
    G_pure pure{value_runtime, G_pure::grade_type{}};
    G_bg bg{value_runtime + 1, G_bg::grade_type{}};

    G_pure pure_default{};
    G_pure pure_copy{pure};
    G_pure pure_moved{std::move(pure_copy)};

    [[maybe_unused]] int const& peeked = pure.peek();
    [[maybe_unused]] int moved = std::move(pure_default).consume();
    [[maybe_unused]] auto grade = pure.grade();

    G_pure pure_a{};
    G_pure pure_b{};
    pure_a.peek_mut() = 7;
    pure_a.swap(pure_b);

    [[maybe_unused]] auto widened = std::move(bg).weaken(G_bg::grade_type{});

    G_pure pure_c{value_runtime, G_pure::grade_type{}};
    G_pure pure_d{value_runtime + 2, G_pure::grade_type{}};
    [[maybe_unused]] G_pure composed = pure_c.compose(pure_d);

    [[maybe_unused]] G_pure pure_bot = G_pure::at_bottom(value_runtime);
    [[maybe_unused]] G_bg bg_bot = G_bg::at_bottom(value_runtime + 3);

    static_assert(detail::computation_graded_caps::HasPeekMut<G_pure>);
    static_assert(detail::computation_graded_caps::HasWeaken<G_bg>);
    static_assert(!detail::computation_graded_caps::HasComonadExtract<G_pure>);
    static_assert(!detail::computation_graded_caps::HasRelMonadInject<G_bg>);
}

template <typename R, typename T>
class Computation;

// Both traits are specialized for Computation after the class is
// complete.  The requires-clauses that use them inside the class body
// are checked when those members are instantiated, by which point the
// specializations are visible.
namespace detail {
template <typename>
struct is_computation : std::false_type {};

// A payload that is itself an engaged Computation hides its row from
// everything that looks at the outer type.  Wrapping
// Computation<Row<Bg>, U> inside an empty-row Computation would let
// the value out through extract while the intervening code believed it
// was handling pure data.  A plain payload has nothing to hide, so it
// always admits.
template <typename>
struct extract_admits_payload : std::true_type {};
template <typename T>
inline constexpr bool extract_admits_payload_v = extract_admits_payload<T>::value;
}  // namespace detail

template <typename T>
concept IsComputation = detail::is_computation<std::remove_cvref_t<T>>::value;

template <typename R, typename T>
class [[nodiscard]] Computation : public ComputationGraded<R, T> {
    using base = ComputationGraded<R, T>;

public:
    using row_type = R;
    using graded_type = base;
    using typename base::grade_type;
    using typename base::lattice_type;
    using typename base::value_type;

    static constexpr std::size_t row_size = row_size_v<R>;

    [[nodiscard]] static consteval std::size_t effect_count_in_row() noexcept { return row_size_v<R>; }

    constexpr Computation() = default;
    constexpr Computation(const Computation&) = default;
    constexpr Computation(Computation&&) = default;
    constexpr Computation& operator=(const Computation&) = default;
    constexpr Computation& operator=(Computation&&) = default;
    ~Computation() = default;

    // Explicit, so that no value slides into a Computation without the
    // lift being written out.
    explicit constexpr Computation(T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        : base{std::move(x), grade_type{}} {}

    [[nodiscard]] static constexpr Computation mk(T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(row_size_v<R> == 0)
    {
        return Computation{std::move(x)};
    }

    // The same lift under the naming convention that makes every
    // authorization point findable by one grep.  New call sites use
    // this spelling.  It derives its authority from the empty-row
    // constraint alone, so it takes no context.
    [[nodiscard]] static constexpr Computation mint_computation(T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(row_size_v<R> == 0)
    {
        return Computation{std::move(x)};
    }

    // The payload constraint is what stops an engaged Computation from
    // being carried out through a pure-looking wrapper.
    [[nodiscard]] constexpr const T& extract() const& noexcept
        requires(row_size_v<R> == 0) && detail::extract_admits_payload_v<T>
    {
        return base::peek();
    }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(row_size_v<R> == 0) && detail::extract_admits_payload_v<T>
    {
        return std::move(*this).base::consume();
    }

    // This form takes the caller's word for it.  Nothing about a raw T
    // proves that producing it exercised Cap, and no type-level axis
    // could supply that proof, so the honesty of the claim is a matter
    // of review.  Where the caller has a context in hand, lift_in below
    // checks the claim against that context's row instead.
    template <Effect Cap>
        requires IsEffect<Cap>
    [[nodiscard]] static constexpr auto lift(T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        -> Computation<Row<Cap>, T> {
        return Computation<Row<Cap>, T>{std::move(x)};
    }

    // The third predicate is the one that matters: it is the proof
    // that the caller's context permits the claimed effect.  The first
    // two only reject typos.  A caller whose context carries the empty
    // row finds no candidate here, whatever effect it asks for.
    //
    // The context argument is read for its type alone.
    template <Effect Cap, class Ctx>
        requires IsEffect<Cap> && IsExecCtx<Ctx> && row_contains_v<typename std::remove_cvref_t<Ctx>::row_type, Cap>
    [[nodiscard]] static constexpr auto lift_in(Ctx const&, T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        -> Computation<Row<Cap>, T> {
        return Computation<Row<Cap>, T>{std::move(x)};
    }

    // The context-bound lift under the grep-able naming convention.
    // New call sites use this spelling.
    template <Effect Cap, class Ctx>
        requires IsEffect<Cap> && IsExecCtx<Ctx> && row_contains_v<typename std::remove_cvref_t<Ctx>::row_type, Cap>
    [[nodiscard]] static constexpr auto mint_computation_in_ctx(Ctx const&,
                                                                T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        -> Computation<Row<Cap>, T> {
        return Computation<Row<Cap>, T>{std::move(x)};
    }

    // Widening the row is the substitution principle: a function
    // declared at row R accepts a Computation declared at a narrower
    // one.  The extra size conjunct is there because the empty row is
    // a subrow of every row, so Subrow alone would let a pure value
    // claim any effect it liked.  Widening is therefore allowed only
    // from an already-engaged row, or between two empty ones.  To
    // attach a row at construction, lift instead.
    //
    // The copy-constructible conjunct moves the failure for a
    // move-only payload from deep inside the body out to overload
    // resolution, where the caller is told to use the rvalue form.
    // The rvalue overload needs no such gate: it moves.
    template <typename R2>
        requires Subrow<R, R2> && (row_size_v<R> > 0 || row_size_v<R2> == 0) && std::is_copy_constructible_v<T>
    [[nodiscard]] constexpr Computation<R2, T> weaken() const& noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return Computation<R2, T>{base::peek()};
    }

    template <typename R2>
        requires Subrow<R, R2> && (row_size_v<R> > 0 || row_size_v<R2> == 0)
    [[nodiscard]] constexpr Computation<R2, T> weaken() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Computation<R2, T>{std::move(*this).base::consume()};
    }

    // The row is unchanged because f is a value transformation and
    // cannot introduce an effect.  An f that wants one goes through
    // `then`.
    template <typename F>
        requires std::is_invocable_v<F, const T&>
    [[nodiscard]] constexpr auto
    map(F&& f) const& noexcept(std::is_nothrow_invocable_v<F, const T&>
                               && std::is_nothrow_move_constructible_v<std::invoke_result_t<F, const T&>>)
        -> Computation<R, std::invoke_result_t<F, const T&>> {
        using U = std::invoke_result_t<F, const T&>;
        return Computation<R, U>{std::forward<F>(f)(base::peek())};
    }

    template <typename F>
        requires std::is_invocable_v<F, T>
    [[nodiscard]] constexpr auto
    map(F&& f) && noexcept(std::is_nothrow_invocable_v<F, T>
                           && std::is_nothrow_move_constructible_v<std::invoke_result_t<F, T>>)
        -> Computation<R, std::invoke_result_t<F, T>> {
        using U = std::invoke_result_t<F, T>;
        return Computation<R, U>{std::forward<F>(f)(std::move(*this).base::consume())};
    }

    // The result row is the union, so it claims every effect any step
    // in the chain might exercise.
    //
    // Requiring the callback to return a Computation is what keeps
    // this distinct from map: a callback returning a plain value is
    // rejected here rather than accepted as a degenerate bind.
    //
    // The payload constraint rejects a callback whose declared row
    // hides an engaged Computation in the returned value.  After the
    // union the inner row would be invisible in the result type.
    // Chaining several binds does the same job honestly.
    template <typename F>
        requires std::is_invocable_v<F, const T&> && IsComputation<std::invoke_result_t<F, const T&>>
              && detail::extract_admits_payload_v<typename std::invoke_result_t<F, const T&>::value_type>
    [[nodiscard]] constexpr auto
    then(F&& k) const& -> Computation<row_union_t<R, typename std::invoke_result_t<F, const T&>::row_type>,
                                      typename std::invoke_result_t<F, const T&>::value_type> {
        using Inner = std::invoke_result_t<F, const T&>;
        using R2 = typename Inner::row_type;
        using U = typename Inner::value_type;
        using Result = Computation<row_union_t<R, R2>, U>;
        Inner intermediate = std::forward<F>(k)(base::peek());
        return Result{std::move(intermediate).graded().consume()};
    }

    template <typename F>
        requires std::is_invocable_v<F, T> && IsComputation<std::invoke_result_t<F, T>>
              && detail::extract_admits_payload_v<typename std::invoke_result_t<F, T>::value_type>
    [[nodiscard]] constexpr auto
    then(F&& k) && -> Computation<row_union_t<R, typename std::invoke_result_t<F, T>::row_type>,
                                  typename std::invoke_result_t<F, T>::value_type> {
        using Inner = std::invoke_result_t<F, T>;
        using R2 = typename Inner::row_type;
        using U = typename Inner::value_type;
        using Result = Computation<row_union_t<R, R2>, U>;
        Inner intermediate = std::forward<F>(k)(std::move(*this).base::consume());
        return Result{std::move(intermediate).graded().consume()};
    }

    // The way out to the substrate view, for code that wants the
    // uniform grade and lattice diagnostics or a cache key.  The view
    // is the base subobject: no copy, no second object.
    [[nodiscard]] constexpr const graded_type& graded() const& noexcept { return *this; }

    [[nodiscard]] constexpr graded_type graded() && noexcept(std::is_nothrow_move_constructible_v<graded_type>) {
        return static_cast<base&&>(*this);
    }
};

// The derived class adds no members, so it is the size of its base,
// which is the size of the payload.
static_assert(sizeof(Computation<Row<>, int>) == sizeof(ComputationGraded<Row<>, int>));
static_assert(sizeof(Computation<Row<Effect::Bg, Effect::IO>, double>) == sizeof(double));

namespace detail {
template <typename R, typename T>
struct is_computation<Computation<R, T>> : std::true_type {};

template <typename R, typename U>
struct extract_admits_payload<Computation<R, U>>
    : std::bool_constant<(row_size_v<R> == 0) && extract_admits_payload<U>::value> {};
}  // namespace detail

#define CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationAlias, T_)                                                   \
    static_assert(sizeof(ComputationAlias<T_>) == sizeof(T_),                                                         \
                  "The Computation alias " #ComputationAlias " over " #T_ " is larger than its payload.  Review the " \
                  "[[no_unique_address]] member.")

namespace detail::computation_self_test {

struct EmptyValue {};
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

using C_empty = Computation<Row<>, EmptyValue>;
using C_one_byte = Computation<Row<>, OneByteValue>;
using C_eight_byte = Computation<Row<Effect::Bg>, EightByteValue>;

static_assert(std::is_same_v<C_empty::row_type, Row<>>);
static_assert(std::is_same_v<C_empty::value_type, EmptyValue>);

static_assert(std::is_same_v<C_empty::graded_type, ComputationGraded<Row<>, EmptyValue>>);
static_assert(std::is_same_v<C_eight_byte::graded_type, ComputationGraded<Row<Effect::Bg>, EightByteValue>>);

static_assert(std::is_default_constructible_v<C_empty>);
static_assert(std::is_default_constructible_v<C_one_byte>);
static_assert(std::is_default_constructible_v<C_eight_byte>);

static_assert(sizeof(C_empty) == 1);
static_assert(sizeof(C_one_byte) == sizeof(OneByteValue));
static_assert(sizeof(C_eight_byte) == sizeof(EightByteValue));

// Alignment and copy-triviality are checked against the substrate as
// well as the size, so that padding or an attribute change that leaves
// the size alone is still caught.
static_assert(alignof(C_empty) == alignof(typename C_empty::graded_type));
static_assert(alignof(C_one_byte) == alignof(typename C_one_byte::graded_type));
static_assert(alignof(C_eight_byte) == alignof(typename C_eight_byte::graded_type));

static_assert(std::is_trivially_copyable_v<C_empty> == std::is_trivially_copyable_v<typename C_empty::graded_type>);
static_assert(std::is_trivially_copyable_v<C_one_byte>
              == std::is_trivially_copyable_v<typename C_one_byte::graded_type>);
static_assert(std::is_trivially_copyable_v<C_eight_byte>
              == std::is_trivially_copyable_v<typename C_eight_byte::graded_type>);

static_assert(C_empty::effect_count_in_row() == 0);
static_assert(C_one_byte::effect_count_in_row() == 0);
static_assert(C_eight_byte::effect_count_in_row() == 1);

template <typename T>
using ComputationOverEmptyRow = Computation<Row<>, T>;
CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationOverEmptyRow, OneByteValue);
CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationOverEmptyRow, EightByteValue);

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mk(42);
        return pure.extract() == 42;
    }(),
    "The round trip through mk and extract on an empty-row Computation<int> failed.");

static_assert(
    [] consteval {
        auto bg = Computation<Row<>, int>::lift<Effect::Bg>(7);
        using BgComp = decltype(bg);
        return std::is_same_v<BgComp, Computation<Row<Effect::Bg>, int>>;
    }(),
    "A lift at Effect::Bg did not produce a Computation at Row<Bg>.");

// The final read goes through the substrate accessor because extract
// is gated off for a non-empty row.
static_assert(
    [] consteval {
        auto bg = Computation<Row<>, int>::lift<Effect::Bg>(13);
        auto wider = bg.template weaken<Row<Effect::Bg, Effect::Alloc>>();
        auto widest = wider.template weaken<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
        return widest.graded().peek() == 13;
    }(),
    "A chain of weaken calls through nested subrows did not preserve the inner value.");

static_assert(
    noexcept(std::declval<Computation<Row<Effect::Bg>, int>>().template weaken<Row<Effect::Bg, Effect::IO>>()),
    "The rvalue weaken must be noexcept for a payload that is trivially move-constructible.");

// The copy-constructible conjunct on the lvalue weaken must not
// exclude the ordinary case, so both overloads are witnessed on a
// copyable payload.
static_assert(
    requires(Computation<Row<Effect::Bg>, int> const& c) { c.template weaken<Row<Effect::Bg, Effect::IO>>(); },
    "The lvalue weaken must remain available for a copy-constructible payload.");

static_assert(
    requires(Computation<Row<Effect::Bg>, int>&& c) { std::move(c).template weaken<Row<Effect::Bg, Effect::IO>>(); },
    "The rvalue weaken must remain available for a copy-constructible payload.");

static_assert(std::is_copy_constructible_v<int>,
              "The positive witness for the lvalue weaken constraint depends on int being "
              "copy-constructible.");

// The pins below exercise the trait the constraint depends on without
// instantiating a Computation over a move-only payload.
namespace weaken_copy_constructible_pin {

struct MoveOnlyProbe {
    constexpr MoveOnlyProbe() noexcept = default;
    constexpr MoveOnlyProbe(MoveOnlyProbe&&) noexcept = default;
    MoveOnlyProbe(MoveOnlyProbe const&) = delete;
    MoveOnlyProbe& operator=(MoveOnlyProbe&&) noexcept = default;
    MoveOnlyProbe& operator=(MoveOnlyProbe const&) = delete;
    ~MoveOnlyProbe() = default;
};

static_assert(!std::is_copy_constructible_v<MoveOnlyProbe>,
              "The lvalue weaken constraint depends on std::is_copy_constructible_v answering false for a "
              "type with a deleted copy constructor.  Should that stop holding, the constraint gates "
              "nothing.");

static_assert(std::is_move_constructible_v<MoveOnlyProbe>,
              "The rvalue weaken on a move-only payload depends on that payload being "
              "move-constructible.");

}  // namespace weaken_copy_constructible_pin

namespace extract_payload_gate {

static_assert(detail::extract_admits_payload_v<int>);
static_assert(detail::extract_admits_payload_v<double>);

static_assert(detail::extract_admits_payload_v<Computation<Row<>, int>>);

static_assert(!detail::extract_admits_payload_v<Computation<Row<Effect::Bg>, int>>);
static_assert(!detail::extract_admits_payload_v<Computation<Row<Effect::Alloc, Effect::IO>, int>>);

static_assert(!detail::extract_admits_payload_v<Computation<Row<>, Computation<Row<Effect::Bg>, int>>>);

static_assert(
    !detail::extract_admits_payload_v<Computation<Row<>, Computation<Row<>, Computation<Row<Effect::Bg>, int>>>>);

static_assert(detail::extract_admits_payload_v<Computation<Row<>, Computation<Row<>, Computation<Row<>, int>>>>);

// Only the admitting direction is witnessed through a requires
// expression.  GCC 16 turns a failed constraint inside the body of a
// negated requires expression into a hard error rather than a
// substitution failure, so the rejecting direction is pinned on the
// trait above.
static_assert(
    requires(Computation<Row<>, Computation<Row<>, int>> const& c) { c.extract(); },
    "A pure Computation wrapped in a pure Computation must admit extract.");

static_assert(requires(Computation<Row<>, int> const& c) { c.extract(); }, "A plain payload must still admit extract.");

}  // namespace extract_payload_gate

namespace then_payload_gate {

constexpr auto legit_callback = [](int x) { return Computation<Row<>, int>::mk(x + 1); };
static_assert(
    requires(Computation<Row<>, int> const& c) { c.then(legit_callback); },
    "A callback returning a plain payload must admit through then.");

constexpr auto nested_pure_callback = [](int) {
    using Inner = Computation<Row<>, int>;
    return Computation<Row<>, Inner>::mk(Inner::mk(42));
};
static_assert(
    requires(Computation<Row<>, int> const& c) { c.then(nested_pure_callback); },
    "A callback returning a nested Computation at the empty row must admit through then.  Nothing is "
    "hidden by an empty inner row.");

using LaunderingInner = Computation<Row<Effect::Bg>, int>;
using LaunderingCallback = decltype([](int) {
    return Computation<Row<>, LaunderingInner>::mk(Computation<Row<>, int>::lift<Effect::Bg>(42));
});
static_assert(!detail::extract_admits_payload_v<LaunderingInner>,
              "A callback whose returned value is itself an engaged Computation must not admit through "
              "then.  The inner row would disappear from the union.");

}  // namespace then_payload_gate

namespace lift_provenance_surface {

// The next two record that the unwitnessed lift grants an effect
// claim to a value produced without it.  They pin that surface, so a
// later change to how lift is gated reds here and forces a deliberate
// migration rather than passing unnoticed.
static_assert(
    std::is_same_v<decltype(Computation<Row<>, int>::lift<Effect::Bg>(42)), Computation<Row<Effect::Bg>, int>>,
    "A lift at Effect::Bg over a plain int yields a Computation engaged at Row<Bg>, with nothing "
    "vouching for the claim.");

static_assert(std::is_same_v<decltype(Computation<Row<>, int>::lift<Effect::IO>(7)), Computation<Row<Effect::IO>, int>>,
              "The same ungated claim holds at Effect::IO.  The surface is not specific to one atom.");

static_assert(IsEffect<Effect::Bg> && IsEffect<Effect::IO> && IsEffect<Effect::Alloc> && IsEffect<Effect::Block>,
              "The IsEffect gate on lift must accept every Effect atom.  If it does not, the gate has "
              "stopped recognizing a core atom, which shows up first as errors at lift call sites.");

static_assert(decltype(Computation<Row<>, int>::lift<Effect::Bg>(0))::effect_count_in_row() == 1u,
              "The row claimed by a lift is visible in the result type.  What the unwitnessed form "
              "lacks is provenance, not the claim.");

// The asymmetry between the two contexts below is the closure: one
// carries Bg in its row and can witness a Bg claim, the other cannot.
// If either pin reds, the gate has lost its discriminating power.
static_assert(row_contains_v<typename detail::exec_ctx_self_test::BgWitness::row_type, Effect::Bg>,
              "The background drain context must carry Effect::Bg in its row.  It is the context that "
              "witnesses a Bg claim.");

static_assert(!row_contains_v<typename detail::exec_ctx_self_test::FgWitness::row_type, Effect::Bg>,
              "The hot foreground context must not carry Effect::Bg in its row.  Foreground code must "
              "not be able to witness a Bg claim.");

static_assert(std::is_same_v<decltype(Computation<Row<>, int>::template lift_in<Effect::Bg>(
                                 std::declval<detail::exec_ctx_self_test::BgWitness const&>(), 42)),
                             Computation<Row<Effect::Bg>, int>>,
              "The witnessed lift must admit when the context's row contains the requested effect, and "
              "must give the same result type as the unwitnessed form.");

static_assert(decltype(Computation<Row<>, int>::template lift_in<Effect::Bg>(
                  std::declval<detail::exec_ctx_self_test::BgWitness const&>(), 0))::effect_count_in_row()
                  == 1u,
              "The witnessed lift preserves the type-level claim.  Only the construction path gained a "
              "check.");

}  // namespace lift_provenance_surface

static_assert(noexcept(std::declval<Computation<Row<>, int>>().extract()),
              "The rvalue extract must be noexcept for a payload that is trivially move-constructible.");

static_assert(IsComputation<Computation<Row<>, int>>);
static_assert(IsComputation<Computation<Row<Effect::Bg>, double>>);
static_assert(IsComputation<Computation<Row<>, int>&>);
static_assert(IsComputation<const Computation<Row<>, int>&>);
static_assert(!IsComputation<int>);
static_assert(!IsComputation<Row<Effect::Bg>>);

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mk(7);
        auto doubled = pure.map([](int x) { return x * 2; });
        using Doubled = decltype(doubled);
        return std::is_same_v<Doubled::row_type, Row<>> && std::is_same_v<Doubled::value_type, int>
            && doubled.extract() == 14;
    }(),
    "A map preserves the row and applies the function to the value.");

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mk(3);
        auto as_double = pure.map([](int x) -> double { return x + 0.5; });
        using D = decltype(as_double);
        return std::is_same_v<D::row_type, Row<>> && std::is_same_v<D::value_type, double>;
    }(),
    "A map admits a change of value type while preserving the row.");

static_assert(
    [] consteval {
        auto bg = Computation<Row<>, int>::lift<Effect::Bg>(10);
        auto chained = bg.then([](int x) { return Computation<Row<>, int>::lift<Effect::IO>(x + 1); });
        using Chained = decltype(chained);
        return is_subrow_v<Row<Effect::Bg>, Chained::row_type> && is_subrow_v<Row<Effect::IO>, Chained::row_type>
            && std::is_same_v<Chained::value_type, int>;
    }(),
    "A bind must carry the effects of both sides into the result row.");

// The two rows below overlap, and the mutual subrow test is how the
// assertion says the union collapsed them into one.
static_assert(
    [] consteval {
        auto bg = Computation<Row<>, int>::lift<Effect::Bg>(100);
        auto chained = std::move(bg).then([](int x) { return Computation<Row<>, int>::lift<Effect::Bg>(x); });
        using Chained = decltype(chained);
        return is_subrow_v<Row<Effect::Bg>, Chained::row_type> && is_subrow_v<Chained::row_type, Row<Effect::Bg>>;
    }(),
    "A bind over two rows naming the same effect must absorb the duplicate.");

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mk(5);
        auto chained = pure.then([](int x) { return Computation<Row<>, int>::mk(x * 2); });
        return chained.extract() == 10 && std::is_same_v<decltype(chained)::row_type, Row<>>;
    }(),
    "A bind over two empty rows must produce a result at the empty row.");

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mk(99);
        auto const& g = pure.graded();
        using G = std::remove_cvref_t<decltype(g)>;
        return std::is_same_v<G, ComputationGraded<Row<>, int>> && g.peek() == 99;
    }(),
    "The lvalue graded accessor must expose the substrate view at the matching specialization.");

static_assert(
    [] consteval {
        auto pure = Computation<Row<Effect::Bg>, int>{77};
        auto g = std::move(pure).graded();
        using G = decltype(g);
        return std::is_same_v<G, ComputationGraded<Row<Effect::Bg>, int>> && g.peek() == 77;
    }(),
    "The rvalue graded accessor must move the substrate out at the matching specialization.");

}  // namespace detail::computation_self_test

// Every operation is driven here with non-constant arguments.  The
// static_assert wall above only proves the constant-evaluated path,
// which does not exercise overload selection on real values.
inline void runtime_smoke_test_computation() {
    auto pure_lvalue = Computation<Row<>, int>::mk(100);
    int read_lvalue = pure_lvalue.extract();
    int read_rvalue = std::move(pure_lvalue).extract();
    (void)read_lvalue;
    (void)read_rvalue;

    auto bg_pure = Computation<Row<>, int>::lift<Effect::Bg>(200);
    static_assert(std::is_same_v<decltype(bg_pure), Computation<Row<Effect::Bg>, int>>);

    auto bg_widened_lvalue = bg_pure.template weaken<Row<Effect::Bg, Effect::Alloc>>();
    auto bg_widened_rvalue = std::move(bg_pure).template weaken<Row<Effect::Bg, Effect::IO>>();
    (void)bg_widened_lvalue;
    (void)bg_widened_rvalue;

    auto map_pure = Computation<Row<>, int>::mk(7);
    auto map_lvalue = map_pure.map([](int x) { return x + 1; });
    auto map_rvalue = std::move(map_pure).map([](int x) { return x * 2; });
    (void)map_lvalue;
    (void)map_rvalue;

    auto then_pure = Computation<Row<>, int>::mk(11);
    auto then_lvalue = then_pure.then([](int x) { return Computation<Row<>, int>::lift<Effect::Bg>(x + 100); });
    auto then_rvalue =
        std::move(then_pure).then([](int x) { return Computation<Row<>, int>::lift<Effect::IO>(x + 200); });
    static_assert(std::is_same_v<decltype(then_lvalue)::row_type, Row<Effect::Bg>>);
    static_assert(std::is_same_v<decltype(then_rvalue)::row_type, Row<Effect::IO>>);
    (void)then_lvalue;
    (void)then_rvalue;

    auto graded_lvalue_owner = Computation<Row<Effect::Bg>, int>{555};
    auto const& g_view = graded_lvalue_owner.graded();
    [[maybe_unused]] int peeked = g_view.peek();

    auto graded_rvalue_owner = Computation<Row<Effect::Bg>, int>{666};
    auto g_moved = std::move(graded_rvalue_owner).graded();
    [[maybe_unused]] int peeked_moved = g_moved.peek();
}

}  // namespace foundation::effects
