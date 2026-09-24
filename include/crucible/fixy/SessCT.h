#pragma once

// The substrate declares the session-flavored constant-time surface into the
// same namespace as the general-purpose constant-time primitives. This layer
// splits them, so an audit grep separates a session CT-payload site from a
// primitive site.

#include <crucible/sessions/SessionCT.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::ct {

using ::crucible::safety::ct::requires_ct;
using ::crucible::safety::ct::requires_ct_v;
using ::crucible::safety::ct::RequiresCT;

using ::crucible::safety::ct::CTPayload;

// The using-declaration brings the whole eq overload set into this namespace,
// not one overload. Overload resolution picks the CTPayload form when both
// arguments are payloads, and the byte-span form stays reachable.
using ::crucible::safety::ct::eq;

using ::crucible::safety::ct::is_ct_payload;
using ::crucible::safety::ct::is_ct_payload_v;
using ::crucible::safety::ct::CTPayloadType;

using ::crucible::safety::ct::ct_payload_value_type;
using ::crucible::safety::ct::ct_payload_value_type_t;

}  // namespace crucible::fixy::sess::ct

// A consumer opts a type into the constant-time discipline by specializing
// requires_ct. The name above is only an alias, so the specialization has to
// be written in the substrate namespace that declares the primary template.
// The block below does exactly that for its own placeholder types.

namespace crucible::fixy::sess::ct::u052b_self_test {

struct TokenA {
    std::byte bytes[16]{};
};
struct TokenB {
    std::byte bytes[16]{};
};
struct NotCt {  // Deliberately not opted in, so the rejection cells have a subject.
    std::byte bytes[16]{};
};

}  // namespace crucible::fixy::sess::ct::u052b_self_test

namespace crucible::safety::ct {

template <>
struct requires_ct<::crucible::fixy::sess::ct::u052b_self_test::TokenA> : std::true_type {};

template <>
struct requires_ct<::crucible::fixy::sess::ct::u052b_self_test::TokenB> : std::true_type {};

}  // namespace crucible::safety::ct

namespace crucible::fixy::sess::ct::u052b_self_test {

using TokenAPayload = CTPayload<TokenA>;
using TokenBPayload = CTPayload<TokenB>;

// A substrate symbol that is renamed, removed or relocated fails here at every
// consumer's include rather than only inside a downstream test.
static_assert(std::is_same_v<CTPayload<TokenA>, ::crucible::safety::ct::CTPayload<TokenA>>,
              "fixy::sess::ct::CTPayload must alias safety::ct::CTPayload.");

static_assert(std::is_same_v<requires_ct<TokenA>, ::crucible::safety::ct::requires_ct<TokenA>>,
              "fixy::sess::ct::requires_ct must alias safety::ct::requires_ct.");

static_assert(requires_ct_v<TokenA>);
static_assert(requires_ct_v<TokenB>);
static_assert(!requires_ct_v<NotCt>);
static_assert(!requires_ct_v<int>);

// The concept is the conjunction of the opt-in trait and trivial copyability,
// not the trait alone.
static_assert(RequiresCT<TokenA>);
static_assert(!RequiresCT<NotCt>);
static_assert(!RequiresCT<int>);

static_assert(is_ct_payload_v<TokenAPayload>);
static_assert(is_ct_payload_v<TokenBPayload>);
static_assert(!is_ct_payload_v<TokenA>);
static_assert(!is_ct_payload_v<int>);
static_assert(!is_ct_payload_v<NotCt>);

static_assert(CTPayloadType<TokenAPayload>);
static_assert(!CTPayloadType<TokenA>);

// The metafunction returns a type that is not a payload unchanged, so generic
// transport code can apply it uniformly.
static_assert(std::is_same_v<ct_payload_value_type_t<TokenAPayload>, TokenA>);
static_assert(std::is_same_v<ct_payload_value_type_t<TokenBPayload>, TokenB>);
static_assert(std::is_same_v<ct_payload_value_type_t<TokenA>, TokenA>);
static_assert(std::is_same_v<ct_payload_value_type_t<int>, int>);

template <typename A, typename B>
concept has_fixy_ct_eq = requires(A const& a, B const& b) {
    { eq(a, b) } -> std::same_as<bool>;
};

static_assert(has_fixy_ct_eq<TokenAPayload, TokenAPayload>);

// eq is constrained to the same payload type on both sides, which keeps a
// comparison between two different payload families from compiling.
static_assert(!has_fixy_ct_eq<TokenAPayload, TokenBPayload>);

static_assert(!std::is_same_v<TokenAPayload, TokenBPayload>);

// The wrapper is move-only. Copying classified bytes would duplicate them
// silently. Both equality operators are deleted so every comparison goes
// through eq, which is the only path that runs in constant time.
static_assert(!std::is_copy_constructible_v<TokenAPayload>);
static_assert(!std::is_copy_assignable_v<TokenAPayload>);
static_assert(std::is_move_constructible_v<TokenAPayload>);
static_assert(std::is_move_assignable_v<TokenAPayload>);

template <typename A, typename B>
concept has_operator_eq = requires(A const& a, B const& b) { a == b; };
template <typename A, typename B>
concept has_operator_neq = requires(A const& a, B const& b) { a != b; };

static_assert(!has_operator_eq<TokenAPayload, TokenAPayload>);
static_assert(!has_operator_neq<TokenAPayload, TokenAPayload>);

static_assert(sizeof(TokenAPayload) == sizeof(TokenA));

constexpr int u052b_surface_cardinality = 10;
static_assert(u052b_surface_cardinality == 10, "fixy::sess::ct:: surface cardinality drifted — update the "
                                               "using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::ct::u052b_self_test

namespace crucible::fixy::sess::ct {

// A static assertion alone can mask a SFINAE, consteval or inline-body fault.
// This body instantiates every public template from a function context and
// makes one real eq call.

inline void runtime_smoke_test() noexcept {
    using ::crucible::fixy::sess::ct::u052b_self_test::TokenA;
    using P = CTPayload<TokenA>;

    [[maybe_unused]] constexpr bool isP = is_ct_payload_v<P>;
    [[maybe_unused]] constexpr bool notP = is_ct_payload_v<int>;
    [[maybe_unused]] constexpr bool cap = CTPayloadType<P>;
    [[maybe_unused]] constexpr bool trait = requires_ct_v<TokenA>;
    [[maybe_unused]] constexpr bool concpt = RequiresCT<TokenA>;

    using InnerT = ct_payload_value_type_t<P>;
    using PassT = ct_payload_value_type_t<int>;

    P lhs{TokenA{}};
    P rhs{TokenA{}};
    [[maybe_unused]] bool equal = eq(lhs, rhs);

    (void)isP;
    (void)notP;
    (void)cap;
    (void)trait;
    (void)concpt;
    (void)static_cast<InnerT*>(nullptr);
    (void)static_cast<PassT*>(nullptr);
    (void)equal;
}

}  // namespace crucible::fixy::sess::ct
