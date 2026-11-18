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
// unaffected: each id reads the name inside its own function in this
// header, so every translation unit reads it in one scope.
//
// The id also holds only for a type whose printed name is a function of
// the type. An entity with no declared name, with internal linkage, or
// declared in a function body breaks that, and every id here refuses
// such a type at compile time.
// The section on identity below says why and how.

#pragma once

#include <foundation/reflect/Mix.h>
#include <foundation/reflect/TypeComponents.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::reflect {

// The ids fold through the one mixer of foundation/reflect/Mix.h: fmix64,
// and combine_ids for each step of a fold.

// The two constants are the ones the 64-bit FNV-1a specification fixes.
// The algorithm is unsigned arithmetic over bytes with no intrinsic and
// no endian dependence, so the digest is identical on every platform.

namespace detail {

inline constexpr std::uint64_t FNV1A_OFFSET_BASIS = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t FNV1A_PRIME = 0x00000100000001b3ULL;

[[nodiscard]] consteval std::uint64_t fnv1a_64(std::string_view s) noexcept {
    const char* const bytes = s.data();
    const std::size_t length = s.size();
    std::uint64_t h = FNV1A_OFFSET_BASIS;
    for (std::size_t index = 0; index < length; ++index) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[index]));
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

// The holder of a function and of a pointer to it.  The function ids and
// the identity walk read its two template arguments, and a specialization
// of the holder changes no template argument.
template <std::meta::info Function, auto Pointer>
struct function_holder {};

}  // namespace detail

// The key of the function that a pointer names: a type whose two template
// arguments are the reflection of the function and the pointer.  An alias
// template has no specialization, so no translation unit can put a
// different function behind a key.  A null pointer names no function, so
// the key of a null pointer fails to compile.
template <auto FnPtr>
    requires std::is_pointer_v<decltype(FnPtr)> && std::is_function_v<std::remove_pointer_t<decltype(FnPtr)>>
using function_key = detail::function_holder<std::meta::reflect_function(*FnPtr), FnPtr>;

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

// The pointer that a holder holds: its second template argument.
[[nodiscard]] consteval std::meta::info pointer_of_holder(std::meta::info holder) {
    return std::meta::template_arguments_of(holder)[1];
}

// The holder that the key of a function names.  Only function_key makes a
// key.  A unit can write a holder whose function is not the function that
// its pointer names, and that holder stops the build too, because the ids
// read the identity of the one and the name of the other.
[[nodiscard]] consteval std::meta::info holder_of_key(std::meta::info key) {
    key = std::meta::dealias(key);
    const bool is_holder = std::meta::has_template_arguments(key) && std::meta::template_of(key) == ^^function_holder;
    if (!is_holder || key != std::meta::dealias(std::meta::substitute(^^function_key, {pointer_of_holder(key)}))) {
        throw std::meta::exception(u8"foundation::reflect: a function id takes the reflection of a function key, "
                                   u8"written ^^function_key<&f>.",
                                   key);
    }
    return key;
}

// The function that the key of a function names: the first template
// argument of its holder.
[[nodiscard]] consteval std::meta::info function_of_key(std::meta::info key) {
    return std::meta::extract<std::meta::info>(std::meta::template_arguments_of(holder_of_key(key))[0]);
}

// The pointer that the key of a function holds.
[[nodiscard]] consteval std::meta::info pointer_of_key(std::meta::info key) {
    return pointer_of_holder(holder_of_key(key));
}

// The function that a template argument of pointer-to-function type
// names.  The walk reads it through function_key, an alias template that
// no unit can specialize.  The pointer is not compared with null: under
// -fno-delete-null-pointer-checks that comparison is not a constant
// expression.  A null pointer argument therefore fails to compile here,
// which refuses the type.
[[nodiscard]] consteval std::meta::info function_named_by(std::meta::info pointer) {
    return function_of_key(std::meta::substitute(^^function_key, {pointer}));
}

// True when the printed text holds one of the marks.  The function is not
// a template, so no translation unit can specialize it and let a mark
// through the identity walk.  Complexity: the length of the text times
// the number of marks.
[[nodiscard]] consteval bool prints_one_of(std::string_view text, const char* const* marks,
                                           std::size_t count) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (text.find(marks[index]) != std::string_view::npos) return true;
    }
    return false;
}

// True when the printed text carries a mark that GCC gives only to an
// entity with no declared name or with internal linkage.
[[nodiscard]] consteval bool prints_a_mark_of_no_identity(std::string_view text) noexcept {
    constexpr const char* marks[] = {"<lambda", "<unnamed", "{anonymous}"};
    return prints_one_of(text, marks, sizeof(marks) / sizeof(marks[0]));
}

// True when the printed value carries a NaN, which GCC prints without its
// payload, so two different NaNs print one text.
[[nodiscard]] consteval bool prints_a_nan(std::string_view text) noexcept {
    constexpr const char* marks[] = {"+QNaN", "-QNaN", "+SNaN", "-SNaN"};
    return prints_one_of(text, marks, sizeof(marks) / sizeof(marks[0]));
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

// Calls visit with each base of the class or union TYPE, and then with each
// of its non-static data members, the private ones too, until visit gives
// true.  True when visit gave true.  Each walk of the parts of a class in
// this header goes through it.  Complexity: linear in the bases and the
// members.
template <class Visit>
[[nodiscard]] consteval bool any_part_(std::meta::info type, Visit visit) {
    const std::meta::access_context everywhere = std::meta::access_context::unchecked();
    for (const std::meta::info base : std::meta::bases_of(type, everywhere)) {
        if (visit(base)) return true;
    }
    for (const std::meta::info member : std::meta::nonstatic_data_members_of(type, everywhere)) {
        if (visit(member)) return true;
    }
    return false;
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
    return any_part_(type,
                     [](std::meta::info part) consteval { return carries_unreadable_part(std::meta::type_of(part)); });
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
    return any_part_(type, [](std::meta::info part) consteval { return carries_a_union(std::meta::type_of(part)); });
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
        const identity_verdict of_function = identity_of_function(function_named_by(argument), suffix);
        if (of_function.fault != identity_fault::none) return of_function;
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

// The reason of a refusal, for each kind of fault.
[[nodiscard]] consteval std::string_view identity_refusal_reason(identity_fault fault) noexcept {
    if (fault == identity_fault::no_declared_name) {
        return " has no declared name.  A closure type, an unnamed class, an unnamed union and an unnamed "
               "enumeration print a name that is not a function of the entity: GCC prints a closure from its "
               "call signature, and numbers each generic parameter by its position in the translation unit.  "
               "Give the entity a declared name: a named class type with a call operator, or a named class "
               "template.";
    }
    if (fault == identity_fault::internal_linkage) {
        return " has internal linkage.  Each translation unit holds its own entity under one printed name, so "
               "two different types would share one id.  Declare it in a named namespace.";
    }
    if (fault == identity_fault::function_local) {
        return " is declared in a function body.  A local entity prints the name of its function and its own "
               "name, and two local classes of one name in one function print one name.  The name of a function "
               "template specialization does not name each type of its arguments, so the local classes of two "
               "different functions can print one name too.  Declare the entity at namespace scope or in a "
               "class.";
    }
    if (fault == identity_fault::ambiguous_value) {
        return " prints a text that a different value also prints: GCC prints a NaN without its payload, and a "
               "union value without its active member.  Pass a number that is not a NaN, or a value of a type "
               "that holds no union.";
    }
    if (fault == identity_fault::object_address) {
        return " names an object by its address or by a reference.  No query reads the object back to its "
               "variable, so a variable with internal linkage would print one name in every translation unit.  "
               "Pass the value, or name a type or a function.";
    }
    return " cannot be read back to a declared name: a class value that holds a pointer, a reference, a "
           "member pointer or a reflection, or a reflection of an object or a base.  Pass a value of a type "
           "with no such part, or name a type.";
}

// The refusal text for a type that has no identity.  Built only when
// the verdict is a fault.  No check reads the text.
[[nodiscard]] consteval std::string identity_refusal(std::meta::info type, identity_verdict verdict) {
    std::string text{"foundation::reflect: a stable id refuses the type "};
    text += std::meta::display_string_of(type);
    text += ", because its part ";
    text += std::meta::display_string_of(verdict.culprit);
    text += identity_refusal_reason(verdict.fault);
    return text;
}

}  // namespace detail

// True when the printed name of a type is a path of declared names from
// the global namespace, so that every translation unit prints it the
// same and no other type prints it.  Every id below requires it.  The
// door is a function that is not a template, and the gate below is a
// concept.  No translation unit can specialize either one, so no
// translation unit can put a different verdict in front of the walk.
[[nodiscard]] consteval bool has_stable_identity(std::meta::info type) {
    return detail::identity_of_type(type).fault == detail::identity_fault::none;
}

template <typename T>
concept HasStableIdentity = has_stable_identity(^^T);

// The same question for a function.  The static invoker of a closure,
// and a function with internal linkage, have no stable identity.
[[nodiscard]] consteval bool has_stable_function_identity(std::meta::info function) {
    return detail::identity_of_function(function, nullptr).fault == detail::identity_fault::none;
}

// The same question for the function that a pointer names.
template <auto FnPtr>
concept HasStableFunctionIdentity =
    std::is_pointer_v<decltype(FnPtr)> && std::is_function_v<std::remove_pointer_t<decltype(FnPtr)>>
    && has_stable_function_identity(std::meta::reflect_function(*FnPtr));

// ── The stable ids ───────────────────────────────────────────────────
//
// Each id is a function that is not a template, and it takes the
// reflection of what it names.  No translation unit can specialize such a
// function, so no unit can put a forged id in front of a reader: the row
// hash, the org id of a federation, the schema hash of a session event and
// the key of the computation cache.  A type with no identity stops the
// constant evaluation, and the diagnostic prints the refusal text, which
// names the part of the type that has no identity.

// The printed name that feeds each id of a type: the name of the type,
// read through its aliases, and the type of each value argument that
// prints without it.  The text lives only during the constant evaluation
// that reads it.
[[nodiscard]] consteval std::string stable_name(std::meta::info type) {
    type = std::meta::dealias(type);
    // The walk writes the suffix, and the printed name goes in front of it.
    std::string name;
    const detail::identity_verdict verdict = detail::identity_of_type(type, &name);
    if (verdict.fault != detail::identity_fault::none) {
        throw std::meta::exception(detail::identity_refusal(type, verdict), verdict.culprit);
    }
    name.insert(0, std::meta::display_string_of(type));
    return name;
}

// The id of a type: the hash of its stable name.
[[nodiscard]] consteval std::uint64_t stable_type_id(std::meta::info type) {
    return detail::hash_name(stable_name(type));
}

// Sorting by name collapses two packs that differ only in order to one
// type. It does not deduplicate: a repeated element stays repeated.

namespace detail {

// The sort is quadratic. Packs reaching here hold a few dozen elements
// at most, and the whole sort runs at compile time.
template <typename... Ts>
[[nodiscard]] consteval auto sort_indices_by_stable_name() noexcept {
    constexpr std::size_t N = sizeof...(Ts);
    std::array<std::string, N> const names{stable_name(^^Ts)...};
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

// Only a decltype names reassemble_tuple_impl, so no call instantiates its
// body.  The body keeps its definition in this header.
template <typename Tuple, std::size_t... Is>
auto reassemble_tuple_impl(std::index_sequence<Is...>) -> std::tuple<std::tuple_element_t<Is, Tuple>...> {
    return {};
}

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

    // Only the decltype below names build, so no call instantiates its body.
    // The body keeps its definition in this header.
    template <std::size_t... Is>
    static auto build(std::index_sequence<Is...>)
        -> std::tuple<std::tuple_element_t<sorted_indices[Is], source_tuple>...> {
        return {};
    }

public:
    using type = decltype(build(std::make_index_sequence<sizeof...(Ts)>{}));
};

template <typename... Ts>
using canonicalize_pack_t = typename canonicalize_pack<Ts...>::type;

// The two ids of a function take the reflection of its key,
// ^^function_key<&f>.
//
// This hashes the function type, never the address. Two distinct
// functions that share a signature therefore share one id, and one
// function reached through different declarations keeps a single id.
// The function type goes through the same door as every other type, so
// a signature that names a closure type is refused.
[[nodiscard]] consteval std::uint64_t stable_function_id(std::meta::info key) {
    return stable_type_id(std::meta::remove_pointer(std::meta::type_of(detail::pointer_of_key(key))));
}

// This hashes the printed name of the pointer that the key holds.  It
// tells apart two functions of one signature, which share one
// stable_function_id.  The name is read only when the function has a
// stable identity, so a static invoker of a closure and a function with
// internal linkage have no name id.
[[nodiscard]] consteval std::uint64_t stable_function_name_id(std::meta::info key) {
    if (!has_stable_function_identity(detail::function_of_key(key))) {
        throw std::meta::exception(u8"stable_function_name_id: the function has no stable identity.  A static "
                                   u8"invoker of a closure or a function with internal linkage prints a name that "
                                   u8"differs between translation units.  Name a function with external linkage.",
                                   key);
    }
    return detail::hash_name(std::meta::display_string_of(detail::pointer_of_key(key)));
}

// ── The layout digest ────────────────────────────────────────────────
//
// The digest of the layout of a type: one word that two shared libraries
// compare before they share values of the type.  Two units that compile one
// type from one text give one digest, and a change of the size, the
// alignment, the place, the name or the type of a part changes it.  The
// fold reads, for the type and for each part that the type holds by value:
//
//   * the size and the alignment, and the stable id of the type when it
//     has a stable identity.  A type with no stable identity, such as an
//     anonymous union, folds its layout and no name
//   * of a class or a union: each base with its offset, and each
//     non-static data member with its offset in bits, its width in bits,
//     its name when it has one, and the fold of its type
//   * of an array: the extent and the fold of the element
//   * of a pointer or a reference: the stable id of the pointee when it has
//     one.  The fold does not follow a pointer, because a value does not
//     hold the object that it points to
//   * of a function type: its stable id, or no name
//
// A class cannot hold itself by value, so the recursion ends.  The fold
// visits a type again each time that a part holds it, so its cost is linear
// in the parts of the tree of values.  The digest reads no address, so each
// host gives the same value.
//
// c_entry_digest<Scope>(prefix) folds the name and the type of each function
// of the namespace Scope with C linkage whose name starts with prefix, in the
// order of the declarations: the C entries of a library, which a foreign
// caller declares again with its own types.

namespace detail {

// The seed and the marks of the layout fold: "layout!!", and one mark for
// each kind of part, so a part of one kind never folds as a part of another.
inline constexpr std::uint64_t layout_seed = 0x6c61796f75742121ULL;
inline constexpr std::uint64_t layout_mark_pointer = 1;
inline constexpr std::uint64_t layout_mark_reference = 2;
inline constexpr std::uint64_t layout_mark_function = 3;
inline constexpr std::uint64_t layout_mark_array = 4;
inline constexpr std::uint64_t layout_mark_base = 5;
inline constexpr std::uint64_t layout_mark_member = 6;
inline constexpr std::uint64_t layout_mark_value = 7;
inline constexpr std::uint64_t layout_mark_no_name = 8;
inline constexpr std::uint64_t layout_mark_entry = 9;

// The stable id of a type, or the mark of no name.
[[nodiscard]] consteval std::uint64_t layout_name_of(std::meta::info type) {
    return has_stable_identity(type) ? stable_type_id(type) : layout_mark_no_name;
}

// The fold of one type into state.  Complexity: linear in the parts that the
// type holds by value.
[[nodiscard]] consteval std::uint64_t fold_layout(std::uint64_t state, std::meta::info type) {
    const std::meta::info plain = std::meta::dealias(std::meta::remove_cv(std::meta::dealias(type)));
    if (std::meta::is_reference_type(plain)) {
        state = combine_ids(state, layout_mark_reference);
        return combine_ids(state, layout_name_of(std::meta::remove_reference(plain)));
    }
    if (std::meta::is_pointer_type(plain)) {
        state = combine_ids(state, layout_mark_pointer);
        return combine_ids(state, layout_name_of(std::meta::remove_pointer(plain)));
    }
    if (std::meta::is_function_type(plain)) {
        state = combine_ids(state, layout_mark_function);
        return combine_ids(state, layout_name_of(plain));
    }
    if (std::meta::is_array_type(plain)) {
        state = combine_ids(state, layout_mark_array);
        state = combine_ids(state, std::meta::extent(plain));
        return fold_layout(state, std::meta::remove_extent(plain));
    }
    state = combine_ids(state, layout_mark_value);
    state = combine_ids(state, std::meta::size_of(plain));
    state = combine_ids(state, std::meta::alignment_of(plain));
    state = combine_ids(state, layout_name_of(plain));
    if (!std::meta::is_class_type(plain) && !std::meta::is_union_type(plain)) return state;
    // Each base, and then each member: its mark, its offset in bits, for a
    // member its width and its name, and the fold of its type.
    static_cast<void>(any_part_(plain, [&state](std::meta::info part) consteval {
        const bool is_base = std::meta::is_base(part);
        state = combine_ids(state, is_base ? layout_mark_base : layout_mark_member);
        state = combine_ids(state, static_cast<std::uint64_t>(std::meta::offset_of(part).total_bits()));
        if (!is_base) {
            state = combine_ids(state, std::meta::is_bit_field(part) ? std::meta::bit_size_of(part) : 0);
            state = combine_ids(state, std::meta::has_identifier(part) ? hash_name(std::meta::identifier_of(part))
                                                                       : layout_mark_no_name);
        }
        state = fold_layout(state, std::meta::type_of(part));
        return false;
    }));
    return state;
}

}  // namespace detail

// The layout digest of a type.  Complexity: linear in the parts that the
// type holds by value.
[[nodiscard]] consteval std::uint64_t layout_digest(std::meta::info type) {
    return detail::fold_layout(detail::layout_seed, type);
}

// The layout digest of each type of Ts, in order.  Two lists of the same
// types in another order give two digests.
template <typename... Ts>
[[nodiscard]] consteval std::uint64_t layout_digest_of() {
    std::uint64_t state = detail::layout_seed;
    ((state = combine_ids(state, layout_digest(^^Ts))), ...);
    return state;
}

// The digest of the C entries of the namespace Scope whose names start with
// the text of prefix, such as c_entry_digest<^^::>("crucible_").  Scope is a
// template argument, so only a unit that names the digest walks the
// namespace.  Complexity: linear in the members of the namespace.
template <std::meta::info Scope, std::size_t Size>
[[nodiscard]] consteval std::uint64_t c_entry_digest(char const (&prefix)[Size]) {
    std::uint64_t state = detail::layout_seed;
    const auto unchecked = std::meta::access_context::unchecked();
    for (const std::meta::info member : std::meta::members_of(Scope, unchecked)) {
        if (!std::meta::is_function(member) || !std::meta::has_c_language_linkage(member)) continue;
        if (!std::meta::identifier_of(member).starts_with({prefix, Size - 1})) continue;
        state = combine_ids(state, detail::layout_mark_entry);
        state = combine_ids(state, detail::hash_name(std::meta::identifier_of(member)));
        state = combine_ids(state, detail::layout_name_of(std::meta::type_of(member)));
    }
    return state;
}

}  // namespace foundation::reflect
