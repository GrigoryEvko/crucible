#pragma once

// The compile-time half of the escape guard: a wrapper cannot hand out a raw
// reference or pointer through a member the guard did not sanction, and
// the compiler is what reads the return type, so no spelling evades it.
//
// scripts/check-escape-doors.sh scans source text.  A text scan is a
// coarse net: a member written in a shape its regex does not match — a
// trailing return, a macro-hidden type, a declaration split across
// lines — is a raw escape the scan never sees, and a missed escape is
// the dangerous direction.  This header closes that gap for member
// functions.  It walks a type's public members through std::meta and
// reads each function's return type as the compiler resolved it, so a
// later member added to a checked wrapper, however it is spelled, is
// seen exactly as the first was.
//
// The rule
// --------
// For a checked type T, every PUBLIC member function whose return type
// is a reference or a raw pointer must be one of:
//   - a getter whose identifier is in the accessor vocabulary (its
//     provenance is *this, so the reference lives as long as the
//     object);
//   - a discouraged, type-checked escape hatch: from_raw,
//     from_raw_nonnull, into_raw, release, declassify;
//   - an assignment or a subscript/deref operator (operator=, a
//     compound assignment, operator[], operator*, operator->), which
//     return into *this;
//   - a mint (an identifier beginning `mint_`).
// A member function returning a raw reference or pointer that is none of
// these fails RawEscapesSanctioned<T>, and CRUCIBLE_NO_RAW_ESCAPE(T)
// turns that into a compile error naming the offending member.
//
// What this does NOT cover, stated rather than implied
// ----------------------------------------------------
//   - Free functions and function templates.  std::meta cannot read the
//     return type of an uninitialised template, and a free function is
//     not a member of any type, so neither is walked here.  The text
//     scan is the net for those, and its ledger names the handful that
//     exist.  Measured 2026-09-20: one free function in the two trees
//     returns a raw reference, an internal static-const helper.
//   - Member function TEMPLATES of a checked type: skipped, for the same
//     reason.  A templated accessor is admitted unchecked here and seen
//     by the text scan instead.
//   - Where the reference actually points.  The check reads the return
//     TYPE and the member NAME, not the body; a getter named `data`
//     that returned a dangling reference passes.  The name is the
//     claim; review reads it.  This is the same limit every reflection
//     and text guard in the tree carries, because a body's provenance
//     is not a type-system fact.

#include <foundation/Platform.h>

#include <meta>
#include <string_view>
#include <type_traits>

namespace foundation::reflect {

namespace detail {

// The conventional getter identifiers.  Provenance is the object the
// getter is called on.  A new getter with a new name is added here in a
// reviewed edit, which is the point: a raw escape justifies itself.
inline constexpr std::string_view accessor_names[] = {
    "data",   "begin",  "end",     "cbegin", "cend",    "front",   "back",   "at",
    "get",    "get_or_init",       "peek",   "peek_mut", "value",  "resource", "ctx",
    "in",     "out",    "input",   "output", "stage",   "machine", "state",  "carrier",
    "handle", "pin",    "c_str",   "sq_ring", "cq_ring", "sqes",   "observe", "consume",
    "try_get", "raw_ptr", "graded", "cap",
};

// The discouraged, type-checked escape hatches.
inline constexpr std::string_view hatch_names[] = {
    "from_raw", "from_raw_nonnull", "into_raw", "release", "declassify",
};

[[nodiscard]] consteval bool name_in(std::string_view name, std::span<const std::string_view> set) noexcept {
    for (std::string_view s : set) {
        if (name == s) return true;
    }
    return false;
}

[[nodiscard]] consteval bool identifier_is_sanctioned(std::string_view name) noexcept {
    if (name.starts_with("mint_")) return true;
    if (name_in(name, accessor_names)) return true;
    if (name_in(name, hatch_names)) return true;
    return false;
}

}  // namespace detail

// The identifier (or operator display) of the first public member
// function of T that returns a raw reference or pointer and is not
// sanctioned, or an empty view when every such member is sanctioned.
// Member function templates are skipped: the compiler has no single
// return type to read for them.
template <typename T>
[[nodiscard]] consteval std::string_view first_unsanctioned_raw_escape() {
    std::string_view offender{};
    template for (constexpr auto member :
                  std::define_static_array(std::meta::members_of(^^T, std::meta::access_context::current()))) {
        if constexpr (std::meta::is_function(member) && !std::meta::is_constructor(member)
                      && !std::meta::is_destructor(member) && !std::meta::is_special_member_function(member)
                      && !std::meta::is_template(member)) {
            using ReturnType = typename[:std::meta::return_type_of(member):];
            if constexpr (std::is_reference_v<ReturnType> || std::is_pointer_v<ReturnType>) {
                constexpr bool has_id = std::meta::has_identifier(member);
                if (offender.empty()) {
                    if constexpr (has_id) {
                        // A named member: its identifier must be an
                        // accessor, a hatch, or a mint.
                        constexpr std::string_view name = std::meta::identifier_of(member);
                        if (!detail::identifier_is_sanctioned(name)) offender = name;
                    } else if constexpr (std::meta::is_conversion_function(member)) {
                        // A conversion operator to a raw reference or
                        // pointer hands the resource out under a cast
                        // rather than a named door: it is flagged.  Its
                        // display string is the whole signature, which is
                        // what the diagnostic then shows.
                        offender = std::meta::display_string_of(member);
                    }
                    // Any other operator with no identifier — subscript,
                    // dereference, arrow, assignment, comparison — returns
                    // into *this or into an element of it, so it is a
                    // sanctioned within-object accessor.  A raw-pointer
                    // conversion operator is the one operator shape this
                    // does not admit, handled above; the text scan is the
                    // net for anything a future operator shape hides.
                }
            }
        }
    }
    return offender;
}

template <typename T>
concept RawEscapesSanctioned = first_unsanctioned_raw_escape<T>().empty();

}  // namespace foundation::reflect

// Fails to compile when T has a public member function that returns a
// raw reference or pointer and is not an accessor, a hatch, a
// sanctioned operator, or a mint.  Place it beside a wrapper's concrete
// instantiation; a later door added to that wrapper stops the build.
#define CRUCIBLE_NO_RAW_ESCAPE(...)                                                                      \
    static_assert(::foundation::reflect::RawEscapesSanctioned<__VA_ARGS__>,                              \
                  "a public member of this type returns a raw reference or pointer that is not a "       \
                  "sanctioned accessor, escape hatch, operator, or mint; give it an accessor name, "     \
                  "route it through a marked hatch (from_raw / release / declassify), or if it is a "    \
                  "genuine new escape, add its name to accessor_names or hatch_names in "                \
                  "foundation/reflect/RawEscape.h in a reviewed edit")
