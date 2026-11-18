#pragma once

// The predicates of a refinement, their combinators, and the seal of a
// call of a predicate.
//
// fixy/Refined.h grades a value with one predicate of this header.
// fixy/PredicateImplies.h holds the implication relation between the
// predicates, and only a gate that weakens a refinement includes it.

#include <foundation/Platform.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <type_traits>
#include <utility>

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
// takes one value of that type by reference to const.  So a predicate
// reads a value that cannot be copied, such as a Linear or a Secret, and
// the checked mint of fixy/Refined.h can mint a refinement over it.  The
// seal below reads the specialization of that template for the value type
// of a mint, and the checked mint refuses each other shape.
struct IsPositive {
    template <class V>
        requires requires(V const& x) { x > V{0}; }
    constexpr bool operator()(V const& x) const noexcept {
        return x > V{0};
    }
};

inline constexpr IsPositive positive{};

struct IsNonNegative {
    template <class V>
        requires requires(V const& x) { x >= V{0}; }
    constexpr bool operator()(V const& x) const noexcept {
        return x >= V{0};
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
    constexpr bool operator()(V const& p) const noexcept {
        return p != nullptr;
    }
};

inline constexpr IsNonNull non_null{};

struct IsPowerOfTwo {
    template <class V>
        requires requires(V const& x) { x != V{0} && (x & (x - V{1})) == V{0}; }
    constexpr bool operator()(V const& x) const noexcept {
        return x != V{0} && (x & (x - V{1})) == V{0};
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
// of fixy/PredicateImplies.h could not deduce against it. Callers are
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

namespace refined {

// An alignment is a power of two that is not zero, as the alignment of
// foundation/core/DataAddress.h is.  The test of Aligned reads the low bits
// of an address under the mask N - 1, which is alignment to N only for such
// an N.  The rule aligned_weakens also holds only for such an N: Aligned<6>
// admits the address 2, and Aligned<3> refuses it.
template <std::size_t Alignment>
concept IsAlignment = Alignment != 0 && (Alignment & (Alignment - 1)) == 0;

}  // namespace refined

template <std::size_t Alignment>
    requires refined::IsAlignment<Alignment>
struct Aligned {
    template <class V>
        requires std::is_pointer_v<V>
    constexpr bool operator()(V const& p) const noexcept {
        return (std::bit_cast<std::uintptr_t>(p) & (Alignment - 1)) == 0;
    }
};

template <std::size_t Alignment>
    requires refined::IsAlignment<Alignment>
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
    constexpr bool operator()(V const& x) const noexcept {
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
    constexpr bool operator()(V const& x) const noexcept {
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
    constexpr bool operator()(V const& x) const noexcept {
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
    constexpr bool operator()(V const& x) const noexcept {
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

namespace refined {

// The readers of a predicate type.  The rules of fixy/PredicateImplies.h
// take a predicate apart with them, and so does the vacuity check of
// fixy/Atom.h.

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

// The predicate type that a template argument of a combinator names,
// stripped of const and read through its aliases, as predicate_t reads
// it.
[[nodiscard]] consteval std::meta::info predicate_type_of(std::meta::info argument) {
    return std::meta::dealias(std::meta::remove_cv(std::meta::type_of(argument)));
}

}  // namespace refined

}  // namespace fixy
