// SPDX-License-Identifier: Apache-2.0
//
// These ids key a cache that installations share, so the stability they
// carry is the whole contract, and it is narrow. The same T yields the
// same id within one build, across the translation units of that build,
// and across rebuilds with the same compiler version and the same
// include order.
//
// The id is not stable across compilers, nor across a compiler major
// version. Each implementation phrases a reflected name its own way, and
// the hash amplifies any difference. Wrapping an existing type in a new
// inline namespace moves the id the same way. Two installations that
// share these ids must first agree on the toolchain, or one silently
// reads another's entry as its own.
//
// A reflected name is also qualified to a depth that follows the scope
// chain of the including translation unit. Compare such a name with
// ends_with against the simple name, never with == against a literal.
// The simple name is always a suffix of the qualified form, so equality
// compiles in one translation unit and fails in the next. Hashing is
// unaffected: the variable template resolves to one inline constexpr
// definition that every translation unit shares.
//
// The id also holds only for a type whose printed name is a function of
// the type. An entity with no declared name, with internal linkage, or
// declared in a function body breaks that, and every id here refuses
// such a type at compile time.
// The section on identity below says why and how.

#pragma once

#include <foundation/reflect/TypeComponents.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::reflect {

// The one mixing primitive in the tree.  Accumulator folds do not call it
// directly — they call `combine_ids(state, input)` just below, which is this
// function behind one displacement step.  The displacement is load-bearing,
// and the last paragraph says why.
//
// This is the MurmurHash3 finalizer.  Each step is invertible modulo 2^64 —
// an xor-shift is its own inverse family, and a multiply by an odd constant
// is invertible — so fmix64 is a PERMUTATION of the 64-bit words.  That one
// property is why it is the only mixer here:
//
//   * A permutation has no absorbing value.  There is no `x` for which
//     `fmix64(state ^ x)` forgets `state`, so no single input can erase the
//     prefix of a fold.  Measured: zero collisions over 400,000 consecutive
//     inputs, as a bijection must give.
//   * It is total.  Every 64-bit input maps to a distinct 64-bit output, so
//     a content hash of 0 — the KernelCache empty-slot sentinel, and a value
//     make_region's own postcondition refuses — can only arise from the one
//     accumulator state that maps to it, never from a degenerate input.
//
// Do not replace it with a folded 128-bit product such as
// `lo(a*b) ^ hi(a*b)`.  That mix loses bits, and it has two absorbing
// values for its second operand: 0 gives 0 and ~0 gives ~0 for every
// first operand.  Real tensor metadata reaches both.  A uint8 tensor on
// CPU:0 packs to zero, and ScalarType::Undefined is int8_t(-1), which
// sign-extends to all ones.  Two different regions then get one content
// hash, and that hash keys the compiler cache.
//
// Two reasons a fold calls combine_ids rather than fmix64 on a bare xor.
//
// A single `fmix64(a ^ b)` is xor-symmetric, so it cannot on its own tell
// operand order.  Order sensitivity comes from the CHAIN: the accumulator
// has already been through fmix64 and the input has not, so the two are not
// interchangeable across steps.  Do not flatten a fold into one xor.
//
// And `fmix64(0)` is 0.  A permutation still has exactly one preimage of
// zero, and for the bare form that preimage is `input == accumulator`.
// Ordinary data reaches that case, because a seed and a schema hash can
// come from the same constant.  A region whose schema hash is
// `1 * 0x9E3779B97F4A7C15`, the seed of the fold, makes the first bare
// step `fmix64(0)`, and its content hash is then 0, the KernelCache
// empty-slot sentinel.  combine_ids displaces the input by the golden
// ratio and mixes the bits of the accumulator before the xor.  The zero
// preimage is then not `input == accumulator`.  Measured: each of 200,000
// self-mixes gives 0 through the bare form, and none gives 0 through
// combine_ids.
constexpr uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// The accumulator step every fold in the tree spells.  Boost-style combine
// — a golden-ratio salt and two shifts of the accumulator — finalized by
// fmix64 above.  It is order-sensitive: combining a with b differs from
// combining b with a, and callers that fold a sequence rely on that.
//
// Read the fmix64 block above for why a fold calls this rather than
// fmix64 on a bare xor.
//
// constexpr and not consteval because one body has to serve both the
// compile-time fold and a runtime check that re-derives the same value.
// A second copy of this body under any other name is a drift surface:
// changing the salt, the mix or the finalizer would leave that copy stale
// and change the shared key while every assertion against it still passed.
// utils/scripts/check-no-combine-ids-duplicate.py is the gate on that.
[[nodiscard]] constexpr uint64_t combine_ids(uint64_t a, uint64_t b) noexcept {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return fmix64(a);
}

// The two constants are the ones the 64-bit FNV-1a specification fixes.
// The algorithm is unsigned arithmetic over bytes with no intrinsic and
// no endian dependence, so the digest is identical on every platform.

namespace detail {

inline constexpr std::uint64_t FNV1A_OFFSET_BASIS = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t FNV1A_PRIME = 0x00000100000001b3ULL;

[[nodiscard]] consteval std::uint64_t fnv1a_64(std::string_view s) noexcept {
    std::uint64_t h = FNV1A_OFFSET_BASIS;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= FNV1A_PRIME;
    }
    return h;
}

// FNV-1a alone leaves the high bits weakly mixed, so the finalizer runs
// over the digest before any caller sees it.
[[nodiscard]] consteval std::uint64_t hash_name(std::string_view s) noexcept { return fmix64(fnv1a_64(s)); }

}  // namespace detail

// ── Identity ─────────────────────────────────────────────────────────
//
// An id is a function of the type only when the printed name is a
// function of the type.  Three kinds of entity break that, and so does
// every type that names one of them:
//
//   - An entity with no declared name: a closure type, an unnamed class,
//     an unnamed union or an unnamed enumeration.  GCC prints a closure
//     from its call signature, so two closures of one signature print
//     one name.  A generic closure also prints auto:N, and N counts the
//     generic parameters that the translation unit declared before it.
//     So one closure prints two names in two translation units.
//   - An entity with internal linkage, such as a class in an unnamed
//     namespace.  Each translation unit has its own, and all of them
//     print one name, so two different types would share one id.
//   - An entity declared in a function body, such as a local class.  It
//     prints the name of the function and its own name.  Two local
//     classes of one name in two blocks of one function print one name.
//     The name of a function template specialization does not name each
//     type of its arguments: f<Hidden>, where Hidden has internal
//     linkage, is one function in each translation unit, and the local
//     classes of these functions print one name.
//
// A name is an identity only when it is a path of declared names from
// the global namespace, through namespaces and classes only.  The walk
// below reads that path by reflection.  It opens the cv, reference,
// pointer, array, function and member pointer parts of a type.  It reads
// the template that a class instantiates and each template argument.  It
// follows each class up to the global namespace, and it refuses a path
// that goes through a function.
//
// A template argument that is a value is read by its type.  A class value
// arrives as its template parameter object, which prints the value, and
// the walk reads it as a value.  A pointer to a function names the
// function, and the walk reads that function.  A reflection names an
// entity, and the walk reads that entity.  A pointer to an object, a
// reference to an object, and a class value that holds a pointer, a
// reference or a reflection name an object that no query can read back
// to its variable, so the linkage of that variable is unknown.
// A variable with internal linkage prints the same name in every
// translation unit, so the walk refuses each of these values.  For the
// rest it reads the printed value for the marks that GCC gives an entity
// with no identity.  A builtin type with no declared parts, such as a
// vector extension type, is read the same way.
//
// A number prints without its type.  The values 1 and 1L print as 1, a
// null data member pointer prints as -1, and a bfloat16 prints as a
// double.  So the stable name appends the type of each value argument of
// arithmetic or member pointer type.  In two cases two different values
// print one text, and the walk refuses both: a NaN prints without its
// payload, and a union value prints without its active member.

namespace detail {

enum class identity_fault : std::uint8_t {
    none,
    no_declared_name,
    internal_linkage,
    function_local,
    object_address,
    unreadable_argument,
    ambiguous_value,
};

// The part of a type that has no identity, and the kind of fault, or
// none.
struct identity_verdict {
    identity_fault fault = identity_fault::none;
    std::meta::info culprit{};
};

// The function that a template argument of pointer-to-function type
// names, or a null reflection for any other value.  The pointer is not
// compared with null: under -fno-delete-null-pointer-checks that
// comparison is not a constant expression.  A null pointer argument
// therefore fails to compile here, which refuses the type.
template <auto Value>
[[nodiscard]] consteval std::meta::info function_named_by() {
    if constexpr (std::is_pointer_v<decltype(Value)> && std::is_function_v<std::remove_pointer_t<decltype(Value)>>) {
        return std::meta::reflect_function(*Value);
    } else {
        return {};
    }
}

template <auto Value>
inline constexpr std::meta::info function_named_by_v = function_named_by<Value>();

// True when the printed text carries a mark that GCC gives only to an
// entity with no declared name or with internal linkage.
[[nodiscard]] consteval bool prints_a_mark_of_no_identity(std::string_view text) noexcept {
    return text.find("<lambda") != std::string_view::npos || text.find("<unnamed") != std::string_view::npos
        || text.find("{anonymous}") != std::string_view::npos;
}

// True when the printed value carries a NaN, which GCC prints without its
// payload, so two different NaNs print one text.
[[nodiscard]] consteval bool prints_a_nan(std::string_view text) noexcept {
    for (const std::string_view mark : {"+QNaN", "-QNaN", "+SNaN", "-SNaN"}) {
        if (text.find(mark) != std::string_view::npos) return true;
    }
    return false;
}

// The walk takes an optional sink for the suffix of the stable name.
// When one is given, the walk appends, in the order it meets them, the
// type of each value that prints without it.
[[nodiscard]] consteval identity_verdict identity_of_type(std::meta::info type, std::string* suffix = nullptr);

// Append " =type" for a value whose print does not name its type.
consteval void append_value_type(std::string& suffix, std::meta::info type) {
    suffix += " =";
    suffix += std::meta::display_string_of(type);
}

// True when a value of this type can print without its type, such as a
// number or a member pointer.  The test names the types whose print does
// name the type, so a builtin type that the standard traits do not
// classify, such as __float128 in strict mode, still gets the suffix.  A
// bool prints true or false, and only a bool does.  A class value and a
// cast enumerator print the type, and a named enumerator is a qualified
// name.  A pointer names a function, and nullptr is its own type.
[[nodiscard]] consteval bool prints_without_its_type(std::meta::info type) {
    return !(std::meta::is_class_type(type) || std::meta::is_union_type(type) || std::meta::is_enum_type(type)
             || type == ^^bool || std::meta::is_null_pointer_type(type) || std::meta::is_pointer_type(type)
             || std::meta::is_reflection_type(type));
}

// True when a value of this type can hold the address of an object, a
// reference, a member pointer or a reflection.  None of these can be
// read back to a declared name from inside a class value.  Complexity:
// linear in the number of members and bases, counted through arrays.
[[nodiscard]] consteval bool carries_unreadable_part(std::meta::info type) {
    type = std::meta::dealias(std::meta::remove_cv(type));
    if (std::meta::is_reference_type(type) || std::meta::is_reflection_type(type) || std::meta::is_pointer_type(type)
        || std::meta::is_member_pointer_type(type)) {
        return true;
    }
    if (std::meta::is_array_type(type)) return carries_unreadable_part(std::meta::remove_all_extents(type));
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return false;
    const std::meta::access_context everywhere = std::meta::access_context::unchecked();
    for (const std::meta::info base : std::meta::bases_of(type, everywhere)) {
        if (carries_unreadable_part(std::meta::type_of(base))) return true;
    }
    for (const std::meta::info member : std::meta::nonstatic_data_members_of(type, everywhere)) {
        if (carries_unreadable_part(std::meta::type_of(member))) return true;
    }
    return false;
}

// True when a value of this type holds a union at any depth.  A union
// value prints the value of its active member and not the member, so two
// values that differ only in the active member print one text.
// Complexity: linear in the number of members and bases, counted through
// arrays.
[[nodiscard]] consteval bool carries_a_union(std::meta::info type) {
    type = std::meta::dealias(std::meta::remove_cv(type));
    if (std::meta::is_union_type(type)) return true;
    if (std::meta::is_array_type(type)) return carries_a_union(std::meta::remove_all_extents(type));
    if (!std::meta::is_class_type(type)) return false;
    const std::meta::access_context everywhere = std::meta::access_context::unchecked();
    for (const std::meta::info base : std::meta::bases_of(type, everywhere)) {
        if (carries_a_union(std::meta::type_of(base))) return true;
    }
    for (const std::meta::info member : std::meta::nonstatic_data_members_of(type, everywhere)) {
        if (carries_a_union(std::meta::type_of(member))) return true;
    }
    return false;
}

[[nodiscard]] consteval identity_verdict identity_of_function(std::meta::info function, std::string* suffix);

// The entity that a reflection names.  A reflection that names no
// declared entity, such as an object or a base, cannot be read, so the
// walk refuses it.
[[nodiscard]] consteval identity_verdict identity_of_reflection(std::meta::info named, std::string* suffix);

// The scopes that enclose an entity, up to the global namespace.  An
// enclosing class is walked as a type, so a class declared inside a
// closure is refused with it.  A namespace with no name is an unnamed
// namespace, whose members have internal linkage.  A function in the
// chain encloses a local entity, which has no identity.  Complexity:
// linear in the depth of the scope chain, plus the walk of each
// enclosing class.
[[nodiscard]] consteval identity_verdict identity_of_scope(std::meta::info scope, std::string* suffix = nullptr) {
    while (true) {
        if (std::meta::is_type(scope)) return identity_of_type(scope, suffix);
        if (scope == ^^::) return {};
        if (std::meta::is_function(scope)) return {identity_fault::function_local, scope};
        if (std::meta::is_namespace(scope) && !std::meta::has_identifier(scope)) {
            return {identity_fault::internal_linkage, scope};
        }
        if (!std::meta::has_parent(scope)) return {};
        scope = std::meta::parent_of(scope);
    }
}

// The scopes that enclose a declared entity.  An entity that a function
// body declares is refused, and the verdict names the entity.
[[nodiscard]] consteval identity_verdict identity_of_enclosing(std::meta::info entity, std::string* suffix) {
    const std::meta::info scope = std::meta::parent_of(entity);
    if (std::meta::is_function(scope)) return {identity_fault::function_local, entity};
    return identity_of_scope(scope, suffix);
}

// Each template argument of a class or a function specialization.
// Complexity: linear in the number of arguments, plus the walk of each.
[[nodiscard]] consteval identity_verdict identity_of_template_arguments(std::meta::info specialization,
                                                                        std::string* suffix);

// A function entity: its linkage and its name, then its template
// arguments and its scopes.  A static invoker of a closure has the
// closure as its parent, so the scope walk refuses it.
[[nodiscard]] consteval identity_verdict identity_of_function(std::meta::info function, std::string* suffix) {
    if (std::meta::has_internal_linkage(function)) return {identity_fault::internal_linkage, function};
    if (!std::meta::has_identifier(function)) return {identity_fault::no_declared_name, function};
    if (std::meta::has_template_arguments(function)) {
        const identity_verdict arguments = identity_of_template_arguments(function, suffix);
        if (arguments.fault != identity_fault::none) return arguments;
    }
    return identity_of_enclosing(function, suffix);
}

// A template argument that is a value: its type, then what the value
// names.  Complexity: linear in the walk of the type, plus the members
// of a class value.
[[nodiscard]] consteval identity_verdict identity_of_value(std::meta::info argument, std::string* suffix) {
    const std::meta::info type = std::meta::dealias(std::meta::remove_cv(std::meta::type_of(argument)));
    const identity_verdict of_type = identity_of_type(type, suffix);
    if (of_type.fault != identity_fault::none) return of_type;
    if (carries_a_union(type) || prints_a_nan(std::meta::display_string_of(argument))) {
        return {identity_fault::ambiguous_value, argument};
    }
    if (suffix != nullptr && prints_without_its_type(type)) append_value_type(*suffix, type);
    if (std::meta::is_reflection_type(type)) {
        return identity_of_reflection(std::meta::extract<std::meta::info>(argument), suffix);
    }
    if (std::meta::is_pointer_type(type)) {
        if (!std::meta::is_function_type(std::meta::remove_pointer(type))) {
            return {identity_fault::object_address, argument};
        }
        // A pointer to a function prints the function's name, so the
        // function must have an identity too.
        const std::meta::info function =
            std::meta::extract<std::meta::info>(std::meta::substitute(^^function_named_by_v, {argument}));
        if (function != std::meta::info{}) {
            const identity_verdict of_function = identity_of_function(function, suffix);
            if (of_function.fault != identity_fault::none) return of_function;
        }
    }
    if ((std::meta::is_class_type(type) || std::meta::is_union_type(type)) && carries_unreadable_part(type)) {
        return {identity_fault::unreadable_argument, argument};
    }
    if (prints_a_mark_of_no_identity(std::meta::display_string_of(argument))) {
        return {identity_fault::no_declared_name, argument};
    }
    return {};
}

// True when an object is a template parameter object: the one const
// object of a class type that a class value argument names.  It prints
// its type and then its value, and constant_of gives the same object
// back, which is the proof.  The print test comes first, because
// constant_of fails to compile on an object that is not usable in
// constant expressions, and the print of a variable is its name, which
// is never its type followed by a brace or a parenthesis.
[[nodiscard]] consteval bool is_template_parameter_object(std::meta::info object) {
    const std::meta::info type = std::meta::type_of(object);
    const std::meta::info bare = std::meta::remove_cv(type);
    if (!std::meta::is_const(type) || !(std::meta::is_class_type(bare) || std::meta::is_union_type(bare))) {
        return false;
    }
    const std::string_view printed = std::meta::display_string_of(object);
    const std::string_view type_name = std::meta::display_string_of(bare);
    if (!printed.starts_with(type_name) || printed.size() == type_name.size()) return false;
    const char after_type = printed[type_name.size()];
    if (after_type != '{' && after_type != '(') return false;
    return std::meta::constant_of(object) == object;
}

// A template argument that is an object.  A template parameter object is
// a class value and is read as one.  Any other object is named by a
// reference, prints the name of its variable, and no query reads it back
// to that variable, so the walk refuses it.
[[nodiscard]] consteval identity_verdict identity_of_object(std::meta::info object, std::string* suffix) {
    if (is_template_parameter_object(object)) return identity_of_value(object, suffix);
    return {identity_fault::object_address, object};
}

[[nodiscard]] consteval identity_verdict identity_of_reflection(std::meta::info named, std::string* suffix) {
    if (named == std::meta::info{}) return {};
    if (std::meta::is_type(named)) return identity_of_type(named, suffix);
    if (std::meta::is_function(named)) return identity_of_function(named, suffix);
    if (std::meta::is_namespace(named)) return named == ^^::? identity_verdict{} : identity_of_scope(named, suffix);
    if (std::meta::is_variable(named) || std::meta::is_template(named)) {
        if (std::meta::has_internal_linkage(named)) return {identity_fault::internal_linkage, named};
        if (!std::meta::has_identifier(named)) return {identity_fault::no_declared_name, named};
        return identity_of_enclosing(named, suffix);
    }
    if (std::meta::is_enumerator(named) || std::meta::is_nonstatic_data_member(named)) {
        return identity_of_enclosing(named, suffix);
    }
    if (std::meta::is_value(named)) return identity_of_value(named, suffix);
    return {identity_fault::unreadable_argument, named};
}

// The walk over one type.  Complexity: linear in the number of nodes of
// the type, counting template arguments and enclosing classes.
[[nodiscard]] consteval identity_verdict identity_of_type(std::meta::info type, std::string* suffix) {
    type = std::meta::dealias(type);
    if (std::meta::is_reference_type(type)) return identity_of_type(std::meta::remove_reference(type), suffix);
    if (std::meta::is_pointer_type(type)) return identity_of_type(std::meta::remove_pointer(type), suffix);
    if (std::meta::is_array_type(type)) return identity_of_type(std::meta::remove_all_extents(type), suffix);
    type = std::meta::dealias(std::meta::remove_cv(type));
    if (std::meta::is_member_pointer_type(type)) {
        const identity_verdict owner = identity_of_type(member_pointer_class_of(type), suffix);
        if (owner.fault != identity_fault::none) return owner;
        return identity_of_type(member_pointer_member_of(type), suffix);
    }
    if (std::meta::is_function_type(type)) {
        const identity_verdict result = identity_of_type(std::meta::return_type_of(type), suffix);
        if (result.fault != identity_fault::none) return result;
        for (const std::meta::info parameter : std::meta::parameters_of(type)) {
            const identity_verdict verdict = identity_of_type(parameter, suffix);
            if (verdict.fault != identity_fault::none) return verdict;
        }
        return {};
    }
    if (std::meta::is_fundamental_type(type) || std::meta::is_reflection_type(type)) return {};
    if (std::meta::is_class_type(type) || std::meta::is_union_type(type) || std::meta::is_enum_type(type)) {
        if (std::meta::has_template_arguments(type)) {
            const std::meta::info primary = std::meta::template_of(type);
            if (std::meta::has_internal_linkage(primary)) return {identity_fault::internal_linkage, primary};
            const identity_verdict scope = identity_of_enclosing(primary, suffix);
            if (scope.fault != identity_fault::none) return scope;
            return identity_of_template_arguments(type, suffix);
        }
        if (!std::meta::has_identifier(type)) return {identity_fault::no_declared_name, type};
        if (std::meta::has_internal_linkage(type)) return {identity_fault::internal_linkage, type};
        return identity_of_enclosing(type, suffix);
    }
    if (prints_a_mark_of_no_identity(std::meta::display_string_of(type))) {
        return {identity_fault::no_declared_name, type};
    }
    return {};
}

[[nodiscard]] consteval identity_verdict identity_of_template_arguments(std::meta::info specialization,
                                                                        std::string* suffix) {
    for (const std::meta::info argument : std::meta::template_arguments_of(specialization)) {
        identity_verdict verdict{};
        if (std::meta::is_type(argument)) {
            verdict = identity_of_type(argument, suffix);
        } else if (std::meta::is_template(argument)) {
            if (std::meta::has_internal_linkage(argument)) {
                verdict = {identity_fault::internal_linkage, argument};
            } else {
                verdict = identity_of_enclosing(argument, suffix);
            }
        } else if (std::meta::is_object(argument)) {
            verdict = identity_of_object(argument, suffix);
        } else if (std::meta::is_function(argument)) {
            verdict = identity_of_function(argument, suffix);
        } else {
            verdict = identity_of_value(argument, suffix);
        }
        if (verdict.fault != identity_fault::none) return verdict;
    }
    return {};
}

// The refusal text for a type that has no identity.  Built only when
// the verdict is a fault.
[[nodiscard]] consteval std::string_view identity_refusal_text(std::meta::info type, identity_verdict verdict) {
    if (verdict.fault == identity_fault::none) return "";
    std::string text{"foundation::reflect: a stable id refuses the type "};
    text += std::meta::display_string_of(type);
    text += ", because its part ";
    text += std::meta::display_string_of(verdict.culprit);
    if (verdict.fault == identity_fault::no_declared_name) {
        text += " has no declared name.  A closure type, an unnamed class, an unnamed union and an unnamed "
                "enumeration print a name that is not a function of the entity: GCC prints a closure from its "
                "call signature, and numbers each generic parameter by its position in the translation unit.  "
                "Give the entity a declared name: a named class type with a call operator, or a named class "
                "template.";
    } else if (verdict.fault == identity_fault::internal_linkage) {
        text += " has internal linkage.  Each translation unit holds its own entity under one printed name, so "
                "two different types would share one id.  Declare it in a named namespace.";
    } else if (verdict.fault == identity_fault::function_local) {
        text += " is declared in a function body.  A local entity prints the name of its function and its own "
                "name, and two local classes of one name in one function print one name.  The name of a function "
                "template specialization does not name each type of its arguments, so the local classes of two "
                "different functions can print one name too.  Declare the entity at namespace scope or in a "
                "class.";
    } else if (verdict.fault == identity_fault::ambiguous_value) {
        text += " prints a text that a different value also prints: GCC prints a NaN without its payload, and a "
                "union value without its active member.  Pass a number that is not a NaN, or a value of a type "
                "that holds no union.";
    } else if (verdict.fault == identity_fault::object_address) {
        text += " names an object by its address or by a reference.  No query reads the object back to its "
                "variable, so a variable with internal linkage would print one name in every translation unit.  "
                "Pass the value, or name a type or a function.";
    } else {
        text += " cannot be read back to a declared name: a class value that holds a pointer, a reference, a "
                "member pointer or a reflection, or a reflection of an object or a base.  Pass a value of a type "
                "with no such part, or name a type.";
    }
    return std::define_static_string(text);
}

}  // namespace detail

// True when the printed name of T is a path of declared names from the
// global namespace, so that every translation unit prints it the same
// and no other type prints it.  Every id below requires it.  The concept
// calls the walk itself.  A concept has no specialization, so no
// translation unit can put a different verdict in front of the walk.
template <typename T>
concept HasStableIdentity = detail::identity_of_type(^^T).fault == detail::identity_fault::none;

// The same question for a function that a pointer names.  A pointer to
// the static invoker of a closure, or to a function with internal
// linkage, has no stable identity.
template <auto FnPtr>
    requires std::is_pointer_v<decltype(FnPtr)> && std::is_function_v<std::remove_pointer_t<decltype(FnPtr)>>
inline constexpr bool function_has_stable_identity_v =
    detail::identity_of_function(std::meta::reflect_function(*FnPtr), nullptr).fault == detail::identity_fault::none;

namespace detail {

// The one door to a printed name that feeds an id.  The assertion names
// the part of the type that has no identity.
template <typename T>
[[nodiscard]] consteval std::string_view checked_stable_name() {
    static_assert(HasStableIdentity<T>, identity_refusal_text(^^T, identity_of_type(^^T)));
    std::string suffix;
    (void)identity_of_type(^^T, &suffix);
    if (suffix.empty()) return std::meta::display_string_of(^^T);
    return std::define_static_string(std::string{std::meta::display_string_of(^^T)} + suffix);
}

}  // namespace detail

// The string lives in consteval result storage, which outlives every
// caller, so holding the view for the life of the program is safe.

template <typename T>
inline constexpr std::string_view stable_name_of = detail::checked_stable_name<T>();

template <typename T>
inline constexpr std::uint64_t stable_type_id = detail::hash_name(stable_name_of<T>);

// Sorting by name collapses two packs that differ only in order to one
// type. It does not deduplicate: a repeated element stays repeated.

namespace detail {

// The sort is quadratic. Packs reaching here hold a few dozen elements
// at most, and the whole sort runs at compile time.
template <typename... Ts>
[[nodiscard]] consteval auto sort_indices_by_stable_name() noexcept {
    constexpr std::size_t N = sizeof...(Ts);
    std::array<std::string_view, N> const names{stable_name_of<Ts>...};
    std::array<std::size_t, N> indices{};
    for (std::size_t i = 0; i < N; ++i)
        indices[i] = i;
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (names[indices[j]] < names[indices[i]]) {
                std::size_t const tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }
    return indices;
}

template <typename Tuple, std::size_t... Is>
auto reassemble_tuple_impl(std::index_sequence<Is...>) -> std::tuple<std::tuple_element_t<Is, Tuple>...>;

}  // namespace detail

template <typename... Ts>
struct canonicalize_pack {
private:
    using source_tuple = std::tuple<Ts...>;
    static constexpr auto sorted_indices = []() consteval {
        if constexpr (sizeof...(Ts) == 0) {
            return std::array<std::size_t, 0>{};
        } else {
            return detail::sort_indices_by_stable_name<Ts...>();
        }
    }();

    template <std::size_t... Is>
    static auto build(std::index_sequence<Is...>)
        -> std::tuple<std::tuple_element_t<sorted_indices[Is], source_tuple>...>;

public:
    using type = decltype(build(std::make_index_sequence<sizeof...(Ts)>{}));
};

template <typename... Ts>
using canonicalize_pack_t = typename canonicalize_pack<Ts...>::type;

// This hashes the function type, never the address. Two distinct
// functions that share a signature therefore share one id, and one
// function reached through different declarations keeps a single id.
// The function type goes through the same door as every other type, so
// a signature that names a closure type is refused.

template <auto FnPtr>
inline constexpr std::uint64_t stable_function_id =
    detail::hash_name(detail::checked_stable_name<std::remove_pointer_t<decltype(FnPtr)>>());

// This hashes the printed name of the function that the pointer names.
// It tells apart two functions of one signature, which share one
// stable_function_id.  The name is read only when the function has a
// stable identity, so a static invoker of a closure and a function with
// internal linkage have no name id.

namespace detail {

template <auto FnPtr>
[[nodiscard]] consteval std::uint64_t checked_function_name_hash() {
    static_assert(function_has_stable_identity_v<FnPtr>,
                  "stable_function_name_id: the function has no stable identity.  A static invoker of a closure "
                  "or a function with internal linkage prints a name that differs between translation units.  "
                  "Name a function with external linkage.");
    return hash_name(std::meta::display_string_of(std::meta::reflect_constant(FnPtr)));
}

}  // namespace detail

template <auto FnPtr>
    requires std::is_pointer_v<decltype(FnPtr)> && std::is_function_v<std::remove_pointer_t<decltype(FnPtr)>>
inline constexpr std::uint64_t stable_function_name_id = detail::checked_function_name_hash<FnPtr>();

}  // namespace foundation::reflect
