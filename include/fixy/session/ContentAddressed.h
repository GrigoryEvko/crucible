#pragma once

// A payload marked this way may be delivered without crossing the wire
// at all, when the recipient already holds a copy addressed by the same
// content.  What the recipient ends up in depends only on the content,
// so which of the two routes delivered it is invisible to the protocol.
//
// Deciding whether a copy is already held is a question for whatever
// moves the bytes.  The type says only that the answer cannot change
// the protocol's outcome, which is what makes the elision safe.
//
// The marked and unmarked payload types are therefore subsorts of each
// other.  That is unusual: every other payload relation here runs one
// way, since it records a guarantee one side has and the other does
// not.  Here neither side gains or loses anything, so a protocol that
// takes up the marking stays equivalent to the one that did not.  The
// axiom that says so stands in the family `content` of the axioms of the
// payload order, below.
//
// This header holds the marker, its relation, its payload rule and its
// axiom, so that the payload order and the payload walk read the marker
// through the registry (fixy/session/Registry.h) and name it nowhere else.
//
// Each answer about the marker is a function at namespace scope that is
// not a template, so no translation unit can specialize an answer.  The
// concepts and the alias templates below read the functions, and no unit
// can specialize a concept or an alias template either.

#include <fixy/Predicates.h>
#include <fixy/session/Registry.h>

#include <cstdint>
#include <meta>

namespace fixy::session {

template <typename T>
struct ContentAddressed {
    using wrapped = T;
};

// True when type, read through its aliases, is a specialization of
// ContentAddressed.  A cv-qualified type and a pointer to a marked payload
// are not marked.
[[nodiscard]] consteval bool is_content_addressed(std::meta::info type) {
    const std::meta::info named = std::meta::dealias(type);
    return !std::meta::is_const_type(named) && !std::meta::is_volatile_type(named)
        && std::meta::has_template_arguments(named) && std::meta::template_of(named) == ^^ContentAddressed;
}

// The type under one layer of the marker, or the type itself when it is
// not marked.  The argument of the marker comes from the reader of the
// refinements.  Each type is read through its aliases.
[[nodiscard]] consteval std::meta::info peel_content_address(std::meta::info type) {
    const std::meta::info named = std::meta::dealias(type);
    return is_content_addressed(named) ? std::meta::dealias(::fixy::refined::argument_of(named, 0)) : named;
}

// The type under every layer of the marker.  Complexity: linear in the
// layers of the marker.
[[nodiscard]] consteval std::meta::info unwrap_content_addressed(std::meta::info type) {
    std::meta::info named = std::meta::dealias(type);
    while (is_content_addressed(named))
        named = peel_content_address(named);
    return named;
}

// Two payloads stand for the same content when the marker is on at
// least one side and stripping every layer of it from both sides leaves
// one type.  The payload order reads this relation in both directions.
[[nodiscard]] consteval bool is_content_addressed_equivalent(std::meta::info left, std::meta::info right) {
    const std::meta::info low = std::meta::dealias(left);
    const std::meta::info high = std::meta::dealias(right);
    return low != high && (is_content_addressed(low) || is_content_addressed(high))
        && unwrap_content_addressed(low) == unwrap_content_addressed(high);
}

template <typename T>
concept ContentAddressedType = is_content_addressed(^^T);

template <typename T, typename U>
concept ContentAddressedEquivalent = is_content_addressed_equivalent(^^T, ^^U);

template <typename T>
using content_addressed_underlying_t = typename[:peel_content_address(^^T):];

template <typename T>
using unwrap_content_addressed_t = typename[:unwrap_content_addressed(^^T):];

// The payload rule of the marker (fixy/session/Registry.h).  The payload
// walk reads its members, and its row is the row of the payload it marks.
namespace combinators::content {

inline constexpr ::fixy::session::transition::payload_rule content_addressed{.shape = ^^ContentAddressed,
                                                                             .row_arguments = std::uint64_t{1}};

inline constexpr ::fixy::session::transition::seal payload_rule_seal{
    .kind = ^^::fixy::session::transition::payload_rule, .count = 1};

}  // namespace combinators::content

// A payload and its content-addressed form are below each other, at any
// depth of the marker.
namespace payload_axioms::content {

inline constexpr ::fixy::session::transition::subsort_axiom content_addressed{.weakens = ^^ContentAddressedEquivalent};

inline constexpr ::fixy::session::transition::seal axiom_seal{.kind = ^^::fixy::session::transition::subsort_axiom,
                                                              .count = 1};

}  // namespace payload_axioms::content

}  // namespace fixy::session
