#pragma once

// The compile-time half of the escape guard: a wrapper cannot hand out a raw
// reference or pointer through a member the guard did not sanction, and
// the compiler is what reads the return type, so no spelling evades it.
//
// scripts/check-escape-doors.py reads the parse tree of the source.  It
// is a coarse net: a return type that a macro hides is a raw escape it
// never sees, and a missed escape is the dangerous direction.  This
// header closes that gap for member functions.  It reads the public
// surface of a type through std::meta, and it reads the return type of
// each member function as the compiler resolved it.  So a later member
// added to a checked wrapper, however it is spelled, is seen exactly as
// the first was.
//
// The surface
// -----------
// The public surface of a type T is each member that a caller outside T
// can name through T.  That is a public member of T, a public member of a
// base that T inherits publicly at every depth, and a member of any base
// that a public using-declaration of T names.  The walk asks the compiler
// whether each member of T and of each base is accessible when a caller
// at namespace scope names it through T, so it reads the access rules the
// compiler applies and keeps no copy of them.
//
// A caller also converts a reference to T into a reference to a public
// base, so the surface of a public base is part of the surface of T.
//
// The rule
// --------
// On the surface of a checked type T, every member function whose return
// type is a reference or a raw pointer must be one of:
//   - a getter whose identifier is in the accessor vocabulary (its
//     provenance is *this, so the reference lives as long as the
//     object);
//   - a discouraged, type-checked escape hatch: from_raw,
//     from_raw_nonnull, into_raw, release, declassify;
//   - an assignment or a subscript/deref operator (operator=, a
//     compound assignment, operator[], operator*, operator->), which
//     return into *this;
//   - a mint (an identifier beginning `mint_`).
// A conversion function to a raw reference or pointer is refused.  A
// non-static data member on the surface is refused whatever its type,
// because a caller takes its address.  Anything else on the surface that
// returns a raw reference or pointer fails RawEscapesSanctioned<T>, and
// CRUCIBLE_NO_RAW_ESCAPE(T) turns that into a compile error that names
// the member.
//
// What this does NOT cover, stated rather than implied
// ----------------------------------------------------
//   - Free functions and function templates.  std::meta cannot read the
//     return type of an uninstantiated template, and a free function is
//     not a member of any type, so neither is walked here.  The text
//     scan is the net for those, and its ledger names each one.
//   - Member function TEMPLATES of a checked type: skipped, for the same
//     reason.  A templated accessor is admitted unchecked here and seen
//     by the text scan instead.
//   - A return of class type that views the resource, such as std::span
//     or std::string_view.  An owning class and a view both hold a
//     pointer, and no query of the type tells them apart.
//   - Where the reference actually points.  The check reads the return
//     TYPE and the member NAME, not the body; a getter named `data`
//     that returned a dangling reference passes.  The name is the
//     claim; review reads it.  This is the same limit every reflection
//     and text guard in the tree carries, because a body's provenance
//     is not a type-system fact.

#include <cstddef>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace foundation::reflect {

namespace detail {

// The conventional getter identifiers.  Provenance is the object the
// getter is called on.  A new getter with a new name is added here in a
// reviewed edit, which is the point: a raw escape justifies itself.
inline constexpr std::string_view accessor_names[] = {
    "data",        "begin", "end",      "cbegin",  "cend",     "front",   "back",   "at",    "get",
    "get_or_init", "peek",  "peek_mut", "value",   "resource", "ctx",     "in",     "out",   "input",
    "output",      "stage", "machine",  "state",   "carrier",  "handle",  "pin",    "c_str", "sq_ring",
    "cq_ring",     "sqes",  "observe",  "consume", "try_get",  "raw_ptr", "graded", "cap",
};

// The discouraged, type-checked escape hatches.
inline constexpr std::string_view hatch_names[] = {
    "from_raw", "from_raw_nonnull", "into_raw", "release", "declassify",
};

[[nodiscard]] consteval bool name_in(std::string_view name, std::span<const std::string_view> set) noexcept {
    for (std::string_view entry : set) {
        if (name == entry) return true;
    }
    return false;
}

[[nodiscard]] consteval bool identifier_is_sanctioned(std::string_view name) noexcept {
    if (name.starts_with("mint_")) return true;
    if (name_in(name, accessor_names)) return true;
    if (name_in(name, hatch_names)) return true;
    return false;
}

// The member on the surface that the guard refuses.  A null member means
// that the guard refuses nothing.
struct RawEscape {
    std::meta::info member{};
    std::meta::info naming_class{};
};

[[nodiscard]] consteval bool is_raw_address_type(std::meta::info type) {
    return std::meta::is_reference_type(type) || std::meta::is_pointer_type(type);
}

[[nodiscard]] consteval bool list_holds(std::vector<std::meta::info> const& list, std::meta::info item) {
    for (const std::meta::info entry : list) {
        if (entry == item) return true;
    }
    return false;
}

// True when a member function on the surface returns a raw reference or
// pointer through a door that the rule does not sanction.
[[nodiscard]] consteval bool is_unsanctioned_function(std::meta::info member) {
    if (!std::meta::is_function(member) || std::meta::is_template(member)) return false;
    if (std::meta::is_constructor(member) || std::meta::is_destructor(member)
        || std::meta::is_special_member_function(member)) {
        return false;
    }
    if (!is_raw_address_type(std::meta::return_type_of(member))) return false;
    if (std::meta::has_identifier(member)) return !identifier_is_sanctioned(std::meta::identifier_of(member));
    // A conversion function hands the resource out under a cast rather
    // than a named door.  Any other operator with no identifier returns
    // into *this or into an element of it.
    return std::meta::is_conversion_function(member);
}

// The first member on the public surface of `root` that the guard
// refuses.  The walk visits each naming class once: the root, and each
// public base of a naming class.  For each naming class it reads the
// class and every base at every depth, and it keeps a member only when a
// caller at namespace scope can name it through the naming class.
// Complexity: linear in the number of members of the classes it reads,
// times the scans of its two visited lists.
[[nodiscard]] consteval RawEscape first_raw_escape_on_surface(std::meta::info root) {
    const std::meta::access_context everywhere = std::meta::access_context::unchecked();
    std::vector<std::meta::info> naming_classes{std::meta::dealias(std::meta::remove_cv(root))};
    for (std::size_t named = 0; named < naming_classes.size(); ++named) {
        const std::meta::info naming_class = naming_classes[named];
        if (!std::meta::is_class_type(naming_class) && !std::meta::is_union_type(naming_class)) continue;
        const std::meta::access_context outside = std::meta::access_context::unprivileged().via(naming_class);
        // A caller converts a reference to T into a reference to a public
        // base, so a public base names its own surface too.
        for (const std::meta::info base : std::meta::bases_of(naming_class, everywhere)) {
            const std::meta::info base_class = std::meta::dealias(std::meta::type_of(base));
            if (std::meta::is_public(base) && !list_holds(naming_classes, base_class)) {
                naming_classes.push_back(base_class);
            }
        }
        // A using-declaration can make a member of a base of any access
        // public, so the walk reads every base at every depth.
        std::vector<std::meta::info> declaring_classes{naming_class};
        for (std::size_t declared = 0; declared < declaring_classes.size(); ++declared) {
            for (const std::meta::info base : std::meta::bases_of(declaring_classes[declared], everywhere)) {
                const std::meta::info base_class = std::meta::dealias(std::meta::type_of(base));
                if (!list_holds(declaring_classes, base_class)) declaring_classes.push_back(base_class);
            }
        }
        for (const std::meta::info declaring_class : declaring_classes) {
            for (const std::meta::info member : std::meta::members_of(declaring_class, everywhere)) {
                if (!std::meta::is_accessible(member, outside)) continue;
                if (is_unsanctioned_function(member) || std::meta::is_nonstatic_data_member(member)) {
                    return RawEscape{member, naming_class};
                }
            }
        }
    }
    return RawEscape{};
}

// The refusal text for the member that the guard refuses, or an empty
// view when the guard refuses nothing.  The verdict is text and not a
// reflection, so no caller gets a reflection of a member from the walk.
[[nodiscard]] consteval std::string_view raw_escape_refusal_text(std::meta::info checked) {
    const RawEscape escape = first_raw_escape_on_surface(checked);
    if (escape.member == std::meta::info{}) return "";
    std::string text{"foundation::reflect: the public surface of "};
    text += std::meta::display_string_of(checked);
    text += " holds ";
    text += std::meta::display_string_of(escape.member);
    if (escape.naming_class != std::meta::dealias(std::meta::remove_cv(checked))) {
        text += ", which a caller names through ";
        text += std::meta::display_string_of(escape.naming_class);
    }
    if (std::meta::is_nonstatic_data_member(escape.member)) {
        text += ".  A caller takes the address of a public data member.  Make the member private and give it "
                "an accessor.";
    } else {
        text += ".  It returns a raw reference or pointer, and it is not a sanctioned accessor, escape hatch, "
                "operator or mint.  Give it an accessor name, send the resource through a marked hatch (from_raw, "
                "release, declassify), or add its name to accessor_names or hatch_names in "
                "foundation/reflect/RawEscape.h in a reviewed edit.";
    }
    return std::define_static_string(text);
}

template <typename T>
inline constexpr std::string_view raw_escape_refusal_v = raw_escape_refusal_text(^^T);

}  // namespace detail

template <typename T>
concept RawEscapesSanctioned = detail::raw_escape_refusal_v<T>.empty();

}  // namespace foundation::reflect

// Fails to compile when the public surface of T holds a member function
// that returns a raw reference or pointer and is not an accessor, a hatch,
// a sanctioned operator or a mint, or when it holds a non-static data
// member.  The diagnostic names the member.  Place it beside a
// wrapper's concrete instantiation; a later door added to that wrapper,
// or to a base of it, stops the build.
#define CRUCIBLE_NO_RAW_ESCAPE(...)                                         \
    static_assert(::foundation::reflect::RawEscapesSanctioned<__VA_ARGS__>, \
                  ::foundation::reflect::detail::raw_escape_refusal_v<__VA_ARGS__>)
