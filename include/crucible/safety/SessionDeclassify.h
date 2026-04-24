#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety — DeclassifyOnSend<T, Policy> payload wrapper
//                    (SAFEINT-B13, #402,
//                    misc/24_04_2026_safety_integration.md §13)
//
// Sending a `Secret<T>` over a session normally requires a manual
// `.declassify<Policy>()` at every send site, scattered across the
// codebase.  Each site is its own audit point; the audit-grep
// `declassify<` returns dozens of sites with no obvious reason — the
// "why declassify here" is buried in the surrounding code.
//
// `DeclassifyOnSend<T, Policy>` puts the policy IN THE PROTOCOL TYPE.
// The protocol declaration itself names every wire-classification
// site:
//
//     using AuthHandshake = Send<
//         DeclassifyOnSend<AuthToken, secret_policy::WireSerialize>,
//         Recv<Tagged<AuthAck, source::Sanitized>, End>>;
//
// `grep "DeclassifyOnSend<"` finds every wire-classified payload
// across the codebase; the policy tag names WHY the value escapes
// classification at each site.  Audit becomes a one-line grep
// instead of a code-walk.
//
// ─── Design ─────────────────────────────────────────────────────────
//
// DeclassifyOnSend is a PAYLOAD MARKER, not a session combinator.
// Send<DeclassifyOnSend<T, P>, K> uses the existing Send mechanism
// — no SessionHandle specialisation needed.  The Send's transport
// callback is responsible for extracting the wire bytes via
// `.declassify_for_wire()`; the wrapper provides the typed helper
// so the transport doesn't need to recall the policy tag at the
// call site (it's encoded in the wrapper type).
//
// On the receiver side, the natural pattern is to receive the raw
// T (post-declassification) and re-wrap as Secret<T> at the
// recipient if the value should remain classified after wire
// transit.  Asymmetric by design: declassification is an explicit
// event in the protocol; reclassification is a deliberate choice
// at the recipient and not implied by the type.
//
// ─── What this ships, what it doesn't ─────────────────────────────
//
// Shipped:
//   * DeclassifyOnSend<T, Policy> — the wrapper struct
//   * Move-only (mirrors Secret<T>'s move-only discipline)
//   * declassify_for_wire() — single chokepoint for transport-side
//     extraction; routes through Secret::declassify<Policy>
//   * is_declassify_on_send_v / wire_payload_type_t / wire_policy_t
//     introspection traits
//   * DeclassifyOnSendable concept for require-clause use
//   * Composition with Send/Recv via the existing payload covariance
//     rules (no new SessionHandle specialisation)
//
// NOT shipped (by design):
//   * Auto-unwrap SessionHandle specialisation that calls
//     declassify_for_wire() automatically.  Would hide the
//     declassification event behind framework magic; the discipline
//     is "every wire-bytes extraction is grep-able and explicit".
//   * Reclassification on Recv.  The recipient picks whether to
//     re-Secret the value at the protocol's recv site; the framework
//     does not impose a default direction.
//   * Mutual subsort with the underlying T.  DeclassifyOnSend is a
//     STRONGER type than T (carries the wire-policy); silently
//     flowing it to a bare-T position would defeat the audit point.
//     Same load-bearing asymmetry as External / FromUser tags
//     (SessionPayloadSubsort.h §6).
//
// ─── Usage example: Vessel auth handshake ──────────────────────────
//
//     struct AuthToken { /* ... */ };
//     struct AuthAck   { bool granted; };
//
//     // Protocol declaration NAMES the policy.  grep finds it.
//     using AuthHandshake = Send<
//         DeclassifyOnSend<AuthToken, secret_policy::WireSerialize>,
//         Recv<AuthAck, End>>;
//
//     // Sender side:
//     auto sender = make_session_handle<AuthHandshake>(channel);
//     auto next = std::move(sender).send(
//         DeclassifyOnSend<AuthToken, secret_policy::WireSerialize>{
//             Secret<AuthToken>{ make_token() }
//         },
//         [](Channel& c, auto&& payload) {
//             // The single chokepoint — auditable in this transport.
//             auto raw = std::move(payload).declassify_for_wire();
//             c.write_bytes(serialize(raw));
//         });
//
// The transport's `declassify_for_wire()` call IS the declassification.
// The policy tag is implicit in the payload type — no risk of
// mis-policy at the call site.
//
// ─── Resolves ───────────────────────────────────────────────────────
//
//   * misc/24_04_2026_safety_integration.md §13 — design rationale.
//   * Foundational for §20 ConstantTime crypto-payload sessions
//     (#409): CT comparison rules can be layered atop this wrapper
//     for protocols that carry crypto values.
//   * Composes with Cipher cold-tier S3 uploads, Canopy peer auth
//     handshakes, mTLS handshakes, InferenceSession session-token
//     delivery — each currently scatters declassify<> calls; the
//     refactor centralizes them at the protocol declaration.
//
// ─── Cost ───────────────────────────────────────────────────────────
//
//   sizeof(DeclassifyOnSend<T, Policy>) == sizeof(Secret<T>)
//                                        == sizeof(T)
//
// Policy is a phantom (zero-cost via [[no_unique_address]] EBO if
// ever stored; we don't store it — it's a type parameter only).
// declassify_for_wire is one move + Secret's declassify body —
// inlined to zero overhead in release.
//
// ─── References ────────────────────────────────────────────────────
//
//   safety/Secret.h — the underlying classified-by-default wrapper
//                     and DeclassificationPolicy concept.
//   safety/Session.h — Send/Recv combinators that consume the
//                      DeclassifyOnSend payload via covariance.
//   safety/SessionPayloadSubsort.h — payload-wrapper subsort axioms;
//                                    DeclassifyOnSend deliberately
//                                    does NOT install one
//                                    (analogous to External/FromUser).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Session.h>
#include <crucible/safety/SessionSubtype.h>

#include <type_traits>
#include <utility>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── DeclassifyOnSend<T, Policy> ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Wraps a Secret<T> with a compile-time policy tag.  Move-only;
// destructive declassify routes through the wrapped Secret<T>.

template <typename T, typename Policy>
    requires DeclassificationPolicy<Policy>
class [[nodiscard]] DeclassifyOnSend {
    Secret<T> value_;

public:
    using value_type   = T;
    using policy_type  = Policy;
    using secret_type  = Secret<T>;

    // Construct from a Secret<T> (move-in).
    constexpr explicit DeclassifyOnSend(Secret<T> s)
        noexcept(std::is_nothrow_move_constructible_v<Secret<T>>)
        : value_{std::move(s)} {}

    // Construct from a raw T — wraps it in Secret first.  The
    // explicit signature means callers must be intentional about
    // which path they take; passing a raw T to a function expecting
    // DeclassifyOnSend won't happen by accident.
    constexpr explicit DeclassifyOnSend(T raw)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{Secret<T>{std::move(raw)}} {}

    // In-place construction — forwards args to Secret<T>'s
    // in-place ctor (which forwards to T's ctor).
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit DeclassifyOnSend(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : value_{Secret<T>{std::in_place, std::forward<Args>(args)...}} {}

    // Move-only (mirrors Secret<T>'s discipline).
    DeclassifyOnSend(const DeclassifyOnSend&)
        = delete("DeclassifyOnSend wraps Secret<T>; classified values cannot silently duplicate");
    DeclassifyOnSend& operator=(const DeclassifyOnSend&)
        = delete("DeclassifyOnSend wraps Secret<T>; classified values cannot silently duplicate");
    DeclassifyOnSend(DeclassifyOnSend&&)            noexcept = default;
    DeclassifyOnSend& operator=(DeclassifyOnSend&&) noexcept = default;
    ~DeclassifyOnSend()                                       = default;

    // ── Wire-extraction chokepoint ─────────────────────────────────
    //
    // The single audit-grep-able point where the wrapped Secret is
    // declassified.  Policy is encoded in the wrapper type — the
    // call site can't pick the wrong policy because it's not a
    // template argument the caller specifies.
    //
    // Rvalue-only (`&&`-qualified) — consumes *this and the wrapped
    // Secret in one motion.  After this, the wrapper is moved-from
    // and unusable; no double-declassification possible.

    [[nodiscard]] constexpr T declassify_for_wire() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(value_).template declassify<Policy>();
    }

    // Length-preserving accessor — exposes only the size, never the
    // content.  Useful for serializers that need to size the wire
    // buffer before extracting the bytes.  Compiles only when T has
    // .size().
    [[nodiscard]] constexpr auto size() const noexcept
        requires requires(const T& t) { t.size(); }
    {
        return value_.size();
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Shape traits + concept ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
struct is_declassify_on_send : std::false_type {};

template <typename T, typename Policy>
struct is_declassify_on_send<DeclassifyOnSend<T, Policy>> : std::true_type {};

template <typename T>
inline constexpr bool is_declassify_on_send_v =
    is_declassify_on_send<T>::value;

template <typename T>
concept DeclassifyOnSendable = is_declassify_on_send_v<T>;

// wire_payload_type_t<DeclassifyOnSend<T, P>> — the underlying T
// that comes out of declassify_for_wire().  For non-DeclassifyOnSend
// types, returns the type unchanged (so generic code can use this
// without first checking is_declassify_on_send_v).

template <typename T>
struct wire_payload_type {
    using type = T;
};

template <typename T, typename Policy>
struct wire_payload_type<DeclassifyOnSend<T, Policy>> {
    using type = T;
};

template <typename T>
using wire_payload_type_t = typename wire_payload_type<T>::type;

// wire_policy_t<DeclassifyOnSend<T, P>> — extract the policy tag.
// Defined only for DeclassifyOnSend types (no fallback).

template <typename T>
struct wire_policy;

template <typename T, typename Policy>
struct wire_policy<DeclassifyOnSend<T, Policy>> {
    using type = Policy;
};

template <typename T>
using wire_policy_t = typename wire_policy<T>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Zero-cost size guarantee ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// DeclassifyOnSend wraps Secret<T>; Secret<T> is sizeof(T).  Policy
// is a phantom (zero-cost type parameter).

namespace detail::declassify_size_test {

struct OneByteToken { char x; };
struct FourByteToken { int x; };

static_assert(sizeof(DeclassifyOnSend<OneByteToken, secret_policy::WireSerialize>)
              == sizeof(OneByteToken),
    "DeclassifyOnSend must add zero bytes beyond the wrapped Secret<T>.");

static_assert(sizeof(DeclassifyOnSend<FourByteToken, secret_policy::WireSerialize>)
              == sizeof(FourByteToken),
    "DeclassifyOnSend must add zero bytes beyond the wrapped Secret<T>.");

}  // namespace detail::declassify_size_test

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::declassify_self_test {

struct Token { int v = 0; };
struct Other {};

using TokenWire = DeclassifyOnSend<Token, secret_policy::WireSerialize>;
using TokenAudit = DeclassifyOnSend<Token, secret_policy::AuditedLogging>;

// Shape predicates.
static_assert( is_declassify_on_send_v<TokenWire>);
static_assert( is_declassify_on_send_v<TokenAudit>);
static_assert(!is_declassify_on_send_v<Token>);
static_assert(!is_declassify_on_send_v<Secret<Token>>);
static_assert(!is_declassify_on_send_v<int>);

// Concept rejection on non-DeclassifyOnSend.
static_assert( DeclassifyOnSendable<TokenWire>);
static_assert(!DeclassifyOnSendable<Token>);

// wire_payload_type_t extracts the inner T.
static_assert(std::is_same_v<wire_payload_type_t<TokenWire>,  Token>);
static_assert(std::is_same_v<wire_payload_type_t<TokenAudit>, Token>);
// Non-DeclassifyOnSend types pass through unchanged.
static_assert(std::is_same_v<wire_payload_type_t<Token>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<int>,   int>);

// wire_policy_t extracts the policy tag.
static_assert(std::is_same_v<wire_policy_t<TokenWire>,
                              secret_policy::WireSerialize>);
static_assert(std::is_same_v<wire_policy_t<TokenAudit>,
                              secret_policy::AuditedLogging>);

// Different policies on the same T are distinct types.
static_assert(!std::is_same_v<TokenWire, TokenAudit>);

// Move-only discipline.
static_assert(!std::is_copy_constructible_v<TokenWire>);
static_assert(!std::is_copy_assignable_v<TokenWire>);
static_assert( std::is_move_constructible_v<TokenWire>);
static_assert( std::is_move_assignable_v<TokenWire>);

// ═════════════════════════════════════════════════════════════════════
// ── Composition with Send / Recv via existing payload covariance ───
// ═════════════════════════════════════════════════════════════════════
//
// DeclassifyOnSend is a payload type — it composes with Send/Recv
// like any other.  The wrapper's STRONGER information (the policy
// tag) means it must NOT silently flow to a bare-payload Send via
// the subtype lattice — same load-bearing asymmetry as External /
// FromUser provenance tags.  Verify this stays asymmetric.

using namespace crucible::safety::proto;

// Reflexivity: DeclassifyOnSend ⩽ DeclassifyOnSend (via primary
// is_subsort = is_same).
static_assert(is_subtype_sync_v<
    Send<TokenWire, End>,
    Send<TokenWire, End>>);

// Different policies on same T are unrelated payloads.
static_assert(!is_subtype_sync_v<
    Send<TokenWire,  End>,
    Send<TokenAudit, End>>);

// DELIBERATELY ABSENT axiom: Send<DeclassifyOnSend<T, P>, K> is NOT
// a subtype of Send<T, K>.  Allowing it would mean the type system
// silently strips the wire-policy tag at any subtype boundary —
// defeating the audit-discoverability of `grep DeclassifyOnSend<`.
//
// (No specialisation in SessionPayloadSubsort.h for DeclassifyOnSend.)
static_assert(!is_subtype_sync_v<
    Send<TokenWire, End>,
    Send<Token,     End>>);

// And the reverse — bare T should NOT silently gain the wire-policy
// tag (would let unclassified values masquerade as classified).
static_assert(!is_subtype_sync_v<
    Send<Token,     End>,
    Send<TokenWire, End>>);

// Recv direction — same asymmetry holds via Recv contravariance.
static_assert(!is_subtype_sync_v<
    Recv<TokenWire, End>,
    Recv<Token,     End>>);
static_assert(!is_subtype_sync_v<
    Recv<Token,     End>,
    Recv<TokenWire, End>>);

}  // namespace detail::declassify_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety
