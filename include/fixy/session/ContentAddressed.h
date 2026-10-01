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
// axiom that says so stands with the other axioms of the payload order,
// in fixy/session/Subtype.h.
//
// This header holds only the marker and its traits, so that the payload
// order and the payload walk can both name the marker without either
// one reaching the other.

#include <foundation/contracts/Armed.h>

#include <type_traits>

namespace fixy::session {

template <typename T>
struct ContentAddressed {
    using wrapped = T;
};

template <typename T>
struct is_content_addressed : std::false_type {};

template <typename T>
struct is_content_addressed<ContentAddressed<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_content_addressed_v = is_content_addressed<T>::value;

template <typename T>
concept ContentAddressedType = is_content_addressed_v<T>;

template <typename T>
struct content_addressed_underlying {
    using type = T;
};

template <typename T>
struct content_addressed_underlying<ContentAddressed<T>> {
    using type = T;
};

template <typename T>
using content_addressed_underlying_t = typename content_addressed_underlying<T>::type;

template <typename T>
struct unwrap_content_addressed {
    using type = T;
};

template <typename T>
struct unwrap_content_addressed<ContentAddressed<T>> : unwrap_content_addressed<T> {};

template <typename T>
using unwrap_content_addressed_t = typename unwrap_content_addressed<T>::type;

// Two payloads stand for the same content when the marker is on at
// least one side and stripping every layer of it from both sides leaves
// one type.  The payload order reads this relation in both directions.
template <typename T, typename U>
inline constexpr bool content_addressed_equivalent_v =
    !std::is_same_v<T, U> && (is_content_addressed_v<T> || is_content_addressed_v<U>)
    && std::is_same_v<unwrap_content_addressed_t<T>, unwrap_content_addressed_t<U>>;

}  // namespace fixy::session

// A marked payload is content-addressed at any depth of the marker.  The
// bare payload is not, and neither is a pointer to a marked payload.
template <>
struct foundation::contracts::armed_cell<::fixy::session::is_content_addressed> {
    using accepts = witnesses<::fixy::session::ContentAddressed<int>,
                              ::fixy::session::ContentAddressed<::fixy::session::ContentAddressed<int>>>;
    using refuses = witnesses<int, ::fixy::session::ContentAddressed<int>*>;
};
