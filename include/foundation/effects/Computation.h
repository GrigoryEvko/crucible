#pragma once

// A Computation<R, T> is a value of type T whose production may use
// the effects in row R.  Hot-path code holds the empty row and so can
// call nothing effectful.  The row lives in the type alone; the stored
// grade is an empty singleton, so the carrier is the size of T however
// many atoms R names.
//
// It is one instantiation of the graded substrate, not a parallel
// hierarchy: ComputationGraded<R, T> is Graded<Relative, At<Es...>, T>
// and Computation<R, T> derives from it privately, with no members of
// its own.  The modality is Relative because a row carries neither a
// unit nor a counit.  A value cannot be injected into a row, since the
// row is already the typing context, and it cannot be extracted out of
// one short of closing the universe.  What a row does carry is
// propagation, which `weaken` widens up the lattice, and the derived
// class adds the row-level operations the substrate cannot express — the
// empty-row extract, the type-changing weaken, map, then, and the two
// mints.
//
// The base is private, so the substrate's consume, peek and peek_mut are
// not members of a Computation.  They read and move the payload whatever
// the row and whatever the payload holds, and each route out of a
// Computation carries the gate that belongs to it.  An engaged row is
// built only by the witnessed mint, which reads the row of a context, or
// by a member that keeps or widens a row the value already had.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
// Forward declarations only, for the authority relation below. Permission
// carries a defaulted Brand parameter, so it has to be named through the
// header that declares it rather than redeclared here.
#include <foundation/permissions/Fwd.h>
#include <foundation/reflect/TypeComponents.h>

#include <concepts>
#include <cstddef>
#include <meta>
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

// The row is a claim about how the value was produced, and the substrate
// pairs a value with a claim only through a key.  Computation below holds
// that key, so the bare carrier builds no default value and no bottom one.
//
// The probes below ask which members of the substrate the bare carrier
// has.  The check file of this header and
// test/foundation/test_computation.cpp read them, so they live here.
namespace detail::computation_graded_caps {

template <typename G>
concept HasAtBottom = requires { G::at_bottom(); };
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

}  // namespace detail::computation_graded_caps

template <typename R, typename T>
class Computation;

// Both traits are specialized for Computation after the class is
// complete.  The requires-clauses that use them inside the class body
// are checked when those members are instantiated, by which point the
// specializations are visible.
// Declared, not included.  The authority relation below names Capability
// and nothing here calls it, so pulling in Capability.h would add a
// dependency for a name.  Capability carries no defaulted parameter, so
// unlike Permission it can be declared here.
template <Effect Cap, class Source>
class Capability;

namespace detail {
template <typename>
struct is_computation : std::false_type {};

// Whether handing a payload out through extract would give the receiver
// a power the outer row never named.
//
// This relation used to be its own complement, and the complement was
// false.  It read "a plain payload has nothing to hide, so it always
// admits", with a single specialization for a nested Computation hiding
// its row.  A Capability is a plain payload by that test, so
// Computation<Row<>, Capability<Effect::IO, S>> — a value typed PURE —
// admitted extract and handed out an IO capability.  The mechanism
// checked one thing and the sentence generalised it to every payload.
//
// So the relation is stated positively, and the set is enumerated here
// in one place rather than assumed empty.  Three kinds convey authority:
//
//   * a capability, which IS the authority to perform an effect;
//   * a permission token, exclusive or shared, which is the authority to
//     touch a region;
//   * an execution context, which holds a capability member and would
//     hand that capability out with itself.
//
// A nested Computation conveys authority when its own row is engaged.
//
// Those are the kinds that convey authority by themselves.  A payload
// conveys authority when one of them is a component of it, read by the
// walk in foundation/reflect/TypeComponents.h: a template argument, a
// base, a by-value member, the target of a pointer, the return or a
// parameter type of a function, or a part of a pointer to member.  So a
// pair, an optional or a plain struct that holds a capability conveys
// the capability, a pointer to a function that returns one conveys it
// too, and a stack of carriers cannot launder one.
//
// A class that holds state the walk cannot read is refused, because the
// gate cannot say what that state is.  That is a class that is not empty
// and that reflects no base and no data member, which is the shape of a
// lambda with captures: GCC 16 reflects no capture.  A lambda that
// captures a capability would otherwise leave through extract as a
// plain value.
//
// A type outside the list says so itself by carrying
// `static constexpr bool conveys_authority = true;`, the same opt-in
// shape GradedTrait.h uses for value_type_decoupled.
//
// The residual, stated rather than papered over.  The walk does not see
// a value behind type erasure, as in std::function or std::any, or a
// member of a specialization that it reaches only through a pointer or a
// template argument.  TypeComponents.h states both.  Inert payloads are
// the common case, and a refusal of each unknown type would make extract
// unusable rather than safe, so a type the walk reads in full and finds
// no authority in is admitted.

template <typename T>
concept DeclaresConveysAuthority = requires {
    { T::conveys_authority } -> std::convertible_to<const bool&>;
} && T::conveys_authority;

// The enumerated kinds alone, without the opt-in member.  The walk asks
// this of a node whose members it may not read.
template <typename T>
struct conveys_authority_listed : std::false_type {};

template <Effect Cap, class Source>
struct conveys_authority_listed<Capability<Cap, Source>> : std::true_type {};

template <typename Tag, typename Brand>
struct conveys_authority_listed<::foundation::permissions::Permission<Tag, Brand>> : std::true_type {};

template <typename Tag, typename Brand>
struct conveys_authority_listed<::foundation::permissions::SharedPermission<Tag, Brand>> : std::true_type {};

template <class Cap, class R>
struct conveys_authority_listed<ExecCtx<Cap, R>> : std::true_type {};

template <typename T>
inline constexpr bool conveys_authority_listed_v = conveys_authority_listed<T>::value;

// The enumerated kinds and the opt-in member together.
template <typename T>
struct conveys_authority_directly : std::bool_constant<conveys_authority_listed_v<T> || DeclaresConveysAuthority<T>> {};

template <typename T>
inline constexpr bool conveys_authority_directly_v = conveys_authority_directly<T>::value;

// The opt-in member is read only where the walk may read members.  A
// read elsewhere would instantiate a specialization that the walk
// reached through an argument or a pointer, and TypeComponents.h
// promises no such instantiation.  The enumerated kinds match by
// partial specialization, which instantiates nothing.
[[nodiscard]] consteval bool node_conveys_authority(::foundation::reflect::TypeNode node) {
    if (::foundation::reflect::holds_unreadable_state(node)) return true;
    const std::meta::info trait = node.may_read_members ? ^^conveys_authority_directly_v : ^^conveys_authority_listed_v;
    return std::meta::extract<bool>(std::meta::substitute(trait, {node.type}));
}

template <typename T>
struct conveys_authority
    : std::bool_constant<::foundation::reflect::any_component_satisfies<
          [](::foundation::reflect::TypeNode node) consteval { return node_conveys_authority(node); }>(^^T)> {};

template <typename T>
inline constexpr bool conveys_authority_v = conveys_authority<std::remove_cvref_t<T>>::value;

template <typename T>
struct extract_admits_payload : std::bool_constant<!conveys_authority_v<T>> {};
template <typename T>
inline constexpr bool extract_admits_payload_v = extract_admits_payload<T>::value;
}  // namespace detail

template <typename T>
concept IsComputation = detail::is_computation<std::remove_cvref_t<T>>::value;

template <typename R, typename T>
class [[nodiscard]] Computation : private ComputationGraded<R, T> {
    using base = ComputationGraded<R, T>;

public:
    using row_type = R;
    using graded_type = base;
    using typename base::grade_type;
    using typename base::lattice_type;
    using typename base::value_type;

    // The diagnostic surface of the substrate, which GradedWrapper reads.
    // Each names the grade or the type and none of them reads the payload.
    using base::grade;
    using base::lattice_name;
    using base::modality;
    using base::modality_name;
    using base::value_type_name;

    static constexpr std::size_t row_size = ::foundation::effects::row_size(^^R);

    [[nodiscard]] static consteval std::size_t effect_count_in_row() noexcept {
        return ::foundation::effects::row_size(^^R);
    }

    // A default value makes no claim only at the empty row.  At an engaged
    // row it would claim effects that nothing exercised, which the
    // witnessed mint exists to check.  The constraint also keeps the body
    // from being defined for a payload that has no default constructor.
    constexpr Computation() noexcept(std::is_nothrow_default_constructible_v<T>
                                     && std::is_nothrow_move_constructible_v<T>)
        requires(::foundation::effects::row_size(^^R) == 0) && std::is_default_constructible_v<T>
        : base{::foundation::algebra::grade_key<Computation>{}, T{}, grade_type{}} {}
    constexpr Computation(const Computation&) = default;
    constexpr Computation(Computation&&) = default;
    constexpr Computation& operator=(const Computation&) = default;
    constexpr Computation& operator=(Computation&&) = default;
    ~Computation() = default;

private:
    // Every Computation reaches the private constructor and the payload
    // of every other one, so a member that keeps or widens a row builds
    // its result through the same door as the two mints.
    template <typename, typename>
    friend class Computation;

    explicit constexpr Computation(T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        : base{::foundation::algebra::grade_key<Computation>{}, std::move(x), grade_type{}} {}

public:
    // The lift of a pure value, named so that one grep over mint_ finds
    // every authorization point.  It derives its authority from the
    // empty-row constraint alone, so it takes no context.
    [[nodiscard]] static constexpr Computation mint_computation(T x) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(::foundation::effects::row_size(^^R) == 0)
    {
        return Computation{std::move(x)};
    }

    // The payload constraint is what stops an engaged Computation from
    // being carried out through a pure-looking wrapper.
    [[nodiscard]] constexpr const T& extract() const& noexcept
        requires(::foundation::effects::row_size(^^R) == 0) && detail::extract_admits_payload_v<T>
    {
        return base::peek();
    }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(::foundation::effects::row_size(^^R) == 0) && detail::extract_admits_payload_v<T>
    {
        return std::move(*this).base::consume();
    }

    // The one mint of an engaged row.  Its constraint is the proof that
    // the caller's context owns the claimed effect, so a caller whose
    // context carries the empty row finds no candidate here, whatever
    // effect it asks for.  Nothing about a raw T proves that producing it
    // exercised Cap.  The context does prove that the caller may exercise
    // Cap, and that is the claim the row makes.
    //
    // The context argument is read for its type alone.
    template <Effect Cap, class Ctx>
        requires IsEffect<Cap> && CtxOwnsCapability<Ctx, Cap>
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
    // attach a row at construction, use the witnessed mint.
    //
    // The copy-constructible conjunct moves the failure for a
    // move-only payload from deep inside the body out to overload
    // resolution, where the caller is told to use the rvalue form.
    // The rvalue overload needs no such gate: it moves.
    template <typename R2>
        requires Subrow<R, R2>
              && (::foundation::effects::row_size(^^R) > 0 || ::foundation::effects::row_size(^^R2) == 0)
              && std::is_copy_constructible_v<T>
    [[nodiscard]] constexpr Computation<R2, T> weaken() const& noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return Computation<R2, T>{base::peek()};
    }

    template <typename R2>
        requires Subrow<R, R2>
              && (::foundation::effects::row_size(^^R) > 0 || ::foundation::effects::row_size(^^R2) == 0)
    [[nodiscard]] constexpr Computation<R2, T> weaken() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Computation<R2, T>{std::move(*this).base::consume()};
    }

    // The row is unchanged because f is a value transformation and
    // cannot introduce an effect.  An f that wants one goes through
    // `then`.
    //
    // The payload constraint is the one extract carries.  f receives the
    // payload, so a payload that conveys an authority would hand it to f
    // under a row that does not name it.
    template <typename F>
        requires std::is_invocable_v<F, const T&> && detail::extract_admits_payload_v<T>
    [[nodiscard]] constexpr auto
    map(F&& f) const& noexcept(std::is_nothrow_invocable_v<F, const T&>
                               && std::is_nothrow_move_constructible_v<std::invoke_result_t<F, const T&>>)
        -> Computation<R, std::invoke_result_t<F, const T&>> {
        using U = std::invoke_result_t<F, const T&>;
        return Computation<R, U>{std::forward<F>(f)(base::peek())};
    }

    template <typename F>
        requires std::is_invocable_v<F, T> && detail::extract_admits_payload_v<T>
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
    // The payload constraint on the returned value rejects a callback
    // whose declared row hides an engaged Computation in the returned
    // value.  After the union the inner row would be invisible in the
    // result type.  Chaining several binds does the same job honestly.
    // The payload constraint on T is the one map carries.
    template <typename F>
        requires std::is_invocable_v<F, const T&> && detail::extract_admits_payload_v<T>
              && IsComputation<std::invoke_result_t<F, const T&>>
              && detail::extract_admits_payload_v<typename std::invoke_result_t<F, const T&>::value_type>
    [[nodiscard]] constexpr auto
    then(F&& k) const& -> Computation<row_union_t<R, typename std::invoke_result_t<F, const T&>::row_type>,
                                      typename std::invoke_result_t<F, const T&>::value_type> {
        using Inner = std::invoke_result_t<F, const T&>;
        using R2 = typename Inner::row_type;
        using U = typename Inner::value_type;
        using Result = Computation<row_union_t<R, R2>, U>;
        Inner intermediate = std::forward<F>(k)(base::peek());
        return Result{std::move(intermediate).Inner::base::consume()};
    }

    template <typename F>
        requires std::is_invocable_v<F, T> && detail::extract_admits_payload_v<T>
              && IsComputation<std::invoke_result_t<F, T>>
              && detail::extract_admits_payload_v<typename std::invoke_result_t<F, T>::value_type>
    [[nodiscard]] constexpr auto
    then(F&& k) && -> Computation<row_union_t<R, typename std::invoke_result_t<F, T>::row_type>,
                                  typename std::invoke_result_t<F, T>::value_type> {
        using Inner = std::invoke_result_t<F, T>;
        using R2 = typename Inner::row_type;
        using U = typename Inner::value_type;
        using Result = Computation<row_union_t<R, R2>, U>;
        Inner intermediate = std::forward<F>(k)(std::move(*this).base::consume());
        return Result{std::move(intermediate).Inner::base::consume()};
    }

    // The way out to the substrate view, for code that wants the
    // uniform grade and lattice diagnostics or a cache key.  The view
    // is the base subobject and const: no copy, no second object, and no
    // write through it.  Each form carries the payload constraint that
    // map carries, because the substrate reads the payload out.  The
    // rvalue form moves the substrate out, and the row stays in its
    // lattice type.
    [[nodiscard]] constexpr const graded_type& graded() const& noexcept
        requires detail::extract_admits_payload_v<T>
    {
        return *this;
    }

    [[nodiscard]] constexpr graded_type graded() && noexcept(std::is_nothrow_move_constructible_v<graded_type>)
        requires detail::extract_admits_payload_v<T>
    {
        return std::move(*this);
    }
};

namespace detail {
template <typename R, typename T>
struct is_computation<Computation<R, T>> : std::true_type {};

// A nested carrier hides its own row from everything that reads the
// outer type, so an engaged inner row conveys authority exactly as a
// capability does.  The payload is a template argument, so the walk
// reads it, which is what keeps a stack of carriers from laundering one.
template <typename R, typename U>
struct conveys_authority_listed<Computation<R, U>> : std::bool_constant<(::foundation::effects::row_size(^^R) != 0)> {};

// A Computation declares no opt-in member, so its answer is the row
// alone.  This keeps the walk from a member lookup that instantiates
// the carrier.
template <typename R, typename U>
struct conveys_authority_directly<Computation<R, U>> : conveys_authority_listed<Computation<R, U>> {};
}  // namespace detail

#define CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationAlias, T_)                                                   \
    static_assert(sizeof(ComputationAlias<T_>) == sizeof(T_),                                                         \
                  "The Computation alias " #ComputationAlias " over " #T_ " is larger than its payload.  Review the " \
                  "[[no_unique_address]] member.")

}  // namespace foundation::effects
