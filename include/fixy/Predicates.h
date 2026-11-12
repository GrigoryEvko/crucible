#pragma once

// The predicates of a refinement, their combinators, and the implication
// relation between them.
//
// fixy/Refined.h grades a value with one predicate of this header.  The
// implication relation is a closed namespace, and each predicate that a
// rule of the relation names stands in this header too.  So no answer of
// the relation depends on which headers a file included.

#include <foundation/Platform.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/reflect/Anchor.h>

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy {

// Every predicate is stateless, so that it can serve as a template
// argument.
//
// Every predicate is also a named class type, never a closure.  The
// predicate's type is a template argument of the refinement's lattice,
// and the lattice's printed name keys the row hash.  GCC prints a
// generic closure with a counter that runs across the translation unit,
// so a closure predicate gives one refinement a different hash in each
// translation unit.  foundation/reflect/Hash.h refuses such a type in
// every id, so a closure predicate fails to hash rather than hashing
// wrong.  Each predicate ships as a class and a value, and call sites
// name the value.

// Each call operator states the expression its body evaluates as a
// constraint, so a predicate that cannot evaluate a type fails
// PredicateInvocableOn at the mint instead of failing inside the body.
//
// Each call operator has one shape: a template over the value type that
// takes one value of that type, by value or by reference to const.  The
// seal below reads the specialization of that template for the value type
// of a mint, and the checked mint refuses each other shape.
struct IsPositive {
    constexpr bool operator()(auto x) const noexcept
        requires requires { x > decltype(x){0}; }
    {
        return x > decltype(x){0};
    }
};

inline constexpr IsPositive positive{};

struct IsNonNegative {
    constexpr bool operator()(auto x) const noexcept
        requires requires { x >= decltype(x){0}; }
    {
        return x >= decltype(x){0};
    }
};

inline constexpr IsNonNegative non_negative{};

// For an unsigned type this coincides with positive. The two are kept
// apart because they state different intents: non_zero reserves a
// sentinel, positive claims a sign class.
//
// The zero is value-initialised rather than written as a literal, so
// that the zero value of a pointer type is spelled as the null pointer
// it is.  For an arithmetic type the two spellings are the same value.
struct IsNonZero {
    constexpr bool operator()(const auto& x) const noexcept
        requires requires { x.raw() != 0; } || requires { x != std::remove_cvref_t<decltype(x)>{}; }
    {
        if constexpr (requires { x.raw(); })
            return x.raw() != 0;
        else
            return x != std::remove_cvref_t<decltype(x)>{};
    }
};

inline constexpr IsNonZero non_zero{};

// The dual of non_zero. It holds where a wire or disk format reserves
// zero as the only valid payload for a field, so that the must-be-zero
// invariant lives in the type instead of being discovered by reading a
// write routine and noticing the zero literal.
struct IsZero {
    constexpr bool operator()(const auto& x) const noexcept
        requires requires { x.raw() == 0; } || requires { x == std::remove_cvref_t<decltype(x)>{}; }
    {
        if constexpr (requires { x.raw(); })
            return x.raw() == 0;
        else
            return x == std::remove_cvref_t<decltype(x)>{};
    }
};

inline constexpr IsZero is_zero{};

// The value type is the pointer type, so the seal reads the call for the
// value type of a mint.
struct IsNonNull {
    template <class V>
        requires std::is_pointer_v<V>
    constexpr bool operator()(V p) const noexcept {
        return p != nullptr;
    }
};

inline constexpr IsNonNull non_null{};

struct IsPowerOfTwo {
    constexpr bool operator()(auto x) const noexcept
        requires requires { x != decltype(x){0} && (x & (x - decltype(x){1})) == decltype(x){0}; }
    {
        using U = decltype(x);
        return x != U{0} && (x & (x - U{1})) == U{0};
    }
};

inline constexpr IsPowerOfTwo power_of_two{};

struct IsNonEmpty {
    constexpr bool operator()(const auto& c) const noexcept
        requires requires { !c.empty(); }
    {
        return !c.empty();
    }
};

inline constexpr IsNonEmpty non_empty{};

// Each parameterised predicate is a named struct template rather than
// a variable template of lambdas. A lambda produces a distinct
// unnamed closure type per parameter, and the compiler cannot
// pattern-match that back to the parameter, so the implication rules
// at the foot of this file could not deduce against it. Callers are
// unaffected, since the paired variable template still reads as a
// call.
//
// Each predicate therefore ships in two pieces: the struct template is
// the type that the implication rules deduce against, and the variable
// template is the value that call sites pass.

namespace refined {

// An integer type that std::cmp_less and std::in_range accept.  The
// standard excludes bool and the character types from both.
template <class T>
concept ExactInteger = std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t>
                    && !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

// True when the value type V holds the bound exactly.  A bound compared
// after a conversion to the value type changes: 256 as a std::uint8_t is
// 0, and -1 as an unsigned is its largest value.  An integer value type
// holds an integer bound in its range.  A floating-point value type holds
// an integer bound that its significand spans, and a finite
// floating-point bound of a type that is no wider than itself.  Every
// other pair is refused: a floating-point bound on an integer value, a
// NaN or an infinity, and a bound that a narrower floating-point type
// would round.
//
// A predicate whose bound the value type cannot hold is not defined on
// that type, and the checked mint refuses it at the call.  The two
// concepts hold the whole rule, so no translation unit can specialize a
// verdict in front of it.
//
// A significand of Digits bits holds each integer Bound with a magnitude of
// at most 2^Digits.  A significand as wide as std::uintmax_t holds each
// integer bound, and the disjunction stops before the shift of that width.
template <auto Bound, int Digits>
concept SignificandSpans = Digits >= std::numeric_limits<std::uintmax_t>::digits
                        || (std::cmp_less_equal(Bound, std::uintmax_t{1} << Digits)
                            && std::cmp_greater_equal(Bound, -static_cast<std::intmax_t>(std::uintmax_t{1} << Digits)));

template <class V, auto Bound>
concept BoundFits = (ExactInteger<V> && ExactInteger<decltype(Bound)> && std::in_range<V>(Bound))
                 || (std::floating_point<V> && ExactInteger<decltype(Bound)>
                     && SignificandSpans<Bound, std::numeric_limits<V>::digits>)
                 || (std::floating_point<V> && std::floating_point<decltype(Bound)>
                     && std::numeric_limits<decltype(Bound)>::digits <= std::numeric_limits<V>::digits
                     && std::numeric_limits<decltype(Bound)>::max_exponent <= std::numeric_limits<V>::max_exponent
                     && std::numeric_limits<decltype(Bound)>::min_exponent >= std::numeric_limits<V>::min_exponent
                     && __builtin_isfinite(Bound) != 0);

// The type of a predicate value, stripped of const.  An inline
// constexpr predicate variable is const at file scope while the
// template argument that binds it is not, so every place that reasons
// about a predicate by type goes through this one alias.
template <auto Pred>
using predicate_t = std::remove_cv_t<decltype(Pred)>;

// THE SEAL
//
// A translation unit can specialize a class template, a member template of
// a class, and a member of a class template instance.  A specialization of
// a predicate or of its call operator changes what the predicate admits.
// The mint and the implication relation read only the name of the
// predicate, so a minted value would then carry a claim that the predicate
// of this header refutes.
//
// An implicit instantiation stands where its template stands.  An explicit
// specialization stands where a unit declares it, and so does each member
// of a partial specialization.  So the seal reads the file of each
// declaration on the path of a call: the class, each call operator, and
// the specialization of the call operator for the value type.  Each one
// must stand in the home of its class: the file of its primary template,
// or the file of the class when the class is no specialization.  The
// functions of the seal are not templates, so no unit can specialize the
// seal either.
//
// A #line directive can give a declaration the name of a different file,
// and the seal reads that name.

// True when type is a class with a base class.  The mint refuses such a
// predicate, because a call can run a call operator of a base.  No rule of
// the relation takes such a class as a premise or as a conclusion, so a
// predicate that derives from BoundedAbove<9> borrows no place of its base
// in the relation, and its own call operator decides nothing there.
// Complexity: O(bases).
[[nodiscard]] consteval bool has_base_class(std::meta::info type) {
    return std::meta::is_class_type(type) && !std::meta::bases_of(type, std::meta::access_context::unchecked()).empty();
}

// The declaration that gives a class its home: its primary template when
// the class is a specialization, and the class when it is not.
[[nodiscard]] consteval std::meta::info home_of(std::meta::info type) {
    return std::meta::has_template_arguments(type) ? std::meta::template_of(type) : type;
}

// Stops the build when a declaration on the path of a call of a predicate
// stands in a file other than the file of home.  The two file names
// compare as text, and the text names the declaration and its file.
consteval void require_at_home(std::meta::info declaration, std::meta::info home) {
    const char* const declared_in = std::meta::source_location_of(declaration).file_name();
    if (__builtin_strcmp(declared_in, std::meta::source_location_of(home).file_name()) == 0) return;
    throw std::meta::exception(
        std::string{"fixy::refined: a predicate is specialized outside the file of its class, while the mint and the "
                    "implication relation read only its name.  Declare a predicate of your own, and specialize no "
                    "predicate.  The specialized declaration is "}
            + std::meta::display_string_of(declaration) + " in " + declared_in,
        declaration);
}

// True when member is a call operator: a member function or a member
// function template whose operator is the call.
[[nodiscard]] consteval bool is_call_operator(std::meta::info member) {
    return (std::meta::is_operator_function(member) || std::meta::is_operator_function_template(member))
        && std::meta::operator_of(member) == std::meta::operators::op_parentheses;
}

// Seals a class, and gives the number of its call operators.  value is the
// value type of a call, or the null reflection for a check of the class
// alone.  For a value type, the specialization of each call operator
// template for that type is the function that the call runs, so it must
// take one value of that type, and a template of another shape gives the
// count 0.  Complexity: O(members²) reads of the members, because the walk
// asks for them again at each index.
consteval std::size_t seal_class(std::meta::info type, std::meta::info value) {
    const std::meta::info home = home_of(type);
    require_at_home(type, home);
    std::size_t calls = 0;
    for (std::size_t index = 0; index < std::meta::members_of(type, std::meta::access_context::unchecked()).size();
         ++index) {
        const std::meta::info member = std::meta::members_of(type, std::meta::access_context::unchecked())[index];
        if (!is_call_operator(member)) continue;
        ++calls;
        require_at_home(member, home);
        if (value == std::meta::info{} || !std::meta::is_function_template(member)) continue;
        if (!std::meta::can_substitute(member, {value})) return 0;
        const std::meta::info call = std::meta::substitute(member, {value});
        if (std::meta::parameters_of(call).size() != 1
            || std::meta::dealias(std::meta::remove_cvref(std::meta::type_of(std::meta::parameters_of(call)[0])))
                   != value) {
            return 0;
        }
        require_at_home(call, home);
    }
    return calls;
}

// True when the class of the predicate and each of its call operators
// stand at home.  The implication relation reads this.  It gives true or
// stops the build.
[[nodiscard]] consteval bool is_sealed_predicate(std::meta::info predicate) {
    predicate = std::meta::dealias(predicate);
    if (std::meta::is_class_type(predicate) && std::meta::is_complete_type(predicate)) {
        static_cast<void>(seal_class(predicate, std::meta::info{}));
    }
    return true;
}

// True when the predicate is sealed and the function that a call of it
// runs on a value of the type value stands at home too.  The mints and the
// combinators read this through PredicateInvocableOn.  A specialization
// outside the home stops the build.  A call that the seal cannot read gives
// false: a class with a base, with no call operator or with more than one,
// or a call operator template that does not take one value of the value
// type.  Then the checked mint refuses the predicate, and the trusted mint
// still takes it.  A predicate that is not a class has no call operator to
// specialize, and Refinement refuses it.
[[nodiscard]] consteval bool is_sealed_call(std::meta::info predicate, std::meta::info value) {
    predicate = std::meta::dealias(predicate);
    if (!std::meta::is_class_type(predicate)) return true;
    return !has_base_class(predicate) && seal_class(predicate, std::meta::dealias(std::meta::remove_cvref(value))) == 1;
}

}  // namespace refined

template <std::size_t Alignment>
struct Aligned {
    template <class V>
        requires std::is_pointer_v<V>
    constexpr bool operator()(V p) const noexcept {
        return (std::bit_cast<std::uintptr_t>(p) & (Alignment - 1)) == 0;
    }
};

template <std::size_t Alignment>
inline constexpr Aligned<Alignment> aligned{};

// The four bounded predicates below compare the value with each bound by
// value, and each is defined only on a value type that holds its bounds
// (refined::BoundFits).  Two integers compare through std::cmp_*, and a
// floating-point value compares with the conversion of the bound, which
// BoundFits makes exact.  Each body makes its own comparison and calls no
// helper template, because a unit could specialize such a helper.
template <auto Lo, auto Hi>
struct InRange {
    template <class V>
        requires refined::BoundFits<V, Lo> && refined::BoundFits<V, Hi>
    constexpr bool operator()(V x) const noexcept {
        if constexpr (refined::ExactInteger<V>)
            return std::cmp_greater_equal(x, Lo) && std::cmp_less_equal(x, Hi);
        else
            return x >= static_cast<V>(Lo) && x <= static_cast<V>(Hi);
    }
};

template <auto Lo, auto Hi>
inline constexpr InRange<Lo, Hi> in_range{};

template <auto Max>
struct BoundedAbove {
    template <class V>
        requires refined::BoundFits<V, Max>
    constexpr bool operator()(V x) const noexcept {
        if constexpr (refined::ExactInteger<V>)
            return std::cmp_less_equal(x, Max);
        else
            return x <= static_cast<V>(Max);
    }
};

template <auto Max>
inline constexpr BoundedAbove<Max> bounded_above{};

// The two size predicates are defined only on a value with a size, as
// non_empty is.  The clause keeps a type with no size out of the checked
// mint, so the refusal is at the gate and not inside the predicate body.
template <std::size_t N>
struct LengthGe {
    constexpr bool operator()(const auto& c) const noexcept
        requires requires { c.size() >= N; }
    {
        return c.size() >= N;
    }
};

template <std::size_t N>
inline constexpr LengthGe<N> length_ge{};

template <std::size_t N>
struct ExactSize {
    constexpr bool operator()(auto const& c) const noexcept
        requires requires { c.size() == N; }
    {
        return c.size() == N;
    }
};

template <std::size_t N>
inline constexpr ExactSize<N> exact_size{};

template <auto Min>
struct BoundedBelow {
    template <class V>
        requires refined::BoundFits<V, Min>
    constexpr bool operator()(V x) const noexcept {
        if constexpr (refined::ExactInteger<V>)
            return std::cmp_greater_equal(x, Min);
        else
            return x >= static_cast<V>(Min);
    }
};

template <auto Min>
inline constexpr BoundedBelow<Min> bounded_below{};

// This is divisibility of a count, distinct from the byte-alignment of
// an address that `aligned` tests.  The divisor must be positive as well
// as held by the value type: a divisor that the value type turns into
// zero is a modulo by zero, and INT_MIN % -1 overflows.
template <auto Divisor>
struct DivisibleBy {
    static_assert(Divisor != decltype(Divisor){0}, "DivisibleBy<0> is undefined (modulo by zero).  Pick a non-"
                                                   "zero divisor or omit the predicate.");
    template <class V>
        requires refined::ExactInteger<V> && refined::BoundFits<V, Divisor> && (std::cmp_greater(Divisor, 0))
    constexpr bool operator()(V x) const noexcept {
        return x % static_cast<V>(Divisor) == 0;
    }
};

template <auto Divisor>
inline constexpr DivisibleBy<Divisor> divisible_by{};

// A conjunction of two predicates can already be spelled two other
// ways: as two nested refinement wrappers, or as a hand-rolled struct
// combining them. Both lose. Nesting produces a type the implication
// relation cannot see through, so every subsumption chain stops at
// the outer wrapper, and both forms grow at each call site. A
// combinator is one predicate type instead, which composes with any
// other and still collapses inside the wrapper.
//
// Each combinator is itself a predicate, so they nest freely.  A
// combinator is defined on a value type only where each predicate it
// names is, so a conjunct that cannot evaluate the value refuses the
// combinator at the mint, as the conjunct alone would.

// This gates the mints of fixy/Refined.h, never the class template
// itself.  The subsort machinery reasons over a Refined type without
// ever constructing one, so a requires-clause on the class template
// would make those types unnameable and break that discipline.  The
// checked mint is also the only place the predicate is actually
// evaluated, and gating it turns what would be a SFINAE cascade inside
// the contract clause into one concept-violation message at the call
// site.
//
// The second part is the seal of the call.  It reads the function that
// the call runs, and it stops the build when a unit specialized the
// predicate or its call operator.  A combinator gates its call operator
// on this concept, so the seal reaches each predicate that a combinator
// names.
template <auto Pred, typename T>
concept PredicateInvocableOn = requires(T const& v) {
    { Pred(v) } -> std::convertible_to<bool>;
} && refined::is_sealed_call(^^refined::predicate_t<Pred>, ^^T);

namespace refined_algebra {

template <auto... Preds>
struct AllOf {
    template <class V>
        requires(PredicateInvocableOn<Preds, V> && ...)
    constexpr bool operator()(V const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0)
            return true;
        else
            return (... && Preds(v));
    }
};

template <auto... Preds>
inline constexpr AllOf<Preds...> all_of{};

template <auto... Preds>
struct AnyOf {
    template <class V>
        requires(PredicateInvocableOn<Preds, V> && ...)
    constexpr bool operator()(V const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0)
            return false;
        else
            return (... || Preds(v));
    }
};

template <auto... Preds>
inline constexpr AnyOf<Preds...> any_of{};

template <auto Pred>
struct Negate {
    template <class V>
        requires PredicateInvocableOn<Pred, V>
    constexpr bool operator()(V const& v) const noexcept {
        return !Pred(v);
    }
};

template <auto Pred>
inline constexpr Negate<Pred> negate{};

template <auto Pre, auto Post>
struct Implies {
    template <class V>
        requires PredicateInvocableOn<Pre, V> && PredicateInvocableOn<Post, V>
    constexpr bool operator()(V const& v) const noexcept {
        return !Pre(v) || Post(v);
    }
};

template <auto Pre, auto Post>
inline constexpr Implies<Pre, Post> implies{};

}  // namespace refined_algebra

// The atomic predicates already live one namespace out, so the
// combinators are re-exported beside them and the whole vocabulary
// reads the same at a call site.
using refined_algebra::AllOf;
using refined_algebra::AnyOf;
using refined_algebra::Negate;
using refined_algebra::Implies;
using refined_algebra::all_of;
using refined_algebra::any_of;
using refined_algebra::negate;
using refined_algebra::implies;

// PredicateImplies<P, Q> reads: every value satisfying P also satisfies
// Q.  P is therefore at least as strong as Q, and P's truth set is
// contained in Q's.  Reversing the reading inverts every axiom below.
//
// The relation is closed.  Its members are the variables and the
// functions declared in the namespace admitted_implications and nothing
// else: an edge for each pair of atomic predicates, and a rule for each
// parameterised family.  There is no primary template to specialise.
// Each reader of the relation is a concept or a function at namespace
// scope that is not a template, and each rule is such a function too, so
// no translation unit can specialise one to admit a pair.  A pair the
// namespace does not admit stays refused wherever the check runs.
// Declare every member before the first check against the relation.
//
// The relation is transitive.  predicate_implies computes the closure of
// the admitted steps at compile time.  A conclusion that two admitted
// steps reach then needs no member of its own.  A path uses admitted
// steps only: an edge or a rule that the namespace does not declare does
// not exist for the closure either.
//
// Each step must be sound on every value type on which its two
// predicates are defined. A bounded predicate compares a value with its
// bound by value and is defined only where the value type holds the
// bound, so a step that compares two bounds as integers holds on each
// such type. A chain of such steps is sound on a value
// type only when each inner predicate of the chain is defined there
// too. Most steps keep or widen the domain, and they can stand
// anywhere in a chain. A narrowing edge is a step whose conclusion is
// defined on fewer value types than its premise. The closure takes a
// narrowing edge only as the last step of a chain, because no
// predicate after it can be defined where it is not.
//
// Two distinct predicates that imply each other through steps that do
// not narrow are one predicate under two names. The closure refuses
// that cycle where it meets it, and the edge walk at the foot of this
// file searches from every edge for it. non_null and non_zero imply each other, but only
// through the narrowing edge non_zero ⇒ non_null, and they differ on
// every value type that is not a pointer.
//
// A rule that brings a predicate to a different family names the
// strongest predicate it reaches as its successor, and the verdict of the
// rule on that step still decides it.  A rule that only weakens the
// parameters of one predicate needs no successor: the last step of a
// chain covers it.  The closure reaches at most closure_node_limit
// predicates from one premise, and a larger search answers false.
//
// Reflexivity is deliberately absent. The subsort machinery already
// supplies it from a same-type fall-through, and stating it twice
// invites the two to drift apart. For the same reason, the closure
// answers a query of a predicate against itself with the one-step
// relation alone.

namespace refined {

// The verdict of one rule on one pair.  admits is true when the rule
// admits premise ⇒ conclusion.  successor is the strongest predicate
// that the rule reaches from the premise, or the null reflection when
// the rule names none.  The closure stands on a successor only when the
// rule also admits the step to it, so a successor can propose a
// predicate but never admit one.
struct rule_verdict {
    bool admits = false;
    std::meta::info successor{};
};

// The type of a rule.  A rule is a function of this type in
// admitted_implications.  It takes the reflections of the premise and of
// the conclusion, each read through its aliases.  A null conclusion asks
// for the successor alone.
using rule_signature = rule_verdict(std::meta::info, std::meta::info);

// True when type is a specialization of the class template that family
// reflects.  A class that derives from a specialization is not one, and
// neither is a class template of the same name in another namespace.
[[nodiscard]] consteval bool is_specialization_of(std::meta::info type, std::meta::info family) {
    return type != std::meta::info{} && std::meta::has_template_arguments(type)
        && std::meta::template_of(type) == family;
}

// The template argument at index of a specialization.
[[nodiscard]] consteval std::meta::info argument_of(std::meta::info type, std::size_t index) {
    return std::meta::template_arguments_of(type)[index];
}

// The number of template arguments of a specialization.
[[nodiscard]] consteval std::size_t argument_count_of(std::meta::info type) {
    return std::meta::template_arguments_of(type).size();
}

// A std::size_t parameter of a predicate: an alignment, a length or a
// size.
[[nodiscard]] consteval std::size_t size_argument_of(std::meta::info type, std::size_t index) {
    return std::meta::extract<std::size_t>(argument_of(type, index));
}

// The predicate type that a template argument of a combinator names,
// stripped of const and read through its aliases, as predicate_t reads
// it.
[[nodiscard]] consteval std::meta::info predicate_type_of(std::meta::info argument) {
    return std::meta::dealias(std::meta::remove_cv(std::meta::type_of(argument)));
}

// The widest integer types that ExactInteger admits.  std::integral holds
// for them in this dialect, so a bound of either type compares by value
// like a bound of a standard type.
__extension__ using widest_signed = __int128;
__extension__ using widest_unsigned = unsigned __int128;

// Lower ≤ Upper for two bounds of predicates.  The bounds compare by
// value, as std::cmp_less_equal compares them, which is how each bounded
// predicate compares a value with its bound.  An unsigned bound and a
// negative bound then never meet through a conversion that changes a
// sign: 9u ≤ -1 is false, as it is for the values.  A bound that is not
// an exact integer defines its predicate on no value type, and every
// step over it is refused.
template <auto Lower, auto Upper>
concept BoundAtMost =
    ExactInteger<decltype(Lower)> && ExactInteger<decltype(Upper)> && std::cmp_less_equal(Lower, Upper);

// Weak divides Strong, for two bounds with 1 ≤ Weak ≤ Strong.  Both bounds
// are positive then, so the remainder of the two magnitudes is the
// remainder of the two values.  The conjunction stops before the modulo
// when Weak is less than one.
template <auto Strong, auto Weak>
concept BoundDivides = BoundAtMost<1, Weak> && BoundAtMost<Weak, Strong>
                    && static_cast<widest_unsigned>(Strong) % static_cast<widest_unsigned>(Weak) == 0;

// The rules read a bound through these two functions.  Each one asks its
// concept through a reflection, and a concept has no specialization, so
// no translation unit can put a different reading of a bound in front of
// a rule.
[[nodiscard]] consteval bool bound_at_most(std::meta::info lower, std::meta::info upper) {
    return std::meta::extract<bool>(std::meta::substitute(^^BoundAtMost, {lower, upper}));
}

[[nodiscard]] consteval bool bound_divides(std::meta::info strong, std::meta::info weak) {
    return std::meta::extract<bool>(std::meta::substitute(^^BoundDivides, {strong, weak}));
}

// An admitted step whose conclusion is defined on fewer value types
// than its premise.  The header declares it in admitted_implications
// like an edge, and the closure takes it only as the last step of a
// chain.
template <class From, class To>
struct narrowing_edge {};

// The relation over predicate types, closed under chains.  Declared here
// because the conjunction rule below reaches a conclusion through what
// its conjuncts imply, and defined once the namespace is complete.
[[nodiscard]] consteval bool predicate_implies(std::meta::info premise, std::meta::info conclusion);

// The number of predicates the closure reaches from one premise before
// it stops.  A chain of the rules below reaches at most six.
inline constexpr std::size_t closure_node_limit = 64;

namespace admitted_implications {

// positive ⇒ non_negative, because x > 0 gives x ≥ 0.
// positive ⇒ non_zero, because x > 0 gives x ≠ 0.
// power_of_two ⇒ non_zero, because the definition excludes zero.

inline constexpr ::foundation::fail_closed::edge<predicate_t<positive>, predicate_t<non_negative>>
    positive_implies_non_negative{};

inline constexpr ::foundation::fail_closed::edge<predicate_t<positive>, predicate_t<non_zero>>
    positive_implies_non_zero{};

inline constexpr ::foundation::fail_closed::edge<predicate_t<power_of_two>, predicate_t<non_zero>>
    power_of_two_implies_non_zero{};

// non_null and non_zero imply each other. For a pointer, the zero
// value of the pointer type is the null pointer, so the two predicates
// evaluate identically. The pair is safe to state in both directions
// because non_null only accepts a pointer argument, so for any
// non-pointer type the opposite direction names a type that cannot be
// formed at all.
//
// non_zero ⇒ non_null narrows the domain from every value type to the
// pointers. It is a narrowing edge. A chain ends there and never passes
// through non_null to a third predicate.

inline constexpr ::foundation::fail_closed::edge<predicate_t<non_null>, predicate_t<non_zero>>
    non_null_implies_non_zero{};

inline constexpr narrowing_edge<predicate_t<non_zero>, predicate_t<non_null>> non_zero_implies_non_null{};

// Aligned<N> ⇒ Aligned<M> when N ≥ M and M divides N, so a
// cache-line-aligned pointer is also word-aligned.
[[nodiscard]] consteval rule_verdict aligned_weakens(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^Aligned) || !is_specialization_of(conclusion, ^^Aligned)) return {};
    const std::size_t strong = size_argument_of(premise, 0);
    const std::size_t weak = size_argument_of(conclusion, 0);
    return {.admits = strong >= weak && weak > 0 && strong % weak == 0};
}

// A smaller ceiling implies a larger one.
[[nodiscard]] consteval rule_verdict bounded_above_weakens(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^BoundedAbove) || !is_specialization_of(conclusion, ^^BoundedAbove)) {
        return {};
    }
    return {.admits = bound_at_most(argument_of(premise, 0), argument_of(conclusion, 0))};
}

// A tighter range implies a looser one.
[[nodiscard]] consteval rule_verdict in_range_weakens(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^InRange) || !is_specialization_of(conclusion, ^^InRange)) return {};
    return {.admits = bound_at_most(argument_of(conclusion, 0), argument_of(premise, 0))
                   && bound_at_most(argument_of(premise, 1), argument_of(conclusion, 1))};
}

// A range ceiling is an upper bound. The successor names the ceiling
// itself, the strongest upper bound the range gives. A chain from a
// range then reaches every looser ceiling through bounded_above_weakens.
[[nodiscard]] consteval rule_verdict in_range_is_bounded_above(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^InRange)) return {};
    const std::meta::info ceiling = std::meta::substitute(^^BoundedAbove, {argument_of(premise, 1)});
    return {.admits = conclusion == ceiling, .successor = ceiling};
}

// A longer minimum implies a shorter one.
[[nodiscard]] consteval rule_verdict length_ge_weakens(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^LengthGe) || !is_specialization_of(conclusion, ^^LengthGe)) return {};
    return {.admits = size_argument_of(premise, 0) >= size_argument_of(conclusion, 0)};
}

// A container's emptiness test equals a size of zero, so a minimum
// length of one or more implies non-emptiness. The clause excluding
// zero is load-bearing: a size is unsigned, so a minimum of zero is
// vacuously true and an empty container satisfies it.
[[nodiscard]] consteval rule_verdict length_ge_is_non_empty(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^LengthGe)) return {};
    const std::meta::info non_empty_type = std::meta::dealias(^^predicate_t<non_empty>);
    return {.admits = conclusion == non_empty_type && size_argument_of(premise, 0) >= 1, .successor = non_empty_type};
}

// non_zero is a union of two half-lines rather than one. A range whose
// ceiling is minus one or less keeps zero out from below, and this
// rule admits it. A range whose floor is one or more keeps zero out
// from above, and the chain through bounded_below and positive admits
// that one.
//
// A bound strictly between minus one and zero is conservatively
// excluded, which under-asserts rather than risking a truncation.
[[nodiscard]] consteval rule_verdict in_range_is_non_zero(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^InRange)) return {};
    const std::meta::info non_zero_type = std::meta::dealias(^^predicate_t<non_zero>);
    return {.admits =
                conclusion == non_zero_type && bound_at_most(argument_of(premise, 1), std::meta::reflect_constant(-1)),
            .successor = non_zero_type};
}

// A conjunction implies each of its conjuncts, and each disjunct
// implies the disjunction.  Wiring both through the relation lets a
// composed refinement subsume exactly as its parts do.
//
// The conjunction also reaches a conclusion through what its atomic
// parts already imply, rather than only through a literal match.  That
// is what lets a composed predicate weaken to a predicate none of its
// conjuncts spells.
[[nodiscard]] consteval rule_verdict all_of_implies_conjunct(std::meta::info premise, std::meta::info conclusion) {
    if (conclusion == std::meta::info{} || !is_specialization_of(premise, ^^refined_algebra::AllOf)) return {};
    for (std::size_t index = 0; index < argument_count_of(premise); ++index) {
        const std::meta::info conjunct = predicate_type_of(argument_of(premise, index));
        if (conjunct == conclusion || predicate_implies(conjunct, conclusion)) return {.admits = true};
    }
    return {};
}

[[nodiscard]] consteval rule_verdict disjunct_implies_any_of(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(conclusion, ^^refined_algebra::AnyOf)) return {};
    for (std::size_t index = 0; index < argument_count_of(conclusion); ++index) {
        if (predicate_type_of(argument_of(conclusion, index)) == premise) return {.admits = true};
    }
    return {};
}

// The lower-bound axioms mirror the upper-bound ones but with the
// inequality flipped: a tighter floor is a larger N, whereas a tighter
// ceiling is a smaller one. A range's floor is itself a lower bound.
// The bridges out to the unparameterised predicates are gated: a
// negative floor still admits negative values, and a floor of zero
// still admits zero, so neither reaches non_negative or positive
// respectively. A floor strictly between zero and one is
// conservatively excluded too.
[[nodiscard]] consteval rule_verdict bounded_below_weakens(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^BoundedBelow) || !is_specialization_of(conclusion, ^^BoundedBelow)) {
        return {};
    }
    return {.admits = bound_at_most(argument_of(conclusion, 0), argument_of(premise, 0))};
}

// A range floor is a lower bound. The successor names the floor
// itself. A chain from a range then reaches non_negative, positive and
// non_zero through the floor.
[[nodiscard]] consteval rule_verdict in_range_is_bounded_below(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^InRange)) return {};
    const std::meta::info floor = std::meta::substitute(^^BoundedBelow, {argument_of(premise, 0)});
    return {.admits = conclusion == floor, .successor = floor};
}

[[nodiscard]] consteval rule_verdict bounded_below_is_non_negative(std::meta::info premise,
                                                                   std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^BoundedBelow)) return {};
    const std::meta::info non_negative_type = std::meta::dealias(^^predicate_t<non_negative>);
    return {.admits = conclusion == non_negative_type
                   && bound_at_most(std::meta::reflect_constant(0), argument_of(premise, 0)),
            .successor = non_negative_type};
}

// A floor of one or more gives positive, and positive gives non_zero
// through its edge. The floor is sound for both categories the
// predicate accepts: for a number the value is at least one, and for a
// pointer the address is. Neither can be the zero value of its type.
[[nodiscard]] consteval rule_verdict bounded_below_is_positive(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^BoundedBelow)) return {};
    const std::meta::info positive_type = std::meta::dealias(^^predicate_t<positive>);
    return {.admits =
                conclusion == positive_type && bound_at_most(std::meta::reflect_constant(1), argument_of(premise, 0)),
            .successor = positive_type};
}

// The same shape on the size axis. An exact size of N satisfies any
// minimum up to N. The successor names the minimum N. A chain then
// reaches non-emptiness through length_ge_is_non_empty once N is at
// least one. A size of zero stops that chain, since it means the
// container is empty, the very opposite of the conclusion.
[[nodiscard]] consteval rule_verdict exact_size_is_length_ge(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^ExactSize)) return {};
    const std::meta::info minimum = std::meta::substitute(^^LengthGe, {argument_of(premise, 0)});
    const bool admits =
        is_specialization_of(conclusion, ^^LengthGe) && size_argument_of(premise, 0) >= size_argument_of(conclusion, 0);
    return {.admits = admits, .successor = minimum};
}

// The same relation as pointer alignment, on the modulo axis. If x is
// a multiple of N and N is a multiple of M, then x is a multiple of M.
// The clause excluding a zero M matches the predicate's own assertion,
// keeping modulo by zero out before it can be evaluated. The clause
// requiring N at least M already follows from N being a multiple of a
// positive M, and is stated anyway so the clause reads in one pass.
// BoundDivides compares the two bounds by value first.
[[nodiscard]] consteval rule_verdict divisible_by_weakens(std::meta::info premise, std::meta::info conclusion) {
    if (!is_specialization_of(premise, ^^DivisibleBy) || !is_specialization_of(conclusion, ^^DivisibleBy)) return {};
    return {.admits = bound_divides(argument_of(premise, 0), argument_of(conclusion, 0))};
}

// Every read counts the members against this seal: the edges, the
// narrowing edges and the rules.  A member that another file adds stops
// the build rather than widening the relation.
inline constexpr ::foundation::fail_closed::seal sealed{.members = 20};
}  // namespace admitted_implications

// True when m reflects a variable of type narrowing_edge<From, To> for
// some From and To.  The kind is settled before the type is read.
[[nodiscard]] consteval bool is_narrowing_edge(std::meta::info m) noexcept {
    if (!std::meta::is_variable(m)) return false;
    const auto type = std::meta::remove_cvref(std::meta::type_of(m));
    return std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^narrowing_edge;
}

// The two ends of a narrowing edge variable, each read through its
// aliases, in the shape fail_closed::ends_of gives for an edge.
[[nodiscard]] consteval ::foundation::fail_closed::edge_ends narrowing_ends_of(std::meta::info variable) noexcept {
    const auto args = std::meta::template_arguments_of(std::meta::remove_cvref(std::meta::type_of(variable)));
    return ::foundation::fail_closed::edge_ends{std::meta::dealias(args[0]), std::meta::dealias(args[1])};
}

// True when m reflects a rule: a function of type rule_signature.  A
// function template is not a function, so no rule is a template.
[[nodiscard]] consteval bool is_rule(std::meta::info m) {
    return std::meta::is_function(m) && std::meta::type_of(m) == std::meta::dealias(^^rule_signature);
}

// What one predicate gives the closure: whether one step admits the
// conclusion from it, and the predicates one step past it that the
// closure can stand on.
struct step_view {
    bool admits = false;
    std::vector<std::meta::info> successors;
};

// The steps from node, read in one walk of the namespace as it stands
// where this function is defined.  An edge admits its own pair and gives
// its far end as a successor.  A narrowing edge admits its own pair and
// gives no successor.  A rule takes part only where node has no base
// class.  It admits the pair only where the conclusion has no base class
// either, and it gives a successor only where the successor has no base
// class and the rule admits the step to it.  Every other member is
// skipped here, and the edge walk below refuses it.  Complexity:
// O(members of admitted_implications) rule calls.
//
// The members are in a std::array of their count.  The function is not a
// template, so std::define_static_array would be instantiated in each
// includer, at a cost of about 68 M instructions.
[[nodiscard]] consteval step_view steps_from(std::meta::info node, std::meta::info conclusion) {
    static constexpr auto members = [] consteval {
        const std::vector<std::meta::info> found =
            std::meta::members_of(^^admitted_implications, std::meta::access_context::unchecked());
        std::array<std::meta::info,
                   std::meta::members_of(^^admitted_implications, std::meta::access_context::unchecked()).size()>
            items{};
        const std::meta::info* const source = found.data();
        std::meta::info* const target = items.data();
        for (std::size_t index = 0; index < items.size(); ++index)
            target[index] = source[index];
        return items;
    }();
    step_view view{};
    const bool node_has_base = has_base_class(node);
    const std::meta::info asked = node_has_base || has_base_class(conclusion) ? std::meta::info{} : conclusion;
    // -Wshadow fires on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info m : members) {
        if constexpr (::foundation::fail_closed::is_edge(m)) {
            constexpr ::foundation::fail_closed::edge_ends ends = ::foundation::fail_closed::ends_of(m);
            if (ends.from == node) {
                view.admits = view.admits || ends.to == conclusion;
                view.successors.push_back(ends.to);
            }
        } else if constexpr (is_narrowing_edge(m)) {
            constexpr ::foundation::fail_closed::edge_ends ends = narrowing_ends_of(m);
            view.admits = view.admits || (ends.from == node && ends.to == conclusion);
        } else if constexpr (is_rule(m)) {
            if (!node_has_base) {
                const rule_verdict verdict = [:m:](node, asked);
                view.admits = view.admits || verdict.admits;
                const std::meta::info next = verdict.successor;
                if (next != std::meta::info{} && !has_base_class(next) && [:m:](node, next).admits) {
                    view.successors.push_back(next);
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return view;
}

// A cycle of steps that do not narrow, back to the premise, means two
// distinct predicates with one meaning.  This function has no constant
// definition.  A call to it stops the constant evaluation, and the
// diagnostic names it.
void implication_cycle_between_two_names_for_one_predicate() noexcept;

// The closure of the one-step relation from premise to conclusion.  A
// search from the premise follows the successors, and it answers true
// when a reached predicate implies the conclusion in one step, a
// narrowing edge included.  The reached set holds at most
// closure_node_limit predicates, and a larger search answers false.
// Complexity: O(V²) comparisons and V walks of the namespace for V
// reached predicates.
[[nodiscard]] consteval bool implies_closed(std::meta::info premise, std::meta::info conclusion) {
    std::vector<std::meta::info> reached{premise};
    for (std::size_t cursor = 0; cursor < reached.size(); ++cursor) {
        const std::meta::info node = reached[cursor];
        const step_view view = steps_from(node, conclusion);
        if (view.admits) return true;
        for (std::size_t step = 0; step < view.successors.size(); ++step) {
            const std::meta::info next = view.successors[step];
            if (next == node) continue;
            if (next == premise) implication_cycle_between_two_names_for_one_predicate();
            bool is_reached = false;
            for (std::size_t seen = 0; seen < reached.size(); ++seen)
                is_reached = is_reached || reached[seen] == next;
            if (is_reached) continue;
            if (reached.size() == closure_node_limit) return false;
            reached.push_back(next);
        }
    }
    return false;
}

// The walk above reads the namespace where steps_from is defined, so a
// member that another file adds later takes no part in a step.  Each
// query reads the seal after its walk, so a planted cycle is named as a
// cycle, and every other planted member stops the build at the seal.  A
// late member stops the build at the first query that reads the seal
// after it, so a late member never changes an answer.  The closure
// answers a query of a predicate against itself with one step.
//
// The relation reads the name of each predicate, so a query stops the
// build when a unit specialized the premise or the conclusion (THE SEAL).
// A chain needs no seal of a predicate between the two ends: the steps
// hold for the predicates of this header, and they compose to a claim
// about the two ends alone.
[[nodiscard]] consteval bool predicate_implies(std::meta::info premise, std::meta::info conclusion) {
    const std::meta::info from = std::meta::dealias(premise);
    const std::meta::info to = std::meta::dealias(conclusion);
    static_cast<void>(is_sealed_predicate(from) && is_sealed_predicate(to));
    const bool is_implied = from == to ? steps_from(from, to).admits : implies_closed(from, to);
    ::foundation::fail_closed::require_seal_holds(^^admitted_implications);
    return is_implied;
}

}  // namespace refined

template <auto P, auto Q>
concept PredicateImplies = refined::predicate_implies(^^refined::predicate_t<P>, ^^refined::predicate_t<Q>);

namespace detail::refined_edge_walk {

// The atomic edges are folded over by reflection rather than listed:
// every edge variable in the admitted namespace is read back, checked
// against the relation it belongs to, and then checked against the
// predicates it names on a roster of sample values.  An edge that
// admits a value its target rejects fails here, and so does a
// reflexive edge, which the relation deliberately never states.

inline constexpr std::array<int, 9> signed_samples{-3, -1, 0, 1, 2, 3, 8, 42, 1024};
inline constexpr std::array<unsigned, 7> unsigned_samples{0u, 1u, 2u, 3u, 8u, 1024u, 4294967295u};

// Every value that P admits, Q admits too.
template <class P, class Q, class Sample>
[[nodiscard]] consteval bool edge_sound_on(Sample const& samples) noexcept {
    for (auto const& v : samples) {
        if (P{}(v) && !Q{}(v)) return false;
    }
    return true;
}

// A pair that both accept an integer is sampled on the two arithmetic
// rosters.  Any other pair names a pointer predicate, and is sampled
// on a null and a non-null pointer instead: an arithmetic predicate
// given a pointer would order it against null, which is no constant
// expression, so the two rosters never mix.
template <class P, class Q>
[[nodiscard]] consteval bool edge_sound() noexcept {
    if constexpr (requires(int v) {
                      P{}(v);
                      Q{}(v);
                  }) {
        return edge_sound_on<P, Q>(signed_samples) && edge_sound_on<P, Q>(unsigned_samples);
    } else {
        int const some_object = 7;
        std::array<int const*, 2> const pointer_samples{nullptr, &some_object};
        return edge_sound_on<P, Q>(pointer_samples);
    }
}

// A conclusion no premise implies.  A query against it runs the whole
// closure search from its premise, and a cycle on that search stops the
// build.
struct unreachable_conclusion {};

// The checks every atomic step shares, an edge or a narrowing edge
// alike.  The last check runs the closure search from each end, which
// is where a cycle between two names for one predicate is refused.
template <class From, class To>
[[nodiscard]] consteval bool atomic_step_holds() noexcept {
    static_assert(refined::predicate_implies(^^From, ^^To), "a step declared in admitted_implications must be "
                                                            "admitted by the relation");
    static_assert(!std::is_same_v<From, To>, "a reflexive edge is never stated, because the subsort machinery "
                                             "supplies reflexivity");
    static_assert(std::is_empty_v<From> && std::is_empty_v<To>, "an edge names two stateless predicates");
    static_assert(edge_sound<From, To>(), "an admitted edge is unsound on the sample roster: a value "
                                          "satisfies the source predicate and fails the target");
    static_assert(!refined::predicate_implies(^^From, ^^unreachable_conclusion)
                  && !refined::predicate_implies(^^To, ^^unreachable_conclusion));
    return true;
}

// Walks admitted_implications two times.  is_edge and ends_of are the
// relation's own readers, so an edge is whatever the relation calls
// one.  The first pass refuses a member that is neither an edge, a
// narrowing edge, a rule nor the seal, because the relation would
// ignore it.  The second pass checks each step, and it runs only when
// each member is known.  Each step queries the relation, each query
// reads the seal, and the seal refuses the same member again.
//
// The template parameter defers the walk to the first call.  A
// translation unit that includes this header and does not call the walk
// does not evaluate it.  The check file of this header calls it, and so
// do test/fixy/test_refined.cpp and the fixtures that plant a cycle.
template <class Site = void>
[[nodiscard]] consteval bool every_edge_holds() noexcept {
    static constexpr auto members =
        std::define_static_array(static_cast<::foundation::reflect::anchored_t<^^Site, std::vector<std::meta::info>>>(
            std::meta::members_of(^^refined::admitted_implications, std::meta::access_context::unchecked())));
    constexpr bool is_each_member_known = [] consteval {
        for (std::meta::info const member : members) {
            if (!::foundation::fail_closed::is_edge(member) && !refined::is_narrowing_edge(member)
                && !::foundation::fail_closed::is_seal(member) && !refined::is_rule(member))
                return false;
        }
        return true;
    }();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        if constexpr (!::foundation::fail_closed::is_edge(m) && !refined::is_narrowing_edge(m)
                      && !::foundation::fail_closed::is_seal(m)) {
            static_assert(refined::is_rule(m), "a member of admitted_implications is an edge, a narrowing edge, "
                                               "the seal, or a rule of type refined::rule_signature.  A class, a "
                                               "template or a function of another type decides nothing there");
        }
    }
    if constexpr (is_each_member_known) {
        template for (constexpr auto m : members) {
            if constexpr (::foundation::fail_closed::is_edge(m)) {
                constexpr auto ends = ::foundation::fail_closed::ends_of(m);
                static_assert(atomic_step_holds<typename[:ends.from:], typename[:ends.to:]>());
            } else if constexpr (refined::is_narrowing_edge(m)) {
                constexpr auto ends = refined::narrowing_ends_of(m);
                static_assert(atomic_step_holds<typename[:ends.from:], typename[:ends.to:]>());
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail::refined_edge_walk

}  // namespace fixy
